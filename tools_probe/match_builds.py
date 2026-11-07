"""Cross-build address correspondence between TestDrive2.exe (PC) and the
decrypted PS3 EBOOT.ELF, joined on the strings both builds contain.

Why: the PC exe has 2.81 MiB of SecuROM-encrypted .text (VA 0x401000-0x6D1000)
and every [ENC] marker in docs/re/*.md lives there. The PS3 build has the same
Eden code in the clear. If a PC address can be translated to its PS3 twin, the
[ENC] list becomes readable. A single constant offset does NOT work (tested:
deltas +0x230..+0x2750, no exact function start) -- so this tool measures the
mapping empirically from many shared-string anchors instead of assuming one.

Inputs
  C:\\TDU2-PS3-Recomp\\port\\out\\abs_refs.json   addr -> [[site, func], ...]  (PS3)
  C:\\TDU2-PS3-Recomp\\port\\out\\strings.json    [[vaddr, text], ...]        (PS3)
  C:\\Program Files (x86)\\Steam\\...\\TestDrive2.exe                          (PC)

Output
  docs/re/ps3/pc_ps3_anchors.json   { string: {"pc_site":.., "ps3_func":..} }

Usage
  python tools_probe/match_builds.py                 # build + report
  python tools_probe/match_builds.py --top 40        # more anchors in the report
"""
import argparse
import collections
import json
import os
import re
import struct
import sys

PC = r"C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2\TestDrive2.exe"
PS3DIR = r"C:\TDU2-PS3-Recomp\port\out"
OUT = r"C:\OpenTDU2\docs\re\ps3\pc_ps3_anchors.json"

IMAGEBASE = 0x400000
# PC sections that hold strings and code (encrypted .text band excluded later).
PC_STRING_SECS = (".rdata", ".data", ".rsrc")
PC_CODE_SEC = ".text"
ENC_LO, ENC_HI = 0x401000, 0x6D1000          # SecuROM band (measured)


def pc_sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    magic = struct.unpack_from("<H", data, pe + 24)[0]
    sec = pe + 24 + (240 if magic == 0x20B else 224)
    out = {}
    for i in range(nsec):
        o = sec + 40 * i
        nm = data[o:o + 8].rstrip(b"\x00").decode("latin1")
        vs, va, rs, rp = struct.unpack_from("<IIII", data, o + 8)
        out[nm] = (IMAGEBASE + va, vs, rp, rs)
    return out


def pc_strings(data, secs, min_len=6):
    """[(va, text)] over the PC string sections."""
    found = []
    for nm in PC_STRING_SECS:
        va, vs, rp, rs = secs[nm]
        seg = data[rp:rp + rs]
        pos = 0
        while pos < len(seg):
            b = seg[pos]
            if 0x20 <= b < 0x7F:
                start = pos
                while pos < len(seg) and 0x20 <= seg[pos] < 0x7F:
                    pos += 1
                if pos - start >= min_len:
                    found.append((va + start, seg[start:pos].decode("ascii")))
            else:
                pos += 1
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()

    abs_refs = json.load(open(os.path.join(PS3DIR, "abs_refs.json")))
    ps3_strings = json.load(open(os.path.join(PS3DIR, "strings.json")))

    # PS3: string text -> the functions that reference its address
    ps3_by_addr = {int(k, 16): v for k, v in abs_refs.items()}
    ps3_text = {}
    for va, text in ps3_strings:
        if va in ps3_by_addr:
            fns = sorted({f for _, f in ps3_by_addr[va]})
            ps3_text.setdefault(text, []).append((va, fns))

    data = open(PC, "rb").read()
    secs = pc_sections(data)
    pc_str = pc_strings(data, secs)
    pc_addr = {}
    for va, text in pc_str:
        pc_addr.setdefault(text, va)

    # which strings do both builds have, and does the PS3 side have an xref?
    shared = [t for t in ps3_text if t in pc_addr]
    targets = {pc_addr[t] for t in shared}
    print(f"PS3 strings with an xref : {len(ps3_text)}")
    print(f"shared with the PC build  : {len(shared)}")

    # single pass over the readable PC .text: rolling 4-byte window membership
    va, vs, rp, rs = secs[PC_CODE_SEC]
    text = data[rp:rp + rs]
    n = len(text)
    print(f"scanning PC .text: {n} bytes ({n/1048576:.1f} MiB)")
    sites = collections.defaultdict(list)
    targets = targets
    for off in range(0, n - 4):
        if off % 0x1000 == 0 and off:
            pass
        val = text[off] | (text[off + 1] << 8) | (text[off + 2] << 16) | (text[off + 3] << 24)
        if val in targets:
            sites[val].append(va + off)

    anchors = {}
    for t in shared:
        a = pc_addr[t]
        if a not in sites:
            continue
        pv, pfns = ps3_text[t][0]
        anchors[t] = {
            "pc_site": sites[a][0],
            "pc_site_count": len(sites[a]),
            "pc_addr": a,
            "ps3_str": pv,
            "ps3_func": pfns[0] if pfns else None,
        }
    print(f"anchors with both sides    : {len(anchors)}")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(anchors, fh, indent=1, sort_keys=True)
    print(f"-> {OUT}")

    # deltas: pc_site - ps3_func, to see whether the mapping is piecewise
    deltas = collections.Counter()
    for t, a in anchors.items():
        if a["ps3_func"]:
            deltas[a["pc_site"] - a["ps3_func"]] += 1
    print(f"\ndistinct deltas (pc_site - ps3_func): {len(deltas)}")
    for d, c in deltas.most_common(args.top):
        print(f"  {d:+#010x}  x{c}")
    print("\nsample anchors:")
    for t, a in list(sorted(anchors.items()))[:12]:
        print(f"  {t[:44]:46s} pc {a['pc_site']:#010x}  ps3 {a['ps3_func'] and hex(a['ps3_func'])}")

    # ---------------------------------------------------------------- orphans
    # A string that the PC build also contains but whose PC .text has NO
    # reference at all can only be referenced from the SecuROM band. The PS3
    # function referencing it is therefore the readable twin of an [ENC] PC
    # function. This is the [ENC] locator.
    #
    # Caveat: a reference made as base+offset (not an absolute immediate) is
    # invisible to this scan, so a function can appear "orphan" while its PC
    # twin is readable. Treat the list as candidates, not proof.
    ps3_fn_strings = collections.defaultdict(set)
    for t in shared:
        a = pc_addr[t]               # NOTE: sites is keyed by the *PC* address
        if a in sites:               # has a readable PC reference -> not orphan
            continue
        pv, pfns = ps3_text[t][0]
        for f in pfns:
            ps3_fn_strings[f].add(t)

    orphans = {f: sorted(s) for f, s in ps3_fn_strings.items() if len(s) >= 2}
    print(f"\n[ENC]-twin candidates (PS3 functions whose strings have NO readable"
          f" PC reference): {len(orphans)}")
    for f, ss in sorted(orphans.items(), key=lambda kv: -len(kv[1]))[:args.top]:
        print(f"  func_{f:08X}  {len(ss):3d} strings  e.g. {[s[:28] for s in ss[:4]]}")
    with open(os.path.join(os.path.dirname(OUT), "ps3_enc_twins.json"), "w") as fh:
        json.dump({f"{f:08X}": s for f, s in orphans.items()}, fh, indent=1, sort_keys=True)
    print(f"-> {os.path.join(os.path.dirname(OUT), 'ps3_enc_twins.json')}")


if __name__ == "__main__":
    sys.exit(main())
