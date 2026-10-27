// Package match is the server's rules for one match: roster, teams, state,
// de-duplicated hit reports and the scoreboard. It is pure (no I/O, an
// injectable clock) so it is tested without a network. The gear decides that
// a hit landed; the match only counts what the gear reports.
package match

import (
	"errors"
	"fmt"
	"sort"
	"sync"
	"time"

	"github.com/scshafe/lightfight/server/protocol"
)

type Mode string

const (
	FreeForAll     Mode = "ffa"
	TeamDeathmatch Mode = "tdm"
)

type State string

const (
	Lobby    State = "lobby"
	Running  State = "running"
	Finished State = "finished"
)

const (
	MaxPlayers = 64 // shooter ids 0..63
	MaxTeams   = 4  // v1 (the IR frame allows 8)
)

type Config struct {
	Mode         Mode          `json:"mode"`
	Duration     time.Duration `json:"-"`
	DurationSec  int           `json:"duration_s"`
	FriendlyFire bool          `json:"friendly_fire"` // team deathmatch only
	HitPoints    int           `json:"hit_points"`
	DownPoints   int           `json:"down_points"`
}

// DefaultConfig is ten minutes, 1 point per hit and 5 more per down.
func DefaultConfig(mode Mode) Config {
	return Config{Mode: mode, DurationSec: 600, HitPoints: 1, DownPoints: 5}
}

func (c *Config) validate() error {
	if c.Mode != FreeForAll && c.Mode != TeamDeathmatch {
		return fmt.Errorf("unknown mode %q", c.Mode)
	}
	if c.DurationSec < 1 || c.DurationSec > 4*3600 {
		return errors.New("duration_s must be 1..14400")
	}
	if c.HitPoints < 0 || c.DownPoints < 0 {
		return errors.New("points must not be negative")
	}
	c.Duration = time.Duration(c.DurationSec) * time.Second
	return nil
}

type Stats struct {
	Hits      int `json:"hits"`       // hits landed on others
	Downs     int `json:"downs"`      // others put down
	HitsTaken int `json:"hits_taken"` // hits received
	Deaths    int `json:"deaths"`
	Score     int `json:"score"`
}

type Player struct {
	ID       string `json:"id"`
	Callsign string `json:"callsign"` // a made-up name, never a real one
	Shooter  uint8  `json:"shooter"`  // the id in the IR frame
	Team     uint8  `json:"team"`
	Stats
}

// Report is one hit as the target's gear reported it.
type Report struct {
	Target      string `json:"target"` // player id
	EventID     uint16 `json:"event_id"`
	Shooter     uint8  `json:"shooter"`
	Seq         uint8  `json:"seq"`
	Damage      uint8  `json:"damage"`
	HealthAfter uint16 `json:"health_after"`
	Down        bool   `json:"down"`
	TMs         uint32 `json:"t_ms"`
}

// Key identifies a report for de-duplication. The IR sequence wraps every 16
// shots, so the key is the gear's own event id plus its uptime stamp: a replay
// of the same event repeats both, a later event never does (a gear reboot
// restarts the ids but not the uptime).
func (r Report) Key() string { return fmt.Sprintf("%s/%d/%d", r.Target, r.EventID, r.TMs) }

type Result string

const (
	Accepted  Result = "accepted"
	Duplicate Result = "duplicate"
	Rejected  Result = "rejected"
)

type Outcome struct {
	Result  Result `json:"result"`
	Reason  string `json:"reason,omitempty"`
	Shooter string `json:"shooter,omitempty"` // player id credited, when accepted
}

type Match struct {
	mu      sync.Mutex
	now     func() time.Time
	ID      string
	cfg     Config
	state   State
	started time.Time
	ended   time.Time
	players []*Player
	seen    map[string]bool
}

var (
	ErrState   = errors.New("match is not in the right state")
	ErrFull    = errors.New("no free shooter id or team slot")
	ErrUnknown = errors.New("unknown player")
)

func New(id string, cfg Config, now func() time.Time) (*Match, error) {
	if err := cfg.validate(); err != nil {
		return nil, err
	}
	if cfg.Mode == FreeForAll {
		cfg.FriendlyFire = true // every player is the others' enemy
	}
	return &Match{now: now, ID: id, cfg: cfg, state: Lobby, seen: map[string]bool{}}, nil
}

func (m *Match) Config() Config { m.mu.Lock(); defer m.mu.Unlock(); return m.cfg }

// Join adds a player in the lobby with the lowest free shooter id. Free-for-all
// puts everyone on team 0 (the gear is configured with friendly fire on); team
// deathmatch uses the requested team, 0..MaxTeams-1.
func (m *Match) Join(id, callsign string, team uint8) (Player, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.state != Lobby {
		return Player{}, ErrState
	}
	if callsign == "" || len(callsign) > 24 {
		return Player{}, errors.New("callsign must be 1..24 characters")
	}
	for _, p := range m.players {
		if p.Callsign == callsign {
			return Player{}, errors.New("callsign is taken")
		}
	}
	if m.cfg.Mode == FreeForAll {
		team = 0
	} else if team >= MaxTeams {
		return Player{}, ErrFull
	}
	used := map[uint8]bool{}
	for _, p := range m.players {
		used[p.Shooter] = true
	}
	for s := uint8(0); s < MaxPlayers; s++ {
		if !used[s] {
			p := &Player{ID: id, Callsign: callsign, Shooter: s, Team: team}
			m.players = append(m.players, p)
			return *p, nil
		}
	}
	return Player{}, ErrFull
}

// restore re-adds a stored player with its stored ids (loading from disk).
func (m *Match) Restore(p Player) { m.mu.Lock(); m.players = append(m.players, &p); m.mu.Unlock() }

func (m *Match) Start() error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.state != Lobby || len(m.players) < 2 {
		return ErrState
	}
	m.state, m.started = Running, m.now()
	return nil
}

// Stop finishes a running match. It is idempotent once finished.
func (m *Match) Stop() error {
	m.mu.Lock()
	defer m.mu.Unlock()
	switch m.state {
	case Running:
		m.state, m.ended = Finished, m.now()
		return nil
	case Finished:
		return nil
	}
	return ErrState
}

// Expire finishes the match when its time is up and reports whether it did.
func (m *Match) Expire() bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.state == Running && !m.now().Before(m.started.Add(m.cfg.Duration)) {
		m.state, m.ended = Finished, m.started.Add(m.cfg.Duration)
		return true
	}
	return false
}

// Restore the lifecycle of a stored match.
func (m *Match) RestoreState(s State, started, ended time.Time) {
	m.mu.Lock()
	m.state, m.started, m.ended = s, started, ended
	m.mu.Unlock()
}

func (m *Match) find(id string) *Player {
	for _, p := range m.players {
		if p.ID == id {
			return p
		}
	}
	return nil
}

// Apply counts a report once. Reports for a match that is not running, for
// players outside it, against oneself, or friendly (when that is off) are
// rejected; a repeat of an already counted event is a harmless duplicate.
func (m *Match) Apply(r Report) Outcome {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.state == Running && !m.now().Before(m.started.Add(m.cfg.Duration)) {
		m.state, m.ended = Finished, m.started.Add(m.cfg.Duration)
	}
	if m.state != Running && !m.seen[r.Key()] {
		return Outcome{Result: Rejected, Reason: "match is not running"}
	}
	return m.count(r)
}

// Replay counts a stored report while loading from disk: the match's state
// and clock are not consulted, since the report was accepted when it was live.
func (m *Match) Replay(r Report) Outcome {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.count(r)
}

func (m *Match) count(r Report) Outcome {
	if m.seen[r.Key()] {
		return Outcome{Result: Duplicate}
	}
	reject := func(why string) Outcome { return Outcome{Result: Rejected, Reason: why} }
	target := m.find(r.Target)
	if target == nil {
		return reject("unknown target")
	}
	if r.Shooter > 63 || r.Seq > 15 {
		return reject("shooter or seq out of range")
	}
	var shooter *Player
	for _, p := range m.players {
		if p.Shooter == r.Shooter {
			shooter = p
		}
	}
	switch {
	case shooter == nil:
		return reject("unknown shooter")
	case shooter == target:
		return reject("self hit")
	case !m.cfg.FriendlyFire && shooter.Team == target.Team:
		return reject("friendly fire is off")
	}
	m.seen[r.Key()] = true
	target.HitsTaken++
	shooter.Hits++
	friendly := m.cfg.Mode == TeamDeathmatch && shooter.Team == target.Team
	if !friendly {
		shooter.Score += m.cfg.HitPoints
	}
	if r.Down {
		target.Deaths++
		shooter.Downs++
		if !friendly {
			shooter.Score += m.cfg.DownPoints
		}
	}
	return Outcome{Result: Accepted, Shooter: shooter.ID}
}

type TeamScore struct {
	Team  uint8 `json:"team"`
	Score int   `json:"score"`
}

type Snapshot struct {
	ID         string      `json:"id"`
	Config     Config      `json:"config"`
	State      State       `json:"state"`
	RemainingS int         `json:"remaining_s"`
	Players    []Player    `json:"players"` // best score first
	Teams      []TeamScore `json:"teams,omitempty"`
}

func (m *Match) Snapshot() Snapshot {
	m.mu.Lock()
	defer m.mu.Unlock()
	s := Snapshot{ID: m.ID, Config: m.cfg, State: m.state}
	if m.state == Running {
		if left := m.started.Add(m.cfg.Duration).Sub(m.now()); left > 0 {
			s.RemainingS = int(left.Round(time.Second) / time.Second)
		}
	} else if m.state == Lobby {
		s.RemainingS = m.cfg.DurationSec
	}
	for _, p := range m.players {
		s.Players = append(s.Players, *p)
	}
	sort.SliceStable(s.Players, func(i, j int) bool {
		a, b := s.Players[i], s.Players[j]
		if a.Score != b.Score {
			return a.Score > b.Score
		}
		if a.Deaths != b.Deaths {
			return a.Deaths < b.Deaths
		}
		return a.Shooter < b.Shooter
	})
	if m.cfg.Mode == TeamDeathmatch {
		sum := map[uint8]int{}
		for _, p := range m.players {
			sum[p.Team] += p.Score
		}
		for t, v := range sum {
			s.Teams = append(s.Teams, TeamScore{t, v})
		}
		sort.Slice(s.Teams, func(i, j int) bool {
			if s.Teams[i].Score != s.Teams[j].Score {
				return s.Teams[i].Score > s.Teams[j].Score
			}
			return s.Teams[i].Team < s.Teams[j].Team
		})
	}
	return s
}

// GearConfig is what the phone writes to a player's gear before the match.
func (m *Match) GearConfig(playerID string) (protocol.Config, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	p := m.find(playerID)
	if p == nil {
		return protocol.Config{}, ErrUnknown
	}
	return protocol.Config{
		Player: p.Shooter, Team: p.Team, FriendlyFire: m.cfg.FriendlyFire,
		MaxHealth: 100, RespawnMs: 10000,
		DamageTable: [8]uint8{10, 20, 34, 50, 75, 100, 5, 1},
		Mode:        protocol.DefaultModes(),
	}, nil
}

// Players and times for persistence.
func (m *Match) Times() (State, time.Time, time.Time) {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.state, m.started, m.ended
}
