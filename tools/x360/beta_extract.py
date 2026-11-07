import os, re, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import beta_list as bl

XZXD = bl.XZXD
OUT = r"C:\TDU2-Proto"
CUSTOM = len(sys.argv) > 1
pat = re.compile(sys.argv[1] if CUSTOM else r"scenarics/cinematics/.*__us\.bnk$|sound/voices/us/", re.I)

def extract(beta, i, e, rel):
    dst = os.path.join(OUT, beta, rel.replace("/", os.sep))
    if os.path.exists(dst) and os.path.getsize(dst) == e["uncomp"]:
        return True
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    big = os.path.join(bl.BETAS[beta], f"bigfile{i}.big")
    size = e["comp"] if e["comp"] > 0 else e["uncomp"]
    with open(big, "rb") as fh:
        fh.seek(e["offset"])
        data = fh.read(size)
    if e["comp"] <= 0:
        open(dst, "wb").write(data)
        return True
    tmp = dst + ".z"
    open(tmp, "wb").write(data)
    r = subprocess.run([XZXD, tmp, dst, str(e["uncomp"])], capture_output=True)
    os.remove(tmp)
    ok = r.returncode == 0 and os.path.exists(dst) and os.path.getsize(dst) == e["uncomp"]
    if not ok:
        print("  FAILED", rel, r.returncode, r.stderr[:200])
    return ok

for beta in bl.BETAS:
    rows = bl.index(beta)
    done = failed = 0
    for i, e, name in rows:
        if name and pat.search(name):
            rel = name
        elif not CUSTOM and not name and i == 0 and 200_000 <= e["uncomp"] <= 2_300_000:
            rel = f"_unnamed/big0_{e['hash']:016X}_{e['uncomp']}.bin"
        else:
            continue
        if extract(beta, i, e, rel):
            done += 1
        else:
            failed += 1
    print(f"{beta}: {done} extracted, {failed} failed")
