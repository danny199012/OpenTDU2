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

## Body skin UV map (region mapping, 2026-11-26)

Method: `-dumpbodyuv <csv>` writes `u v x y z bone material` of every `man_body` vertex (+ `<csv>.tri` triangles);
`tools/tattoo_uv_probe.py` rasterises them back into UV space (mesh V is bottom-up: texture row = 1 - V) so each skin pixel knows
its 3D position and facing. `-nudeavatar` draws the avatar without clothes so the decals can be seen on the skin.
**Confirmed in 3D** (nude player, front view, coloured star decals) [DATA]:

| Anchor (u, v) | Where it lands |
|---|---|
| 0.50, 0.34 (also 0.479, 0.298) | chest front, on the sternum / pectoral line |
| 0.49, 0.16-0.175 | upper sternum / collarbone notch |
| 0.50, 0.42 | upper abdomen, under the chest |
| 0.94, 0.10 | forearm, inner side near the elbow - **both arms** |
| 0.58, 0.69 | shin, front - **both legs** |
| 0.45, 0.30 + 0.70 scale | forearms and hands (the first test, red crosses) |

**Wrong guesses, ruled out by the same test**: (0.073, 0.57) is the *groin*, not the hand island; (0.423, 0.062) is not the abdomen
(it showed at the throat); (0.30, 0.43) and (0.28, 0.24) did not show on the front of the thigh / calf.

**What this means** [DATA]:
- The atlas is **packed and overlapped**: several body parts share UV space (the left and right limbs use the same pixels, and
  some small parts sit under others), so a position-based region table from the mesh is ambiguous in places and a decal on a limb
  appears on **both** sides. A tattoo "region" list written from UV guesses will keep landing on the wrong part.
- The torso front is one island (u 0.36-0.64, v 0.17-0.46; shoulders at its top corners), the torso back another (u 0.67-1.0,
  v 0.12-0.44). Back / glutes / thigh-back / calf-back anchors from the probe (`_regions4.json` output of the script) are **not**
  verified in 3D (the headless garage view only shows the front).

**Decision for the shop**: place the tattoo by **aiming at the body** (raycast the skinned mesh, take the hit triangle's UV) instead of a
fixed region list. That is immune to the overlap, and "left arm only" can be done by taking the hit and also rejecting the mirrored
side - the shared UV means one-sided limb tattoos need a per-side skin copy (not done).

## Not done / next
- Back view of the anchors (needs a rotating camera in the headless run).
- The face (TD_Skinface, `w_head_c`) is not composited; the body only.
- **3D look verified** (`docs/images/tattoo_3d_player_arms.webp`): `-gm FIRST_WM -skipcine -nopad -fixedtimestep -tattoo "cross,0.45,0.3,0.7,0,1,ff0000;cross,0.5,0.75,0.7,0,1,ff0000;cross,0.85,0.35,0.5,0,1,ff0000"`, shot at frame 150: the player's forearms and hands are red, the chest decal is hidden under the T-shirt. Skin UV: forearms/hands lie around u 0.45-0.9, v 0.3-0.8 (cover blobs, exact regions still to map).
- Clothes hide body tattoos through the body-zone mesh hiding already in `EdenAvatar::load`.
- Then: region map (chest/back/arms/legs/neck), the tattoo shop page, a decal editor, face tattoos.
