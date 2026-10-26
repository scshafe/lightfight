# Hit log

The gear prints one line per event to its serial console; the bench and field
tests save the text and score it with `tools/lfhits.py`. The format is written
by `firmware/lib/lf_log.c` (tested on the host); lines may carry other text
before `LFLOG`, and every other line is ignored.

| Line | From | Meaning |
|------|------|---------|
| `LFLOG t_ms=T ev=shot shooter=S team=M seq=Q` | blaster | a frame was sent |
| `LFLOG t_ms=T ev=hit sensor=N shooter=S seq=Q dmg=D health=H` | hub | a hit was counted; `sensor` is the receiver that read it first |
| `LFLOG t_ms=T ev=drop sensor=N shooter=S seq=Q reason=R` | hub | a valid frame was read and not counted: `duplicate` (another sensor or a repeat), `friendly`, `self`, `dead`, `inactive` |
| `LFLOG t_ms=T ev=bad sensor=N why=W` | hub | a burst of edges with no valid frame in it: `crc`, `timing`, `length` |
| `LFLOG t_ms=T ev=rx sensor=N glitches=.. overflows=.. stuck=.. late=.. frames=..` | hub | every 10 s: the receiver's counters (missed edges, buffer overflows, saturation, late polls, valid frames read) |

`t_ms` is the board's own uptime: clocks of different boards are never
compared. `lfhits.py` pairs a `shot` with the next `hit` of the same
`shooter` and `seq`, in order.

Use made-up shooter ids and callsigns in any log that is attached to a PR.

## Reading the numbers

- **Hit rate** = shots counted / shots fired, for shots aimed at the target.
- **False hits** = hits with no shot, or any hit while aiming away. Must be 0.
- **Duplicates suppressed** are good: several sensors read one shot.
- **`bad` bursts** are noise or collisions rejected by the timing and CRC
  checks. A few are normal indoors with sun and lamps; a stream means a
  receiver is saturated or a sensor is picking up something else.
- `rx` counters: growing `glitches`, `overflows` or `stuck` point at wiring
  or lighting, not at the protocol.
