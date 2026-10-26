# IR protocol

The wire format of one shot (design: `docs/design.md`, "IR protocol"). The
reference codec is `firmware/lib/lf_ir.c`; the shared test vectors are
`protocol/ir-vectors.csv`, which every implementation (firmware, server, app)
must reproduce.

## Physical

940 nm LED, on-off keyed at a 38 kHz carrier. A receiver module (TSOP38238
class) demodulates it: a *mark* is the carrier present, a *space* the carrier
absent. Everything below is the module's output timing.

## Frame

Durations in microseconds, alternating mark/space, 51 durations in all:

| Part | Mark | Space |
|------|------|-------|
| Header | 3000 | 1500 |
| Each of 24 bits | 560 | 560 for 0, 1690 for 1 |
| Trailer | 560 | none |

A frame lasts 32–59 ms (the bit values decide: about 32 ms for an all-zero
word, about 59 ms for all ones). Bits go most significant first.

## Payload (24 bits)

| Bits | Field | Range |
|------|-------|-------|
| 23..18 | shooter id | 0..63 |
| 17..15 | team | 0..7 |
| 14..12 | damage code | 0..7 (a table set by the match, never a raw value) |
| 11..8 | sequence | 0..15, +1 per shot, wraps |
| 7..0 | CRC-8 | poly 0x07, init 0, no reflection, no final xor, over the first 16 bits as two bytes (high byte first) |

## Receiving

- A frame is accepted only with exactly 51 durations, each within ±25% of
  nominal (a space is a 1 if near 1690, a 0 if near 560, else the frame is
  rejected), and a matching CRC. Anything else is dropped without trace:
  with no valid frame there is no hit.
- The CRC has Hamming distance 4 at 24 bits, so every 1-, 2- or 3-bit error
  is detected. A random 24-bit word passes the CRC about 1 time in 256, but
  random durations essentially never pass the timing checks.
- De-duplication by `(shooter, seq)` is the rules engine's job (M2), not the
  codec's.

## Not in the codec

Transmit rate caps and the LED drive limits are safety rules (design,
"Safety"), enforced in firmware logic in M5.
