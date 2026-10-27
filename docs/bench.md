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

Before the first flash, with the LED, the current-sink transistors (or MOSFET) and resistors on the breadboard
and the board powered (or unpowered):

1. Measure the gate-to-ground resistance (the main LED: Q1's base to ground) with the board unpowered: about 100 k.
2. Power the board and hold it in reset (the RESET button): the LED current is
   zero. Release it: the LED stays off until a shot.

Pass: no light and no current at rest or in reset. Fail: add or fix the
pull-down before going on.

## B2 - LED current

Put a 1 ohm shunt in the LED's ground leg (below Rs and Q2's emitter, so it is outside the current loop, for the main LED; between the MOSFET source and ground for the wide one)
and an oscilloscope across it, or a multimeter in series for the average.

1. Flash the `blaster-bench` role (`firmware/scripts/idf-flash.sh blaster-bench
   /dev/ttyACM0`): the blaster with `LF_BENCH_AUTOFIRE`, one frame a second
   through the safety gate, no trigger needed.
2. Read the peak current in the marks. For the main (lensed) LED **it must be
   no more than 200 mA, its repetitive-pulse rating at 50% duty**, and the
   mean over a mark (peak times the carrier duty) no more than its 100 mA
   continuous rating; expect about 181 mA with the 3.6 ohm sense resistor
   (docs/hardware.md), the same at every cell voltage. Look at the edges
   too: the turn-on must not overshoot 200 mA even briefly (a scope of 20 MHz
   or better; the shunt's own inductance can fake a spike, so compare with the
   wide LED's trace) and the rise and fall should be about a microsecond or
   less, the pulse about 13 us. The wide LED is checked in B10.
3. Read the average over a second: it is the peak times the carrier duty (50%
   for the main LED) times the mark fraction of the frame; expect about 3.4%
   of the peak with four shots a second, and under 2% with one a second.
4. Repeat at 4.2, 3.7 and 3.3 V on the BAT rail (a bench supply set to each
   voltage with a 500 mA limit stands in for the cell; the XIAO then runs from
   it). The peak current at the three voltages must agree within 10%.

Record the numbers at each voltage and the room temperature. Fail if the peak
exceeds 200 mA at any of the three voltages, or the spread is over 10%: check
Q2 and Rs (a larger Rs lowers the current); if it is well below 181 mA, check
the sink's headroom and the base drive. The sink's sense voltage drifts about
-2 mV/C (docs/hardware.md): note the temperature, and when the blaster will be
used in the cold, repeat once with the board cooled (a cold pack, about 5 C)
before trimming Rs down. The carrier duty in the capture (B3)
reads 50%, within the tool's 2% tolerance.

## B3 - waveform

Probe the GPIO (or the MOSFET gate) with the logic analyzer at 1 MS/s or more,
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

## B6b - haptic cues

With the hub running (B6) and the blaster firing at it:

1. **Body shot, light** (trigger A at the chest): the vest motor ticks once
   (about 60 ms); the headband motor stays off.
2. **Body shot, heavy** (trigger B, then C): the vest motor buzzes about
   250 ms, clearly longer and stronger than the tick; headband off.
3. **Headshot:** aim at the headband sensors only (a mask of tape over the
   chest and shoulder receivers works): the headband motor buzzes, the vest
   stays off, light and heavy as above. With the tape off and the blaster
   aimed at the face from 2 m, a shot that reaches both must buzz only the
   headband. Record how often a shot aimed at the chest buzzes the headband
   by mistake (it should be never or rarely).
4. **Kill:** shoot until health is 0 (B6): both motors and the buzzer, three
   pulses, the last one long.
5. **Respawn** (10 s later, or write `0103` to the command characteristic):
   both motors double-tap, no buzzer.
6. Wear it: record whether each cue is clearly felt through clothing, and
   whether the headband motor is comfortable. Report the three motor currents
   (peak and what the 3.3 V rail does while both run, as in the kill).
7. Each motor's gate is pulled low at reset: with the board held in reset
   (RST button) neither motor runs.

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

## B11 - the lensed blaster's range and cone

Owner decision 2026-10-27. The goal: 75 m feasible and 50 m reliable outdoors.
Needs the blaster with the lens and tube fitted (docs/hardware.md), the cap
off, an open site with a straight line of 75 m, two hubs (or one hub moved
between runs), a 100 m tape or a rangefinder, and a sunny day with some shade, a lux meter (a phone app will do) and a bench supply.
Sun hoods and a back sensor are not part of this build, so run it as built.

1. Current first: run B2 for the finished blaster (lens fitted) at 4.2, 3.7 and
   3.3 V and write the three peak currents in the table's header row; every
   later range run uses the same driver and is only meaningful if the current
   held within 10% across the three.
2. Focus: with the `blaster-bench` role flashed (B2, one frame a second) and a
   hub 25 m away facing the blaster, slide the LED holder in 1 mm steps and
   note the hit rate; lock it where the hit rate and the cone are best. Check
   by eye (phone camera) that no glow leaks from the tube sides or the
   back: if any does, fix the baffle or the seal before going on. Record the
   position.
3. Range. Distances 10, 25, 50 and 75 m. Conditions, each at every distance:
   - **sun, sun-facing receiver**: a clear sky, the hub's receivers facing
     the sun (the blaster fires toward it);
   - **sun, shaded receiver**: the same sky, the hub's receivers in shade
     (a hood-free vest in a shadow, or the hub turned away from the sun);
   - **daylight shade**: both ends in shade in daylight, with about 8 klx of
     ambient light on a lux meter or phone app (the threshold in the model is
     about 1.5 mW/m^2, TSOP382 Fig. 6); note the reading;
   - **dark**: after dusk (the threshold is 0.04-0.12 mW/m^2).
   Per distance and condition fire 30 shots about a second apart, aimed at the
   vest on the axis, with the logs saved, and score each with
   `python3 tools/lfhits.py --expect hit blaster.log hub.log`. Note the sky
   and the sun's bearing, the time, the battery voltage at the start and end
   (B9), and any `stuck` in the `rx` lines. Repeat the 50 and 75 m runs in the
   sun-facing condition with the cell (or a bench supply on the BAT rail) at
   3.7 V and at 3.3 V: with the constant-current driver the hit rate should
   match the 4.2 V run within 10 points; a drop is a finding about the driver
   (headroom, Rs) before it is about the optics.
4. Cone width. At 50 m (sun, sun-facing receiver) and at 75 m move the vest
   sideways in 0.5 m steps, still facing the blaster, and record the hit rate
   at each offset until it falls below 10%. Report the width where it is 95%
   and where it is 50%.
5. Aimed away and at the sky: zero hits, as in B7. Fire 50 shots at each.
6. Fill in the table below, in the PR that reports it, with the `lfhits.py` outputs attached. The pass criteria
   (owner's goal): at least 95% hits at 50 m in every condition on the axis,
   and at least one run at 75 m with the rate above 50%. Anything below it is
   a finding for the optics, not a firmware failure: report the numbers and
   the focus position and the three currents, and say which of lens, focus, current and
   threshold explains the gap.

| Distance | Condition | Receiver | Cell V | Shots | Counted | Hit rate | Cone 95% / 50% width |
|----------|-----------|----------|--------|-------|---------|----------|----------------------|
| 10 m | sun | sun-facing | | | | | |
| 10 m | sun | shaded | | | | | |

(one row per distance 10, 25, 50 and 75 m and per condition above, plus the 3.7 V and
3.3 V repeats; the model predicts about 6 dB of margin at 50 m and 3 dB
at 75 m with a sun-facing receiver at 4.2 V.)

## Records

Keep the results (numbers and the capture's output) in the PR that reports
them, with made-up player names only.
