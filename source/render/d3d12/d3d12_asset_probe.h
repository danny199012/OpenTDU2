#pragma once

// -d3d12-3dgtest [<bank>]: the first *game geometry* on Direct3D 12
// (docs/backends.md section 3.13).
//
// The probes before it drew synthetic quads: -d3d12-edentest put one original
// .sob program on a quad with the engine's checker, -d3d12-scenetest a cube
// through the engine's own mesh pair, and -d3d12-pipetest created a PSO for
// every distinct program in the game without drawing anything. This one takes a
// real bank out of the game data - its .3DD scene, .3DG geometry, .2DM
// materials and .2DB textures - and draws it:
//
//   1. the bank's (3dd, 3dg) pair with the most indexed primitives is chosen,
//      its 2dm the material library;
//   2. every node's primitives are matched to the material its .3DD names, the
//      material's .sob permutation is looked up exactly the way
//      EdenRenderer::setupMaterial / updateProgram look it up (the .2DM's 48
//      switches, S_NBTEXTURES set to the number of textures that resolved);
//   3. the primitive's own vertex streams go through D3D12PrepareMesh (the
//      SNORM / UScaled expansion of docs/backends.md 3.12) and the vertex input
//      layout comes from the shader's dcl list and the mesh's declaration
//      (D3D12BuildVertexLayout, 3.10);
//   4. the cbuffers are filled by the *shared* filler - FillEdenConstants - with
//      the material's real parameters, so a register that reads 1.0 here is one
//      the .2DM does not fill either;
//   5. the material's textures are the bank's own .2db uploads (TextureCache's
//      Direct3D 12 path); an unresolved slot binds the white texture, because a
//      shader that samples an *unfilled* descriptor reads garbage and may fault
//      the GPU;
//   6. the model is drawn whole (every node, its own world transform), framed by
//      the geometry's own bounds, and the frame is read back and measured.
//
// What the measurement can prove and what it cannot: pixel coverage and colour
// variety prove the geometry reached the frame with its textures; they do not
// prove the model looks right (that needs eyes on the .ppm). The per-material
// log lines name the program, the index count and every slot that did not
// resolve, so a partially drawn model is visible in the log rather than guessed
// at.
//
// Runs in the game bootstrap (it needs Euro/Bnk and Euro/Shaders), creates its
// own hidden window and device like -d3d12-edentest, and ends the run with its
// verdict. `-d3d12-dump-shaders <dir>` writes the HLSL of every program it
// built, `-renderer-shot <path.ppm>` the last frame.
bool D3D12RunMeshAssetTest();
