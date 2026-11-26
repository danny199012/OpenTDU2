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

## Body skin UV layout [DATA, seen in docs/images/tattoo_skin_composite_test.webp]
Torso front across the top centre (u 0.17-0.72, v 0.05-0.55), the two arms right of it and along the top left, the legs
and back in the lower half. Regions for the shop are still to be mapped by placing decals and looking.

## Not done / next
- The face (TD_Skinface, `w_head_c`) is not composited; the body only.
- **3D look verified** (`docs/images/tattoo_3d_player_arms.webp`): `-gm FIRST_WM -skipcine -nopad -fixedtimestep -tattoo "cross,0.45,0.3,0.7,0,1,ff0000;cross,0.5,0.75,0.7,0,1,ff0000;cross,0.85,0.35,0.5,0,1,ff0000"`, shot at frame 150: the player's forearms and hands are red, the chest decal is hidden under the T-shirt. Skin UV: forearms/hands lie around u 0.45-0.9, v 0.3-0.8 (cover blobs, exact regions still to map).
- Clothes hide body tattoos through the body-zone mesh hiding already in `EdenAvatar::load`.
- Then: region map (chest/back/arms/legs/neck), the tattoo shop page, a decal editor, face tattoos.
