# Hardware

The prototype gear and what to buy for it (owner step: buying needs the
owner's approval). Part numbers are candidates to check against the current
datasheets and stock before ordering; the design (`docs/design.md`, "Bill of
materials") fixes the shape, not the part.

## Blaster

- **Board:** Seeed XIAO ESP32-C3. It has a LiPo charger on board (battery on
  the BAT pads, charge over USB-C), so no separate charger module.
- **Emitter:** one 940 nm 5 mm IR LED (candidate: Vishay TSAL6200) behind a
  lens (below), driven by a **constant-current sink** so the peak current stays
  about the same from a full cell to a nearly flat one (owner decision
  2026-10-27). The LED is run at its repetitive-pulse rating, 200 mA at 50%
  duty with pulses under 100 us; the 38 kHz carrier at 50% duty gives 13 us
  pulses and the firmware sets that duty for this LED
  (`NARROW_CARRIER_DUTY_PERCENT` in `blaster.c`). Never exceed 200 mA.

  The sink is the classic two-transistor limiter, on the LED's low side:

  ```
  BAT+ --[PTC 0.1 A hold]--+-- LED anode..cathode -- Q1 collector (BC817-40, NPN, SOT-23)
                           +-- 100 uF + 100 nF to GND, close to the LED
  GPIO(LF_PIN_IR_LED) --[680 R]--+-- Q1 base
                                 +-- Q2 collector (BC847, NPN, SOT-23)
                                 +-- [100 k] -- GND   (base pull-down)
  Q1 emitter --+-- [Rs 3.6 R, 1%] -- GND
               +-- Q2 base                  Q2 emitter -- GND
  ```

  When the voltage across Rs reaches Q2's base-emitter voltage (about 0.65 V)
  Q2 turns on and takes away Q1's base drive, so the LED current is
  `I = Vbe(Q2) / Rs` whatever the cell voltage is. The base is driven from the
  GPIO's regulated 3.3 V, not the cell, so nothing in the loop depends on the
  cell. The 100 k pull-down keeps Q1 off while the MCU is in reset or booting;
  it is part of the safety design and is checked in the bench procedure.
  Neither transistor saturates in use, so the edges are fast (about a
  microsecond or less; bench B2 looks at them).
- **Current and headroom:** Vbe(Q2) is about 0.65 V at 25 C, 0.70 V at 0 C
  and 0.60 V at 45 C (about -2 mV/C), so with Rs = 3.6 ohm, 1%:

  | Condition | Vbe | Peak current |
  |-----------|-----|--------------|
  | 25 C | 0.65 V | about 181 mA |
  | cold (0 C), worst high | 0.72 V (with tolerance) | at most 200 mA |
  | hot (45 C) | 0.60 V | about 167 mA |

  So the peak never exceeds the 200 mA rating at any cell voltage or
  temperature, and it varies by about +-10% instead of the old 120 to 177 mA
  with the cell. Choosing Rs for the cold worst case costs about 10% of
  intensity against a 200 mA nominal (0.85 dB); if the bench shows the current
  has a margin under 200 mA in the cold, Rs can go down (3.3 ohm gives 197 mA
  nominal) but check it at 0 C first. Q1 needs headroom of
  Vf_max + 0.65 V + Vce(sat) = about 1.6 + 0.65 + 0.2 = 2.5 V at the LED node,
  so with the PTC's 0.36 V the current stays constant down to a cell of about
  2.8 V (3.3 V is the lowest the bench checks). Q1 dissipates up to
  (4.2 - 1.3 - 0.65) x 0.2 = 0.45 W for 13 us pulses, 3.4% of the time at
  most: about 15 mW mean, well inside a SOT-23. Rs dissipates 0.13 W in a
  pulse, a few mW mean. Q1's base drive is (3.3 - 1.4) / 680 = 2.8 mA, enough
  for hFE 100 at 200 mA; Q2 sinks the excess. These figures come from typical
  datasheet values: **recompute them from the datasheets of the parts bought and
  confirm by measurement** (bench steps B2, B11).
- **A single fault in the sink, and the PTC in the LED branch:** if Q1 fails
  short, the LED sees the cell through Rs alone: 0.76 A at 4.2 V, 0.63 A at
  3.7 V, 0.53 A at 3.3 V, continuously (about 0.6 A). If Rs is shorted, Q2 loses
  its sense voltage and Q1 is limited only by its base drive, 0.4 to 0.7 A.
  Neither the firmware gate nor the 1 A pack fuse covers it, and the LED's
  continuous rating is 100 mA. The design's safety section requires a hardware
  current limit that holds under a single fault, so the LED branch has its own
  resettable fuse (PTC) from BAT+ to the LED node (diagram above). I checked
  the owner's 250 mA figure against the numbers and the result is that a
  smaller part fits better; typical datasheet values, to be confirmed with the
  chosen part's curves and by bench B2:
  - **What the PTC carries in normal use.** The 100 uF capacitor on the LED
    side supplies the 38 kHz pulses (2.4 uC each, a 24 mV ripple); the PTC only
    supplies their mean. In a mark the mean is 181 mA x 50% carrier = 90 mA,
    for 17 ms (59 ms at most) per frame; over time it is 90 mA x
    sqrt(0.068) = about 23 mA RMS at four frames a second. A PTC heats on
    the mean square over seconds, not on a 59 ms burst, so the hold current
    needs to cover about 25 mA at the hottest, not 200 mA: the 200 mA pulses
    never pass through it.
  - **Why not 250 mA hold.** A PTC's trip current is about twice its hold, and
    a part trips quickly only at several times its hold. With a 250 mA part
    (trip about 0.5 A, series resistance 1-2 Ω) the Q1-short fault current is
    about 0.5 A at 4.2 V and 0.4 A at 3.7 V, 0.35 A at 3.3 V: 1.4 to 2 times
    the hold, below or at the trip current, where the trip time is tens of
    seconds to minutes or the part does not trip at all. That fails
    "trips in seconds".
  - **The part: hold 0.10 A (0.10-0.125 A), trip 0.20-0.25 A, series
    resistance about 2-6 Ω (4 Ω assumed).** Mean 90 mA in a mark is under the
    hold and the RMS is 25 mA, so it does not trip in use, even at 60 C where
    the hold falls to about 65%. The 4 Ω adds 0.36 V at the mean current, so
    the LED node has 2.9 V at a 3.3 V cell, against the 2.45 V the sink needs.
    The Q1-short fault current with the PTC's 4 Ω in series (the PTC is a
    series resistor until it trips) is 0.36 A at 4.2 V, 0.30 A at 3.7 V and
    0.25 A at 3.3 V, which is 2.5 to 3.6 times the hold. A small PTC at that
    multiple typically trips in a few seconds up to tens of seconds; at
    3.3 V it is the slowest. After it trips it passes about 10-20 mA until the
    power is removed and it cools; the blaster then looks dead until a
    power cycle (a few minutes with the cell disconnected).
  - **Limits.** A PTC will not protect the LED itself: at 0.25-0.36 A for
    seconds the LED is far over its continuous rating and may be damaged or
    fail; what the PTC protects is the person in front of it (eye safety,
    docs/design.md "Safety"), the wiring and the cell. If bench B2 shows a trip
    time over 20 s at any cell voltage, a PTC is not enough. The alternative
    is an adjustable current-limit load switch with thermal shutdown (the
    TPS2553 class, about $0.6) in the same place, limit about 150 mA with the
    capacitor carrying the pulses: it clamps the fault at a flat 150 mA at once
    instead of waiting to heat, and retries with a tiny duty. I have not
    added it.
  - **The 250 mA part from the owner's note** is on the shopping list too, to
    be tried only if the 0.1 A part trips in normal use.
- **Lens and tube (owner decision 2026-10-27):** a 40 mm diameter lens of about
  50 mm focal length, PMMA asphere or Fresnel, with the LED at the focus on
  the lens axis, 50 mm behind the lens (a Fresnel's focal length is set by its
  maker; use the datasheet's value). The aperture sets the range (intensity
  grows with its area) and the focal length sets the cone width. The tube:
  - printed in an opaque material, inside diameter 40 mm, matte black (flat
    black paint or flocking), about 50 mm long;
  - 2 or 3 baffle rings with a 36 mm bore inside the tube, so light from the
    LED that misses the lens is absorbed instead of bouncing out, and a
    retaining ring that seals the lens edge, so no light leaves except through
    the lens;
  - the LED holder slides about 5 mm along the axis and locks, to focus on the
    bench (B11), and the LED's leads exit through a sealed back plate;
  - a lens cap. The lens focuses sunlight to a hot spot at the LED and can melt
    it or the tube: never leave the blaster uncapped in the sun. Wear and
    store it with the cap on;
  - the shotgun's wide LED sits on the outside of the housing, beside the tube,
    not behind the lens.
- **Shotgun emitter:** a second 940 nm LED, wide-angle (5 mm, half-angle
  45-60 degrees on the datasheet, no lens; candidate: a TSAL4400-class part or
  any 940 nm LED whose datasheet gives a half-angle of 45 degrees or more),
  on its *own* MOSFET and gate pull-down, wired exactly like the narrow LED but
  with a larger resistor so it decodes only at close range:

  ```
  BAT+ --[R_wide]--|>|-- wide LED --+-- MOSFET drain (source to GND)
  GPIO(LF_PIN_IR_WIDE) --[100 R]-- gate --+-- [100 k] -- GND
  ```

  `R_wide` sets both the current and the range. Start with 100 ohm (about
  28 mA at 4.2 V, 20 mA at 3.3 V, with V_f about 1.3 V) and tune on the bench
  (B10) with the assortment in the shopping list: 68 ohm for more range, 150,
  220 or 330 ohm for less. Never go below the value that keeps the current
  within the LED's continuous rating at 4.2 V (`R >= (4.2 - V_f - 0.1) / I_max`).
  Eye safety: docs/design.md, "Safety". The firmware shares one safety gate
  between the two LEDs; only one sends at a time.
- **Inputs:** four tact switches to ground (internal pull-ups): trigger A
  (light) `LF_PIN_TRIGGER`, trigger B (heavy) `LF_PIN_TRIGGER_B`, trigger C
  (shotgun) `LF_PIN_TRIGGER_C` and reload `LF_PIN_RELOAD`. Trigger C is on
  GPIO2, a strapping pin that must be high at reset, so give it an external
  10 k pull-up to 3.3 V. Reload: a short press reloads the mode fired last,
  held 0.8 s reloads every mode.
- **Buzzer:** a *passive* piezo element (it needs a tone, which the firmware
  makes with the LEDC timer) between `LF_PIN_BUZZER` and ground, for the heavy
  shot's sound. Louder: a small piezo with its resonant disc, driven through a
  logic MOSFET like the motor; the first build does without. A status LED with its own 330 ohm
  resistor on `LF_PIN_STATUS_LED`.
- **Pins (defaults in `firmware/idf/main/Kconfig.projbuild`, XIAO labels):**
  IR gate GPIO3 (D1), battery sense GPIO4 (D2), trigger A GPIO5 (D3), reload
  GPIO6 (D4), trigger B GPIO7 (D5), trigger C GPIO2 (D0, with the pull-up), wide
  LED gate GPIO21 (D6), buzzer GPIO20 (D7), status LED GPIO10 (D10). GPIO8/9 and GPIO2 are strapping pins: avoid them.
- **Battery sense:** two equal 220 k resistors in series from the cell's
  positive (after the fuse) to ground, the midpoint to `LF_PIN_BATTERY`
  (GPIO4, D2, an ADC1 pin) with a 100 nF capacitor to ground. The ADC sees half
  the cell voltage (2.1 V at 4.2 V, inside the 12 dB range) and the divider
  draws about 10 uA. Firmware averages 16 samples a second, converts with the
  chip's calibration and reports a percentage from a typical curve
  (`firmware/lib/lf_battery.c`); bench step B9 checks and calibrates it.
- **Power:** one protected 1S LiPo (built-in protection circuit against
  over-charge, over-discharge and short circuit) with an inline resettable
  fuse, in an enclosure that holds it fixed and away from the grip's crush
  points.

## Vest hub (M6 onward)

XIAO ESP32-C3 again; five 38 kHz receiver modules (TSOP38238 class, 3.3 V),
each with its own 100 nF decoupling capacitor and a 100 ohm series resistor
in the supply, on a wired harness (chest, left and right shoulder, two on the
headband); two coin vibration motors, one in the vest and one in the headband,
each behind its own MOSFET with a flyback diode; a buzzer; status LEDs. Same
battery arrangement as the blaster.

The motors, vest and headband alike:

```
  BAT+ (or 3.3 V for a 3 V coin motor) --+-- motor --+-- MOSFET drain
                                         |           |
                                         +-|<|-------+      (1N4148 or a Schottky, cathode to supply)
  GPIO --[100 R]-- gate --+-- [100 k] -- GND ; source to GND
```

A coin motor is rated about 3 V, so run it from the 3.3 V rail (about 60-80 mA
while running, well inside what a MOSFET and the regulator carry for pulses
of 0.06-1.4 s), or from the cell through a small series resistor. The gate
pull-down keeps both motors off at reset. A headshot buzzes the headband motor
only, a body shot the vest motor only; the kill and the respawn buzz both
(docs/design.md, "Haptics"). Mount the headband motor in its pocket against the
webbing, not on the skull's bone, so the cue is felt, not painful.

Hub pins (defaults in `Kconfig.projbuild`, XIAO labels): receivers on GPIO3,
GPIO2, GPIO8, GPIO6, GPIO7 (chest, left shoulder, right shoulder, headband
front, headband back; GPIO2 and GPIO8 are strapping pins that must be high at
reset, which a receiver's idle-high output is), battery sense GPIO4 (D2, same divider as the
blaster),
vest motor gate GPIO21 (D6), headband motor gate GPIO5 (D3), buzzer GPIO20 (D7),
status LED GPIO10 (D10).
A receiver module's output is open-collector-like with an internal pull-up and
idles high; keep each harness lead short or shielded, as it carries 560 us
edges.

The pair button is the XIAO's own BOOT button (GPIO9); nothing to wire.

## Prototype shopping list (2 players, with spares)

| Item | Qty |
|------|-----|
| Seeed XIAO ESP32-C3 | 6 |
| 1S LiPo 3.7 V, 1000-1500 mAh, with protection circuit and connector | 5 |
| Resettable fuse / PTC, about 1 A hold | 5 |
| Resettable fuse / PTC, 0.1 A hold, 0.2-0.25 A trip, 6 V or more (the LED branch; 1206 or radial) | 8 |
| Resettable fuse / PTC, 0.25 A hold (the fallback, and a comparison for B2) | 4 |
| 940 nm IR LED, 5 mm (TSAL6200 or equivalent; needs 200 mA repetitive-pulse rating at 50% duty) | 6 |
| 940 nm IR LED, 5 mm, wide-angle (half-angle 45 degrees or more) for the shotgun | 4 |
| Logic-level N-MOSFET, SOT-23 (AO3400A or equivalent; the wide LED and the motors, no longer the main LED) | 12 |
| NPN transistor BC817-40, SOT-23 (the main LED's current sink, Q1) | 6 |
| NPN transistor BC847, SOT-23 (the current limit, Q2) | 6 |
| Resistors: 3.6 ohm 1% 1/4 W (Rs, the main LED; also 3.3 ohm 1% for trimming, 4 of each); 680 ohm; 100 ohm; 100 k; 330 ohm (a handful of each, 10 per value) | |
| Resistors 220 k (battery divider: 2 per board, so 12) | 14 |
| Resistors 47, 68, 100, 150, 220, 330 ohm 1/4 W, 4 of each (tuning the shotgun LED's range) | 24 |
| Resistor 10 k (trigger C pull-up; the gate pull-downs of the new MOSFETs use the 100 k) | 4 |
| 38 kHz IR receiver module, TSOP38238 class | 14 |
| Capacitors, 100 nF ceramic (receivers and battery sense) | 20 |
| Tact switches (per blaster: A, B, C, reload; plus spares) | 14 |
| Coin vibration motor 3 V, with 1N4148 diode and a spare MOSFET each (vest and headband: 2 per player, plus a spare) | 5 |
| Lens, 40 mm diameter, about 50 mm focal length, PMMA asphere or Fresnel (1 per blaster plus 2 spares; pick one with a datasheet focal length) | 4 |
| Matte black paint or flocking, a few mm of M3 hardware and PETG or similar opaque filament for the tube, baffles and cap | 1 set |
| Electrolytic or polymer capacitor, 100 uF 10 V (the LED supply) | 6 |
| Piezo buzzer, 3 V (the hub's, a plain buzzer element) | 3 |
| Passive piezo element for the blaster's sound (needs a tone) | 3 |
| LEDs for status, 3 mm | 6 |
| USB-C cables (data, not charge-only) | 3 |
| Breadboard or perfboard, jumper wire, heat shrink, solder | 1 set |
| Vest and headband webbing, cable sleeve | 2 sets |

Tools the owner needs for the bench steps: a multimeter; a logic analyzer
(any 8-channel USB one, 8 MS/s or better, with sigrok/PulseView) or an
oscilloscope; a 1 ohm 1% resistor as a current shunt. A phone camera shows a
940 nm LED as a faint glow, which is enough to see that it fires, not to
measure it.
