#!/usr/bin/env python3
"""Report / validate PROGRESS.md (RULES.md §8).

  python tools_probe/status_report.py            # local report
  python tools_probe/status_report.py --check     # exit 1 on a malformed board
  python tools_probe/status_report.py --summary out.md
                                                   # append a GitHub job summary

The workflow .github/workflows/status.yml runs this on every push/PR so the
board is visible on GitHub and a WIP row cannot rot without an owner or a next
step. Kept dependency-free (stdlib only).
"""
import re
import sys

STATUSES = {"Done", "WIP", "Partial", "Not started", "Blocked", "Open"}
BOARD = "PROGRESS.md"


def cells(line):
    line = line.strip()
    if not line.startswith("|") or not line.endswith("|"):
        return []
    return [c.strip() for c in line[1:-1].split("|")]


def is_separator(row):
    return all(re.fullmatch(r":?-{2,}:?", c) for c in row if c) and any(row)


def parse(path):
    with open(path, encoding="utf-8") as handle:
        lines = handle.readlines()
    section = None
    wip = []
    features = []
    for line in lines:
        if line.startswith("## "):
            section = line[3:].strip().lower()
            continue
        row = cells(line)
        if len(row) < 2 or is_separator(row):
            continue
        if section is not None and section.startswith("wip") and len(row) >= 4:
            wip.append(row)
        elif section is not None and section.startswith("feature board") and len(row) >= 3:
            features.append(row)
    # Drop the header rows ("Area | Status | ...").
    wip = [r for r in wip if r[0].lower() not in ("area", "")]
    features = [r for r in features if r[0].lower() not in ("area", "")]
    return wip, features


def report(wip, features):
    counts = {}
    for row in features:
        counts[row[1]] = counts.get(row[1], 0) + 1
    out = []
    out.append("PROGRESS.md")
    out.append("  WIP rows: %d" % len(wip))
    for row in wip:
        out.append("    - %s | %s | next: %s" % (row[0], row[1], row[3]))
    out.append("  Features: %d" % len(features))
    for status in ("Done", "Partial", "WIP", "Open", "Blocked", "Not started"):
        if status in counts:
            out.append("    %-12s %d" % (status, counts[status]))
    return out, counts


def check(wip, features):
    problems = []
    for row in wip:
        if not row[1] or row[1] in ("-", "—"):
            problems.append("WIP row '%s' has no owner/branch" % row[0])
        if not row[3] or row[3] in ("-", "—", "TBD", "TODO"):
            problems.append("WIP row '%s' has no next step" % row[0])
    for row in features:
        if row[1] not in STATUSES:
            problems.append("feature row '%s' has status '%s' (allowed: %s)" %
                            (row[0], row[1], ", ".join(sorted(STATUSES))))
    return problems


def main():
    wip, features = parse(BOARD)
    lines, _ = report(wip, features)
    text = "\n".join(lines)
    print(text)
    if "--summary" in sys.argv:
        path = sys.argv[sys.argv.index("--summary") + 1]
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("```\n" + text + "\n```\n")
    if "--check" in sys.argv:
        problems = check(wip, features)
        if problems:
            print("\nBOARD PROBLEMS:")
            for problem in problems:
                print("  - " + problem)
            return 1
        print("\nOK: every WIP row has an owner and a next step; every status is legal.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
