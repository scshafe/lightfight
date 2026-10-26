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
- **Inputs:** trigger and reload tact switches to ground on `LF_PIN_TRIGGER`
  and `LF_PIN_RELOAD` (internal pull-ups). A status LED with its own 330 ohm
  resistor on `LF_PIN_STATUS_LED`.
- **Pins (defaults in `firmware/idf/main/Kconfig.projbuild`, XIAO labels):**
  IR gate GPIO3 (D1), trigger GPIO4 (D2), reload GPIO5 (D3), status LED
  GPIO10 (D10). GPIO8/9 and GPIO2 are strapping pins: avoid them.
- **Power:** one protected 1S LiPo (built-in protection circuit against
  over-charge, over-discharge and short circuit) with an inline resettable
  fuse, in an enclosure that holds it fixed and away from the grip's crush
  points.

## Vest hub (M6 onward)

XIAO ESP32-C3 again; five 38 kHz receiver modules (TSOP38238 class, 3.3 V),
each with its own 100 nF decoupling capacitor and a 100 ohm series resistor
in the supply, on a wired harness (chest, left and right shoulder, two on the
headband); a coin vibration motor behind a MOSFET with a flyback diode; a
buzzer; status LEDs. Same battery arrangement as the blaster.

Hub pins (defaults in `Kconfig.projbuild`, XIAO labels): receivers on GPIO3-7
(D1-D5: chest, left shoulder, right shoulder, headband front, headband back),
vibration motor gate GPIO21 (D6), buzzer GPIO20 (D7), status LED GPIO10 (D10).
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
| Logic-level N-MOSFET, SOT-23 (AO3400A or equivalent) | 8 |
| Resistors: 27 ohm 1/4 W; 100 ohm; 100 k; 330 ohm (a handful of each, 10 per value) | |
| 38 kHz IR receiver module, TSOP38238 class | 14 |
| Capacitors, 100 nF ceramic | 14 |
| Tact switches | 10 |
| Coin vibration motor 3 V, with 1N4148 diode and a spare MOSFET each | 3 |
| Piezo buzzer, 3 V | 3 |
| LEDs for status, 3 mm | 6 |
| USB-C cables (data, not charge-only) | 3 |
| Breadboard or perfboard, jumper wire, heat shrink, solder | 1 set |
| Vest and headband webbing, cable sleeve | 2 sets |

Tools the owner needs for the bench steps: a multimeter; a logic analyzer
(any 8-channel USB one, 8 MS/s or better, with sigrok/PulseView) or an
oscilloscope; a 1 ohm 1% resistor as a current shunt. A phone camera shows a
940 nm LED as a faint glow, which is enough to see that it fires, not to
measure it.
