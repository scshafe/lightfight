# Hardware

The prototype gear and what to buy for it (owner step: buying needs the
owner's approval). Part numbers are candidates to check against the current
datasheets and stock before ordering; the design (`docs/design.md`, "Bill of
materials") fixes the shape, not the part.

## Blaster

- **Board:** Seeed XIAO ESP32-C3. It has a LiPo charger on board (battery on
  the BAT pads, charge over USB-C), so no separate charger module.
- **Emitter:** one 940 nm 5 mm IR LED (candidate: Vishay TSAL6200) switched by
  a logic-level N-MOSFET (candidate: AO3400A or IRLML6344, Vgs(th) well below
  3.3 V), the LED on the battery rail through a series resistor:

  ```
  BAT+ --[R_led]--|>|-- LED anode..cathode --+-- MOSFET drain
                                             MOSFET source -- GND
  GPIO(LF_PIN_IR_LED) --[100 R]-- MOSFET gate --+-- [100 k] -- GND   (gate pull-down)
  ```

  The 100 k pull-down keeps the LED off while the MCU is in reset or booting;
  it is part of the safety design and is checked in the bench procedure.
- **Resistor:** size it for the *highest* battery voltage so the current never
  exceeds the LED's continuous rating, then check the lowest voltage still
  gives enough range:

  `R_led = (V_batt_max - V_f - V_ds_on) / I_max`

  Example with a 100 mA continuous rating, V_f about 1.4 V at 100 mA,
  V_ds_on about 0.1 V and V_batt_max = 4.2 V: R is at least 27 ohm (1/4 W; at
  the 4% duty it dissipates a small fraction of that). At 3.3 V the same
  resistor gives about 60 mA. These figures come from my reading of typical
  datasheet values; **recompute them from the datasheet of the part actually
  bought and confirm by measurement** (bench step B2).
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
| 940 nm IR LED, 5 mm (TSAL6200 or equivalent) | 6 |
| 940 nm IR LED, 5 mm, wide-angle (half-angle 45 degrees or more) for the shotgun | 4 |
| Logic-level N-MOSFET, SOT-23 (AO3400A or equivalent) | 12 |
| Resistors: 27 ohm 1/4 W; 100 ohm; 100 k; 330 ohm (a handful of each, 10 per value) | |
| Resistors 220 k (battery divider: 2 per board, so 12) | 14 |
| Resistors 47, 68, 100, 150, 220, 330 ohm 1/4 W, 4 of each (tuning the shotgun LED's range) | 24 |
| Resistor 10 k (trigger C pull-up; the gate pull-downs of the new MOSFETs use the 100 k) | 4 |
| 38 kHz IR receiver module, TSOP38238 class | 14 |
| Capacitors, 100 nF ceramic (receivers and battery sense) | 20 |
| Tact switches (per blaster: A, B, C, reload; plus spares) | 14 |
| Coin vibration motor 3 V, with 1N4148 diode and a spare MOSFET each (vest and headband: 2 per player, plus a spare) | 5 |
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
