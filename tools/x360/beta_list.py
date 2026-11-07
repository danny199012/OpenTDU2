import os, re, sys, collections
TOOLS = r"E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools"
XZXD = os.path.join(TOOLS, "xzxd.exe")
sys.path.insert(0, TOOLS)
import tdu2_bigfile as bf

BETAS = {
    "dec2010": r"C:\Users\cuz_c\Downloads\Test Drive Unlimited 2 (Dec 3, 2010 prototype)\2010-12-03_TDU2_X360_QAv.046",
    "aug2011": r"C:\Users\cuz_c\Downloads\Test_Drive_Unlimited_2_(Aug_8,_2011_prototype)",
}
names = {}
for line in open(os.path.join(BETAS["aug2011"], "FilelistFull.log"), encoding="latin-1"):
    m = re.match(r"\s*([0-9A-Fa-f]{16})\s+\"([^\"]+)\"", line)
    if m:
        names[int(m.group(1), 16)] = m.group(2)
extra = bf.load_names(os.path.join(TOOLS, "FileNames.list"))

def index(beta):
    out = []
    for i in range(3):
        mp = os.path.join(BETAS[beta], f"bigfile{i}.map")
        header, entries = bf.parse_map(mp)
        for e in entries:
            out.append((i, e, names.get(e["hash"]) or extra.get(e["hash"])))
    return out

if __name__ == "__main__":
    pat = re.compile(sys.argv[1] if len(sys.argv) > 1 else r"database|cinematics|voices/us", re.I)
    for beta in BETAS:
        rows = index(beta)
        named = sum(1 for r in rows if r[2])
        sel = [r for r in rows if r[2] and pat.search(r[2])]
        comp = sum(1 for r in sel if r[1]["comp"] > 0)
        print(f"== {beta}: {len(rows)} entries, {named} named; {len(sel)} match, {comp} compressed, "
              f"{sum(r[1]['uncomp'] for r in sel) / 1e6:.0f} MB uncompressed")
        for r in sel[:8]:
            print(f"   big{r[0]} {r[1]['comp']:>10} {r[1]['uncomp']:>10} {r[2]}")
