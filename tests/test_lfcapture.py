"""tools/lfcapture.py against synthetic captures built from the shared vectors."""

import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lfcapture  # noqa: E402


def vectors():
    path = os.path.join(ROOT, "protocol", "ir-vectors.csv")
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.strip() and not line.startswith("#"):
                s, t, d, q, h = line.strip().split(",")
                yield int(s), int(t), int(d), int(q), int(h, 16)


def timings(word, space_scale=1.0):
    d = [3000, 1500]
    for b in range(23, -1, -1):
        d += [560, 1690 if word >> b & 1 else 560]
    d.append(560)
    return d


def capture(frames, start=0.1, interval=0.3, carrier=38000.0, duty=0.33, scale=1.0):
    """Edges of marks keyed at the carrier, as seen on the gate pin."""
    edges = []
    period = 1.0 / carrier
    t0 = start
    for word in frames:
        t = t0
        d = timings(word)
        for i, us in enumerate(d):
            us *= scale
            if i % 2 == 0:  # mark: carrier pulses
                end = t + us * 1e-6
                p = t
                while p + period * duty <= end + 1e-12:
                    edges += [(p, 1), (p + period * duty, 0)]
                    p += period
                t = end
            else:
                t += us * 1e-6
        t0 += interval
    return "time,level\n" + "\n".join(f"{t:.9f},{v}" for t, v in edges) + "\n"


class CaptureTest(unittest.TestCase):
    def test_vectors_decode_and_pass(self):
        words = [h for *_, h in vectors()]
        lines, ok = lfcapture.analyze(capture(words))
        self.assertTrue(ok, "\n".join(lines))
        self.assertEqual(sum("ok {" in l for l in lines), len(words))

    def test_fields_reported(self):
        lines, ok = lfcapture.analyze(capture([0x1539B9]))
        self.assertTrue(ok)
        self.assertIn("'shooter': 5", "\n".join(lines))
        self.assertIn("'seq': 9", "\n".join(lines))

    def test_wrong_carrier_fails(self):
        lines, ok = lfcapture.analyze(capture([0x1539B9], carrier=36000.0))
        self.assertFalse(ok)
        self.assertIn("carrier", "\n".join(lines))

    def test_duty_over_budget_fails(self):
        lines, ok = lfcapture.analyze(capture([0x1539B9], duty=0.65))
        self.assertFalse(ok)
        self.assertIn("duty", "\n".join(lines))

    def test_slow_timing_fails(self):
        lines, ok = lfcapture.analyze(capture([0x1539B9], scale=1.2))
        self.assertFalse(ok, "\n".join(lines))

    def test_too_fast_fire_fails(self):
        lines, ok = lfcapture.analyze(capture([0x1539B9, 0x1539B9], interval=0.1))
        self.assertFalse(ok)
        self.assertIn("250 ms", "\n".join(lines))

    def test_bad_crc_fails(self):
        lines, ok = lfcapture.analyze(capture([0x1539B9 ^ 1]))
        self.assertFalse(ok)
        self.assertIn("CRC", "\n".join(lines))

    def test_stray_mark_fails(self):
        text = capture([0x1539B9])
        text += "5.000000000,1\n5.000013000,0\n"  # a lone pulse long after the frame
        lines, ok = lfcapture.analyze(text)
        self.assertFalse(ok)
        self.assertIn("outside any frame", "\n".join(lines))

    def test_empty_capture_fails(self):
        self.assertFalse(lfcapture.analyze("time,level\n")[1])


if __name__ == "__main__":
    unittest.main()
