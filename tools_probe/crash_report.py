"""What happened in my last run? - crashes, dumps and log tails in one command.

    python tools_probe/crash_report.py --logs E:\\openTDU2
    python tools_probe/crash_report.py --route          (the A/B recipe)
    python tools_probe/crash_report.py --logs E:\\openTDU2 --hours 2

Run it *after* a session and *before* the next launch - both game logs are
truncated when a new run starts. It prints:

  1. the opentdu2 crashes the Windows Application log recorded in the window,
     as `time  exe  faulting module+offset`, flagged against the known family
     (`nvoglv64.dll+0xecf46c`: an access violation that started 10-28 with the
     ray tracing work and hits every build);
  2. the newest minidumps, with the exception of the newest one (dump_info);
  3. the tail of `opentdu2.log` and `opentdu2-watchdog.log` plus the markers
     that matter: whether ray tracing was on in that run, the device-lost
     report, sector streaming, `[perf] vram`, the watchdog's worst frame gap.
"""
import argparse
import datetime
import glob
import os
import subprocess
import sys

KNOWN_FAMILY = ("nvoglv64.dll", "0x0000000000ecf46c")
MARKERS = (
    "Ray tracing:",
    "World structures:",
    "device lost",
    "VK_ERROR_DEVICE_LOST",
    "EdenWorld:",
    "sectors unloaded",
    "BLAS",
    "vram:",
    "STALL",
    "stopped after",
)

RECIPE = """A/B recipe (one variable per run, same route, ~10 minutes each):

  A  ray tracing off - the complete switch, it forces the device off whatever the ini says
     cd /d E:\\openTDU2
     build-win\\bin\\opentdu2-win.exe -game C:\\TDU2-Gamedata\\TestDrive2 ^
         -gm freeride -skipcine -storytime 86400 -nort > _ab_a_nort.log 2>&1

  B  ray tracing on, world structures off - the narrower test (restart to apply)
     %APPDATA%\\OpenTDU2\\OpenTDU2\\options.ini :  ray_tracing_world = 0   (ray_tracing stays 1)
     ... same command line without -nort > _ab_b_noworld.log 2>&1

  C  B + a lower resolution (options.ini video_width / video_height = 1920 / 1080)

Then: python tools_probe\\crash_report.py --logs E:\\openTDU2 --hours 6

What each result means:
  * a hard crash to desktop, `nvoglv64.dll+0xecf46c` in the event log -> the driver access
    violation this report flags; if it survives A, ray tracing is not the trigger.
  * a freeze / unresponsive game, then a clean exit -> the VK_ERROR_DEVICE_LOST family
    (the app logs one line and exits; docs/STATE.md session 48).
  * `Ray tracing: disabled (-nort)` must appear in run A's log - if it does not, the flag
    never reached the device and that run proves nothing.
"""


def crashes(hours):
    """The opentdu2 Application Error events of the last `hours`."""
    script = (
        "$s=(Get-Date).AddHours(-%d); "
        "Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'; StartTime=$s} "
        "-ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'opentdu2' } | ForEach-Object { "
        "'{0:MM-dd HH:mm:ss}|{1}|{2}|{3}' -f $_.TimeCreated,"
        "([regex]::Match($_.Message,'Faulting application name: ([^,\\r]+)').Groups[1].Value),"
        "([regex]::Match($_.Message,'Faulting module name: ([^,\\r]+)').Groups[1].Value),"
        "([regex]::Match($_.Message,'Fault offset: ([^,\\r]+)').Groups[1].Value) }" % hours)
    try:
        out = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                             capture_output=True, text=True, timeout=180).stdout
    except (OSError, subprocess.SubprocessError) as ex:
        print("  (could not read the event log: %s)" % ex)
        return []
    rows = []
    for line in out.splitlines():
        parts = line.strip().split("|")
        if len(parts) == 4 and parts[0]:
            rows.append(parts)
    return rows


def dump_files():
    folder = os.path.join(os.environ.get("LOCALAPPDATA", ""), "CrashDumps")
    found = []
    for pattern in ("opentdu2*.dmp", "opentdu2*.exe.*.dmp"):
        found += glob.glob(os.path.join(folder, pattern))
    return sorted(set(found), key=os.path.getmtime)


def read_lines(path):
    if not os.path.exists(path):
        return None
    with open(path, "r", errors="replace") as handle:
        return handle.read().splitlines()


def report_crashes(hours):
    print("=== crashes in the last %g h (Windows Application log) ===" % hours)
    rows = crashes(hours)
    if not rows:
        print("  none - no opentdu2 Application Error event in the window")
        return
    for when, app, module, offset in rows:
        known = "   <-- the known RT-era family" if (module, offset) == KNOWN_FAMILY else ""
        print("  %s  %-20s %s+%s%s" % (when, app, module, offset, known))
    families = {}
    for _when, _app, module, offset in rows:
        key = "%s+%s" % (module, offset)
        families[key] = families.get(key, 0) + 1
    print("  signatures: " + ", ".join(
        "%s x%d" % (k, v) for k, v in sorted(families.items(), key=lambda kv: -kv[1])))


def report_dumps(count):
    print("\n=== minidumps ===")
    dumps = dump_files()
    if not dumps:
        print("  none in %LOCALAPPDATA%\\CrashDumps")
        return
    for path in dumps[-count:]:
        print("  %s  %s" % (datetime.datetime.fromtimestamp(os.path.getmtime(path))
                            .strftime("%m-%d %H:%M:%S"), path))
    print("\n=== newest dump ===")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        import dump_info
        sys.argv = ["dump_info", dumps[-1]]
        dump_info.main()
    except SystemExit:
        pass
    except Exception as ex:                                     # noqa: BLE001
        print("  (dump_info failed: %s)" % ex)


def report_logs(folder):
    for name in ("opentdu2.log", "opentdu2-watchdog.log"):
        path = os.path.join(folder, name)
        print("\n=== %s ===" % path)
        lines = read_lines(path)
        if lines is None:
            print("  (not found)")
            continue
        for line in lines[-14:]:
            print("  " + line)
        hits = [line for line in lines if any(marker in line for marker in MARKERS)][-14:]
        if hits:
            print("  --- markers ---")
            for line in hits:
                print("  " + line)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--logs", default=".", help="the tree whose logs to read (the launcher's cwd)")
    parser.add_argument("--hours", type=float, default=6.0, help="how far back the event log goes")
    parser.add_argument("--dumps", type=int, default=4, help="how many recent dumps to list")
    parser.add_argument("--route", action="store_true", help="print the A/B recipe instead")
    args = parser.parse_args()

    if args.route:
        print(RECIPE)
        return 0
    report_crashes(args.hours)
    report_dumps(args.dumps)
    report_logs(args.logs)
    print("\n=== how to read this ===")
    print("  hard crash + nvoglv64.dll+0xecf46c  -> the driver access violation family")
    print("  freeze then a clean exit + 'device lost' -> the VK_ERROR_DEVICE_LOST family")
    print("  'Ray tracing: disabled (-nort)' must be in run A's log for that run to prove anything")
    print("  --route prints the A/B recipe")
    return 0


if __name__ == "__main__":
    sys.exit(main())

