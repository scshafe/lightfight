package match

import (
	"testing"
	"time"
)

type clock struct{ t time.Time }

func (c *clock) now() time.Time { return c.t }

func newMatch(t *testing.T, mode Mode, mutate func(*Config)) (*Match, *clock) {
	t.Helper()
	cfg := DefaultConfig(mode)
	if mutate != nil {
		mutate(&cfg)
	}
	c := &clock{time.Date(2026, 1, 1, 12, 0, 0, 0, time.UTC)}
	m, err := New("m1", cfg, c.now)
	if err != nil {
		t.Fatal(err)
	}
	return m, c
}

func join(t *testing.T, m *Match, id, call string, team uint8) Player {
	t.Helper()
	p, err := m.Join(id, call, team)
	if err != nil {
		t.Fatal(err)
	}
	return p
}

func TestFreeForAllScoring(t *testing.T) {
	m, _ := newMatch(t, FreeForAll, nil)
	a := join(t, m, "a", "Ace", 5) // team is forced to 0
	b := join(t, m, "b", "Bolt", 2)
	if a.Team != 0 || b.Team != 0 || a.Shooter != 0 || b.Shooter != 1 {
		t.Fatalf("ids: %+v %+v", a, b)
	}
	if err := m.Start(); err != nil {
		t.Fatal(err)
	}
	// Ace shoots Bolt (target b, shooter 0): a hit, then a down
	if o := m.Apply(Report{Target: "b", EventID: 1, Shooter: 0, Seq: 0, TMs: 100}); o.Result != Accepted || o.Shooter != "a" {
		t.Fatalf("%+v", o)
	}
	if o := m.Apply(Report{Target: "b", EventID: 2, Shooter: 0, Seq: 1, Down: true, TMs: 400}); o.Result != Accepted {
		t.Fatalf("%+v", o)
	}
	s := m.Snapshot()
	if s.Players[0].ID != "a" || s.Players[0].Score != 1+1+5 || s.Players[0].Hits != 2 || s.Players[0].Downs != 1 {
		t.Errorf("leader: %+v", s.Players[0])
	}
	if s.Players[1].HitsTaken != 2 || s.Players[1].Deaths != 1 {
		t.Errorf("target: %+v", s.Players[1])
	}
	if s.Teams != nil {
		t.Errorf("ffa has no team scores: %v", s.Teams)
	}
}

func TestDuplicatesAndSeqWrap(t *testing.T) {
	m, _ := newMatch(t, FreeForAll, nil)
	join(t, m, "a", "Ace", 0)
	join(t, m, "b", "Bolt", 0)
	m.Start()
	r := Report{Target: "b", EventID: 7, Shooter: 0, Seq: 3, TMs: 5000}
	if m.Apply(r).Result != Accepted {
		t.Fatal("first")
	}
	// a replay after a BLE reconnect, any number of times
	for i := 0; i < 3; i++ {
		if m.Apply(r).Result != Duplicate {
			t.Fatal("replay must be a duplicate")
		}
	}
	// 16 shots later the IR seq is the same again, but it is a new event
	again := Report{Target: "b", EventID: 23, Shooter: 0, Seq: 3, TMs: 9000}
	if m.Apply(again).Result != Accepted {
		t.Fatal("wrapped seq is a new hit")
	}
	if got := m.Snapshot().Players[0].Hits; got != 2 {
		t.Errorf("hits = %d, want 2", got)
	}
	// a gear reboot restarts event ids; the uptime stamp tells it apart
	reboot := Report{Target: "b", EventID: 7, Shooter: 0, Seq: 4, TMs: 1200}
	if m.Apply(reboot).Result != Accepted {
		t.Fatal("event id reuse after a reboot is a new hit")
	}
}

func TestRejections(t *testing.T) {
	m, c := newMatch(t, TeamDeathmatch, func(c *Config) { c.DurationSec = 60 })
	join(t, m, "a", "Ace", 0)
	join(t, m, "b", "Bolt", 0)
	join(t, m, "c", "Cleo", 1)
	if o := m.Apply(Report{Target: "c", EventID: 1, Shooter: 0}); o.Result != Rejected {
		t.Errorf("lobby: %+v", o)
	}
	if err := m.Start(); err != nil {
		t.Fatal(err)
	}
	cases := map[string]Report{
		"unknown target":  {Target: "zz", EventID: 1, Shooter: 0},
		"unknown shooter": {Target: "a", EventID: 2, Shooter: 9},
		"self hit":        {Target: "a", EventID: 3, Shooter: 0},
		"friendly":        {Target: "b", EventID: 4, Shooter: 0},
		"range":           {Target: "b", EventID: 5, Shooter: 64},
	}
	for name, r := range cases {
		if o := m.Apply(r); o.Result != Rejected {
			t.Errorf("%s: %+v", name, o)
		}
	}
	for _, p := range m.Snapshot().Players {
		if p.Hits != 0 || p.HitsTaken != 0 {
			t.Errorf("rejections counted: %+v", p)
		}
	}
	// ok after all that, then time runs out
	if o := m.Apply(Report{Target: "c", EventID: 6, Shooter: 0, TMs: 1}); o.Result != Accepted {
		t.Fatalf("%+v", o)
	}
	c.t = c.t.Add(60 * time.Second)
	if o := m.Apply(Report{Target: "c", EventID: 7, Shooter: 0, TMs: 2}); o.Result != Rejected {
		t.Errorf("after time: %+v", o)
	}
	if st, _, _ := m.Times(); st != Finished {
		t.Errorf("state %s", st)
	}
	// a replay of a counted event after the end is still a duplicate, not an error
	if o := m.Apply(Report{Target: "c", EventID: 6, Shooter: 0, TMs: 1}); o.Result != Duplicate {
		t.Errorf("late replay: %+v", o)
	}
}

func TestTeamScoresAndFriendlyFire(t *testing.T) {
	m, _ := newMatch(t, TeamDeathmatch, func(c *Config) { c.FriendlyFire = true })
	join(t, m, "a", "Ace", 0)
	join(t, m, "b", "Bolt", 0)
	join(t, m, "c", "Cleo", 1)
	m.Start()
	m.Apply(Report{Target: "c", EventID: 1, Shooter: 0, Down: true, TMs: 1}) // enemy down: 6
	m.Apply(Report{Target: "b", EventID: 1, Shooter: 0, Down: true, TMs: 2}) // friendly: no points
	m.Apply(Report{Target: "a", EventID: 1, Shooter: 2, TMs: 3})             // Cleo hits Ace: 1
	s := m.Snapshot()
	if len(s.Teams) != 2 || s.Teams[0] != (TeamScore{0, 6}) || s.Teams[1] != (TeamScore{1, 1}) {
		t.Errorf("teams: %+v", s.Teams)
	}
}

func TestLobbyRules(t *testing.T) {
	m, _ := newMatch(t, TeamDeathmatch, nil)
	if err := m.Start(); err == nil {
		t.Error("cannot start empty")
	}
	join(t, m, "a", "Ace", 0)
	if _, err := m.Join("x", "Ace", 0); err == nil {
		t.Error("callsign reuse")
	}
	if _, err := m.Join("x", "", 0); err == nil {
		t.Error("empty callsign")
	}
	if _, err := m.Join("x", "Dan", MaxTeams); err == nil {
		t.Error("team out of range")
	}
	join(t, m, "b", "Bolt", 1)
	if err := m.Start(); err != nil {
		t.Fatal(err)
	}
	if _, err := m.Join("c", "Cleo", 0); err != ErrState {
		t.Errorf("join while running: %v", err)
	}
	if err := m.Stop(); err != nil || m.Stop() != nil {
		t.Error("stop is idempotent")
	}
	if err := m.Start(); err == nil {
		t.Error("restart")
	}
}

func TestFullLobby(t *testing.T) {
	m, _ := newMatch(t, FreeForAll, nil)
	for i := 0; i < MaxPlayers; i++ {
		if _, err := m.Join(string(rune('A'+i/26))+string(rune('a'+i%26)), "p"+string(rune('A'+i/26))+string(rune('a'+i%26)), 0); err != nil {
			t.Fatalf("player %d: %v", i, err)
		}
	}
	if _, err := m.Join("late", "Late", 0); err != ErrFull {
		t.Errorf("65th player: %v", err)
	}
}

func TestBadConfig(t *testing.T) {
	for _, c := range []Config{
		{Mode: "chess", DurationSec: 60},
		{Mode: FreeForAll, DurationSec: 0},
		{Mode: FreeForAll, DurationSec: 60, HitPoints: -1},
	} {
		if _, err := New("x", c, time.Now); err == nil {
			t.Errorf("accepted %+v", c)
		}
	}
}

func TestGearConfig(t *testing.T) {
	m, _ := newMatch(t, FreeForAll, nil)
	join(t, m, "a", "Ace", 0)
	c, err := m.GearConfig("a")
	if err != nil || c.Player != 0 || !c.FriendlyFire {
		t.Errorf("%+v %v", c, err)
	}
	if _, err := c.Encode(); err != nil {
		t.Error(err)
	}
	if _, err := m.GearConfig("nobody"); err != ErrUnknown {
		t.Error(err)
	}
}
