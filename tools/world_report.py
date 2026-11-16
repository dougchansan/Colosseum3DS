#!/usr/bin/env python3
"""Group the whole-game story test report (tests/test_world_story.c) by cause.

Reads the "world prefix=.. floor=.. event=.. status=.. reason=.." lines and
prints: totals per event kind, failures grouped by reason (script addresses
folded so one unported native is one row, with how many floors it blocks),
the floors whose arrival fails, and the floor connectors scripts requested.
"""
import argparse
from collections import Counter, defaultdict
import re


def parse(path):
    rows = []
    for line in open(path, encoding="utf-8"):
        if not line.startswith("world "):
            continue
        head, _, reason = line.rstrip("\n").partition(" reason=")
        row = dict(part.split("=", 1) for part in head.split()[1:])
        row["reason"] = reason
        rows.append(row)
    return rows


def cause(reason):
    return re.sub(r"\b[0-9a-f]{8}\b", "X", reason)


def summary(rows):
    lines = []
    kinds = Counter((r["event"], r["status"]) for r in rows)
    floors = {r["floor"] for r in rows}
    lines.append("floors %d, events %d, ok %d, failed %d, partial %d, skipped %d" % (
        len(floors), len(rows), sum(r["status"] == "ok" for r in rows),
        sum(r["status"] == "failed" for r in rows), sum(r["status"] == "partial" for r in rows),
        sum(r["status"] == "skipped" for r in rows)))
    for event in ("arrival", "door", "touch", "talk"):
        lines.append("  %-8s ok %4d  failed %4d  skipped %4d" % (
            event, kinds[(event, "ok")], kinds[(event, "failed")], kinds[(event, "skipped")]))
    groups = defaultdict(list)
    for r in rows:
        if r["status"] in ("failed", "partial", "blocked"):
            groups[cause(r["reason"])].append(r)
    lines.append("not ok by cause (events, floors):")
    for reason, items in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        lines.append("  %4d %3d  %s" % (len(items), len({r["floor"] for r in items}), reason))
    arrivals = [r for r in rows if r["event"] == "arrival" and r["status"] != "ok"]
    lines.append("arrival not ok (%d):" % len(arrivals))
    for r in arrivals:
        lines.append("  %s %-18s %s" % (r["floor"], r["prefix"], cause(r["reason"])))
    links = sorted({(r["floor"], r["next"]) for r in rows if r.get("next", "000") != "000"})
    lines.append("connectors requested by scripts (%d): %s" % (
        len(links), " ".join("%s>%s" % link for link in links)))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("report")
    print(summary(parse(parser.parse_args().report)))


if __name__ == "__main__":
    main()
