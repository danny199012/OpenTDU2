# The Apr 2012 dev prototype as a physics reading source

`E:\Documents\TDU2+Dev+Build\TDU2 Dev Build\TestDrive2DEV.exe` (Hidden Palace, "Apr 6, 2012 prototype";
used by Eden for physics / damage testing). Tags: `[CODE-dev]` read from this exe, `[CODE]` retail exe,
`[INF]` inferred. Tool: `tools/devbuild_disasm.py`.

## 1. What it is [CODE-dev]
- PE timestamp 2012-04-08 (retail: 2012-07-10). 19.8 MB, `.text` 16.6 MB, entropy 5.89: **not encrypted, no
  SecuROM sections** (retail `.text` 7.24 + `Stext` 8.00). The whole PC engine is readable, including the
  `0x401000-0x6D1000` band that is `[ENC]` in retail. The SecuROM policy is untouched: nothing is decrypted.
- 278 source paths `...\Branches\MC_PC\Dev\Sources\*` (carphysics, havokvehicle, vehicletdu, displayvehicledamage,
  mngcartireskid, gsphysics, aidebugmenu, gsdebug...), 709 RTTI classes (Havok), build string `PC OI v2.4.2 Jun 15 2011`.
- Ships its own ~15 GB bigfile set; launch: `TestDrive2DEV.exe -mode offline`. Layout differs from retail, so
  **no address carries over**: match by string, constant or vtable slot index.
- Dev-only content: debug menu strings ("Full Damage ON/OFF", `CheatTeleportForward`), `AiDebugMenu.cpp`.
  `"Lateral grip cheat"` (0x1497e64, loader 0x552420) is **AI opponent tuning** (Tuning level / aggressiveness /
  Max turning speed factor / Lateral grip cheat / Rear pressure ...), *not* a player tyre knob.

## 2. Tyre: same law as retail [CODE-dev, partly read]
`0x100d010` (longitudinal, `B = 99999` guard at `0x14c68cc`), fast atan with 0.28 (`0x14d45d0`, users `0xfe7820`),
`D = MaxFriction[surface] (esi+0x90+4*surf) x Fz x ...`, `E` quadratic in Fz (coefficients +0x54/+0x58/+0x5c).
Same family as retail `FUN_00868dc0` (spec 4.2). **Conclusion: Pacejka `D` is linear in Fz in TDU2 as well, so
"the missing load sensitivity of D" (CLAUDE.md 5.4) is not in the tyre evaluator.** The lever is elsewhere
(suspension, contact, weight transfer, the unread `0x100cbc0`/`0x100d730` siblings).

## 3. Suspension: the `[ENC]` suspension vfunc 0x24 is a rest-length solve [CODE-dev]
Spec 6.8 lists "suspension force law (suspension vfunc 0x24 = 0x54bc70 / 0x54bac0)" as encrypted. In the dev
build the vtable `0x155a630` (the class with the stabilizer-bar fields, serializers `0x1011be0` / `0x1011ce0`)
has **slot 9 (+0x24) = `0x1012930`**, and the base `0x1559a20` slot 9 = `0x1001dd0`. Both compute
`EmptyLength (+0x84)` from `ForceAtRest +0x70`, `StiffnessBias +0x74`, `StiffnessAtRest +0x78` (k > 0 else 0):
- `0x1001dd0`: `|b| > 1e-5`: `x = (sqrt(k^2 + 4 k |b| F0) - k) / (2 k |b|)`, else `x = F0 / k`. Solves `k x (1 + b x) = F0`.
- `0x1012930` (car class): `m = k |b| / UpperStop (+0xcc)`; `x = (k - sqrt(k^2 + 4 m F0)) / (2 m)`, else `x = -F0 / k`
  (negative compression axis). I.e. the bias is **normalised by UpperStop** here.
- So 0x54bc70 / 0x54bac0 are most likely these two (the *rest length*, not the per-frame force) `[INF]`.
- `0x1002680` / `0x1002710`: force at `LowerStop` / `UpperStop` = `k x (1 + b x)` with `x = stop - EmptyLength`
  (stiffness factor clamped at 0), used by nothing readable (no callers / vtable refs).

## 4. Suspension: per-frame force `0x10122f0` [CODE-dev]
(called through wrappers `0x10001c0`, `0x1002050`; which class the cars instantiate is **not yet established**.)
- `x = Offset (+0x88) - EmptyLength (+0x84)`; `stiff = max(k (1 + bias x), 0)`; `spring = stiff x`. **No UpperStop
  normalisation here** (so it matches the port's `k x (1 + b x)`; it disagrees with the rest solve above - open).
- Damper `0x1012840`: `F = -c clamp(v, -V, V)`, `c = DampingArray[1]` (+0x64) if `v > 0` else `[0]` (+0x60),
  `v = +0x7c`, `V = +0x80`. **The damping velocity saturates at V.** `+0x80` is a transient written by the
  length update `0x1012130` (`0x7c` / `0x80` / `0x6c`); in the "no clamp" branch `0x80 = 0x6c = [0x1481d44]`.
  If bit 1 of `+0x90` is set and the owner state `[+0x5dc] != 3`, a different damper path (`0x1012800`, x 0.25) runs.
- Stabilizer bar (when the opposite wheel `+0xe0` exists): `+0xc0 = Offset_this - Offset_other`,
  `+0xbc = v_this - v_other`, `arb = (K (+0xb8) d0xc0 - C (+0xb4) d0xbc) x w` stored at `+0xb0`, added to the force;
  `w` = wheel vfunc `+0x40` (`0x1011460`), also scales the **total** force. Force vector = axis (+0xa0..+0xac) x total.
- `+0x6c MaxDamperForce` is **computed** each frame by `0x1012130` as `|m_body x (axis . ...)|`, not a static value.

## 5. Differences from `source/physics/vehicle_dynamics.cpp` (not changed yet)
| Item | Port | Dev build | Size of effect |
|---|---|---|---|
| spring | `k x (1 + b x)` (line 1288) | same | none - confirmed |
| damper | `c v` unclamped | `c clamp(v, -V, V)` | unknown until `V` (+0x80) is decoded |
| stabilizer bar | `(dx K + dv C)` on both wheels, only if both contact | `(K dx - C dv) x w`, `w` = wheel vfunc 0x40 | sign convention of `v` unverified |
| rest compression | linear `F0/k` (line 311) | solves the bias (4 above) | small: bias x 0.01 per metre |
| `MaxDamperForce` | not modelled | per-frame momentum-based limit | candidate for the rollover / kerb launch |

## 6. Next (in order)
1. Decode `0x1012130` fully: the origin of `V (+0x80)` and `MaxDamperForce (+0x6c)`.
2. Identify `w` (wheel vfunc `+0x40` of the object at `this+0x10`) - the contact / lift factor on the whole force.
3. Establish which joint class the cars use (callers `0x10001c0`, `0x1002050`) to settle the normalised vs
   unnormalised rest solve.
4. Then prototype `V` / `MaxDamperForce` behind a flag, A/B with `-vehtest` / `-logcorner`.
