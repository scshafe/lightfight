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

1. Flash the blaster with `LF_BENCH_AUTOFIRE` (menuconfig: lightfight > Bench:
   send one frame per second). It sends one frame a second through the safety
   gate.
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

## B4 - trigger, ammo, reload (no AUTOFIRE)

Rebuild without `LF_BENCH_AUTOFIRE`. Pull the trigger once per second for
thirty shots: the monitor logs each shot with a rising seq (wrapping at 16)
and the ammo going from 29 to 0; the 31st pull logs "no shot". Press reload
and wait 2 s: ammo is 30 again. Holding the trigger fires one shot, not a
stream. Pulling faster than four a second logs "no shot: 5" (rate limited).

## B5 - fault latch

The latch that holds the LED off after a stuck transmission is covered by the
host tests (`firmware/tests/test_emitter.c`); there is no safe way to provoke
it on the bench. What the bench can check is the backstop: pause the running
board in a debugger during a frame. The task watchdog (5 s) resets the board,
and while it restarts the gate pull-down keeps the LED off (B1).

## Records

Keep the results (numbers and the capture's output) in the PR that reports
them, with made-up player names only.
