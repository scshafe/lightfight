// Package api serves matches over HTTP (lobby, start, stop, scoreboard) and
// WebSocket (a phone reports its player's hits and receives the scoreboard).
// The server is on the tailnet only; it holds no secrets and the ids it hands
// out only tell players apart.
package api

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/coder/websocket"

	"github.com/scshafe/lightfight/server/internal/match"
	"github.com/scshafe/lightfight/server/internal/store"
)

const maxBody = 4 << 10

type Server struct {
	store *store.Store
	now   func() time.Time

	mu      sync.Mutex
	matches map[string]*entry
}

type entry struct {
	m     *match.Match
	mu    sync.Mutex // guards subs, timer
	subs  map[*sub]struct{}
	timer *time.Timer
}

type sub struct {
	player string // empty: a spectator
	out    chan []byte
}

// New loads every stored match, rebuilding scores by replaying their reports,
// and restarts the clocks of the ones still running.
func New(st *store.Store, now func() time.Time) (*Server, error) {
	s := &Server{store: st, now: now, matches: map[string]*entry{}}
	all, err := st.LoadAll()
	if err != nil {
		return nil, err
	}
	for _, sm := range all {
		m, err := match.New(sm.ID, sm.Config, now)
		if err != nil {
			return nil, err
		}
		for _, p := range sm.Players {
			m.Restore(p)
		}
		for _, r := range sm.Reports {
			m.Replay(r)
		}
		m.RestoreState(sm.State, sm.Started, sm.Ended)
		e := &entry{m: m, subs: map[*sub]struct{}{}}
		s.matches[sm.ID] = e
		if sm.State == match.Running {
			s.arm(e)
		}
	}
	return s, nil
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, _ *http.Request) { w.Write([]byte("ok\n")) })
	mux.HandleFunc("POST /api/matches", s.create)
	mux.HandleFunc("GET /api/matches/{id}", s.get)
	mux.HandleFunc("POST /api/matches/{id}/players", s.join)
	mux.HandleFunc("GET /api/matches/{id}/players/{pid}/config", s.gearConfig)
	mux.HandleFunc("POST /api/matches/{id}/start", s.start)
	mux.HandleFunc("POST /api/matches/{id}/stop", s.stop)
	mux.HandleFunc("GET /ws", s.ws)
	return mux
}

func newID(n int) string {
	b := make([]byte, n)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}

func reply(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func fail(w http.ResponseWriter, code int, msg string) {
	reply(w, code, map[string]string{"error": msg})
}

func decode(w http.ResponseWriter, r *http.Request, v any) bool {
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, maxBody))
	dec.DisallowUnknownFields()
	if err := dec.Decode(v); err != nil {
		fail(w, http.StatusBadRequest, "bad request body")
		return false
	}
	return true
}

func (s *Server) entry(w http.ResponseWriter, r *http.Request) *entry {
	s.mu.Lock()
	e := s.matches[r.PathValue("id")]
	s.mu.Unlock()
	if e == nil {
		fail(w, http.StatusNotFound, "no such match")
	}
	return e
}

func (s *Server) create(w http.ResponseWriter, r *http.Request) {
	var cfg match.Config
	if !decode(w, r, &cfg) {
		return
	}
	if cfg.DurationSec == 0 {
		cfg.DurationSec = 600
	}
	if cfg.HitPoints == 0 && cfg.DownPoints == 0 {
		cfg.HitPoints, cfg.DownPoints = 1, 5
	}
	m, err := match.New(newID(3), cfg, s.now)
	if err != nil {
		fail(w, http.StatusBadRequest, err.Error())
		return
	}
	if err := s.store.SaveMatch(m.ID, m.Config(), match.Lobby, time.Time{}, time.Time{}); err != nil {
		fail(w, http.StatusInternalServerError, "storage error")
		return
	}
	s.mu.Lock()
	s.matches[m.ID] = &entry{m: m, subs: map[*sub]struct{}{}}
	s.mu.Unlock()
	reply(w, http.StatusCreated, m.Snapshot())
}

func (s *Server) get(w http.ResponseWriter, r *http.Request) {
	if e := s.entry(w, r); e != nil {
		e.m.Expire()
		reply(w, http.StatusOK, e.m.Snapshot())
	}
}

type joinRequest struct {
	Callsign string `json:"callsign"`
	Team     uint8  `json:"team"`
}

type joinReply struct {
	Player    match.Player `json:"player"`
	ConfigHex string       `json:"config_hex"` // write to the gear's config characteristic
}

func (s *Server) join(w http.ResponseWriter, r *http.Request) {
	e := s.entry(w, r)
	if e == nil {
		return
	}
	var req joinRequest
	if !decode(w, r, &req) {
		return
	}
	p, err := e.m.Join(newID(4), req.Callsign, req.Team)
	if err != nil {
		code := http.StatusBadRequest
		if errors.Is(err, match.ErrState) || errors.Is(err, match.ErrFull) {
			code = http.StatusConflict
		}
		fail(w, code, err.Error())
		return
	}
	if err := s.store.SavePlayer(e.m.ID, p); err != nil {
		fail(w, http.StatusInternalServerError, "storage error")
		return
	}
	s.broadcast(e)
	reply(w, http.StatusCreated, joinReply{p, s.configHex(e, p.ID)})
}

func (s *Server) configHex(e *entry, player string) string {
	c, err := e.m.GearConfig(player)
	if err != nil {
		return ""
	}
	b, err := c.Encode()
	if err != nil {
		return ""
	}
	return hex.EncodeToString(b)
}

func (s *Server) gearConfig(w http.ResponseWriter, r *http.Request) {
	e := s.entry(w, r)
	if e == nil {
		return
	}
	c, err := e.m.GearConfig(r.PathValue("pid"))
	if err != nil {
		fail(w, http.StatusNotFound, "no such player")
		return
	}
	reply(w, http.StatusOK, map[string]any{"config": c, "config_hex": s.configHex(e, r.PathValue("pid"))})
}

func (s *Server) persistState(e *entry) {
	st, started, ended := e.m.Times()
	if err := s.store.SaveMatch(e.m.ID, e.m.Config(), st, started, ended); err != nil {
		log.Printf("match %s: saving state: %v", e.m.ID, err)
	}
}

func (s *Server) start(w http.ResponseWriter, r *http.Request) {
	e := s.entry(w, r)
	if e == nil {
		return
	}
	if err := e.m.Start(); err != nil {
		fail(w, http.StatusConflict, "needs a lobby with at least two players")
		return
	}
	s.persistState(e)
	s.arm(e)
	s.broadcast(e)
	reply(w, http.StatusOK, e.m.Snapshot())
}

func (s *Server) stop(w http.ResponseWriter, r *http.Request) {
	e := s.entry(w, r)
	if e == nil {
		return
	}
	if err := e.m.Stop(); err != nil {
		fail(w, http.StatusConflict, "match has not started")
		return
	}
	s.persistState(e)
	s.broadcast(e)
	reply(w, http.StatusOK, e.m.Snapshot())
}

// arm ends the match when its time is up.
func (s *Server) arm(e *entry) {
	snap := e.m.Snapshot()
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.timer != nil {
		e.timer.Stop()
	}
	e.timer = time.AfterFunc(time.Duration(snap.RemainingS)*time.Second+50*time.Millisecond, func() {
		if e.m.Expire() {
			s.persistState(e)
			s.broadcast(e)
		}
	})
}

func (s *Server) broadcast(e *entry) {
	msg, _ := json.Marshal(struct {
		Type string `json:"type"`
		match.Snapshot
	}{"snapshot", e.m.Snapshot()})
	e.mu.Lock()
	defer e.mu.Unlock()
	for c := range e.subs {
		e.send(c, msg)
	}
}

// send never blocks on a slow phone: it drops the message and, if the phone's
// queue stays full, the connection (it reconnects and gets a fresh snapshot).
func (e *entry) send(c *sub, msg []byte) {
	if _, ok := e.subs[c]; !ok {
		return // already dropped or gone
	}
	select {
	case c.out <- msg:
	default:
		log.Printf("match %s: dropping a slow subscriber", e.m.ID)
		close(c.out)
		delete(e.subs, c)
	}
}

type wsIn struct {
	Type string `json:"type"`
	match.Report
}

func (s *Server) ws(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	e := s.matches[r.URL.Query().Get("match")]
	s.mu.Unlock()
	if e == nil {
		fail(w, http.StatusNotFound, "no such match")
		return
	}
	player := r.URL.Query().Get("player")
	if player != "" {
		if _, err := e.m.GearConfig(player); err != nil {
			fail(w, http.StatusNotFound, "no such player")
			return
		}
	}
	conn, err := websocket.Accept(w, r, nil)
	if err != nil {
		return
	}
	defer conn.CloseNow()
	conn.SetReadLimit(maxBody)
	c := &sub{player: player, out: make(chan []byte, 32)}
	e.mu.Lock()
	e.subs[c] = struct{}{}
	e.mu.Unlock()
	defer func() {
		e.mu.Lock()
		if _, ok := e.subs[c]; ok {
			delete(e.subs, c)
			close(c.out)
		}
		e.mu.Unlock()
	}()

	s.serve(r.Context(), conn, e, c)
}

type ackOut struct {
	Type    string `json:"type"`
	EventID uint16 `json:"event_id"`
	match.Outcome
}

// serve sends the current snapshot, then reads the phone's messages until it
// goes away while a second goroutine drains the outgoing queue.
func (s *Server) serve(ctx context.Context, conn *websocket.Conn, e *entry, c *sub) {
	e.m.Expire()
	first, _ := json.Marshal(struct {
		Type string `json:"type"`
		match.Snapshot
	}{"snapshot", e.m.Snapshot()})
	e.mu.Lock()
	e.send(c, first)
	e.mu.Unlock()

	go func() {
		for msg := range c.out {
			wctx, cancel := context.WithTimeout(ctx, 5*time.Second)
			err := conn.Write(wctx, websocket.MessageText, msg)
			cancel()
			if err != nil {
				conn.CloseNow()
				return
			}
		}
	}()

	for {
		_, data, err := conn.Read(ctx)
		if err != nil {
			return
		}
		var in wsIn
		if json.Unmarshal(data, &in) != nil || in.Type != "hit" {
			s.reply(e, c, ackOut{Type: "error", Outcome: match.Outcome{Result: match.Rejected, Reason: "expected a hit message"}})
			continue
		}
		if c.player == "" || in.Target != c.player {
			s.reply(e, c, ackOut{Type: "ack", EventID: in.EventID, Outcome: match.Outcome{Result: match.Rejected, Reason: "a phone reports only its own player's hits"}})
			continue
		}
		out := e.m.Apply(in.Report)
		if out.Result == match.Accepted {
			if err := s.store.AddReport(e.m.ID, in.Report); err != nil {
				log.Printf("match %s: saving a report: %v", e.m.ID, err)
			}
		}
		s.reply(e, c, ackOut{Type: "ack", EventID: in.EventID, Outcome: out})
		if out.Result == match.Accepted {
			s.confirm(e, out.Shooter, in.Report)
			s.broadcast(e)
		}
	}
}

func (s *Server) reply(e *entry, c *sub, v any) {
	msg, _ := json.Marshal(v)
	e.mu.Lock()
	e.send(c, msg)
	e.mu.Unlock()
}

// confirm tells the shooter's phone its shot counted.
func (s *Server) confirm(e *entry, shooter string, r match.Report) {
	msg, _ := json.Marshal(map[string]any{"type": "confirmed", "target": r.Target, "down": r.Down})
	e.mu.Lock()
	defer e.mu.Unlock()
	for c := range e.subs {
		if c.player == shooter {
			e.send(c, msg)
		}
	}
}
