"""tools/visor_check.py: the visor's shading geometry (hardware/visor.scad)."""

import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import visor_check  # noqa: E402


class VisorTest(unittest.TestCase):
    def test_default_visor_shades_the_sun_and_keeps_the_horizon(self):
        lines, ok = visor_check.check()
        self.assertTrue(ok, lines)

    def test_short_brim_fails(self):
        text = visor_check.SCAD.read_text()
        text = re.sub(r"^roof_len\s*=.*$", "roof_len = 7;", text, flags=re.M)
        lines, ok = visor_check.check(text)
        self.assertFalse(ok)
        self.assertIn("FAIL", "\n".join(lines))

    def test_overlong_brim_fails(self):
        text = visor_check.SCAD.read_text()
        text = re.sub(r"^roof_len\s*=.*$", "roof_len = 22;", text, flags=re.M)
        lines, ok = visor_check.check(text)
        self.assertFalse(ok)
