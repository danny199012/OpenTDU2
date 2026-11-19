#!/usr/bin/env python3
"""Hong Kong building footprints / region polygons from HONGKONG.PLM (docs/re/tdusc_pws_format.md 2m).

    python plm_footprints.py --pkg-dir "<TDUSC>/WIN32/PKG" --toc-dir C:/temp/tdusc-scan --out C:/temp/hk_footprints.json

Reads the island PLM from the install (read-only), resolves every polygon record's point ids against the global
point table and writes JSON: [{"name", "kind", "pts": [[x, y, z], ...]}] in Hong Kong world metres (x east, y north).
kind = manual (ManualBuildingPoly), exclusion, sidewalk, other.  [DATA, session 62]
"""
import argparse, json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tdusc_read import Tdusc  # noqa: E402

PLM = 'WIN32/LEVELS/HONGKONG/HONGKONG.PLM'.replace('/', chr(92))
# The point table is packed float3 (x, y, z); point id i is entry i. Found as the contiguous run that ends at 0x4f9e2d
# (56,850 plausible points from 0x453555) extended back by 167 entries, then shifted 18 entries: base = 0x452d81 + 18 * 12.
TABLE_START = 0x452d81 + 18 * 12
TABLE_END = 0x4f9e2d


def parse(d):
    n_pts = (TABLE_END - TABLE_START) // 12
    pts = struct.unpack_from('<%df' % (n_pts * 3), d, TABLE_START)
    out = []
    for m in re.finditer(rb'[ -~]{3,120}', d[TABLE_END - 40:]):
        s = TABLE_END - 40 + m.start()
        name = m.group()
        for k in range(min(len(name), 20)):
            L = len(name) - k
            if struct.unpack_from('<I', d, s + k - 4)[0] != L:
                continue
            p = s + k + L
            n = struct.unpack_from('<I', d, p)[0]
            if 1 <= n < 5000 and p + 4 + 4 * n <= len(d):
                ids = struct.unpack_from('<%dI' % n, d, p + 4)
                if max(ids) < n_pts:
                    nm = name[k:].decode()
                    kind = ('sidewalk' if nm.endswith('sidewalk') else 'exclusion' if nm.endswith('exclusion')
                            else 'manual' if nm.endswith('ManualBuildingPoly') else 'other')
                    out.append({'name': nm, 'kind': kind,
                                'pts': [[pts[3 * i], pts[3 * i + 1], pts[3 * i + 2]] for i in ids]})
            break
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg-dir', required=True)
    ap.add_argument('--toc-dir', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    d = Tdusc(a.pkg_dir, a.toc_dir).read(PLM)
    recs = parse(d)
    with open(a.out, 'w') as f:
        json.dump(recs, f)
    print(len(recs), 'records;', sum(r['kind'] == 'manual' for r in recs), 'manual building polygons')


if __name__ == '__main__':
    main()
