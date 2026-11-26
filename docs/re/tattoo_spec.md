# Tattoo compositor (new feature, not in the original)

Status: **Partial** - verified on the texture and on the 3D player body (2026-11-26); face, region map and shop are not done.

## Why it is possible
- [CODE] The retail game composites the skin at runtime: a `SKIN` material slot is redirected to a per-avatar render
  target (FUN_00cbe42f, FUN_00cb0d00; avatar_spec.md 6.3). The avatar profile has a tattoo input the intro leaves empty.
  The retail compositing pass is not decoded, so this is **our own** pass [INF].
- [DATA] The body skin is `m_body_c` / `w_body_c` (1024x1024 DXT, `common_bodies.bnk`), slot 0 of the TD_Skin2 body
  materials (shader set `d5013ed0dbcdcdcd`).

## What it does
- `source/game/avatar/tattoo.{h,cpp}`: `TattooDecal { Design, U, V, Scale, Rotation, Opacity, Color }`, an alpha mask
  placed in skin UV space and blended over a CPU copy of the base skin. Built-in designs: `ring star cross bars tribal`.
  Any `<userdata>/tattoos/<name>.png` is a design too (its alpha, or 1 - luma for an opaque file).
- `EdenAvatar::applyTattoos` (player config only): decodes the slot texture (`DecodeBlockCompressed`), composites,
  uploads with `TextureCache::createFromRGBA`, swaps the body materials' slot texture. One composite is shared by the
  body's materials and freed in `unload()`.
- Profile: `PlayerProfile::Tattoos`, saved as `tattoos = design,u,v,scale,rot,opacity,rrggbb;...` in the profile file.
- Test flags: `-tattoo "<same string>"` overrides the profile for a run; `-dumptattoo <png>` writes the composited skin.

## Aim-to-place (2026-11-26)

The shop places a tattoo by shooting a ray at the body and taking the hit triangle's UV, not from a fixed region list.
- `EdenAvatar::pickSkin( rayOrigin, rayDirection, world, skinMatrices, outU, outV )`: Moller-Trumbore over the visible
  TD_Skin2 triangles of `man_body` / `woman_body` (`.3dg`, 2011 / 2329 vertices), CPU skinning with each vertex's first bone
  (rigid; `skinMatrices` empty = the bind pose). `EdenAvatar::setTattoos( decals )` recomposites live (the swapped slot's hash
  carries a revision so the draw-set cache sees it; the old composite is freed through the device's deferred delete).
- Test: `-tattoopick "ox,oy,oz,dx,dy,dz;..."` (model space, bind pose; each hit gets a star, `-logpick` lists every candidate hit).
  **Verified in 3D** (nude player, `-nudeavatar`): rays from +z at (0.05,1.22), (0.05,1.03), (0.1,0.7), (0.1,0.35) land on the
  chest, the abdomen, the thighs and the shins. Picked UVs: chest (0.502, 0.337), abdomen (0.508, 0.494), thigh (0.248, 0.671),
  shin (0.535, 0.597).
- **The man faces +z in model space** (rays from -z hit his back), his skin texture row = the mesh V (**no flip**).
- Not wired to a camera / aim control yet: the shop UI has to supply the ray (camera through the cursor, in the avatar's space).

## Body skin UV map notes

Method: `-dumpbodyuv <csv>` writes `u v x y z bone material` of every vertex of the **man's** body mesh (+ `<csv>.tri`;
`-dumpbodywoman` for hers), `tools/tattoo_uv_probe.py` rasterises it back into UV space. `-nudeavatar` draws the avatar without
clothes. **Corrections to the first version of this section**: the first dump was the *woman's* mesh (the garage loads her first),
and a V flip was assumed; the man's mesh uses the raw V. The anchors below were measured in 3D on the man, so they stand; the
"abdomen (0.423, 0.062)" and other failures were the wrong mesh, not an atlas property [DATA].

| Anchor (u, v) | Where it lands (3D, man) |
|---|---|
| 0.50, 0.34 | chest front, on the sternum / pectoral line |
| 0.49, 0.16-0.175 | upper sternum / collarbone notch |
| 0.50, 0.42-0.49 | upper abdomen / navel line |
| 0.94, 0.10 | forearm, inner side near the elbow - **both arms** |
| 0.58, 0.69 | shin, front - **both legs** |
| 0.25, 0.67 | thigh front - **both legs** |
| 0.07, 0.57 | groin (avoid) |

Left and right limbs share UV space, so a limb tattoo appears on **both** sides; a one-sided tattoo needs a per-side skin
copy (not built). The torso front and back are separate islands (front u 0.36-0.64, v 0.17-0.46).

## Not done / next
- Back view of the anchors (needs a rotating camera in the headless run): `-tattoopick` rays from -z hit the back and are untested by eye.
- Wire `pickSkin` to the shop's camera / aim control (ray through the cursor into the avatar's space), with the live skin matrices.
- The face (TD_Skinface, `w_head_c`) is not composited; the body only.
- **3D look verified** (`docs/images/tattoo_3d_player_arms.webp`): `-gm FIRST_WM -skipcine -nopad -fixedtimestep -tattoo "cross,0.45,0.3,0.7,0,1,ff0000;cross,0.5,0.75,0.7,0,1,ff0000;cross,0.85,0.35,0.5,0,1,ff0000"`, shot at frame 150: the player's forearms and hands are red, the chest decal is hidden under the T-shirt. Skin UV: forearms/hands lie around u 0.45-0.9, v 0.3-0.8 (cover blobs, exact regions still to map).
- Clothes hide body tattoos through the body-zone mesh hiding already in `EdenAvatar::load`.
- Then: region map (chest/back/arms/legs/neck), the tattoo shop page, a decal editor, face tattoos.
