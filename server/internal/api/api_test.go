package api

import (
	"bytes"
	"context"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"
	"time"

	"github.com/coder/websocket"

	"github.com/scshafe/lightfight/server/internal/match"
	"github.com/scshafe/lightfight/server/internal/sim"
	"github.com/scshafe/lightfight/server/internal/store"
	"github.com/scshafe/lightfight/server/protocol"
)

func start(t *testing.T, dbPath string) (*httptest.Server, *Server, *store.Store) {
	t.Helper()
	st, err := store.Open(dbPath)
	if err != nil {
		t.Fatal(err)
	}
	srv, err := New(st, time.Now)
	if err != nil {
		t.Fatal(err)
	}
	ts := httptest.NewServer(srv.Handler())
	return ts, srv, st
}

func do(t *testing.T, method, url string, in, out any) int {
	t.Helper()
	var body bytes.Buffer
	if in != nil {
		json.NewEncoder(&body).Encode(in)
	}
	req, _ := http.NewRequest(method, url, &body)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if out != nil {
		json.NewDecoder(resp.Body).Decode(out)
	}
	return resp.StatusCode
}

// Simulated phones fire a seeded battle with replayed reports; the board must
// equal the independent count, for both modes.
func TestSimulatedBattles(t *testing.T) {
	ts, _, st := start(t, filepath.Join(t.TempDir(), "lf.db"))
	defer ts.Close()
	defer st.Close()
	for _, o := range []sim.Options{
		{Mode: match.FreeForAll, Players: 6, Shots: 400, Replays: 60, Seed: 1},
		{Mode: match.TeamDeathmatch, Players: 6, Teams: 2, Shots: 400, Replays: 60, Seed: 2},
		{Mode: match.TeamDeathmatch, Players: 8, Teams: 4, Shots: 500, Replays: 100, Seed: 3, FriendlyFire: true},
	} {
		o.Base = ts.URL
		res, err := sim.Run(context.Background(), o)
		if err != nil {
			t.Fatalf("%s seed %d: %v", o.Mode, o.Seed, err)
		}
		if res.Dups == 0 || res.Sent < 100 {
			t.Errorf("%s: the battle did not exercise replays: %+v", o.Mode, res)
		}
	}
}

func TestLobbyOverHTTP(t *testing.T) {
	ts, _, st := start(t, ":memory:")
	defer ts.Close()
	defer st.Close()
	var snap match.Snapshot
	if c := do(t, "POST", ts.URL+"/api/matches", map[string]any{"mode": "chess"}, nil); c != 400 {
		t.Errorf("bad mode: %d", c)
	}
	if c := do(t, "POST", ts.URL+"/api/matches", map[string]any{"mode": "ffa", "extra": 1}, nil); c != 400 {
		t.Errorf("unknown field: %d", c)
	}
	if c := do(t, "POST", ts.URL+"/api/matches", map[string]any{"mode": "tdm"}, &snap); c != 201 {
		t.Fatalf("create: %d", c)
	}
	base := ts.URL + "/api/matches/" + snap.ID
	if c := do(t, "GET", ts.URL+"/api/matches/nope", nil, nil); c != 404 {
		t.Errorf("missing: %d", c)
	}
	if c := do(t, "POST", base+"/start", nil, nil); c != 409 {
		t.Errorf("start empty: %d", c)
	}
	var jr joinReply
	if c := do(t, "POST", base+"/players", map[string]any{"callsign": "Ace", "team": 1}, &jr); c != 201 {
		t.Fatalf("join: %d", c)
	}
	if c := do(t, "POST", base+"/players", map[string]any{"callsign": "Ace"}, nil); c != 400 {
		t.Errorf("dup callsign: %d", c)
	}
	// the config the phone writes to the gear decodes with the BLE codec
	raw, err := hex.DecodeString(jr.ConfigHex)
	if err != nil {
		t.Fatal(err)
	}
	cfg, err := protocol.DecodeConfig(raw)
	if err != nil || cfg.Player != jr.Player.Shooter || cfg.Team != 1 || cfg.FriendlyFire {
		t.Errorf("gear config %+v %v", cfg, err)
	}
	var again struct {
		ConfigHex string `json:"config_hex"`
	}
	if c := do(t, "GET", base+"/players/"+jr.Player.ID+"/config", nil, &again); c != 200 || again.ConfigHex != jr.ConfigHex {
		t.Errorf("config route: %d", c)
	}
	do(t, "POST", base+"/players", map[string]any{"callsign": "Bolt"}, nil)
	if c := do(t, "POST", base+"/start", nil, &snap); c != 200 || snap.State != match.Running {
		t.Errorf("start: %d %s", c, snap.State)
	}
	if c := do(t, "POST", base+"/players", map[string]any{"callsign": "Late"}, nil); c != 409 {
		t.Errorf("late join: %d", c)
	}
	if c := do(t, "POST", base+"/stop", nil, &snap); c != 200 || snap.State != match.Finished {
		t.Errorf("stop: %d %s", c, snap.State)
	}
}

func dialWS(t *testing.T, ts *httptest.Server, matchID, player string) *websocket.Conn {
	t.Helper()
	u := "ws" + ts.URL[len("http"):] + "/ws?match=" + matchID
	if player != "" {
		u += "&player=" + player
	}
	c, _, err := websocket.Dial(context.Background(), u, nil)
	if err != nil {
		t.Fatal(err)
	}
	return c
}

func readJSON(t *testing.T, c *websocket.Conn) map[string]any {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	_, data, err := c.Read(ctx)
	if err != nil {
		t.Fatal(err)
	}
	var m map[string]any
	json.Unmarshal(data, &m)
	return m
}

func TestWebSocketRules(t *testing.T) {
	ts, _, st := start(t, ":memory:")
	defer ts.Close()
	defer st.Close()
	var snap match.Snapshot
	do(t, "POST", ts.URL+"/api/matches", map[string]any{"mode": "ffa"}, &snap)
	var a, b joinReply
	do(t, "POST", ts.URL+"/api/matches/"+snap.ID+"/players", map[string]any{"callsign": "Ace"}, &a)
	do(t, "POST", ts.URL+"/api/matches/"+snap.ID+"/players", map[string]any{"callsign": "Bolt"}, &b)
	do(t, "POST", ts.URL+"/api/matches/"+snap.ID+"/start", nil, nil)

	if _, _, err := websocket.Dial(context.Background(), "ws"+ts.URL[4:]+"/ws?match=nope", nil); err == nil {
		t.Error("unknown match accepted")
	}
	ca, cb, spec := dialWS(t, ts, snap.ID, a.Player.ID), dialWS(t, ts, snap.ID, b.Player.ID), dialWS(t, ts, snap.ID, "")
	for _, c := range []*websocket.Conn{ca, cb, spec} {
		defer c.CloseNow()
		if m := readJSON(t, c); m["type"] != "snapshot" || m["state"] != "running" {
			t.Fatalf("first message: %v", m)
		}
	}
	send := func(c *websocket.Conn, v any) {
		b, _ := json.Marshal(v)
		if err := c.Write(context.Background(), websocket.MessageText, b); err != nil {
			t.Fatal(err)
		}
	}
	hit := map[string]any{"type": "hit", "target": b.Player.ID, "event_id": 1, "shooter": a.Player.Shooter, "damage": 10, "health_after": 90, "t_ms": 500}

	// Ace's phone cannot report a hit for Bolt; a spectator cannot report at all
	send(ca, hit)
	if m := readJSON(t, ca); m["type"] != "ack" || m["result"] != "rejected" {
		t.Errorf("foreign report: %v", m)
	}
	send(spec, hit)
	if m := readJSON(t, spec); m["result"] != "rejected" {
		t.Errorf("spectator report: %v", m)
	}
	send(cb, map[string]any{"type": "nonsense"})
	if m := readJSON(t, cb); m["type"] != "error" {
		t.Errorf("bad type: %v", m)
	}

	// Bolt's own phone reports it: acked, the shooter is told, everyone sees the board
	send(cb, hit)
	if m := readJSON(t, cb); m["result"] != "accepted" || m["event_id"] != float64(1) {
		t.Errorf("ack: %v", m)
	}
	if m := readJSON(t, cb); m["type"] != "snapshot" {
		t.Errorf("board for target: %v", m)
	}
	sawConfirm := false
	for i := 0; i < 2; i++ {
		m := readJSON(t, ca)
		sawConfirm = sawConfirm || (m["type"] == "confirmed" && m["target"] == b.Player.ID)
	}
	if !sawConfirm {
		t.Error("the shooter was not told")
	}
	if m := readJSON(t, spec); m["type"] != "snapshot" {
		t.Errorf("board for spectator: %v", m)
	}
	// the replay is acked as a duplicate and changes nothing
	send(cb, hit)
	if m := readJSON(t, cb); m["result"] != "duplicate" {
		t.Errorf("replay: %v", m)
	}
}

// A restart replays the stored reports: same board, and the stored events stay duplicates.
func TestRestartKeepsScores(t *testing.T) {
	path := filepath.Join(t.TempDir(), "lf.db")
	ts, _, st := start(t, path)
	res, err := sim.Run(context.Background(), sim.Options{Base: ts.URL, Mode: match.TeamDeathmatch, Players: 6, Teams: 3, Shots: 300, Replays: 30, Seed: 9})
	if err != nil {
		t.Fatal(err)
	}
	ts.Close()
	st.Close()

	ts2, _, st2 := start(t, path)
	defer ts2.Close()
	defer st2.Close()
	var snap match.Snapshot
	if c := do(t, "GET", ts2.URL+"/api/matches/"+res.Snapshot.ID, nil, &snap); c != 200 {
		t.Fatalf("after restart: %d", c)
	}
	if snap.State != match.Running || len(snap.Players) != 6 {
		t.Fatalf("lifecycle: %+v", snap)
	}
	for i, p := range snap.Players {
		if p.Stats != res.Snapshot.Players[i].Stats || p.ID != res.Snapshot.Players[i].ID {
			t.Errorf("player %d: %+v, was %+v", i, p, res.Snapshot.Players[i])
		}
	}
}

func TestExpiryEndsMatch(t *testing.T) {
	st, _ := store.Open(":memory:")
	defer st.Close()
	now := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	srv, _ := New(st, func() time.Time { return now })
	ts := httptest.NewServer(srv.Handler())
	defer ts.Close()
	var snap match.Snapshot
	do(t, "POST", ts.URL+"/api/matches", map[string]any{"mode": "ffa", "duration_s": 60}, &snap)
	for _, n := range []string{"Ace", "Bolt"} {
		do(t, "POST", ts.URL+"/api/matches/"+snap.ID+"/players", map[string]any{"callsign": n}, nil)
	}
	do(t, "POST", ts.URL+"/api/matches/"+snap.ID+"/start", nil, nil)
	now = now.Add(61 * time.Second)
	do(t, "GET", ts.URL+"/api/matches/"+snap.ID, nil, &snap)
	if snap.State != match.Finished {
		t.Errorf("state %s", snap.State)
	}
}
