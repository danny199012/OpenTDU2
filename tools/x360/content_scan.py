"""Content scan: every bank of the Xbox 360 prototypes against the retail PC data.

Finds cut content by comparing the parts of a bank that are **platform
invariant** - the `.3dd` scene and `.2dm` material byte sizes, and the decoded
mip 0 pixels of every texture - plus the asset list itself, named on both sides
from the bank's own directory (`eden_bank.py`).

    python content_scan.py --selftest                 # the Q7 evidence, no setup
    python content_scan.py --bank q7_inuit_i          # one bank, verbose
    python content_scan.py --signatures --jobs 6      # phase 1: decompress + index
    python content_scan.py --compare                  # phase 2: diff + report
    python content_scan.py --restore q7_inuit_i --userdata <folder>   # write the cut textures

Phase 1 is the slow one: it LZX-decompresses every named `.bnk` of a prototype
(13.97 GB, ~187 MB/s through `xzxd.exe`, ~9,553 banks) and writes one signature
per bank to `_scan/<beta>.jsonl`. It is resumable - banks already in the cache
are skipped - and it is the only phase that touches the prototype data.

Phase 2 reads the retail banks straight from `C:\\TDU2-Gamedata\\TestDrive2` (PC
banks are stored, not compressed) and writes `_scan/report_<beta>.txt`.

Why these comparators (all measured, see `docs/re/CROSSBUILD_PLAN.md`):

* `.3dd` / `.2dm` keep the **same byte size** on both platforms for the same
  asset, so a size difference means different content.
* `.3dg` does not (the 360 packs its vertex attributes, ~0.63x) - its size is
  reported as information only.
* A 360 `.2db` is page-aligned and tiled and its mip tail is packed, so texture
  *sizes* are not comparable; the pixels are. Mip 0 of the 360 texture is untiled
  and its 16-bit words swapped (`x360_tex`), which reproduces the retail bytes
  **exactly** for a shared texture (q7_int / q7_int_ts / q7_glass_i; the cut
  q7_int2 differs in 19% of its bytes) - so the comparison is byte equality.
"""
import argparse
import collections
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import eden_bank  # noqa: E402
from x360_tex import tiled_offset  # noqa: E402

try:  # the tiled gather is the hot loop: vectorize it when numpy is around
    import numpy as _np
except Exception:  # pragma: no cover - numpy is optional
    _np = None

DEFAULT_RETAIL = r"C:\TDU2-Gamedata\TestDrive2"
DEFAULT_CACHE = os.path.join(ROOT, "_scan")
SIG_VERSION = 1
COMPARED_KINDS = ("3dd", "2dm")  # platform-invariant byte sizes
# A 360 texture is page aligned and tiled and its mip tail is packed, so .2db
# sizes are never comparable - the pixels are (see texture_mip0).
NOT_COMPARABLE_SIZES = ("2db",)

# eViewFormat -> (tiling element size, byte-swap unit). The element is the tiling
# unit (a 4x4 block for BC formats, one texel otherwise). The swap unit is the
# 360's big-endian component size: measured as 2 for BC blocks (16-bit words,
# exact against the retail bytes), 4 for formats whose components are 32-bit or
# packed in a u32, and the texel size when every component is 8-bit.
FORMATS = {
    0: (3, 3),    # R8G8B8
    1: (4, 4),    # R8G8B8A8
    2: (4, 4),    # X8R8G8B8
    3: (4, 4),    # A8R8G8B8
    4: (2, 2),    # R5G6B5
    5: (2, 2),    # A1R5G5B5
    6: (1, 1),    # A8
    7: (2, 2),    # A8R3G3B2
    8: (4, 4),    # A2R10G10B10
    9: (4, 2),    # G16R16
    10: (8, 2),   # A16B16G16R16
    11: (1, 1),   # L8
    12: (2, 2),   # L16
    13: (2, 2),   # V8U8
    14: (4, 4),   # Q8W8V8U8
    15: (4, 2),   # V16U16
    16: (8, 2),   # Q16W16V16U16
    17: (2, 2),   # R16F
    18: (4, 2),   # G16FR16F
    19: (8, 2),   # A16B16G16R16F
    20: (4, 4),   # R32F
    21: (8, 4),   # G32R32F
    22: (16, 4),  # A32B32G32R32F
    23: (8, 2),   # DXT1
    24: (16, 2),  # DXT2
    25: (16, 2),  # DXT3
    26: (16, 2),  # DXT4
    27: (16, 2),  # DXT5
    61: (16, 2),  # ATI2 (BC5)
}
for _base in list(range(0, 23)):  # 28..50: the sRGB variants of 0..22
    FORMATS[_base + 28] = FORMATS[_base]
for _base in range(23, 28):       # 51..55: sRGB DXT1..DXT5
    FORMATS[_base + 28] = FORMATS[_base]
BLOCK_FORMATS = set([23, 24, 25, 26, 27, 61] + [51, 52, 53, 54, 55])


_tile_cache = {}
_tile_lock = threading.Lock()


def _tile_offsets(units_x, units_y, log_bpp):
    """The tiled-offset table of a 360 texture (XGAddress2DTiledOffset), cached
    per geometry: a bank's textures share their sizes, and the table is the hot
    loop of the whole scan."""
    key = (units_x, units_y, log_bpp)
    with _tile_lock:
        table = _tile_cache.get(key)
    if table is not None:
        return table
    if _np is not None:
        y, x = _np.mgrid[0:units_y, 0:units_x]
        y = y.astype(_np.int64)
        x = x.astype(_np.int64)
        aligned_width = (units_x + 31) & ~31
        macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log_bpp + 7)
        micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
        offset = (macro + ((micro & ~15) << 1) + (micro & 15)
                  + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4))
        table = ((((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63)
                  + ((y & 16) << 7)
                  + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp).ravel()
    else:
        table = [tiled_offset(x, y, units_x, log_bpp)
                 for y in range(units_y) for x in range(units_x)]
    with _tile_lock:
        _tile_cache[key] = table
    return table


def texture_mip0(blob, platform):
    """The decoded mip 0 of a .2db as the retail (untiled, little endian) bytes.

    Returns {w, h, d, mips, fmt, px, note}: `px` is the normalized mip 0 bytes
    (None when the format is not decoded, with the reason in `note`).
    """
    if len(blob) < 0x50:
        return None
    order = "<" if platform == eden_bank.PC else ">"
    width, height, depth = struct.unpack_from(order + "HHH", blob, 0x28)
    mips = blob[0x2E]
    fmt = struct.unpack_from(order + "I", blob, 0x3C)[0]
    info = {"w": width, "h": height, "d": depth, "mips": mips, "fmt": fmt,
            "px": None, "note": ""}
    if width == 0 or height == 0:
        info["note"] = "no dimensions"
        return info
    element, unit = FORMATS.get(fmt, (0, 0))
    if element == 0:
        info["note"] = "format %d not compared" % fmt
        return info
    blocks = fmt in BLOCK_FORMATS
    units_x = max(1, width // 4) if blocks else width
    units_y = max(1, height // 4) if blocks else height
    size = units_x * units_y * element
    if platform == eden_bank.PC:
        texels = blob[0x50:0x50 + size]
        if len(texels) != size:
            info["note"] = "truncated"
        else:
            info["px"] = texels
        return info
    # The 360 build stores mip 0 tiled and 16-bit-word swapped; a page-aligned
    # header precedes it (0x1000). The tiled extent is the block area rounded up
    # to 32x32-block macros, which is larger than `size` for small textures.
    log_bpp = element.bit_length() - 1
    offsets = _tile_offsets(units_x, units_y, log_bpp)
    tiled_size = ((units_x + 31) & ~31) * ((units_y + 31) & ~31) * element
    available = blob[0x1000:]
    if len(available) >= tiled_size:
        data = available[:tiled_size]
    elif len(available) >= size:
        data = available[:size]
    else:
        info["note"] = "tiled data truncated"
        return info
    highest = int(offsets.max()) if _np is not None else max(offsets)
    if highest + element > len(data):
        info["note"] = "tiled data truncated"
        return info
    if _np is not None:
        blocks_data = _np.frombuffer(data, dtype=_np.uint8).reshape(-1, element)[offsets]
        if unit > 1:
            blocks_data = blocks_data.reshape(-1, element // unit, unit)[:, :, ::-1]
            blocks_data = blocks_data.reshape(-1, element)
        info["px"] = blocks_data.tobytes()
        return info
    out = bytearray(size)
    for y in range(units_y):
        for x in range(units_x):
            source = offsets[y * units_x + x] * element
            chunk = data[source:source + element]
            destination = (y * units_x + x) * element
            for i in range(0, element, unit):
                out[destination + i:destination + i + unit] = chunk[i:i + unit][::-1]
    info["px"] = bytes(out)
    return info



def signature(data, platform):
    """The platform-invariant content of a bank (see the module docstring)."""
    bank = eden_bank.parse(data, platform)
    signature = {"v": SIG_VERSION, "entries": len(bank["entries"]),
                 "kinds": {}, "assets": {}, "tex": {}, "bad": []}
    for entry in bank["entries"]:
        if not entry["filename"]:
            signature["bad"].append(entry["offset"])
            continue
        key = entry["filename"].lower()
        kind = entry["kind"] if entry["kind"] in ("3dd", "3dg", "2dm", "2db") else (
            entry["ext"] or entry["kind"])
        signature["kinds"][kind] = signature["kinds"].get(kind, 0) + 1
        signature["assets"][key] = [kind, entry["size"]]
        if kind == "2db":
            info = texture_mip0(eden_bank.payload(bank, entry), platform)
            if info is None:
                signature["tex"][key] = {"w": 0, "h": 0, "d": 1, "mips": 0, "fmt": 0,
                                         "px": None, "note": "unreadable"}
            else:
                pixels = info["px"]
                signature["tex"][key] = {
                    "w": info["w"], "h": info["h"], "d": info["d"], "mips": info["mips"],
                    "fmt": info["fmt"],
                    "px": hashlib.sha1(pixels).hexdigest()[:16] if pixels is not None else None,
                    "note": info["note"] if pixels is None else "",
                }
    return signature


# --------------------------------------------------------------------------
# The prototypes (phase 1): decompress every named bank and index its content
# --------------------------------------------------------------------------

def load_beta_module():
    """`beta_list.py` - the prototype bigfile index (lazy: it needs the 360
    recomp's tools folder and the prototype installs to be present)."""
    try:
        import beta_list
        return beta_list
    except Exception as error:  # pragma: no cover - environment dependent
        raise SystemExit("cannot import tools/x360/beta_list.py: %s" % error)


def iter_banks(beta_module, beta):
    """(bigfile index, entry, name) for every named `.bnk` of a prototype."""
    for index, entry, name in beta_module.index(beta):
        if name and name.lower().endswith(".bnk"):
            yield index, entry, name


_thread_local = threading.local()


def _scratch_paths(tmpdir):
    paths = getattr(_thread_local, "paths", None)
    if paths is None:
        paths = (os.path.join(tmpdir, "raw_%d.bin" % threading.get_ident()),
                 os.path.join(tmpdir, "out_%d.bin" % threading.get_ident()))
        _thread_local.paths = paths
    return paths


def beta_bank_bytes(beta_module, beta, index, entry, tmpdir):
    """The bank bytes of a prototype entry: LZX-decompressed, or stored as is."""
    big = os.path.join(beta_module.BETAS[beta], "bigfile%d.big" % index)
    raw, out = _scratch_paths(tmpdir)
    size = entry["comp"] if entry["comp"] > 0 else entry["uncomp"]
    with open(big, "rb") as handle:
        handle.seek(entry["offset"])
        payload = handle.read(size)
    with open(raw, "wb") as handle:
        handle.write(payload)
    if os.path.exists(out):
        os.remove(out)
    result = subprocess.run([beta_module.XZXD, raw, out, str(entry["uncomp"])],
                            capture_output=True)
    if (result.returncode == 0 and os.path.exists(out)
            and os.path.getsize(out) == entry["uncomp"]):
        with open(out, "rb") as handle:
            return handle.read()
    # Some entries are stored rather than compressed (the PC .map marks those
    # with comp == 0; a few 360 ones are not flagged): the payload is the bank.
    if payload[0x08:0x0C] in (b"BANK", b"KNAB"):
        return payload
    raise RuntimeError("decompress failed (ret %d): %s" % (
        result.returncode, result.stderr[:160].decode("latin-1", "replace")))


def one_bank(beta_module, beta, index, entry, name, tmpdir):
    data = beta_bank_bytes(beta_module, beta, index, entry, tmpdir)
    return signature(data, eden_bank.X360)


def read_cache(path):
    """{bank name (lower): signature} from a JSONL cache (resume support)."""
    out = {}
    if not os.path.exists(path):
        return out
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except ValueError:
                continue  # a truncated last line from an interrupted run
            if row.get("sig", {}).get("v") == SIG_VERSION:
                out[row["bank"].lower()] = row["sig"]
    return out


def run_signatures(args):
    beta_module = load_beta_module()
    os.makedirs(args.cache, exist_ok=True)
    for beta in [b.strip() for b in args.betas.split(",") if b.strip()]:
        path = os.path.join(args.cache, "%s.jsonl" % beta)
        errors_path = os.path.join(args.cache, "%s.errors.txt" % beta)
        done = read_cache(path)
        todo = [(index, entry, name) for index, entry, name in iter_banks(beta_module, beta)
                if name.lower() not in done
                and (not args.filter or re.search(args.filter, name, re.I))]
        if args.limit:
            todo = todo[:args.limit]
        print("[%s] %d banks: %d indexed, %d to decompress" % (
            beta, len(done) + len(todo), len(done), len(todo)), flush=True)
        if not todo:
            continue
        write_lock = threading.Lock()
        started = time.time()
        count = 0
        with open(path, "a", encoding="utf-8") as cache, \
                open(errors_path, "a", encoding="utf-8") as errors, \
                tempfile.TemporaryDirectory() as tmpdir, \
                ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {}
            for index, entry, name in todo:
                futures[pool.submit(one_bank, beta_module, beta, index, entry, name,
                                    tmpdir)] = name
            for future in as_completed(futures):
                name = futures[future]
                count += 1
                try:
                    row = {"bank": name, "sig": future.result()}
                    line = json.dumps(row, separators=(",", ":"))
                except Exception as error:
                    errors.write("%s\t%s\n" % (name, error))
                    errors.flush()
                    continue
                with write_lock:
                    cache.write(line + "\n")
                    cache.flush()
                if count % 200 == 0 or count == len(todo):
                    rate = count / max(1e-9, time.time() - started)
                    left = (len(todo) - count) / max(1e-9, rate)
                    print("  %d/%d  %.1f banks/s  ~%.0f min left" % (
                        count, len(todo), rate, left / 60.0), flush=True)
        print("[%s] done in %.1f min" % (beta, (time.time() - started) / 60.0), flush=True)


# --------------------------------------------------------------------------
# Retail + phase 2: diff the signatures and write the report
# --------------------------------------------------------------------------

def retail_bank_path(name, retail_root):
    return os.path.join(retail_root, name.replace("/", os.sep))


def pair_renames(beta, retail, details):
    """Pair a prototype-only asset with a retail-only one of the same content.

    The frontend Flash texture ids are renumbered between builds (fla_5289 ->
    fla_5352) and the vehicle interiors were re-exported: a prototype-only
    texture whose decoded pixels equal a retail-only texture's is that same
    texture renamed, not cut content. For the platform-invariant kinds (.3dd /
    .2dm) an identical byte size is the only available signal, and it is labelled
    as such. Audio / xmb / vmf are not content-compared at all (a 360 wav is not
    byte-identical to the PC one even when the sound is the same - measured).
    """
    renamed = []
    available = {}
    for key, (kind, size) in details["removed"]:
        if kind == "2db":
            digest = retail["tex"].get(key, {}).get("px")
            if digest:
                available.setdefault(("pixels", digest), []).append(key)
        elif kind in COMPARED_KINDS:
            available.setdefault(("size", kind, size), []).append(key)
    remaining = []
    for key, (kind, size) in details["added"]:
        candidates = []
        if kind == "2db":
            digest = beta["tex"].get(key, {}).get("px")
            if digest:
                candidates = available.get(("pixels", digest), [])
        elif kind in COMPARED_KINDS:
            candidates = available.get(("size", kind, size), [])
        if candidates:
            renamed.append((key, candidates.pop(0), "same pixels" if kind == "2db"
                            else "same size"))
        else:
            remaining.append((key, (kind, size)))
    consumed = set(match for _, match, _ in renamed)
    details["added"] = remaining
    details["removed"] = [row for row in details["removed"] if row[0] not in consumed]
    details["renamed"] = renamed
    return renamed


def diff(beta, retail):
    """Compare two signatures -> (verdict, details)."""
    beta_assets, retail_assets = beta["assets"], retail["assets"]
    details = collections.OrderedDict(
        added=[(key, beta_assets[key]) for key in sorted(set(beta_assets) - set(retail_assets))],
        removed=[(key, retail_assets[key]) for key in sorted(set(retail_assets) - set(beta_assets))],
        size=[], kind=[], pixels=[], renamed=[], skipped=[], info=[])
    for key in sorted(set(beta_assets) & set(retail_assets)):
        kind_beta, size_beta = beta_assets[key]
        kind_retail, size_retail = retail_assets[key]
        if kind_beta != kind_retail:
            details["kind"].append((key, kind_retail, kind_beta))
            continue
        if size_beta != size_retail:
            if kind_beta in COMPARED_KINDS:
                details["size"].append((key, kind_beta, size_retail, size_beta))
            elif kind_beta not in NOT_COMPARABLE_SIZES:
                details["info"].append((key, kind_beta, size_retail, size_beta))
        texture_beta, texture_retail = beta["tex"].get(key), retail["tex"].get(key)
        if texture_beta is None or texture_retail is None:
            continue
        if texture_beta["px"] is None or texture_retail["px"] is None:
            details["skipped"].append((key, texture_beta.get("note") or texture_retail.get("note")))
        elif texture_beta["px"] != texture_retail["px"]:
            details["pixels"].append((key, texture_retail, texture_beta))
    pair_renames(beta, retail, details)
    if details["added"] or details["removed"]:
        verdict = "CUT"
    elif details["size"] or details["kind"] or details["pixels"]:
        verdict = "CHANGED"
    elif details["renamed"]:
        verdict = "RENAMED"
    elif details["info"] or beta["entries"] != retail["entries"]:
        verdict = "SIZE"
    elif details["skipped"]:
        verdict = "UNCHECKED"
    else:
        verdict = "same"
    return verdict, details


def format_bank(bank, verdict, details):
    lines = ["%s  %s" % (verdict, bank)]
    for key, (kind, size) in details["added"]:
        lines.append("    + %-30s %s %d  (prototype only)" % (key, kind, size))
    for key, (kind, size) in details["removed"]:
        lines.append("    - %-30s %s %d  (retail only)" % (key, kind, size))
    for key, kind, size_retail, size_beta in details["size"]:
        lines.append("    %-32s %s  retail %d -> prototype %d" % (key, kind, size_retail, size_beta))
    for key, kind_retail, kind_beta in details["kind"]:
        lines.append("    %-32s kind %s -> %s" % (key, kind_retail, kind_beta))
    for key, match, why in details["renamed"]:
        lines.append("    ~ %-30s -> %-30s (%s, renamed)" % (key, match, why))
    for key, texture_retail, texture_beta in details["pixels"]:
        lines.append("    %-32s pixels differ  %dx%d mips %s fmt %s -> %dx%d mips %s fmt %s" % (
            key, texture_retail["w"], texture_retail["h"], texture_retail["mips"],
            texture_retail["fmt"], texture_beta["w"], texture_beta["h"],
            texture_beta["mips"], texture_beta["fmt"]))
    for key, kind, size_retail, size_beta in details["info"]:
        lines.append("    %-32s %s  size only (informational): %d -> %d" % (
            key, kind, size_retail, size_beta))
    for key, note in details["skipped"]:
        lines.append("    %-32s not compared: %s" % (key, note))
    return lines


def run_compare(args):
    os.makedirs(args.cache, exist_ok=True)
    for beta in [b.strip() for b in args.betas.split(",") if b.strip()]:
        path = os.path.join(args.cache, "%s.jsonl" % beta)
        cache = read_cache(path)
        if not cache:
            print("[%s] no signatures yet - run --signatures first" % beta)
            continue
        retail_cache_path = os.path.join(args.cache, "retail.jsonl")
        retail_cache = read_cache(retail_cache_path)
        report_path = os.path.join(args.cache, "report_%s.txt" % beta)
        counts = collections.Counter()
        sections = collections.defaultdict(list)
        machine = {}
        missing = []
        started = time.time()
        with open(retail_cache_path, "a", encoding="utf-8") as retail_cache_file:
            for bank, sig in sorted(cache.items()):
                path_retail = retail_bank_path(bank, args.retail)
                if not os.path.exists(path_retail):
                    counts["prototype only"] += 1
                    missing.append(bank)
                    continue
                if bank in retail_cache:
                    retail_sig = retail_cache[bank]
                else:
                    try:
                        with open(path_retail, "rb") as handle:
                            retail_sig = signature(handle.read(), eden_bank.PC)
                    except Exception as error:
                        counts["retail unreadable"] += 1
                        sections["retail unreadable"].append("%s  %s" % (bank, error))
                        continue
                    retail_cache[bank] = retail_sig
                    retail_cache_file.write(json.dumps(
                        {"bank": bank, "sig": retail_sig}, separators=(",", ":")) + "\n")
                verdict, details = diff(sig, retail_sig)
                counts[verdict] += 1
                if verdict != "same":
                    sections[verdict].append(format_bank(bank, verdict, details))
                    machine[bank] = {"verdict": verdict, "details": details}
        total = sum(counts.values())
        with open(report_path, "w", encoding="utf-8") as report:
            report.write("Content scan: %s prototype vs retail PC data (%s)\n" % (beta, args.retail))
            report.write("%d banks indexed, %d compared\n\n" % (len(cache), total))
            for verdict in ("CUT", "CHANGED", "RENAMED", "SIZE", "UNCHECKED", "same",
                            "prototype only", "retail unreadable"):
                if counts[verdict]:
                    report.write("  %-16s %5d\n" % (verdict, counts[verdict]))
            report.write(
                "\nCUT / CHANGED = an asset's presence, a .3dd / .2dm byte size, or a\n"
                "texture's decoded mip 0 pixels differ. RENAMED = the only difference is an\n"
                "asset renamed between builds (proven by identical pixels for .2db, by an\n"
                "identical byte size for .3dd / .2dm). SIZE = only platform-dependent sizes\n"
                "differ (.3dg vertex packing ~0.63x, audio, xmb, vmf). UNCHECKED = a texture\n"
                "in a format this scan does not decode. Audio, xmb and vmf are compared by\n"
                "name only: a 360 wav is not byte-identical to the PC one even for the same\n"
                "sound, so an audio difference here is unverified repacking.\n")
            for verdict in ("CUT", "CHANGED", "RENAMED", "SIZE", "UNCHECKED", "retail unreadable"):
                if not sections[verdict]:
                    continue
                report.write("\n" + "=" * 78 + "\n%s (%d)\n" % (verdict, len(sections[verdict])))
                for lines in sections[verdict]:
                    report.write("\n".join(lines) + "\n")
            if missing:
                report.write("\n" + "=" * 78 + "\nprototype only (%d)\n" % len(missing))
                for bank in missing:
                    report.write("  %s\n" % bank)
        print("[%s] %d banks compared in %.1f min -> %s" % (
            beta, total, (time.time() - started) / 60.0, report_path))
        json_path = os.path.join(args.cache, "diff_%s.json" % beta)
        with open(json_path, "w", encoding="utf-8") as handle:
            json.dump({"beta": beta, "retail": args.retail, "counts": dict(counts),
                       "banks": machine, "prototype only": missing}, handle, indent=1)
        for verdict in ("CUT", "CHANGED", "RENAMED", "SIZE", "UNCHECKED", "same",
                        "prototype only"):
            if counts[verdict]:
                print("   %-16s %5d" % (verdict, counts[verdict]))


def run_restore(args):
    """Write the prototype's textures into the restored-content folder.

    For every texture of the matched banks whose **name and dimensions** exist in
    retail, the prototype's mip 0 replaces retail's, in the layout
    `TextureCache::loadBankTextures` expects: `<userdata>/restored/<bank>/<file>.2db`
    (the retail file as the header template - see `restore_texture.py`). The game
    keeps the retail file; `-norestored` switches it back off.
    """
    from restore_texture import build_pc_2db

    beta_module = load_beta_module()
    pattern = args.restore.lower()
    candidates = collections.defaultdict(list)  # (bank stem, file) -> [(bank, staged, beta)]
    identical = 0
    skipped = 0
    staged_index = 0
    with tempfile.TemporaryDirectory() as tmpdir:
        for beta in [b.strip() for b in args.betas.split(",") if b.strip()]:
            rows = [(i, e, n) for i, e, n in iter_banks(beta_module, beta)
                    if re.search(pattern, n, re.I)]
            for index, entry, name in rows:
                try:
                    data = beta_bank_bytes(beta_module, beta, index, entry, tmpdir)
                    beta_bank = eden_bank.parse(data, eden_bank.X360)
                except Exception as error:
                    print("  %s: %s" % (name, error), flush=True)
                    skipped += 1
                    continue
                path_retail = retail_bank_path(name, args.retail)
                if not os.path.exists(path_retail):
                    print("  %s: no retail bank" % name)
                    continue
                retail_bank = eden_bank.parse_file(path_retail, eden_bank.PC)
                retail_textures = {e["filename"].lower(): e for e in retail_bank["entries"]
                                   if e["ext"] == "2db"}
                stem = os.path.basename(name).rsplit(".", 1)[0].lower()
                out_dir = os.path.join(args.userdata, "restored", stem)
                for e in beta_bank["entries"]:
                    if e["ext"] != "2db":
                        continue
                    key = e["filename"].lower()
                    match = retail_textures.get(key)
                    if match is None:
                        skipped += 1
                        continue
                    info = texture_mip0(eden_bank.payload(beta_bank, e), eden_bank.X360)
                    template = eden_bank.payload(retail_bank, match)
                    if info is None or info["px"] is None:
                        print("  %s %s: not decoded (%s)" % (name, key, info and info["note"]))
                        skipped += 1
                        continue
                    template_w, template_h = struct.unpack_from("<HH", template, 0x28)
                    if (template_w, template_h) != (info["w"], info["h"]):
                        print("  %s %s: %dx%d vs retail %dx%d" % (
                            name, key, info["w"], info["h"], template_w, template_h))
                        skipped += 1
                        continue
                    retail_info = texture_mip0(template, eden_bank.PC)
                    if retail_info is not None and retail_info["px"] == info["px"]:
                        identical += 1
                        continue
                    # Stage it: the game keys the restored folder by the bank's
                    # FILE name only, so two banks with the same stem (hires /
                    # medres / lowres gauges, Ibiza / Hawai area-N-M) collide and
                    # only one of them can be restored.
                    staged_index += 1
                    staged = os.path.join(tmpdir, "restored_%d.2db" % staged_index)
                    with open(staged, "wb") as handle:
                        handle.write(build_pc_2db(template, info["px"]))
                    candidates[(stem, key)].append((name, staged, beta))
        written = collections.OrderedDict()
        ambiguous = []
        prototypes = []
        for (stem, key), rows in sorted(candidates.items()):
            blobs = set()
            for _, staged, _ in rows:
                with open(staged, "rb") as handle:
                    blobs.add(handle.read())
            bank_names = {os.path.basename(bank) for bank, _, _ in rows}
            if len(blobs) > 1 and len(bank_names) > 1:
                # Two *different* banks want the same key with different pixels
                # (hires / medres / lowres gauges, Ibiza / Hawai area-N-M): the
                # game cannot tell them apart, so neither is written.
                ambiguous.append((stem, key, [bank for bank, _, _ in rows]))
                continue
            bank, staged, beta = rows[0]
            if len(blobs) > 1:
                # The same bank differs between the two prototypes: the first
                # prototype in --betas wins, the other is reported.
                prototypes.append((stem, key, beta, [b for _, _, b in rows[1:]]))
            destination = os.path.join(args.userdata, "restored", stem, key)
            if args.dry_run:
                print("  would write %s  [%s]" % (destination, beta), flush=True)
            else:
                os.makedirs(os.path.dirname(destination), exist_ok=True)
                shutil.copyfile(staged, destination)
                print("  wrote %s  [%s]" % (destination, beta))
            written[destination] = beta
    for stem, key, banks in ambiguous:
        print("  ambiguous: %s/%s is claimed by %d banks with different pixels - skipped\n"
              "             %s" % (stem, key, len(banks), ", ".join(banks)))
    for stem, key, kept, others in prototypes:
        print("  prototypes differ: %s/%s - kept %s (%s differs)" % (
            stem, key, kept, ", ".join(others)))
    print("%d textures %s, %d identical to retail (skipped), %d ambiguous keys, %d prototype "
          "disagreements, %d without a retail counterpart or not decodable" % (
              len(written), "listed" if args.dry_run else "restored", identical, len(ambiguous),
              len(prototypes), skipped))


def print_signature(name, sig):
    print("   %d entries: %s" % (sig["entries"], ", ".join(
        "%s %d" % (kind, count) for kind, count in sorted(sig["kinds"].items()))))
    for key, (kind, size) in sorted(sig["assets"].items()):
        texture = sig["tex"].get(key)
        if texture:
            print("     %-32s %-4s %9d  %dx%d fmt %s mips %s px %s %s" % (
                key, kind, size, texture["w"], texture["h"], texture["fmt"],
                texture["mips"], texture["px"], texture["note"]))
        else:
            print("     %-32s %-4s %9d" % (key, kind, size))


def run_bank(args):
    """One bank across the prototypes and retail, verbose."""
    beta_module = load_beta_module()
    pattern = args.bank.lower()
    with tempfile.TemporaryDirectory() as tmpdir:
        for beta in [b.strip() for b in args.betas.split(",") if b.strip()]:
            rows = [(i, e, n) for i, e, n in iter_banks(beta_module, beta)
                    if pattern in n.lower()]
            if not rows:
                print("[%s] no bank matching %r" % (beta, args.bank))
                continue
            for index, entry, name in rows:
                data = beta_bank_bytes(beta_module, beta, index, entry, tmpdir)
                sig = signature(data, eden_bank.X360)
                print("\n[%s] %s  %d bytes, sha1 %s" % (
                    beta, name, len(data), hashlib.sha1(data).hexdigest()[:12]))
                path_retail = retail_bank_path(name, args.retail)
                if not os.path.exists(path_retail):
                    print("   retail: %s not present" % path_retail)
                    print_signature(name, sig)
                    continue
                with open(path_retail, "rb") as handle:
                    retail_sig = signature(handle.read(), eden_bank.PC)
                verdict, details = diff(sig, retail_sig)
                print("\n".join(format_bank(name, verdict, details)))
                print("   prototype:")
                print_signature(name, sig)
                print("   retail:")
                print_signature(name, retail_sig)


def run_selftest(args):
    """The Q7 Inuit evidence, as a regression test (no full scan needed)."""
    checks = []

    def check(label, condition, detail=""):
        checks.append((label, bool(condition), detail))

    beta_module = load_beta_module()
    signatures = {}
    with tempfile.TemporaryDirectory() as tmpdir:
        for beta in ("dec2010", "aug2011"):
            rows = [row for row in iter_banks(beta_module, beta) if "q7_inuit_i" in row[2].lower()]
            check("%s ships q7_inuit_i.bnk" % beta, len(rows) == 1)
            if not rows:
                continue
            data = beta_bank_bytes(beta_module, beta, rows[0][0], rows[0][1], tmpdir)
            signatures[beta] = (data, signature(data, eden_bank.X360))
    if len(signatures) == 2:
        (data_a, _), (data_b, _) = signatures["dec2010"], signatures["aug2011"]
        check("both prototypes' banks are byte identical",
              hashlib.sha1(data_a).hexdigest() == hashlib.sha1(data_b).hexdigest(),
              "dec2010 %s vs aug2011 %s" % (hashlib.sha1(data_a).hexdigest()[:12],
                                            hashlib.sha1(data_b).hexdigest()[:12]))
    beta_sig = signatures["dec2010"][1]
    check("prototype .3dd is 11360", beta_sig["assets"]["q7_inuit_i.3dd"][1] == 11360,
          str(beta_sig["assets"].get("q7_inuit_i.3dd")))
    check("prototype .2dm is 25920", beta_sig["assets"]["q7_inuit_i.2dm"][1] == 25920,
          str(beta_sig["assets"].get("q7_inuit_i.2dm")))
    check("prototype has wood_inuit.2db", "wood_inuit.2db" in beta_sig["assets"])

    path_retail = retail_bank_path("Euro/Bnk/vehicules/interiors/q7_inuit_i.bnk", args.retail)
    check("retail q7_inuit_i.bnk present", os.path.exists(path_retail), path_retail)
    if os.path.exists(path_retail):
        with open(path_retail, "rb") as handle:
            retail_sig = signature(handle.read(), eden_bank.PC)
        check("retail .3dd is 11136", retail_sig["assets"]["q7_inuit_i.3dd"][1] == 11136)
        check("retail .2dm is 29904", retail_sig["assets"]["q7_inuit_i.2dm"][1] == 29904)
        check("retail has no wood_inuit.2db", "wood_inuit.2db" not in retail_sig["assets"])
        verdict, details = diff(beta_sig, retail_sig)
        check("verdict is CUT (wood_inuit is prototype only)", verdict == "CUT", verdict)
        check("wood_inuit.2db is the added asset",
              [key for key, _ in details["added"]] == ["wood_inuit.2db"],
              str(details["added"]))
        check("q7_int2.2db pixels differ",
              [key for key, _, _ in details["pixels"]] == ["q7_int2.2db"],
              str([key for key, _, _ in details["pixels"]]))
        for shared in ("q7_int.2db", "q7_int_ts.2db", "q7_glass_i.2db"):
            check("%s pixels are identical" % shared,
                  beta_sig["tex"][shared]["px"] == retail_sig["tex"][shared]["px"],
                  "%s vs %s" % (beta_sig["tex"][shared]["px"], retail_sig["tex"][shared]["px"]))

    failed = [row for row in checks if not row[1]]
    for label, ok, detail in checks:
        print("%s %s%s" % ("PASS" if ok else "FAIL", label, ("  [%s]" % detail) if detail else ""))
    print("\n%d/%d checks passed" % (len(checks) - len(failed), len(checks)))
    return 1 if failed else 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--signatures", action="store_true",
                        help="phase 1: decompress and index every prototype bank")
    parser.add_argument("--compare", action="store_true",
                        help="phase 2: diff the cached signatures against retail")
    parser.add_argument("--bank", help="one bank (substring), verbose")
    parser.add_argument("--restore", metavar="BANK",
                        help="write the prototype's textures of the matching banks into "
                             "<userdata>/restored (the game's restored-content folder); "
                             "BANK is a regex, as for --filter")
    parser.add_argument("--userdata", help="the game's -userdata folder (--restore)")
    parser.add_argument("--dry-run", action="store_true", help="--restore: list, write nothing")
    parser.add_argument("--selftest", action="store_true", help="the Q7 regression test")
    parser.add_argument("--betas", default="dec2010,aug2011",
                        help="comma separated prototype keys from beta_list.py")
    parser.add_argument("--retail", default=DEFAULT_RETAIL, help="retail PC game data root")
    parser.add_argument("--cache", default=DEFAULT_CACHE, help="scratch dir (gitignored)")
    parser.add_argument("--filter", help="only banks whose name matches this regex")
    parser.add_argument("--limit", type=int, help="only the first N banks per prototype")
    parser.add_argument("--jobs", type=int,
                        default=min(8, max(1, (os.cpu_count() or 4) - 1)),
                        help="parallel decompression workers")
    args = parser.parse_args()
    if args.selftest:
        return run_selftest(args)
    if args.restore:
        if not args.userdata:
            parser.error("--restore needs --userdata <the game's -userdata folder>")
        run_restore(args)
        return 0
    if args.bank:
        run_bank(args)
        return 0
    if args.signatures:
        run_signatures(args)
    if args.compare:
        run_compare(args)
    if not (args.signatures or args.compare):
        parser.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())



