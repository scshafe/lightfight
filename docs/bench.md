# Bench procedure

Owner steps, in order, with the first boards. Nothing here is automated: each
step needs the real gear. Stop at the first step that fails and report it in
the PR or issue; do not work around a safety check.

Rules for the bench: never look into an IR LED through a lens or magnifier;
use the phone camera to see that it is on. Power from a USB port or a single
protected LiPo only. No loose cells.

## Build and flash (owner)

The container builds the same image CI does (`firmware/scripts/idf-build.sh`).
To flash through it, give it the board's serial device (your user needs to be in
the group that owns it, `dialout` or `uucp`):

```sh
firmware/scripts/idf-flash.sh blaster /dev/ttyACM0
```

That builds the role and runs `idf.py flash monitor` in the container. Exit the
monitor with Ctrl-]. Flashing needs the owner's approval each time (AGENTS.md).

## B1 - the LED is off at rest

Before the first flash, with the LED, MOSFET and resistors on the breadboard
and the board powered (or unpowered):

1. Measure the gate-to-ground resistance with the board unpowered: about 100 k.
2. Power the board and hold it in reset (the RESET button): the LED current is
   zero. Release it: the LED stays off until a shot.

Pass: no light and no current at rest or in reset. Fail: add or fix the
pull-down before going on.

## B2 - LED current

Put a 1 ohm shunt in the LED's ground leg (between MOSFET source and ground)
and an oscilloscope across it, or a multimeter in series for the average.

1. Flash the `blaster-bench` role (`firmware/scripts/idf-flash.sh blaster-bench
   /dev/ttyACM0`): the blaster with `LF_BENCH_AUTOFIRE`, one frame a second
   through the safety gate, no trigger needed.
2. Read the peak current in the marks: **must be no more than the LED's
   continuous rating** (the resistor was sized for it; docs/hardware.md).
3. Read the average over a second: it is the peak times the carrier duty (33%)
   times the mark fraction of the frame; expect under 3% of the peak.
4. Repeat with the battery fully charged (4.2 V) and at about 3.3 V.

Record the three numbers and the battery voltages. Fail if the peak exceeds
the rating at 4.2 V: use a bigger resistor.

## B3 - waveform

Probe the MOSFET gate (or the GPIO) with the logic analyzer at 1 MS/s or more,
capture a few seconds with a frame a second, export the edges as CSV
(`time_seconds,level`), then:

```sh
python3 tools/lfcapture.py capture.csv
```

It prints each decoded frame (shooter, team, damage, seq) and checks the
carrier (38 kHz +-2%, duty at most 50%), every mark and space against
`docs/ir-protocol.md`, the CRC, the 250 ms spacing, the 80 ms frame cap, and
that there is no mark outside a frame. **Pass = `PASS` and exit 0.** Attach the
output to the PR.

## B4 - triggers, ammo, reload (no AUTOFIRE)

Flash the plain `blaster` role (no autofire). The monitor logs each shot
with its mode, a rising seq (wrapping at 16) and "no shot" lines with a code
(1 inactive, 2 dead, 3 disabled, 4 reloading, 5 no ammo, 6 rate limited,
7 bad mode; the numbering is `lf_shot_result` in `firmware/lib/lf_rules.h`).

1. **Trigger A (light, silent):** thirty pulls a second apart: each logs
   `[mode 0]` with damage code 0, no sound. After the 30th the blaster
   reloads by itself (auto-reload on): 2 s later the next pull fires.
   Holding the trigger fires one shot, not a stream. Pulling faster than four a
   second logs "no shot: 6".
2. **Trigger B (heavy, sound):** each pull logs `[mode 1]` with damage code 3
   and plays a falling chirp from the buzzer. Cooldown: a second pull within
   1.5 s logs "no shot: 6". After the fifth shot the magazine is empty and
   the sixth logs "no shot: 5" (no auto-reload): press reload (short) and
   after 3 s it fires again.
3. **Trigger C (shotgun):** `[mode 2]`, damage code 2, silent, two shots, then
   empty; a short reload press reloads it (the mode fired last), 3.5 s.
4. **Modes are independent:** empty the heavy mode; the light mode still fires.
   The 250 ms gap holds across modes: A then B within 250 ms logs "no shot: 6"
   for the second one.
5. **Reload target:** fire A once, then B once, then short-press reload: only
   B reloads (state, B8, shows the reloading mask 2). Hold reload for a second:
   every mode that is not full reloads.
6. Nothing sounds on A or C. Record the sound level of B at 2 m (a phone's
   noise meter is enough) and whether it is clear outdoors.

## B5 - fault latch

The latch that holds the LED off after a stuck transmission is covered by the
host tests (`firmware/tests/test_emitter.c`); there is no safe way to provoke
it on the bench. What the bench can check is the backstop: pause the running
board in a debugger during a frame. The task watchdog (5 s) resets the board,
and while it restarts the gate pull-down keeps the LED off (B1).

## B6 - hub bring-up

Needs one hub board with the five receivers wired as in docs/hardware.md, and a
second board running the blaster. Flash the hub (`idf-flash.sh hub ...`).

1. With no blaster firing, watch the monitor for 30 s: no `hit`, and `bad`
   lines only rarely. If one receiver prints `bad` constantly, check its wiring
   and decoupling, and shade it from sunlight and lamps.
2. Hold a TV remote at a receiver and press a key: remotes use other protocols,
   so you get `bad` lines or nothing, never a `hit`.
3. Aim the blaster at receiver 0 from 1 m and fire 5 shots slowly: five `hit`
   lines in the hub log, health 90, 80, ... (10 per shot with the default
   damage table), and `drop ... reason=duplicate` lines from the other
   receivers that also read it. The vibration motor buzzes for each hit.
4. Fire at it until health reaches 0: the last hit is the one that puts the
   player down (buzzer sounds longer), further shots log `drop reason=dead`,
   and after 10 s it respawns (status LED back on).
5. Cover receiver 0 with a finger and fire at 1 m again: one of the other
   receivers still counts it (this is why there are five).

## B7 - range and reliability

For each distance in 1, 3, 5, 10 and 15 m (as far as the room allows), with
the blaster and the vest in the same room, indoors with the lights on:

1. Save the blaster's serial output to `blaster.log` and the hub's to
   `hub.log` (both with `LFLOG` lines).
2. **Aimed at the vest**: fire 50 shots about a second apart, the vest worn
   facing the shooter. Score with
   `python3 tools/lfhits.py --expect hit blaster.log hub.log`.
3. **Aimed away**: fire 50 shots with the blaster pointed 90 degrees off, and
   again at the ceiling. Score with `--expect miss`.
4. Repeat once outdoors in daylight at 5 m.

Pass criteria for v1 (from the brief: a hit that lands registers once, a miss
never registers): hit rate at least 99% at 5 m indoors and 95% at the longest
distance claimed in the PR; **zero** false hits and zero counted shots in the
aimed-away runs. Report the table of distance, shots, counted, missed, false
hits, in the PR, with the `lfhits.py` output attached. If sunlight saturates
the receivers (`stuck` in the `rx` lines), say so; that is a finding, not a
failure of the test.

## B8 - the phone link

No iOS app is needed: install a generic BLE tool on the phone (for example
nRF Connect). Flash the hub (or the blaster) as before.

1. Power up a board with no bonded phone: within 60 s it appears as `LF-xxxx`
   (the last two bytes of its Bluetooth address) advertising the lightfight
   service (UUID `4c460000-7f1a-4b6e-9c3d-2a5e8b1d0f47`). Connect and, when the
   phone asks, pair. The monitor prints "connected" and later "mtu".
2. The characteristics are `...0001` config, `...0002` command, `...0003`
   state, `...0004` events, `...0005` ack. Read state: 7 bytes (docs/ble-
   protocol.md). Subscribe to state: a notification at once, then on change
   and every second.
3. In the hub: subscribe to events, then hit the vest with the blaster (B6):
   each hit arrives as a 13-byte event with event ids 1, 2, 3. Do not write
   ack. Disconnect and reconnect, subscribe again: all the events come again,
   in order. Write ack with the last id (`01` followed by the id as two bytes,
   little endian, so id 3 is `010300`) and reconnect: none come.
4. Commands: write `0102` (stop) to command: the state's flags lose the
   "active" bit; `0101` (start) restores health and ammo. Writing `0109` or a
   wrong length is rejected by the phone tool as an ATT error.
5. Config: with the match stopped, write a 23-byte config (the first `config` row of
   `protocol/ble-vectors.csv`, or the `config_hex` of a server join reply), then
   start:
   the monitor and state show the new health and ammo. (With the default
   `LF_STANDALONE=y` the board starts a match at boot: stop it first.)
6. Reconnection: walk out of range and back, or switch the phone's Bluetooth
   off and on: the phone reconnects on its own to a bonded board. Record how
   long it takes.
7. A second phone that is not bonded: outside the pairing window it must be
   dropped as soon as it tries to pair. Hold the pair button 3 s and it can
   pair. Hold 10 s: the first phone must be asked to pair again.

Report what happened at each step in the PR. These steps are the first time
this code runs on a board.

## B9 - battery reading

Needs a multimeter and a charged pack. Flash a board with its pack connected.

1. Measure the cell voltage at the divider's top (after the fuse) and read the
   monitor's battery line (it prints the cell millivolts once a second). They
   must agree within 2%; if not, set `LF_BATTERY_CAL_PERMILLE` (1000 x
   multimeter reading / printed reading) and flash again.
2. State (B8, step 2) shows `battery_pct`: about 100 at 4.2 V, about 25 at
   3.83 V. The percentage is a typical resting-cell curve, not your cell's:
   note how the real pack behaves (a pack rated for 1000 mAh may reach
   3.3 V earlier or later) and record the voltages for 100, 50 and 10 percent.
3. Fire the blaster or buzz the motor and watch the percentage: it must not
   jump (the reading is averaged over about 8 s).
4. Unplug the pack and power from USB only: the percentage stays at its last
   value (100 if none), it does not drop to 0.

## B10 - the shotgun's range

Needs the blaster with the wide LED fitted (docs/hardware.md), the hub on a
vest or a stand with the receivers at chest height, and a tape measure. The
goal is a frame that decodes at 3-5 m across a wide cone and not beyond.

1. Flash the `blaster-bench-shotgun` role
   (`firmware/scripts/idf-flash.sh blaster-bench-shotgun /dev/ttyACM0`): the
   shotgun mode fires one frame a second without the trigger, through the same
   safety gate.
2. Measure the current in the wide LED's leg as in B2 (1 ohm shunt): the peak
   must be within the LED's continuous rating at 4.2 V, and about 20-30 mA with
   the 100 ohm start value. Capture the waveform as in B3
   (`tools/lfcapture.py`): it must be the same frame format with damage code 2.
3. With the hub running (B6) and `blaster.log`/`hub.log` saved, fire 30 shots
   at 1, 2, 3, 4, 5, 6 and 8 m, aimed at the vest; score each distance with
   `python3 tools/lfhits.py --expect hit blaster.log hub.log`. Pass for the
   range: at least 95% at 3 m, and the hit rate falls below 10% by 8 m.
   If it reaches too far, fit a larger resistor (150, 220 ohm); too short, a
   smaller one, never below the rating limit in B2. Record the final resistor.
4. The cone: at the distance where the rate is 95%, move the blaster sideways
   so the vest sits 30, 45 and 60 degrees off the axis: record the hit rate at
   each angle. Two vests side by side 1 m apart at 3 m must both register one
   shot as one hit each.
5. Aimed away and at the ceiling: zero hits, as in B7.

## Records

Keep the results (numbers and the capture's output) in the PR that reports
them, with made-up player names only.
