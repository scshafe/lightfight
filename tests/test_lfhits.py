"""tools/lfhits.py on synthetic logs."""

import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import lfhits  # noqa: E402


def shot(t, shooter, seq):
    return f"I (1) blaster: LFLOG t_ms={t} ev=shot shooter={shooter} team=1 seq={seq}"


def hit(t, sensor, shooter, seq):
    return f"LFLOG t_ms={t} ev=hit sensor={sensor} shooter={shooter} seq={seq} dmg=10 health=90"


def drop(t, sensor, shooter, seq, reason="duplicate"):
    return f"LFLOG t_ms={t} ev=drop sensor={sensor} shooter={shooter} seq={seq} reason={reason}"


class ScoreTest(unittest.TestCase):
    def test_all_counted_once_with_duplicates(self):
        lines = []
        for i in range(40):  # seq wraps at 16
            lines.append(shot(i * 300, 5, i % 16))
            lines.append(hit(9000 + i * 300, i % 5, 5, i % 16))
            lines.append(drop(9001 + i * 300, (i + 1) % 5, 5, i % 16))
        s = lfhits.score(lfhits.parse(lines))
        self.assertEqual((s["shots"], s["counted"], s["missed"], s["false_hits"]), (40, 40, 0, 0))
        self.assertEqual(s["duplicates"], 40)
        ok, _ = lfhits.verdict(s, "hit", 0.99)
        self.assertTrue(ok)

    def test_misses_fail_hit_expectation(self):
        lines = []
        for i in range(100):
            lines.append(shot(i * 300, 5, i % 16))
            if i % 10:  # 10% not counted
                lines.append(hit(i * 300, 0, 5, i % 16))
        s = lfhits.score(lfhits.parse(lines))
        self.assertEqual((s["counted"], s["missed"]), (90, 10))
        self.assertFalse(lfhits.verdict(s, "hit", 0.99)[0])
        self.assertTrue(lfhits.verdict(s, "hit", 0.85)[0])

    def test_false_hit_fails(self):
        lines = [shot(0, 5, 0), hit(1, 0, 5, 0), hit(2, 1, 6, 3)]  # shooter 6 never fired
        s = lfhits.score(lfhits.parse(lines))
        self.assertEqual(s["false_hits"], 1)
        self.assertFalse(lfhits.verdict(s, "hit", 0.5)[0])

    def test_hit_for_an_old_seq_is_not_matched_backwards(self):
        # two shots, the hub reports seq 1 then seq 0: the second is unmatched
        lines = [shot(0, 5, 0), shot(300, 5, 1), hit(1, 0, 5, 1), hit(2, 0, 5, 0)]
        s = lfhits.score(lfhits.parse(lines))
        self.assertEqual((s["counted"], s["false_hits"]), (1, 1))

    def test_miss_expectation(self):
        lines = [shot(i * 300, 5, i) for i in range(10)]
        s = lfhits.score(lfhits.parse(lines + [drop(1, 0, 5, 0, "dead")]))
        self.assertTrue(lfhits.verdict(s, "miss", 0.99)[0])
        s = lfhits.score(lfhits.parse(lines + [hit(1, 0, 5, 3)]))
        self.assertFalse(lfhits.verdict(s, "miss", 0.99)[0])

    def test_no_shots_fails(self):
        s = lfhits.score(lfhits.parse(["boot", hit(1, 0, 5, 0)]))
        self.assertFalse(lfhits.verdict(s, "hit", 0.9)[0])

    def test_noise_lines_ignored_and_bad_counted(self):
        lines = ["E (5) rmt: whatever", "LFLOG garbage", "LFLOG t_ms=1 ev=bad sensor=2 why=crc", shot(0, 1, 0)]
        s = lfhits.score(lfhits.parse(lines))
        self.assertEqual(s["bad_frames"]["crc"], 1)
        self.assertEqual(s["shots"], 1)


if __name__ == "__main__":
    unittest.main()
