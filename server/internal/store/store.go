// Package store keeps matches in one SQLite file: the roster, the lifecycle
// and every counted hit report. Scores are not stored; they are rebuilt by
// replaying the reports through the match rules, so a fix to the rules also
// fixes the history.
package store

import (
	"database/sql"
	"encoding/json"
	"time"

	_ "modernc.org/sqlite"

	"github.com/scshafe/lightfight/server/internal/match"
)

type Store struct{ db *sql.DB }

const schema = `
CREATE TABLE IF NOT EXISTS matches (
  id TEXT PRIMARY KEY, config TEXT NOT NULL, state TEXT NOT NULL,
  started_ms INTEGER NOT NULL DEFAULT 0, ended_ms INTEGER NOT NULL DEFAULT 0,
  created_ms INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS players (
  match_id TEXT NOT NULL, id TEXT NOT NULL, callsign TEXT NOT NULL,
  shooter INTEGER NOT NULL, team INTEGER NOT NULL, PRIMARY KEY (match_id, id));
CREATE TABLE IF NOT EXISTS events (
  n INTEGER PRIMARY KEY AUTOINCREMENT, match_id TEXT NOT NULL, key TEXT NOT NULL,
  report TEXT NOT NULL, UNIQUE (match_id, key));`

// Open opens (creating it) the database file; ":memory:" is for tests.
func Open(path string) (*Store, error) {
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, err
	}
	// one writer at a time; also keeps ":memory:" a single database
	db.SetMaxOpenConns(1)
	for _, q := range []string{`PRAGMA journal_mode=WAL`, `PRAGMA busy_timeout=5000`, schema} {
		if _, err := db.Exec(q); err != nil {
			db.Close()
			return nil, err
		}
	}
	return &Store{db}, nil
}

func (s *Store) Close() error { return s.db.Close() }

func ms(t time.Time) int64 {
	if t.IsZero() {
		return 0
	}
	return t.UnixMilli()
}

func fromMs(v int64) time.Time {
	if v == 0 {
		return time.Time{}
	}
	return time.UnixMilli(v).UTC()
}

func (s *Store) SaveMatch(id string, cfg match.Config, state match.State, started, ended time.Time) error {
	b, err := json.Marshal(cfg)
	if err != nil {
		return err
	}
	_, err = s.db.Exec(`INSERT INTO matches (id, config, state, started_ms, ended_ms, created_ms)
VALUES (?, ?, ?, ?, ?, ?)
ON CONFLICT(id) DO UPDATE SET state=excluded.state, started_ms=excluded.started_ms, ended_ms=excluded.ended_ms`,
		id, string(b), string(state), ms(started), ms(ended), time.Now().UnixMilli())
	return err
}

func (s *Store) SavePlayer(matchID string, p match.Player) error {
	_, err := s.db.Exec(`INSERT OR REPLACE INTO players (match_id, id, callsign, shooter, team) VALUES (?, ?, ?, ?, ?)`,
		matchID, p.ID, p.Callsign, p.Shooter, p.Team)
	return err
}

// AddReport stores a counted report once (a repeat of its key is ignored).
func (s *Store) AddReport(matchID string, r match.Report) error {
	b, err := json.Marshal(r)
	if err != nil {
		return err
	}
	_, err = s.db.Exec(`INSERT OR IGNORE INTO events (match_id, key, report) VALUES (?, ?, ?)`,
		matchID, r.Key(), string(b))
	return err
}

type Stored struct {
	ID      string
	Config  match.Config
	State   match.State
	Started time.Time
	Ended   time.Time
	Players []match.Player
	Reports []match.Report
}

// LoadAll returns every match with its roster and reports in the order counted.
func (s *Store) LoadAll() ([]Stored, error) {
	rows, err := s.db.Query(`SELECT id, config, state, started_ms, ended_ms FROM matches ORDER BY created_ms, id`)
	if err != nil {
		return nil, err
	}
	var out []Stored
	for rows.Next() {
		var m Stored
		var cfg, state string
		var st, en int64
		if err := rows.Scan(&m.ID, &cfg, &state, &st, &en); err != nil {
			rows.Close()
			return nil, err
		}
		if err := json.Unmarshal([]byte(cfg), &m.Config); err != nil {
			rows.Close()
			return nil, err
		}
		m.State, m.Started, m.Ended = match.State(state), fromMs(st), fromMs(en)
		out = append(out, m)
	}
	if err := rows.Close(); err != nil {
		return nil, err
	}
	for i := range out {
		m := &out[i]
		prow, err := s.db.Query(`SELECT id, callsign, shooter, team FROM players WHERE match_id=? ORDER BY shooter`, m.ID)
		if err != nil {
			return nil, err
		}
		for prow.Next() {
			var p match.Player
			if err := prow.Scan(&p.ID, &p.Callsign, &p.Shooter, &p.Team); err != nil {
				prow.Close()
				return nil, err
			}
			m.Players = append(m.Players, p)
		}
		prow.Close()
		erow, err := s.db.Query(`SELECT report FROM events WHERE match_id=? ORDER BY n`, m.ID)
		if err != nil {
			return nil, err
		}
		for erow.Next() {
			var js string
			var r match.Report
			if err := erow.Scan(&js); err != nil {
				erow.Close()
				return nil, err
			}
			if err := json.Unmarshal([]byte(js), &r); err != nil {
				erow.Close()
				return nil, err
			}
			m.Reports = append(m.Reports, r)
		}
		erow.Close()
	}
	return out, nil
}
