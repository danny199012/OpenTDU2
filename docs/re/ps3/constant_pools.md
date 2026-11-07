# PS3 constant pools — how to find the numeric laws (2026-10-28, session 2)

Continuation of GLM's session (`README.md` here). The goal was to reach the
`[ENC]` numeric laws of `docs/re/vehicle_physics_spec.md` §6.8 — the suspension
force law, the RK4 combination, the ABS / TC activation tests, the engine
predicates — from the PS3 build, which has them in the clear.

This file records the *method* and its results, including two negative results
that save the next session a lot of time.

## Method: find the code by its constants

A law is a formula, and a formula needs its constants. So:

1. scan the ELF for the 4-byte big-endian bit patterns of the constants the PC
   spec already names (`1e-5`, `300.0`, `100.0`, `0.8`, `1/6`, `2/3`, ...);
2. index every *reference* to those addresses in the text segment;
3. the referencing functions are the candidates.

Tools added this session (both in `C:\TDU2-PS3-Recomp\tools\`):

| Tool | What |
|---|---|
| `abs_refs.py` | single pass over the whole text segment, indexing **absolute** (`addis rT,r0,HI` + `addi/ori`/d-form load) and TOC-relative (`addis rT,r2,HA`) address materializations -> `port/out/abs_refs.json` (20,871 addresses, each with its referencing function). `--rebuild` to re-index, `--near LO SPAN` to sweep a window. |
| `find_const.py` | follows **one level of pointer indirection**: finds 4-byte slots holding the target address, then looks each slot up in `abs_refs.json`. |
| `find_floats.py` | step 1 of the method: locates the named float constants in the data segments -> `port/out/float_consts.json`. Extra values can be passed on the command line. |

`abs_refs.py` exists because `xref_addr.py` re-scans the 1.8 GB lifted tree for
every query (it times out) and because GLM's `addr_refs.py` only looks for the
TOC-relative form, which turned out not to be the common one.

### Trap: `.rodata` is inside the FIRST PT_LOAD segment

The first version of `abs_refs.py` filtered candidate targets to the second
segment (`0x10A0000+`) and found only 3,256 addresses, none of them useful. The
constant and string pools (`0xf120f0`, `0xfb0000`, `0xf144d0`, the TOC) all live
in **`.rodata`, which sits inside the first segment** (`0x10000..0x10A0508`,
after the code). Widen the accepted range to `0xE00000..0x1200000` and the index
jumps to **20,871** addresses. Any tool that assumes "data = second segment" will
silently miss every constant this project queries.

## Result 1 — the constant pools, mapped

Read directly out of the file (all values verified by dump):

| Range | What | Notable values |
|---|---|---|
| `0xf120a0–0xf1214c` | shared **scalar float pool** | 2.0, −1.0, 0.15, **1e-5**, 30.0, 6.0, 0.7, 0.9, 0.5, 0.1, **1/6, 1/3, 0.5, 1.0**, 0.01, 0.026, 0.7, 0.93, 0.88, 0.03, 1.5, 1.1, **19000.0**, 0.5, 0.6, 0.9, 0.001, 0.0005, then four × 1e-5, then four × 0.5 |
| `0xf12150–0xf121a0` | 16-byte-aligned `hkVector4` constants | (0,0,0,0), (0,1,0,0), (1,0,0,0), (0.5,0.5,0.5,0.5) |
| `0xf7a1e4–0xf7a1f8` | a uniform **1/6 grid** | 1/6, 1/3, 1/2, 2/3, 5/6, 1.0 — immediately after the `/:null` string at `0xf7a1d8` and −1.0 at `0xf7a1e0` |

`19000.0` in the scalar pool is a strong candidate for the default **anti-roll
bar stiffness**: `docs/STATE.md` logs `arb 16000/16000` from the vehicle log, so
the game ships a value in that range and the ARB law is one of the open handling
items.

The `1/6` grid at `0xf7a1e4` is the best RK4 lead so far — the PC spec §6.1 says
the drive train stage time steps are `[h, h/2, h/2, h]`, and the classic RK4
weights are 1/6, 2/3, 2/3, 1/6. **But it is not yet proven**: the table is a
uniform 1/6 grid (1/3, 1/2, 5/6 are in it too), which is equally consistent with
a generic 6-sample blend. Do not treat it as confirmed.

Two more results from `find_floats.py` (full run reproducible):

* **`-9.81` is present three times** — `0xf3ff84`, `0xf40134`, `0xf795c4` — the
  PS3 counterpart of the PC spec's world gravity (`DAT_00f4b4d0`, spec §6.7). A
  good first cross-build landmark for the link-order hypothesis below.
* **`19000.0` occurs exactly once** in the whole image (`0xf12134`), which is
  what a single shipped default looks like — reinforcing the anti-roll-bar
  reading.
* A second 1/6-grid site exists at `0xf42b40` (5/6) / `0xf42b44` (2/3), so the
  grid is not unique to `0xf7a1e4`; the two sites must be told apart before
  either is called RK4.


## Result 2 — the code that uses the pools is NOT at `0xDxxxx`

This corrects the resume plan. GLM mapped the vehicle neighbourhood at
`0x0Bxxxx–0x0D5xxx` (serializers `func_000B5FD0`, `func_000BEE70`, `func_000CB3CC`,
`func_000CF7A8`, `func_000CFEE4`, `func_000D0544`, `func_000D25E0`, and the update
dispatcher `func_000D4F3C`). Those are real — but they are **serializers**, i.e.
name-emitting reflection code. The functions that actually **read the float
constants** are somewhere else entirely:

```
0xf11af0 <- 0x15cb48        0xf11e60 <- 0x1674b4, 0x168198, 0x16847c,
0xf11b50 <- 0x15cec0                    0x16bbc4, 0x16eb88, 0x16f2f0  (6 functions)
0xf11bb0 <- 0x15e678        0xf12150 <- 0x1664e8
0xf11c70 <- 0x1603a0, 0x1609d8   0xf12190 <- 0x169764, 0x4b4de8, 0x4cac10,
0xf11d50 <- 0x160e44                    0x4e0cf8, 0x5f4000
0xf11de0 <- 0x1616cc        0xf123c0 <- 0x171a38, 0x176284, 0x177108, ...
```

So there is a **second code cluster at `0x15xxxx–0x17xxxx`** that directly
materializes `.rodata` addresses, one site per function — the shape of code that
uses a shared constant table. `0xf11e60` is referenced from **six** different
functions, which is what a module-wide tolerance or epsilon looks like.

`0x169764` alone references eight different `.rodata` addresses
(`0xf12160`, `0xf12170`, `0xf12180`, `0xf12190`, `0xf121a0`, `0xf121b0`,
`0xf12250`, ...) — mostly `hkVector4` constants, so it is likely Havok math, not
Eden vehicle code. **Which of these functions is vehicle code is the next thing
to establish** (see the resume points in `README.md`).

## Result 3 — negative: the scalar pool is reached by base + offset

`0xf11f00–0xf1214f` — the whole scalar pool that holds 1e-5, 1/6, 19000.0 — has
**no** absolute or TOC-relative materialization anywhere in the text segment, and
`find_const.py` finds **no 4-byte pointer slot** holding those addresses either.

So the compiler reaches that sub-range as `base + offset` from a base materialized
once, exactly as the serializers do for the name pool
(`gpr[4] = gpr[4] + 0x2FB8`, i.e. `0xFB0000 + 0x2FB8` = `PedalPosition`). This is
why constant-based lookup works for the 16-byte-aligned `hkVector4` constants and
for the `0xf11xxx` scalars, but **not** for `0xf11f00–0xf1214c`.

Consequence: to name the law behind `1e-5` / `1/6` / `19000.0`, the next session
must find the **base register** (dump the materializations in
`0xE00000–0xF20000` and look for a base whose `+offset` arithmetic lands in the
pool), not the constants.

## Hypothesis to test next: PS3 and PC are the same link order

The PC build's `[ENC]` addresses are all in `0x46xxxx–0x54xxxx`. If the PS3 and
PC builds share a link order, then `PS3_VA ≈ PC_VA − 0x3F0000` (PS3 base `0x10000`
vs PC base `0x400000`), which would put the PC `[ENC]` predicates at
`0x7e760` (`FUN_0046e760`), `0x156910` / `0x156810` / `0x156ec0` / `0x156f00`
(`FUN_00546xxx`), `0x15eaa0` / `0x15eae0` (`FUN_0054eaa0` / `FUN_0054eae0`, the
ABS / TC activation tests) and `0x158370` (`FUN_00548370`, extra gravity) — which
is **inside the `0x15xxxx` cluster Result 2 found**, and `0x15e678` / `0x15cec0`
are already known to reference `.rodata` constants.

That is suggestive but **not proven**, and the cheap test now says it is **not a
constant offset**. Checking `port/out/EBOOT.functions.json` (19,842 function
starts) against the predicted addresses:

```
FUN_0046e760 -> 0x7e760    no exact start, nearest 0x7e530  (+0x230)
FUN_00548370 -> 0x158370   no exact start, nearest 0x155c20 (+0x2750)
FUN_00546810 -> 0x156810   no exact start, nearest 0x155c20 (+0xbf0)
FUN_00546910 -> 0x156910   no exact start, nearest 0x155c20 (+0xcf0)
FUN_00546ec0 -> 0x156ec0   no exact start, nearest 0x155c20 (+0x12a0)
FUN_00546f00 -> 0x156f00   no exact start, nearest 0x155c20 (+0x12e0)
FUN_0054eaa0 -> 0x15eaa0   no exact start, nearest 0x15e678 (+0x428)
FUN_0054eae0 -> 0x15eae0   no exact start, nearest 0x15e678 (+0x468)
```

**No exact function start, and the deltas are wildly inconsistent** (+0x230 to
+0x2750), so there is no single offset and the link-order shortcut is dead. It is
not worthless though: the four `FUN_00546xxx` engine / gearbox predicates all land
inside **one** PS3 function starting at `0x155c20`, and both aids tests
(`FUN_0054eaa0` / `FUN_0054eae0`) land inside **one** PS3 function starting at
`0x15e678` — and `0x15e678` is already known from Result 2 to reference three
`.rodata` constants (`0xf11bb0`, `0xf11be0`, `0xf11bf0`). So the *neighbourhood*
correspondence is real even though the address arithmetic is not.

That makes `0x155c20` (engine / gearbox controller) and `0x15e678` (brake aids:
ABS + TC) the **two best candidates to read next** — both are single functions
that subsume several PC `[ENC]` predicates. Verify them by shape (the PC spec
describes each predicate's behaviour in `vehicle_physics_spec.md` §6.3–§6.6)
rather than by address.


## Reproduce

```bat
cd C:\TDU2-PS3-Recomp
python tools/abs_refs.py --rebuild          :: 20,871 addresses -> port/out/abs_refs.json
python tools/abs_refs.py --near 0xf12000 0x400
python tools/abs_refs.py 0xf12190 0xf11e60
python tools/find_const.py 0xf120f0 --slots
python tools/get_func.py 0x15cec0 --out _g_15cec0.cpp
```


