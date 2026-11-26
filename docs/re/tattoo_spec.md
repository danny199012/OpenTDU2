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

## The tattoo studio (2026-11-26)

`source/game/freeride/tattoo_studio.{h,cpp}` (`TattooStudio`, a Renderer2D panel like `SurgeryMenu`) plus the body view in
`IntroCinematic` (`setTattooView`, `aimTattooView`, `pickTattooSpot`, `setPlayerTattoos`).
- **Camera**: orbits the whole player body (default 2.4 m, `-tattoodist <m>`), FOV 38, looking at the body's centre; the
  **crosshair is the view's centre**, so the pick ray is the camera's forward axis (no screen-to-ray maths). The pivot is the
  *skinned body's* bounds (`EdenAvatar::getSkinnedBounds`), not `player.World`: the walk-in animation moves the skeleton's root, and
  the facing follows the hands (`Hand_L` / `Hand_R`). The avatar's draw culling (a sphere on `player.World`) is skipped in this view.
- **Controls** (pad / keyboard): left / right turn the view round the avatar, up / down move the aim up and down the body,
  A / Enter places the tattoo under the crosshair, X (`opt1`) next design, Y (`opt2`) next ink, B / Esc removes the last tattoo
  placed (leaves when none), Start / Pause pays and leaves. A ghost of the next tattoo (55 % opacity) follows the crosshair live
  (the skin is recomposited only when the aim moved more than 0.004).
- **Price**: `TattooStudio::kTattooPrice` $1500 per new tattoo [INF: the retail game has no tattoos]; paid from `PlayerProfile::Money`,
  saved as `PlayerProfile::Tattoos`, every player avatar load then wears them. Designs: the 5 built-ins plus `<userdata>/tattoos/*.png`.
- **Test hook** (the clinic / shop entrance is not wired yet): `-gm FIRST_WM -skipcine -tattooshop <frame>` opens it over the garage
  scene (the player walks in; `-nudeavatar` shows the skin). Verified headless:
  `-nopad -fixedtimestep -nudeavatar -money 20000 -tattooshop 120 -tattoodist 3.2 -autoplay "135:press=validate;138:press=opt1;140:press=down;141:press=down;142:press=down;143:press=validate;...;165:press=pause;185:shot=..."`
  -> log `placed 'star' at u 0.421 v 0.374`, `placed 'ring' at u 0.516 v 0.520`, `bought 2 tattoo(s) for $3000`; the studio closes,
  the camera goes back and the star / ring stay on the avatar (screenshots not committed: nude render).

## Clinic entrance (2026-11-26) [INF]

Inside a clinic (ESUR_A / ESUR_CA) the tattoo studio opens with **X / Option 1**: while walking (no SHOPSAVATAR page up, a prompt "X: tattoo studio" shows), or from the native `SurgeryMenu` (panel line "X: tattoos"). `GMFreeRide::openTattooStudio()` binds the `TattooStudio` host to `interior.getScene()`. `-interior ESUR_A -nudeavatar -tattoodist 3.2 -autoplay "400:press=opt1;..."` verified headless: placed 'star' on the abdomen, shown on the avatar.
Two traps found here: (1) `IntroCinematic::player.World` already includes `sceneOrigin` (the emote/mirror paths build it as `sceneOrigin * ...`), so the studio camera is in world space - applying `sceneOrigin`/`sceneRotation` again put the clinic camera in the sky (the garage hid this: its origin is identity); (2) in the clinic the player is the **mirror-only** double (`bPlayerMirrorOnly`), so the draw condition also passes while `bTattooView`.

## Size, rotation, removal (2026-11-26) [INF]

Studio controls added: size LB / RB (keys V / C) or right stick up / down (half size 0.02-0.12 of the skin width, 100% = 0.045); rotation right stick left / right or keys Q / E (degrees, stored in the decal); remove the tattoo nearest the crosshair skin point (worn or new, free) with right stick click or Delete (reach max(0.04, 2 x scale) in UV). Back undoes the last tattoo placed this visit; Start pays for the new ones and saves removals (nothing to pay for a removal-only visit). Verified headless (`-tattooshop 120 -autoplay "...press=carcamera x5;key=E x3;..."`): size 156% / 45 deg shown on the second star; `removed 'star' ... (new)` refunds nothing and saves only the other; `removed ... (worn)` then `bought 0 tattoo(s), removed 1, for $0` empties the profile line. The crosshair UV drifts between runs (the walk-in animation), so scripted removal tests aim by the log's `placed`/`nothing to remove` u,v.

## Face tattoos (2026-11-26) [INF]

Decals carry `Head` (profile line: optional 8th field `,1`): the head part's skin texture (`m_head_c` 512x512, TD_Skinface set `0xd5013ed0dbcddbca`, `headParts[0]`) is composited separately from the body's (`applyTattoos` runs one pass per part). The pick mesh now holds the body and the head 3dg (`TriHead` per triangle, `pickSkin(..., bool *outHead)`), the studio's Select / M (`IA_Map`) toggles the face close-up (`IntroCinematic::setTattooFace`: distance 0.85 m, aim 1.35-1.95 m). Verified headless: stars placed on the forehead, saved as `...,1`, and still on the head in the whole-body view; the head UV is used raw like the body's (no flip), the face texture is not shared left / right (u 0.26 to 0.74 across the face).
**Bug found on the way (also fixed for the body)**: the pick mesh must use the **un-mirrored** X (`-v.Position[0]`; `Render3DG::parse` mirrors X while the skeleton's skin matrices use the original side), otherwise the pick ray lands a few cm to one side of the crosshair (visible on the head at 0.85 m, ~10 px on the body at 3 m). Rigid pick on the first bone is accurate enough for the head too (all head vertices follow the Head bone; the facial bones add nothing at rest). Debug flags: `-logpickbones`.

## Per-side limbs (2026-11-26) [INF]

The body atlas overlaps the left and right limbs (`-tattoopick` at x = +/-0.12 gives the same u v 0.4072 0.656 with side 2 / 1 [DATA]), so one composited texture put a tattoo on both arms/legs. Fix without touching the mesh: `EdenModel::splitMaterialTriangles` (kept item indices, `setKeepItemIndices`) moves the triangles whose parsed-X centroid is < 0 to a new item drawn with a **copy of the skin material**; `EdenAvatar::applyTattoos` composites each copy with its own decals. Only the player's avatar is split (`bPlayerFace`; `-notattooside` turns it off), the other avatars keep one draw.
- `TattooDecal::Side` 0 = both, 1 = original material (un-mirrored X <= 0), 2 = the copy. Saved in the trailing number `Head | Side << 1` (`,2` = side 1, `,4` = side 2; older saves parse as before).
- `pickSkin` reports the side of the hit triangle (bind-pose centroid X); within 0.10 m of the midline it reports 0, so a chest / belly / back tattoo is on both textures and has no seam. The side is a property of the hit, not of the body part: no classification of limb vs torso is needed because only the shared-UV regions differ between the two textures.
- Verified: the same decal at the leg UV as side 0 / 1 / 2 gives 101 / 2 / 101 red pixels on the visible leg; the studio logs `placed ... side 1` and saves `...,2`. Not verified: arms by eye, the woman, a decal that straddles the midline at 0.10-0.15 m (it is cut at x = 0 of the body).

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

| 0.00-0.16, 0.26-0.28 | **back**, from the spine (u 0.0) outward over one shoulder blade; the two halves mirror (u = |x| in metres, x = +/-0.1 both give u 0.099) [DATA] |
| 0.15, 0.23 / 0.17, 0.28 | upper arm, outer side (one per side with per-side decals; both arms share it) |

The back mirrors about u = 0: a **side 0** decal at u = 0.10 appears on both shoulder blades, one at u = 0 is cut by the texture edge and the two halves join into a symmetric shape on the spine (checked by eye: red stars at u 0.10 v 0.30 on both blades, a blue star at u 0 v 0.15 at the nape). An asymmetric back tattoo needs `Side` 1 / 2 (the aim side, which the studio chooses from the half you aim at; within 10 cm of the spine it is 0 = mirrored). Arms: side 1 and side 2 decals at the arm UV showed on different arms in the studio.

Left and right limbs share UV space, so a limb tattoo appears on **both** sides; a one-sided tattoo needs a per-side skin
copy (not built). The torso front and back are separate islands (front u 0.36-0.64, v 0.17-0.46).

## Not done / next
- Back view of the anchors (needs a rotating camera in the headless run): `-tattoopick` rays from -z hit the back and are untested by eye.
- **Entrance**: open `TattooStudio` from a real place - the clinic (`kind == "surgery"`, `interior.getScene()` is an `IntroCinematic` with the player avatar) as an extra option of `SurgeryMenu` / the retail surgeon page, or a dedicated spot (an area interior of the map, `docs/re/map_spots_spec.md`). Only the `-tattooshop` test hook exists.
- Size / rotation of the decal, a per-side limb tattoo (L/R share UV), removing worn tattoos, the face (`w_head_c`), the woman's body by eye.
- The face (TD_Skinface, `w_head_c`) is not composited; the body only.
- **3D look verified** (`docs/images/tattoo_3d_player_arms.webp`): `-gm FIRST_WM -skipcine -nopad -fixedtimestep -tattoo "cross,0.45,0.3,0.7,0,1,ff0000;cross,0.5,0.75,0.7,0,1,ff0000;cross,0.85,0.35,0.5,0,1,ff0000"`, shot at frame 150: the player's forearms and hands are red, the chest decal is hidden under the T-shirt. Skin UV: forearms/hands lie around u 0.45-0.9, v 0.3-0.8 (cover blobs, exact regions still to map).
- Clothes hide body tattoos through the body-zone mesh hiding already in `EdenAvatar::load`.
- Then: region map (chest/back/arms/legs/neck), the tattoo shop page, a decal editor, face tattoos.
