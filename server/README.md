# server

The match server on the tailnet: it runs matches and keeps score. Go, SQLite,
HTTP and WebSocket with JSON (`docs/design.md`, "Server stack"). It is deployed
later on the PC through the runner lane (a milestone M9 step, reviewed by the
owner).

## Layout

- `protocol/`: the BLE and IR wire formats, tested against `../protocol/*.csv`.
- `internal/match`: the rules of one match (roster, teams, de-duplicated hit
  reports, scores). Pure, no I/O.
- `internal/store`: SQLite persistence. Scores are rebuilt on start by
  replaying the stored reports through the rules.
- `internal/api`: HTTP and WebSocket.
- `internal/sim`, `cmd/lfsim`: simulated phones for tests and for a live check.
- `cmd/lightfightd`: the server binary.

## What the server decides

The *gear* decides that a hit landed; the server only counts what a player's
phone reports, once. A report is identified by the target's gear event id plus
its uptime stamp (`t_ms`), not by the IR `(shooter, seq)` alone: `seq` wraps
every 16 shots. A repeat of a counted report (a BLE replay) is acknowledged as
`duplicate` and changes nothing. A phone can report only its own player's
hits. Friendly fire is off in team deathmatch unless the match turns it on;
free-for-all puts everyone on team 0 with friendly fire on in the gear's
config.

## API

| Request | Effect |
|---------|--------|
| `GET /healthz` | `ok` |
| `POST /api/matches` `{"mode":"ffa"\|"tdm","duration_s":600,"friendly_fire":false,"hit_points":1,"down_points":5}` | create a match in the lobby |
| `GET /api/matches/{id}` | scoreboard: state, remaining seconds, players best first, team scores |
| `POST /api/matches/{id}/players` `{"callsign":"Ace","team":0}` | join the lobby; returns the player (with its IR shooter id) and `config_hex`, the BLE `config` message for the gear |
| `GET /api/matches/{id}/players/{pid}/config` | the same config again |
| `POST /api/matches/{id}/start`, `.../stop` | start (two players needed) or end the match; it also ends when its time is up |
| `GET /ws?match={id}&player={pid}` | WebSocket; omit `player` to spectate |

WebSocket, phone to server: `{"type":"hit","target":"<pid>","event_id":N,"shooter":S,"seq":Q,"damage":D,"health_after":H,"down":false,"t_ms":T}`.
Server to phone: `{"type":"snapshot",...}` on connect and after every change,
`{"type":"ack","event_id":N,"result":"accepted"|"duplicate"|"rejected","reason":...}`
to the sender (the phone then writes the BLE `ack`), and
`{"type":"confirmed","target":"<pid>","down":bool}` to the shooter.

Callsigns are made-up names; the server stores no real names.

## Run and test

```sh
go test -race ./...                       # unit, API and simulated-battle tests
go run ./cmd/lightfightd -db /tmp/lf.db   # serve on :8080
go run ./cmd/lfsim -mode tdm -players 6   # play a simulated battle against it
docker build -t lightfight-server .       # the container image
docker run --rm -p 8080:8080 -v lfdata:/data lightfight-server
```
