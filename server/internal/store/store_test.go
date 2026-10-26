package store

import (
	"path/filepath"
	"testing"
	"time"

	"github.com/scshafe/lightfight/server/internal/match"
)

func TestRoundTripAcrossReopen(t *testing.T) {
	path := filepath.Join(t.TempDir(), "lf.db")
	s, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	cfg := match.DefaultConfig(match.TeamDeathmatch)
	start := time.UnixMilli(1_700_000_000_000).UTC()
	if err := s.SaveMatch("m1", cfg, match.Running, start, time.Time{}); err != nil {
		t.Fatal(err)
	}
	for _, p := range []match.Player{{ID: "b", Callsign: "Bolt", Shooter: 1, Team: 1}, {ID: "a", Callsign: "Ace", Shooter: 0, Team: 0}} {
		if err := s.SavePlayer("m1", p); err != nil {
			t.Fatal(err)
		}
	}
	r1 := match.Report{Target: "b", EventID: 1, Shooter: 0, Seq: 2, Damage: 10, HealthAfter: 90, TMs: 100}
	r2 := match.Report{Target: "b", EventID: 2, Shooter: 0, Seq: 3, Down: true, TMs: 400}
	for _, r := range []match.Report{r1, r2, r1} { // r1 twice: stored once
		if err := s.AddReport("m1", r); err != nil {
			t.Fatal(err)
		}
	}
	if err := s.SaveMatch("m1", cfg, match.Finished, start, start.Add(time.Minute)); err != nil {
		t.Fatal(err)
	}
	s.Close()

	s, err = Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	all, err := s.LoadAll()
	if err != nil || len(all) != 1 {
		t.Fatalf("%v %v", all, err)
	}
	m := all[0]
	if m.State != match.Finished || !m.Started.Equal(start) || !m.Ended.Equal(start.Add(time.Minute)) {
		t.Errorf("lifecycle: %+v", m)
	}
	if m.Config.Mode != match.TeamDeathmatch || m.Config.HitPoints != 1 {
		t.Errorf("config: %+v", m.Config)
	}
	if len(m.Players) != 2 || m.Players[0].Callsign != "Ace" {
		t.Errorf("players: %+v", m.Players)
	}
	if len(m.Reports) != 2 || m.Reports[0] != r1 || m.Reports[1] != r2 {
		t.Errorf("reports: %+v", m.Reports)
	}
}

func TestMemory(t *testing.T) {
	s, err := Open(":memory:")
	if err != nil {
		t.Fatal(err)
	}
	defer s.Close()
	all, err := s.LoadAll()
	if err != nil || len(all) != 0 {
		t.Fatal(all, err)
	}
}
