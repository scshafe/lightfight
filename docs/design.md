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

## Optics (the lensed main emitter)

Owner design decision of 2026-10-27, from an optics review. The goal is 75 m
feasible and 50 m reliable outdoors. A bare TSAL6200 at about 100 mA reaches
only about 5 m against a receiver facing the sun, because the TSOP threshold
rises from about 0.1 to about 3 mW/m^2 in sunlight (TSOP382 datasheet, Fig. 6).
The light and heavy modes therefore share one narrow emitter behind a lens;
the shotgun keeps its wide LED with no lens.

- **Optics:** a 40 mm diameter lens of about 50 mm focal length (PMMA asphere
  or Fresnel) with the LED at its focus, in a blackened tube with baffles so
  no light leaves except through the lens.
- **Drive:** about 200 mA peak, the TSAL6200's repetitive-pulse rating at 50%
  duty (pulses of 13 us, well under its 100 us limit). Do not exceed it: the
  hardware sets the current (15 ohm, docs/hardware.md), the firmware sets the
  carrier duty to 50% for this LED (33% stays on the wide LED) and the safety
  gate still caps the average. The mean current in a mark is then 100 mA, the
  part's continuous rating.
- **The trade-off:** the lens aperture sets the range and the focal length
  sets the cone width. Intensity grows with the lens area (D^2); the beam's
  half-angle is about the virtual source radius over the focal length
  (1.2 mm / 50 mm, so about 1.4 degrees before blur). A bigger or closer
  lens collects more light and goes further; a longer focal length narrows the
  cone, which is more range per watt but harder to aim, since the cone is the
  hit zone. 40 mm and 50 mm give a cone a player can still aim.

Modelled values (the owner's review: Gaussian beam, 2.4 mm virtual source,
80% lens efficiency): about 12.5 W/sr for the bare LED at 200 mA, scaled by
the lens area to about 32 W/sr. These are models, not measurements; the bench
step B11 measures them.

| Case | Range limit | Hit cone | Margin |
|------|-------------|----------|--------|
| Receiver facing the sun, threshold about 3 mW/m^2 | about 104 m | | |
| Shade | about 146 m | | |
| 50 m, sun | | +-2.0 degrees (3.5 m wide) | 6.4 dB |
| 75 m, sun | | about 3.5 m wide | 2.8 dB |

What I checked of the numbers: 32 W/sr over 3 mW/m^2 gives 103 m, and over
the 50 m and 75 m irradiances (12.8 and 5.7 mW/m^2) it gives margins of 6.3
and 2.8 dB, so the sun rows reproduce. The shade row does not follow from the
datasheet's 0.1 mW/m^2 (that would reach several hundred metres); 146 m
implies a threshold of about 1.5 mW/m^2 in shade, so I took the figure as
given. Two things will move the results: the intensity is roughly
proportional to the LED current, so at a cell voltage of 3.3 V (about 120 mA
through the 15 ohm resistor against about 177 mA at 4.2 V) the range drops by
more than a third; and a constant-current driver would remove that, which I
have not added. B11 measures at two cell voltages for that reason.

Out of scope until decided: sun hoods on the receivers and a back-of-torso
sensor.

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
| 940 nm IR LED (eye-safe class, rated ≥100 mA continuous and 200 mA repetitive) + driver (logic MOSFET, series resistor) | 1–2 | 3 |
| 38 kHz IR receiver modules (TSOP38238 class) for vest and headband | 5 | 6 |
| Vibration motor, piezo buzzer, status LEDs, trigger and reload switches | — | 6 |
| 1S Li-ion/LiPo cells with built-in protection (blaster, hub) | 2 | 12 |
| USB-C charger module with charge-controller IC (TP4056-class with DW01 protection or better) | 2 | 4 |
| Blaster extras: wide-angle 940 nm LED + MOSFET + resistor, two more trigger switches, piezo buzzer | — | 4 |
| Battery sense: 2 resistors + capacitor per board | — | 0.5 |
| Blaster optics: 40 mm, f about 50 mm PMMA lens, printed blackened tube and baffles, 100 uF supply capacitor | 1 | 4 |
| Second coin vibration motor (headband) + MOSFET + diode | 1 | 1.5 |
| Fuse or PTC per pack, wiring, connectors | — | 3 |
| Enclosures (3D printed), vest and headband webbing | — | 8 |
| **Total** | | **about 63** (range 55–71) |

Prototype: 2 players about $130–165 including spares; tools (soldering,
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
- **Lensed main emitter (eye safety, owner decision 2026-10-27):** the lens
  raises the intensity on axis (about 32 W/sr modelled, docs/design.md
  "Optics") but not the power: the beam carries at most what the LED emits,
  collected by a 40 mm aperture. So the point-source rule E = Ie/d^2 is not
  valid at 200 mm (it would give 800 W/m^2 from a source whose whole beam
  holds about 0.12 W); the irradiance there is the beam power over the beam
  area. Worked example, all from the modelled figures:
  - Beam power: 32 W/sr over a +-2.0 degree cone (0.0038 sr) is about 0.12 W
    while the carrier is on. That is already generous: the TSAL6200 emits about
    55 mW at 100 mA, and 80% of roughly twice that is nearer 90 mW.
  - Corneal irradiance at 200 mm: the beam is about 54 mm across there
    (40 mm lens plus the spread), 2.3e-3 m^2, so E = 0.12 / 2.3e-3 = about
    52 W/m^2 with the carrier on. At the lens face (an eye against the lens)
    it is 0.12 / 1.26e-3 = about 96 W/m^2.
  - Time-averaged at the duty cap: the safety gate allows 50 ms of LED
    on-time in any second, 5%; normal fire (a frame has 17 ms of marks, so
    8.5 ms of LED on-time at 50% carrier, at most 4 frames a second) is 3.4%.
    At the 5% cap the average is about 2.6 W/m^2 at 200 mm and 4.8 W/m^2 at the
    face.
  - Against IEC 62471, infrared radiation to the eye (780-3000 nm), exempt
    group: 100 W/m^2 for exposures over 1000 s (and 18000 t^-0.75 W/m^2,
    far higher, for shorter ones). The time-averaged value is about 38 times
    under it at 200 mm and about 21 times at the face; even the carrier-on
    value (96 W/m^2 at the face) does not reach 100 W/m^2.
  - Retinal thermal limit (780-1400 nm): radiance is not raised by a lens
    (it falls by the lens loss), so it is the LED's own. With the 2.4 mm
    virtual source of the model, 0.25 W/sr at 200 mA is about 5.5e4
    W/(m^2 sr); the limit for a source of 12 mrad is 6000/0.012 =
    5e5, so the carrier-on value is 11 times under and the 5% average about
    220 times under. If the real emitting area were only the chip (about
    0.3 mm), the radiance would be 50 times higher and the average only about
    5 times under. The datasheet gives no chip radiance, so this is the
    figure to check with the real part before the lens is used on people.
  - These rest on typical datasheet and modelled values, not measurements. The
    firmware gate (5% cap) is what holds the averages; do not raise it, the
    current or the duty without redoing this.
  - A lens is also a burning glass: pointed at the sun it focuses about 1 W
    into a spot of a millimetre or so at the LED, which can melt the LED and
    the tube and start a fire. The tube takes a lens cap, and the blaster is
    stored and carried with the cap on.
- **Shotgun emitter (eye safety, owner decision 2026-10-26):** a second 940 nm
  LED, wide-angle (half-angle 45-60 degrees), no lens, on its own MOSFET with a
  larger series resistor (20-30 mA instead of up to 100 mA). A wide LED spreads
  its power over a larger solid angle, so its radiant intensity is *lower* than
  the narrow LED's, not higher. Worked example at the closest approach we
  design for (200 mm, from the datasheet typicals of the part class, to be
  recomputed with the real part): wide LED about 10-25 mW/sr at 25 mA gives
  E = Ie/d^2 = 0.25-0.6 W/m^2 while the carrier is on, about a hundred times
  under the lensed narrow emitter below. The IEC 62471 exempt limit for infrared
  irradiance (780-3000 nm) is 100 W/m^2 for exposures of 1000 s and more and
  higher for short ones, and the shots are short bursts (the gate allows
  50 ms of LED on-time per second), so the wide LED sits more than 100 times
  under it. The firmware gate is shared: only one
  LED transmits at a time and the on-time budget counts both together. The
  retina thermal limit depends on the chip's radiance, which only the
  datasheet gives; check it when the part is chosen, as for the narrow LED.
- **Current:** the LED current is set in hardware (series resistor sized to the
  datasheet rating; peak current within the part's pulsed rating and the
  average within its continuous rating: the lensed LED peaks at 200 mA, its
  repetitive-pulse rating at 50% duty, and averages 100 mA in a mark). A MOSFET gate pull-down keeps the LED
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
