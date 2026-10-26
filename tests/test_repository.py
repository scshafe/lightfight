"""Repository checks: the skeleton's parts exist and the brief's rules hold."""

import os
import re
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", ".venv", "node_modules", "build", ".pio", "DerivedData"}
# A tailnet hostname ends in the label before "ts.net". The only label
# allowed is the placeholder "<tailnet>" (docs/brief.md: never write the
# tailnet's name).
SUFFIX = ".ts" + ".net"  # split so this file does not match itself
TS_NET = re.compile(r"([A-Za-z0-9<>_-]+)" + re.escape(SUFFIX) + r"\b")
PLACEHOLDER = "<tailnet>"


def text_files():
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            path = os.path.join(dirpath, name)
            try:
                with open(path, encoding="utf-8") as handle:
                    yield os.path.relpath(path, ROOT), handle.read()
            except (UnicodeDecodeError, OSError):
                continue


class SkeletonTest(unittest.TestCase):
    def test_parts_exist(self):
        for part in ("firmware", "ios", "server"):
            with self.subTest(part=part):
                self.assertTrue(
                    os.path.isfile(os.path.join(ROOT, part, "README.md")),
                    f"{part}/README.md is missing",
                )

    def test_brief_exists(self):
        self.assertTrue(os.path.isfile(os.path.join(ROOT, "docs", "brief.md")))


class TailnetNameTest(unittest.TestCase):
    def test_placeholder_matches(self):
        self.assertEqual(TS_NET.findall("lightfight." + PLACEHOLDER + SUFFIX), [PLACEHOLDER])
        self.assertEqual(TS_NET.findall("host.example-name" + SUFFIX), ["example-name"])

    def test_no_tailnet_name_in_repository(self):
        found = []
        for rel, text in text_files():
            for label in TS_NET.findall(text):
                if label != PLACEHOLDER:
                    found.append(rel)
        self.assertEqual(found, [], "write <tailnet> instead of the tailnet's name")


if __name__ == "__main__":
    unittest.main()
