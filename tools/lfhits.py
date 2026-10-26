#!/usr/bin/env python3
"""Score a hit-path test from the gear's logs (docs/hit-log.md, docs/bench.md).

    tools/lfhits.py --expect hit  blaster.log hub.log     # aimed at the target
    tools/lfhits.py --expect miss blaster.log hub.log     # aimed away

Each log is the serial output of one board, saved as text; only lines with
"LFLOG" count. Clocks of different boards are not compared: a shot is matched
to the hit with the same shooter and seq, in order (seq wraps every 16 shots).

Exit 0 when the test passes, 1 when it fails, 2 on bad input.
  --expect hit : pass when at least --min-rate (default 0.99) of the shots were
                 counted once and no hit appeared without a shot.
  --expect miss: pass when nothing was counted.
"""
import argparse
import re
import sys
from collections import Counter

FIELD = re.compile(r"(\w+)=(\S+)")


def parse(lines):
    out = []
    for line in lines:
        if "LFLOG" not in line:
            continue
        rest = line.split("LFLOG", 1)[1]
        f = dict(FIELD.findall(rest))
        if "ev" not in f:
            continue
        out.append(f)
    return out


def score(entries):
    shots = [e for e in entries if e["ev"] == "shot"]
    hits = [e for e in entries if e["ev"] == "hit"]
    drops = [e for e in entries if e["ev"] == "drop"]
    bad = [e for e in entries if e["ev"] == "bad"]

    matched = [False] * len(shots)
    cursor = 0
    unmatched_hits = []
    sensors = Counter()
    for h in hits:
        for i in range(cursor, len(shots)):
            if not matched[i] and shots[i]["shooter"] == h["shooter"] and shots[i]["seq"] == h["seq"]:
                matched[i] = True
                cursor = i
                sensors[h.get("sensor", "?")] += 1
                break
        else:
            unmatched_hits.append(h)
    counted = sum(matched)
    return {
        "shots": len(shots),
        "counted": counted,
        "missed": len(shots) - counted,
        "false_hits": len(unmatched_hits),
        "duplicates": sum(d.get("reason") == "duplicate" for d in drops),
        "other_drops": Counter(d.get("reason") for d in drops if d.get("reason") != "duplicate"),
        "bad_frames": Counter(b.get("why") for b in bad),
        "first_sensor": sensors,
    }


def verdict(s, expect, min_rate):
    notes = []
    if s["shots"] == 0:
        return False, ["no shots in the logs"]
    if expect == "hit":
        rate = s["counted"] / s["shots"]
        ok = rate >= min_rate and s["false_hits"] == 0
        notes.append(f"hit rate {rate * 100:.1f}% (need {min_rate * 100:.0f}%)")
        if s["false_hits"]:
            notes.append(f"{s['false_hits']} hit(s) with no matching shot")
        return ok, notes
    ok = s["counted"] == 0 and s["false_hits"] == 0
    if s["counted"]:
        notes.append(f"{s['counted']} shot(s) counted although aimed away")
    if s["false_hits"]:
        notes.append(f"{s['false_hits']} hit(s) with no matching shot")
    return ok, notes


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--expect", choices=["hit", "miss"], required=True)
    ap.add_argument("--min-rate", type=float, default=0.99)
    ap.add_argument("logs", nargs="+")
    a = ap.parse_args(argv[1:])
    lines = []
    try:
        for path in a.logs:
            with open(path, encoding="utf-8", errors="replace") as fh:
                lines += fh.read().splitlines()
    except OSError as err:
        print(err, file=sys.stderr)
        return 2
    s = score(parse(lines))
    print(f"shots {s['shots']}  counted {s['counted']}  missed {s['missed']}  "
          f"false hits {s['false_hits']}  duplicates suppressed {s['duplicates']}")
    if s["other_drops"]:
        print("other drops:", dict(s["other_drops"]))
    if s["bad_frames"]:
        print("unreadable bursts:", dict(s["bad_frames"]))
    if s["first_sensor"]:
        print("sensor that read a counted shot first:", dict(sorted(s["first_sensor"].items())))
    ok, notes = verdict(s, a.expect, a.min_rate)
    for n in notes:
        print(n)
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
