"""Which build is this exe? - the feature strings it does and does not contain.

    python tools_probe/exe_probe.py E:\\openTDU2\\build-win\\bin\\opentdu2-win.exe
    python tools_probe/exe_probe.py <exe> <exe> ...        (compare two builds)

The exe carries no build stamp, so the honest way to date one is the set of
strings its features put in it. This prints the file's date and size plus a
present/absent table, which is how the "my tree is old" confusion was pinned
down: the hub's `win` build has TESTCAR and IKOffsetDB (from the story spine and
the intro reader) but neither SHOPSCAR nor SeatedAvatar, because glm's story
modes were never merged into `win` - while glm's own build-glm exe has both.
"""
import argparse
import datetime
import os
import sys

# Strings that date a build: each one arrived with a specific piece of work.
KEYS = (
    "-nort",            # ray tracing (10-28)
    "-rtao",            # ray traced ambient occlusion (10-30)
    "-logcarlights",    # car lights / windows / night dashboard
    "-storystep",       # the story spine (10-30)
    "TESTCAR",          # the spine's mode table entry
    "IKOffsetDB",       # the intro cinematic's IK offset reader
    "SHOPSCAR",         # glm's used car dealer mode
    "SeatedAvatar",     # glm's avatars-in-cars modules
    "SHOP",             # the shop mode
    "CINVIEWER",
    "-buildindex",      # the asset index
    "-fuel",            # the fuel feature
    "-vehtest",         # the vehicle self tests
    "-watchdogtest",
    "-xplat",           # never present: a sanity check for the reader
)


def probe(path, keys):
    with open(path, "rb") as handle:
        data = handle.read()
    text = data.decode("latin-1")          # byte-preserving, no decode errors
    wide = data.decode("utf-16-le", errors="ignore")
    have = []
    for key in keys:
        if key in text or key in wide:
            have.append(key)
    return have


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe", nargs="+")
    parser.add_argument("--keys", help="comma-separated strings instead of the default set")
    args = parser.parse_args()
    keys = tuple(args.keys.split(",")) if args.keys else KEYS

    results = {}
    for path in args.exe:
        if not os.path.exists(path):
            print("%s: not found" % path)
            continue
        stamp = datetime.datetime.fromtimestamp(os.path.getmtime(path)).strftime("%Y-%m-%d %H:%M")
        print("%s\n  %s, %d bytes" % (path, stamp, os.path.getsize(path)))
        results[path] = probe(path, keys)
        for key in keys:
            print("    %-16s %s" % (key, "yes" if key in results[path] else "-"))

    if len(results) > 1:
        print("\n=== differences ===")
        paths = list(results)
        for key in keys:
            present = [p for p in paths if key in results[p]]
            if 0 < len(present) < len(paths):
                print("  %-16s only in: %s" % (key, ", ".join(os.path.basename(p) for p in present)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
