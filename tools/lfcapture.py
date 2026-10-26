#!/usr/bin/env python3
"""Check a logic-analyzer capture of the blaster's IR LED gate pin (docs/bench.md).

Input: CSV of edges, one per line, "time_seconds,level" (level 0/1; a header
line and lines starting with # are skipped). This is what sigrok's CSV output
or a saleae export reduces to. The gate pin carries the 38 kHz carrier inside
each mark, so a mark is a burst of rising edges less than GAP_US apart.

The decoder is written separately from firmware/lib/lf_ir.c on purpose: the
capture is judged against docs/ir-protocol.md, not against the firmware.

    tools/lfcapture.py capture.csv        # exit 0 only if every check passes
"""
import sys

GAP_US = 40.0  # carrier period is 26.3 us; a gap longer than this ends a mark
CARRIER_HZ = 38000.0
HEADER = (3000, 1500)
MARK, SPACE0, SPACE1 = 560, 560, 1690
FRAME_TIMINGS = 51

# bench tolerances: the MCU's timer makes these, so they are tight
CARRIER_TOL = 0.02
MARK_TOL = 0.10
MAX_DUTY = 0.50
MIN_INTERVAL_S = 0.250


def parse_edges(text):
    edges = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        try:
            edges.append((float(parts[0]), int(parts[1])))
        except (ValueError, IndexError):
            continue  # a header line
    return edges


def bursts(edges):
    """-> list of (start_s, end_s, period_s_list, high_s_list) per mark."""
    out = []
    cur = None
    last_rise = None
    rise = None
    for t, level in edges:
        if level == 1:
            if cur is not None and (t - last_rise) * 1e6 <= GAP_US:
                cur["periods"].append(t - last_rise)
            else:
                if cur is not None:
                    out.append(cur)
                cur = {"start": t, "end": t, "periods": [], "highs": []}
            last_rise = rise = t
        elif cur is not None and rise is not None:
            cur["end"] = t
            cur["highs"].append(t - rise)
    if cur is not None:
        out.append(cur)
    return out


def durations_us(bs):
    """Alternating mark, space, mark ... in microseconds."""
    d = []
    for i, b in enumerate(bs):
        d.append((b["end"] - b["start"]) * 1e6)
        if i + 1 < len(bs):
            d.append((bs[i + 1]["start"] - b["end"]) * 1e6)
    return d


def crc8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def near(value, nominal, tol):
    return abs(value - nominal) <= nominal * tol


def decode_frame(d, tol=MARK_TOL):
    """51 durations -> (word, errors). errors lists every deviation found."""
    errs = []
    if len(d) != FRAME_TIMINGS:
        return None, [f"{len(d)} durations, expected {FRAME_TIMINGS}"]
    if not near(d[0], HEADER[0], tol) or not near(d[1], HEADER[1], tol):
        errs.append(f"header {d[0]:.0f}/{d[1]:.0f} us, expected {HEADER[0]}/{HEADER[1]}")
    word = 0
    for b in range(24):
        mark, space = d[2 + 2 * b], d[3 + 2 * b]
        if not near(mark, MARK, tol):
            errs.append(f"bit {b}: mark {mark:.0f} us, expected {MARK}")
        if near(space, SPACE1, tol):
            bit = 1
        elif near(space, SPACE0, tol):
            bit = 0
        else:
            errs.append(f"bit {b}: space {space:.0f} us is neither {SPACE0} nor {SPACE1}")
            bit = 0
        word = word << 1 | bit
    if not near(d[50], MARK, tol):
        errs.append(f"trailer mark {d[50]:.0f} us, expected {MARK}")
    payload = word >> 8
    if crc8(bytes([payload >> 8, payload & 255])) != word & 255:
        errs.append("CRC does not match")
    return word, errs


def fields(word):
    p = word >> 8
    return {"shooter": p >> 10, "team": (p >> 7) & 7, "damage": (p >> 4) & 7, "seq": p & 15}


def analyze(text):
    """-> (report lines, ok)."""
    edges = parse_edges(text)
    bs = bursts(edges)
    lines, ok = [], True
    if not bs:
        return ["no marks found"], False

    periods = [p for b in bs for p in b["periods"]]
    highs = [h for b in bs for h in b["highs"]]
    if periods:
        periods.sort()
        f = 1.0 / periods[len(periods) // 2]
        duty = (sorted(highs)[len(highs) // 2]) * f if highs else 0
        good = near(f, CARRIER_HZ, CARRIER_TOL)
        lines.append(f"carrier {f:.0f} Hz ({'ok' if good else 'FAIL, expected 38000'})")
        ok &= good
        good = duty <= MAX_DUTY
        lines.append(f"carrier duty {duty * 100:.0f}% ({'ok' if good else 'FAIL, budget assumes <= 50%'})")
        ok &= good
    else:
        lines.append("carrier: no carrier edges inside the marks (FAIL)")
        ok = False

    d = durations_us(bs)
    starts = [i for i, v in enumerate(d) if i % 2 == 0 and near(v, HEADER[0], 0.25)]
    if not starts:
        return lines + ["no frame header (3000 us mark) found"], False
    prev_start = None
    for n, s in enumerate(starts):
        frame = d[s:s + FRAME_TIMINGS]
        word, errs = decode_frame(frame)
        start_s = bs[s // 2]["start"]
        if errs:
            ok = False
            lines.append(f"frame {n} at {start_s:.3f} s: FAIL: " + "; ".join(errs[:3]))
        else:
            lines.append(f"frame {n} at {start_s:.3f} s: ok {fields(word)}")
        if prev_start is not None and start_s - prev_start < MIN_INTERVAL_S - 0.002:
            ok = False
            lines.append(f"  FAIL: only {(start_s - prev_start) * 1000:.0f} ms after the previous frame, cap is 250 ms")
        prev_start = start_s
        total = sum(frame) / 1000
        if total > 80:
            ok = False
            lines.append(f"  FAIL: frame lasts {total:.0f} ms, cap is 80 ms")
    # stray marks between frames would be an LED on when it should be off
    used = set()
    for s in starts:
        used.update(range(s // 2, s // 2 + 26))
    stray = [i for i in range(len(bs)) if i not in used]
    if stray:
        ok = False
        lines.append(f"FAIL: {len(stray)} mark(s) outside any frame")
    return lines, ok


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    with open(argv[1], encoding="utf-8") as fh:
        lines, ok = analyze(fh.read())
    print("\n".join(lines))
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
