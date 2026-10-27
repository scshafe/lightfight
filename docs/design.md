# lightfight design

Status: **accepted** by the owner on 2026-10-26, all 11 Owner decisions as
recommended. `docs/brief.md` is fixed; this file is kept in
step with the code. Prices and part choices are estimates to be checked at
purchase time; the owner buys the hardware.

## Owner decisions

Each open question from the brief, with a recommendation and the alternatives.
The owner accepted every recommendation on 2026-10-26; the alternatives are
recorded for reference.

| # | Question | Recommendation | Alternatives |
|---|----------|----------------|--------------|
| 1 | Phone platforms | **iOS only for v1** (SwiftUI, CoreBluetooth). The server and BLE protocols are platform-neutral, so Android can follow. | iOS + Android from the start (a cross-platform toolkit such as Flutter; roughly doubles app and test work). |
| 2 | Phone-to-gear link | **BLE (NimBLE on the ESP32), phone as central, one connection to each of the player's two devices.** Gear plays on its own if the link drops (see Offline). | Blaster-to-vest link, phone talks to one device (fewer phone connections, more firmware). Wi-Fi (more power, needs a network). |
| 3 | IR protocol | **Custom frame on a 38 kHz carrier** with a CRC and a sequence number (see IR protocol). | MilesTag II compatible (works with commercial gear, but its shot packet has no error check, so it cannot meet "a miss never registers" alone). Could be added later as a receive-only extra. |
| 4 | How sensors are worn | **Both:** 3 sensors on a vest (chest, left, right shoulder strap) and 2 on a headband, wired to one vest hub. | Vest only (cheaper, hits to the head side do not count). Headband only (small, poor coverage). |
| 5 | Game modes for v1 | **Free-for-all and team deathmatch**, timed, with respawn. | Add elimination (no respawn) first or instead; capture-style modes need beacons and wait for v2. |
| 6 | Offline play | **Yes, two levels:** gear keeps health/ammo/respawn itself (works with no phone); the host phone can act as match authority over local peer-to-peer when the server is unreachable. | Server only (simplest, no play without the tailnet). Gear-only with no scoreboard. |
| 7 | Players outside the tailnet | **v1: guests join through the host's phone** (host phone relays for them, or guests play on gear alone). | Invite guests to the tailnet (requires an install). A public join endpoint such as Tailscale Funnel with a join code (exposes the server, so an owner decision). |
| 8 | Hardware and budget | **About $45–60 per player**, a 2-player prototype set about $120–150 (see Bill of materials). | A cheaper kit with one device per player (about $30–40, fewer sensors, no wired headband). |
| 9 | Firmware toolchain | **ESP-IDF** with the game and protocol logic in a portable C library tested on the host. | PlatformIO with Arduino-ESP32 and an IR library (faster start, looser timing, harder host tests). |
| 10 | Server stack | **Go, SQLite, WebSocket with JSON**, one container image. | Python (FastAPI) or Node/TypeScript; Postgres (more than a house needs). |
| 11 | Milestones | See Milestones: ten PR-sized steps, most testable without hardware. | Fewer, larger steps. |

Further owner steps are listed under Owner steps at the end.

## Architecture

```
 blaster (ESP32-C3) --BLE--+
   trigger, IR emitter     |
 vest hub (ESP32-C3) --BLE-+-- iPhone (HUD) --WebSocket--> match server
   5 IR receivers          |      health, ammo, score        on <tailnet>
   haptics, LEDs           |
        ^                  +-- (offline) host iPhone as authority
        | IR (940 nm, 38 kHz)
 other players' blasters
```

- **Gear** (firmware): the blaster emits a frame when the trigger is pulled;
  the vest hub receives frames, decides whether a hit landed, and holds the
  player's health, ammo and alive/dead state. Gear gives immediate feedback
  (haptics, LEDs, buzzer) without waiting for a phone.
- **Phone**: HUD (health, ammo, score, match state), pairing, configuration
  (player id, team, mode), and the gateway to the server. It sends rule
  parameters to the gear and receives events from it.
- **Server**: owns matches: roster, teams, start/stop, ordering and
  de-duplication of events, scores, history. It does not sit in the hit path,
  so latency or loss to the server never changes whether a hit counted.
- **Who decides what:** the *target's* vest hub decides "I was hit" (it is
  the only device that saw the frame). The server decides scores and match
  results from those reports.

### How a hit travels

1. Trigger pulled. The blaster checks ammo, fire-rate cap and alive state
   locally, then emits one IR frame: shooter id, team, damage code, sequence.
2. A vest sensor demodulates and the hub decodes the frame, checks the CRC,
   and drops it unless it is valid, not from itself, not friendly fire (if
   off), the player is alive, and `(shooter, sequence)` was not seen in the
   last second (several sensors see one shot; the blaster may also repeat
   the frame, see below).
3. The hub subtracts damage, plays feedback, and sends a `hit` event over
   BLE: `{target, shooter, seq, damage, health_after, t}`. Kills send `down`.
4. The phone shows it and forwards it over WebSocket. The server stores it
   under the idempotent key `(target, gear event id, gear uptime)` (the IR `seq` wraps every 16 shots, so it cannot be the key alone) and updates the
   scoreboard; the shooter's phone receives "hit confirmed" and the kill.
5. Events are queued on the phone (and the last N on the hub) and replayed
   after a drop; the idempotent key makes replay safe.

## IR protocol

Custom (decision 3). Goals: correct or nothing, cheap parts, simple to decode
on a hobby MCU.

- Carrier 38 kHz, 940 nm, so ordinary 38 kHz receiver modules (TSOP38238
  class) can be used. Pulse-distance coding in the style of NEC: ~560 µs
  mark, space 560 µs for 0 or ~1690 µs for 1; a 3 ms header mark.
- Payload 24 bits: shooter id 6 bits (0–63), team 3 bits (0–7), damage code
  3 bits (table, not raw), sequence 4 bits (wraps), CRC-8 over the first 16
  bits (polynomial fixed in the spec). A frame lasts about 40 ms.
- The receiver ignores any frame with a bad CRC, a wrong length or timing out
  of tolerance (±25%). A miss registers nothing: with no valid frame there is
  no hit. Ambient IR (sun, remotes) fails the CRC or timing.
- Reliability measures: the blaster sends each shot once at a fixed rate cap
  (default 4 shots/s); sequence + id de-duplication on the receiver; several
  sensors OR together after dedupe, so one shot is one hit.
- Hit-by-whom is the shooter id; ids are assigned by the server or host phone
  per match, so ids need not be unique across matches.
- Spec and test vectors live in `docs/ir-protocol.md` and a shared vector file
  (milestone M1) used by the firmware, server and app tests.

## BLE link

- Gear advertises a GATT service with a project 128-bit UUID and name
  `LF-<4 hex>` (a code printed on the label). One service per device with:
  `config` (write: player id, team, damage table, mode, ammo, health),
  `state` (notify: health, ammo, alive, battery), `events` (notify with
  sequence numbers and an `ack` write so nothing is lost on a drop),
  `command` (write: start, stop, respawn, ping, and disable/enable for the blaster).
  `config` and `state` carry the three firing modes (see Firing modes).
  The phone relays the vest's down state to the blaster as disable/enable
  (owner decision 2026-10-26, about 100 ms gap accepted).
- **Pairing (amended 2026-10-26, owner decision):** LE Secure Connections,
  "just works" (the gear has no display, so no passkey), bonded, so a phone
  pairs once and reconnects on its own through iOS state restoration. A new
  phone can pair only in a 60 s pairing window: after power-up with no bonded
  phone, or after holding the BOOT/pair button for 3 s; 10 s forgets all
  phones. Anyone nearby who connects inside the window can pair; the window
  and the button keep that to the owner's own gear. Details:
  `docs/ble-protocol.md`. The HUD shows link state; gear keeps playing when
  the link is down.
- **Latency:** connection interval 15–30 ms gives well under 100 ms from hit
  to HUD, which is not in the hit path. Phone-to-server adds the tailnet RTT.
- Messages are small, versioned, and use the same test vectors as the IR
  frames.

## Firing modes

Owner decisions of 2026-10-26. The blaster has three trigger buttons, one per
firing mode; every mode stamps its own 3-bit damage code in the IR frame, and
the damage table (match setting) turns the code into health points. A hit is
still one frame, so the vest, the hit log and the server need no change.

| Mode | Button | Emitter | Sound | Damage code (default hp) | Magazine | Cooldown | Reload | Auto-reload |
|------|--------|---------|-------|--------------------------|----------|----------|--------|-------------|
| Light | A | narrow LED | none | 0 (10) | 30 | 250 ms | 2 s | on |
| Heavy | B | narrow LED | blaster buzzer, 0.2 s falling chirp | 3 (50) | 5 | 1.5 s | 3 s | off |
| Shotgun | C | wide-angle LED, short range | none | 2 (34) | 2 | 1 s | 3.5 s | off |

- **Cost of the heavy shot:** the trade-off is built from three settings, not
  one: its own small magazine, a long cooldown and a long reload. Magazines are
  separate, so emptying the heavy mode does not stop the light one. Every
  number is a per-match setting (`config`, `docs/ble-protocol.md`); the table is
  the default the server hands out.
- **Rate cap:** besides each mode's cooldown, no two shots of any mode are
  closer than 250 ms. This is the emitter gate's minimum, kept in the rules
  engine too, so a round is never spent on a shot the gate would refuse.
- **Shotgun:** one frame from a second LED with a wide cone and a series
  resistor sized so the frame decodes only within roughly 3-5 m (tuned on the
  bench, B10). It uses its own damage code and every vest in the cone decodes
  it, so one shot can hit several players; each target's gear and the server
  de-duplicate per target as before. It does not need a different frame.
- **Reload:** each mode reloads separately and the others keep firing. A short
  press of the reload button reloads the mode fired last (the light mode before
  any shot); holding it for 0.8 s reloads every mode that is not full. A mode
  with auto-reload starts by itself when its last round is spent, or when the
  trigger is pulled on an empty magazine. Going down cancels reloads; a respawn
  refills every magazine.
- **Sound:** only the heavy mode makes a sound, so others can hear it; the
  sound table is one line per mode in `firmware/idf/main/blaster.c`. A piezo
  on the blaster is a passive one driven with a tone (LEDC), not the vest's.

## Haptics

Owner decision of 2026-10-26: the vest tells the player what happened with
distinct vibration patterns, two coin motors (vest and headband), both driven
by the hub. A headshot buzzes the headband and a body shot the vest; the kill
and the respawn buzz both. The patterns are plain data in
`firmware/lib/lf_haptic.c`, and the logic that picks them is in the portable
library with host tests (`test_haptic`, `test_hub`).

| Cue | When | Motors | Pattern |
|-----|------|--------|---------|
| Light hit | counted hit, damage under 30% of max health (the 10 hp light shot) | the zone hit | one tick, 60 ms |
| Heavy hit | counted hit, damage at least 30% of max health (the 50 hp heavy shot, the 34 hp shotgun) | the zone hit | one buzz, 250 ms |
| Killed | the hit that takes health to 0 | both, plus the hub buzzer | 300 on, 100 off, 300 on, 100 off, 600 on |
| Respawn | the player is alive again | both | double tap: 80 on, 80 off, 80 on |

- **Zones:** sensors on the vest (chest, both shoulders) are body, the two
  headband sensors are head. One shot is read by several sensors and counts
  once (the first to decode it); to decide the zone anyway the cue waits 15 ms
  for the others, and if any headband sensor read the shot it is a headshot,
  whichever decoded first. A shot read by both a body and a headband sensor is
  therefore a headshot: the headband sits above the vest, so a shot that
  reaches it was aimed high.
- **Priority:** a cue never interrupts a higher one (killed above respawn
  above heavy above light); equal cues restart. Stopping the match silences
  the motors.
- **Classification** uses the damage the shot is worth before the health
  clamp, so a light hit on a nearly dead player is still a light cue.

## Game modes (v1)

- **Free-for-all:** timed (default 10 min); points per hit and per down;
  respawn after a delay (default 10 s) with full health and ammo.
- **Team deathmatch:** same, up to 4 teams in v1 (3 bits allow 8); friendly
  fire is a per-match option (default off).
- Common: health 100, damage code table (e.g. 10/20/34), ammo magazine of 30
  with a 2 s reload by button, match countdown, start/stop from the host.
- Not in v1: capture points, flags, medics, beacons, respawn stations.

## Offline play and players outside the tailnet

- **Level 0, gear-only:** health, ammo, respawn and feedback are local to the
  gear, so a game works with no phone, no server. Scores are not shown
  until a phone reconnects and events are replayed.
- **Level 1, host phone as authority:** when the server is unreachable one
  designated iPhone runs the same match engine as the server (a small shared
  rules module) and the other phones talk to it over Apple's
  MultipeerConnectivity (peer-to-peer, no tailnet). The result is uploaded to
  the server afterwards.
- **Guests off the tailnet:** the HUD must work without tailnet access.
  v1: a guest's gear pairs to the host's phone (or the guest uses a spare
  phone of the host's), or the guest plays with Level 0 and a roster
  entry made by the host. Joining the tailnet or a public join endpoint is
  decision 7. No real names are stored: players are a made-up callsign.

## Bill of materials (per player)

| Item | Qty | Est. cost (USD) |
|------|-----|-----------------|
| ESP32-C3 dev board (blaster, vest hub) | 2 | 10 |
| 940 nm IR LED (eye-safe class, rated ≥100 mA) + driver (logic MOSFET, series resistor) + optional lens | 1–2 | 3 |
| 38 kHz IR receiver modules (TSOP38238 class) for vest and headband | 5 | 6 |
| Vibration motor, piezo buzzer, status LEDs, trigger and reload switches | — | 6 |
| 1S Li-ion/LiPo cells with built-in protection (blaster, hub) | 2 | 12 |
| USB-C charger module with charge-controller IC (TP4056-class with DW01 protection or better) | 2 | 4 |
| Blaster extras: wide-angle 940 nm LED + MOSFET + resistor, two more trigger switches, piezo buzzer | — | 4 |
| Battery sense: 2 resistors + capacitor per board | — | 0.5 |
| Second coin vibration motor (headband) + MOSFET + diode | 1 | 1.5 |
| Fuse or PTC per pack, wiring, connectors | — | 3 |
| Enclosures (3D printed), vest and headband webbing | — | 8 |
| **Total** | | **about 59** (range 51–67) |

Prototype: 2 players about $120–150 including spares; tools (soldering,
multimeter, a 940 nm-sensitive phone camera or IR card for checking) are the
owner's. A cheaper kit variant exists (decision 8).

## Safety

- **Emitters:** 940 nm LEDs only, never a laser. Parts are chosen from
  datasheets that give radiant intensity; the design target is the
  exempt group of IEC 62471 for the eye at 200 mm, and the calculation is
  recorded in the PR that fixes the LED and lens, since a lens raises
  radiant intensity in the beam. The design is hobbyist-level and not a
  certified product; the owner should check the figures before use,
  especially around children.
- **Shotgun emitter (eye safety, owner decision 2026-10-26):** a second 940 nm
  LED, wide-angle (half-angle 45-60 degrees), no lens, on its own MOSFET with a
  larger series resistor (20-30 mA instead of up to 100 mA). A wide LED spreads
  its power over a larger solid angle, so its radiant intensity is *lower* than
  the narrow LED's, not higher. Worked example at the closest approach we
  design for (200 mm, from the datasheet typicals of the part classes, to be
  recomputed with the real parts): narrow LED about 100 mW/sr at 100 mA gives
  E = Ie/d^2 = 2.5 W/m^2 while the carrier is on; wide LED about 10-25 mW/sr
  at 25 mA gives 0.25-0.6 W/m^2. The IEC 62471 exempt limit for infrared
  irradiance (780-3000 nm) is 100 W/m^2 for exposures of 1000 s and more and
  higher for short ones, and the shots are short bursts (a frame is under
  50 ms and the gate allows 50 ms of LED on-time per second), so both emitters
  sit more than 100 times under it. The firmware gate is shared: only one
  LED transmits at a time and the on-time budget counts both together. The
  retina thermal limit depends on the chip's radiance, which only the
  datasheet gives; check it when the part is chosen, as for the narrow LED.
- **Current:** the LED current is set in hardware (series resistor sized to the
  datasheet rating; peak current within the part's pulsed rating and the
  average within its continuous rating). A MOSFET gate pull-down keeps the LED
  off if the MCU hangs or boots. Firmware enforces a fire-rate cap and a
  maximum frame duty cycle (frame 40 ms at most every 250 ms, carrier 50%,
  so the average duty stays under about 8%), and a watchdog resets on a stuck
  emitter.
- **Batteries:** only protected cells (over-charge, over-discharge,
  short-circuit), charged through a charger IC, fused or current-limited,
  mounted in an enclosure that holds them in place and is padded away from
  where a fall would crush or puncture them; the vest pocket is on the back
  panel with a rigid plate in front. No unprotected loose cells.
  Firmware shuts the gear down at a low-voltage cutoff above the cell's
  protection threshold and reports battery in `state`.
- The play area rules (no running with the blaster pointed at faces, no
  play near roads) go in the app's first-run screen.

## Firmware toolchain

ESP-IDF (decision 9): the RMT peripheral generates the carrier and times
the receive, NimBLE provides BLE, FreeRTOS tasks per concern. Protocol,
CRC, dedupe, rules and game state are plain C in `firmware/lib/` with no
ESP-IDF includes, built and unit-tested on the host (Unity or a minimal
runner) from `scripts/verify`. ESP-IDF builds run in the `espressif/idf`
container in CI once M5 lands. Flashing is an owner step.

## Server stack

Go (decision 10): one static binary and one container image (the rootless
docker the agent has can build and test it), standard-library HTTP with a
WebSocket library, SQLite for matches and events (single file on a volume),
versioned JSON messages. Deployed via the runner lane
(`dev.toml` `[deploy] lane` moves from `none` to `runner` in the milestone
that adds the deploy workflow; the owner reviews it and registers the
runner). It listens on `lightfight.<tailnet>.ts.net` inside the tailnet
only. Match rules are a Go package with table-driven tests, driven by
simulated players, so no hardware is needed to test it.

## Milestones

Each is one PR, testable without hardware unless marked **(hw)**; hardware
steps need the owner's purchase and flashing.

1. **M0 verify scaffolding:** `scripts/verify` also runs C and Go tests when
   they exist; CI. No behaviour yet.
2. **M1 IR protocol:** `docs/ir-protocol.md`, C codec with CRC, timing
   waveform generation and parse, test vectors, fuzz and noise tests.
3. **M2 rules engine:** C library: health, ammo, respawn, dedupe, damage
   table; host tests including duplicate and out-of-order frames.
4. **M3 BLE message spec:** GATT layout, event ack/replay, shared codec and
   vectors in C, Go and Swift (Swift tests run on the owner's Mac).
5. **M4 server:** Go match server with roster, events, scoreboard, WebSocket,
   SQLite, a simulator client; container image.
6. **M5 firmware skeleton:** ESP-IDF project builds in CI; emitter driver with
   the safety limits as host-tested logic. **(hw)** bench: LED current and
   waveform check.
7. **M6 hit path:** sensor receive and decode on two boards. **(hw)** bench
   range and false-hit/miss counts with a log.
8. **M7 iOS app:** BLE client, HUD, pairing; runs against a mock gear and the
   simulator server. **(hw)** pairing with a real board.
9. **M8 end-to-end:** two players, server, scoreboard; **(hw)** field test and
   a reliability report (hits sent vs counted).
10. **M9 offline and guests:** Level 1 host-phone authority, guest roster, gear-only
    play; deploy workflow for the server (owner reviews).

## Owner steps

- Accept, or amend, the Owner decisions above.
- Buy the prototype hardware (BOM); flash boards (M5 onwards).
- At M4/M9: review the deploy workflow, register the runner, add the node on
  the tailnet.
- Build and run the iOS app on the owner's iPhone (needs the owner's Apple
  developer account and a Mac).
