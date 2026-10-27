// Package sim plays simulated phones against a server: it creates a match,
// joins players, fires a seeded battle through their WebSockets (with replayed
// reports, as after a BLE reconnect) and checks the final scoreboard against a
// separate count of the reports it generated. Used by the tests and by lfsim.
package sim

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/coder/websocket"

	"github.com/scshafe/lightfight/server/internal/match"
	"github.com/scshafe/lightfight/server/protocol"
)

type Options struct {
	Base         string // http://host:port
	Mode         match.Mode
	Players      int
	Teams        int // team deathmatch: teams to spread players over
	Shots        int
	Seed         int64
	Replays      int  // reports sent a second time, as after a reconnect
	FriendlyFire bool // team deathmatch
}

type Result struct {
	Snapshot   match.Snapshot
	Expected   map[string]match.Stats // by player id
	Sent, Dups int
}

type joined struct {
	ID      string
	Shooter uint8
	Team    uint8
	Name    string
}

func call(ctx context.Context, method, u string, in, out any) error {
	var body io.Reader
	if in != nil {
		b, _ := json.Marshal(in)
		body = bytes.NewReader(b)
	}
	req, err := http.NewRequestWithContext(ctx, method, u, body)
	if err != nil {
		return err
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	if resp.StatusCode >= 300 {
		return fmt.Errorf("%s %s: %s: %s", method, u, resp.Status, strings.TrimSpace(string(data)))
	}
	if out != nil {
		return json.Unmarshal(data, out)
	}
	return nil
}

type conn struct {
	c    *websocket.Conn
	acks chan map[string]any
}

func dial(ctx context.Context, base, matchID, player string) (*conn, error) {
	u, err := url.Parse(base)
	if err != nil {
		return nil, err
	}
	u.Scheme = strings.Replace(u.Scheme, "http", "ws", 1)
	u.Path = "/ws"
	u.RawQuery = url.Values{"match": {matchID}, "player": {player}}.Encode()
	c, _, err := websocket.Dial(ctx, u.String(), nil)
	if err != nil {
		return nil, err
	}
	cn := &conn{c: c, acks: make(chan map[string]any, 4096)}
	go func() {
		for {
			_, data, err := c.Read(context.Background())
			if err != nil {
				close(cn.acks)
				return
			}
			var m map[string]any
			if json.Unmarshal(data, &m) == nil && m["type"] == "ack" {
				cn.acks <- m
			}
		}
	}()
	return cn, nil
}

// Battle generates the gear-side reports of a seeded fight: each target's
// gear numbers its own events, tracks its own health and goes down at zero.
func Battle(rng *rand.Rand, ps []joined, friendly bool, shots int) []match.Report {
	health := map[string]int{}
	eventID := map[string]uint16{}
	seq := map[uint8]uint8{}
	for _, p := range ps {
		health[p.ID] = 100
	}
	modes := protocol.DefaultModes()
	table := [8]int{10, 20, 34, 50, 75, 100, 5, 1}
	var out []match.Report
	for i := 0; i < shots; i++ {
		sh := ps[rng.Intn(len(ps))]
		mode := modes[rng.Intn(protocol.Modes)]
		dmg := table[mode.DamageCode]
		// the shotgun's wide cone can catch several players with one shot
		n := 1
		if mode.DamageCode == modes[protocol.ModeShotgun].DamageCode {
			n = 1 + rng.Intn(3)
		}
		for j := 0; j < n; j++ {
			tg := ps[rng.Intn(len(ps))]
			if sh.ID == tg.ID || (!friendly && sh.Team == tg.Team) {
				continue
			}
			health[tg.ID] -= dmg
			down := health[tg.ID] <= 0
			if down {
				health[tg.ID] = 100 // the real gear respawns after a delay
			}
			eventID[tg.ID]++
			if eventID[tg.ID] == 0 {
				eventID[tg.ID] = 1
			}
			out = append(out, match.Report{
				Target: tg.ID, EventID: eventID[tg.ID], Shooter: sh.Shooter, Seq: seq[sh.Shooter] & 15,
				Damage: uint8(dmg), HealthAfter: uint16(max(health[tg.ID], 0)), Down: down, TMs: uint32(1000 + i*300),
			})
		}
		seq[sh.Shooter]++
	}
	return out
}

// Expected counts the reports with the scoring rules written out separately
// from internal/match, so the two have to agree.
func Expected(ps []joined, reports []match.Report, friendly bool, hitPts, downPts int) map[string]match.Stats {
	by := map[uint8]*joined{}
	team := map[string]uint8{}
	for i := range ps {
		by[ps[i].Shooter] = &ps[i]
		team[ps[i].ID] = ps[i].Team
	}
	exp := map[string]match.Stats{}
	for _, r := range reports {
		sh := by[r.Shooter]
		tg := exp[r.Target]
		tg.HitsTaken++
		if r.Down {
			tg.Deaths++
		}
		exp[r.Target] = tg
		s := exp[sh.ID]
		s.Hits++
		isFriendly := friendly && team[sh.ID] == team[r.Target]
		if !isFriendly {
			s.Score += hitPts
		}
		if r.Down {
			s.Downs++
			if !isFriendly {
				s.Score += downPts
			}
		}
		exp[sh.ID] = s
	}
	return exp
}

// Run plays one battle and returns the server's scoreboard with the expected
// stats; it returns an error if they differ.
func Run(ctx context.Context, o Options) (Result, error) {
	ctx, cancel := context.WithTimeout(ctx, time.Minute)
	defer cancel()
	rng := rand.New(rand.NewSource(o.Seed))
	var snap match.Snapshot
	cfg := map[string]any{"mode": o.Mode, "duration_s": 600, "friendly_fire": o.FriendlyFire}
	if err := call(ctx, "POST", o.Base+"/api/matches", cfg, &snap); err != nil {
		return Result{}, err
	}
	var ps []joined
	for i := 0; i < o.Players; i++ {
		team := uint8(0)
		if o.Mode == match.TeamDeathmatch && o.Teams > 0 {
			team = uint8(i % o.Teams)
		}
		var jr struct{ Player match.Player }
		req := map[string]any{"callsign": fmt.Sprintf("sim%02d", i+1), "team": team}
		if err := call(ctx, "POST", o.Base+"/api/matches/"+snap.ID+"/players", req, &jr); err != nil {
			return Result{}, err
		}
		ps = append(ps, joined{jr.Player.ID, jr.Player.Shooter, jr.Player.Team, jr.Player.Callsign})
	}
	if err := call(ctx, "POST", o.Base+"/api/matches/"+snap.ID+"/start", nil, &snap); err != nil {
		return Result{}, err
	}
	conns := map[string]*conn{}
	for _, p := range ps {
		c, err := dial(ctx, o.Base, snap.ID, p.ID)
		if err != nil {
			return Result{}, err
		}
		defer c.c.CloseNow()
		conns[p.ID] = c
	}

	friendly := snap.Config.FriendlyFire
	reports := Battle(rng, ps, friendly, o.Shots)
	send := append([]match.Report(nil), reports...)
	for i := 0; i < o.Replays && len(reports) > 0; i++ {
		send = append(send, reports[rng.Intn(len(reports))]) // a replayed event
	}
	sentTo := map[string]int{}
	dups := 0
	seen := map[string]bool{}
	for _, r := range send {
		b, _ := json.Marshal(struct {
			Type string `json:"type"`
			match.Report
		}{"hit", r})
		if err := conns[r.Target].c.Write(ctx, websocket.MessageText, b); err != nil {
			return Result{}, err
		}
		sentTo[r.Target]++
		if seen[r.Key()] {
			dups++
		}
		seen[r.Key()] = true
	}
	accepted, duplicates := 0, 0
	for id, n := range sentTo {
		for i := 0; i < n; i++ {
			select {
			case a, ok := <-conns[id].acks:
				if !ok {
					return Result{}, fmt.Errorf("connection closed before all acks")
				}
				switch a["result"] {
				case "accepted":
					accepted++
				case "duplicate":
					duplicates++
				default:
					return Result{}, fmt.Errorf("unexpected ack %v", a)
				}
			case <-ctx.Done():
				return Result{}, ctx.Err()
			}
		}
	}
	res := Result{Sent: len(send), Dups: dups}
	if accepted != len(reports) || duplicates != dups {
		return res, fmt.Errorf("acks: %d accepted, %d duplicate; want %d and %d", accepted, duplicates, len(reports), dups)
	}
	if err := call(ctx, "GET", o.Base+"/api/matches/"+snap.ID, nil, &res.Snapshot); err != nil {
		return res, err
	}
	res.Expected = Expected(ps, reports, friendly && o.Mode == match.TeamDeathmatch, snap.Config.HitPoints, snap.Config.DownPoints)
	for _, p := range res.Snapshot.Players {
		if p.Stats != res.Expected[p.ID] {
			return res, fmt.Errorf("%s: server %+v, expected %+v", p.Callsign, p.Stats, res.Expected[p.ID])
		}
	}
	return res, nil
}
