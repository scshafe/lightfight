# BLE protocol

The phone-to-gear link (design: `docs/design.md`, "BLE link"). The reference
codec is `firmware/lib/lf_ble.c`; the Go (`server/protocol/`) and Swift
(`ios/Sources/LightfightProtocol/`) codecs and all tests use the shared
vectors `protocol/ble-vectors.csv`, written by an independent encoder.

## GATT

The phone is the central; each gear device (blaster, vest hub) is a
peripheral advertising as `LF-<4 hex>` with the service below. Request an MTU
of at least 64. Base UUID `4c46000X-7f1a-4b6e-9c3d-2a5e8b1d0f47` (`4c46` is
"LF"):

| Characteristic | UUID `X` | Access | Message |
|----------------|----------|--------|---------|
| service | 0 | | |
| config | 1 | write | `config` |
| command | 2 | write | `command` |
| state | 3 | read, notify | `state` |
| events | 4 | notify | `event` |
| ack | 5 | write | `ack` |

## Messages

All integers are little-endian, byte 0 is the version (1). A decoder rejects a
wrong length, a version it does not know, or a field out of range; it never
guesses.

| Message | Length | Layout after the version byte |
|---------|--------|-------------------------------|
| config | 40 | player u8 (0..63), team u8 (0..7), friendly_fire u8 (0/1), max_health u16, respawn_ms u16, damage_table 8 x u8, then 3 firing modes (light, heavy, shotgun) of 8 bytes each: damage_code u8 (0..7), auto_reload u8 (0/1), magazine u16, reload_ms u16, fire_interval_ms u16 |
| state | 12 | flags u8 (bit0 active, bit1 alive, bit2 some mode reloading, bit3 disabled, bits 4-5 the mode fired last: 0 light, 1 heavy, 2 shotgun), health u16, ammo u16 x 3 (light, heavy, shotgun), reloading_mask u8 (bit m: mode m reloading), battery_pct u8 (0..100) |
| event | 13 | kind u8 (1 hit, 2 down), event_id u16, shooter u8 (0..63), seq u8 (0..15), damage u8, health_after u16, t_ms u32 (gear uptime) |
| ack | 3 | event_id u16 |
| command | 2 | cmd u8 (1 start, 2 stop, 3 respawn, 4 ping, 5 disable, 6 enable) |

`config` mirrors the rules engine's configuration (`lf_rules_config`); the
phone writes it before a match and the gear applies it on `start`. The firing
modes (docs/design.md, "Firing modes") are per-mode settings in it. The gap
between any two shots is not a setting: the blaster uses the emitter gate's
minimum (250 ms), and a mode's `fire_interval_ms` below it just means "as fast
as the gap". The format changed from the 23-byte config and 7-byte state before
any gear shipped, so the version byte stays 1.

## Events: ack and replay

The hit path never waits for the phone: the gear applies a hit itself and
queues an `event` (`lf_evq`, 32 slots).

- Event ids run 1, 2, 3 ... as u16 and wrap past 65535 to 1 (0 is never an
  id). They are per device and restart at 1 only when the gear reboots.
- The phone writes `ack` with the id of the last event it holds; an ack is
  **cumulative** (everything up to and including that id, in wrap-aware
  order). An old or repeated ack changes nothing.
- On (re)connection, the gear sends every still-unacknowledged event again, in
  order, then continues. The phone ignores ids it already has, and the server
  de-duplicates by `(target, event_id, t_ms)`, so a replay is safe.
- If more than 32 events pile up unacknowledged the oldest is dropped (a
  sustained loss of the link of more than 32 hits; the state message still
  carries the true health).

## Pairing and security (gear side)

- Characteristics that write or read need an encrypted link. The gear pairs
  with LE Secure Connections, "just works" (no passkey: the gear has no
  display) and **bonds**, so a phone pairs once and reconnects on its own.
- A new phone can pair only in the **pairing window**: 60 s after power-up
  when the gear has no bonded phone, or 60 s after holding the pair button
  (`LF_PIN_PAIR`, BOOT on the XIAO) for 3 s. Holding it for 10 s forgets all
  bonded phones. Outside the window an unbonded phone is dropped as soon as the
  link is encrypted. "Just works" gives no protection against someone nearby
  who connects inside the window; the window and the button are what keep that
  to the owner's own gear. The design called for a passkey; the gear cannot
  show one, so this is the smaller step. A passkey on the phone and LED
  blinks is possible later if the owner wants it.
- Up to 4 bonded phones are kept. One phone is connected at a time.

## What each device sends

| | config | command | state | events |
|---|---|---|---|---|
| **Vest hub** | applied between matches (its firing-mode fields are unused) | start, stop, respawn | health, flags, battery (ammo fields are those of an unused magazine) | hits and downs, with ack/replay |
| **Blaster** | applied between matches | start, stop, disable, enable | ammo per mode, reloading mask, flags, battery | none (never notifies) |

The device keeps playing with no phone (docs/design.md). While a match is
active a new `config` is ignored (it applies to the next one); `start` while
active and `stop` while idle do nothing. `battery_pct` is measured: a divider on an ADC pin
(docs/hardware.md), averaged, mapped through a typical resting-cell curve; 100
until the first sample, and unchanged when no cell is connected.

## The blaster and a downed player

The hub knows when the player is down; the blaster, a separate board, does not.
Owner decision (2026-10-26): the phone relays it. When the hub's `state` shows
the player down (alive flag clear) the phone writes `disable` to the blaster;
when the player is alive again it writes `enable`. A disabled blaster refuses
every trigger pull (no round is spent, no frame is sent) and reloads, and
reports flag bit3 in its `state`. Both commands work only while a match is
active, a new `start` always clears the disable, and the hub ignores them.
The phone relay adds the BLE latency, about 100 ms, during which a downed
player can still fire; a direct blaster-to-vest link would remove it and is
not planned.
