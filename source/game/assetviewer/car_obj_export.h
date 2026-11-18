#pragma once

#include <string>

class CinematicCar;

// The car exporter (the asset viewer's Cars tab, -exportcar): writes the
// car's assembled models as OBJ + MTL the Blender route can read and the
// engine can read back - the parts pipeline's round trip
// (docs/re/car_viewer_spec.md).
//
//   <userdata>/export/<car>/
//     <car>_ext.obj / .mtl        the exterior model (BODY_*, glass, lamps...)
//     <car>_interior.obj / .mtl   the cockpit bank
//     <car>_rim_front / _rim_rear / _tire.obj / .mtl
//     anchors.txt                 the attach numbers: node transforms, body /
//                                 rear bounds, the tuner.txt hide boxes +
//                                 offsets
//
// Space law: the engine keeps the .3dg's authored (left handed, D3D) data
// unmirrored for cars (EdenModel::load parses with bMirrorX=false), and the
// OBJ route mirrors on import (obj_mesh.cpp negates x and reverses winding).
// The exporter therefore writes x' = -x and every face reversed - the exact
// involution - so the OBJ opens in Blender like an authored mesh and
// re-importing it lands the same triangles back. All positions are car space
// (the node's world-at-load transform applied, the raycast / collision space);
// the tuner hide boxes stay in item (primitive) space - anchors.txt says so
// and gives both bounds. Units are metres, Y up, -Z forward once mirrored.
//
// LOD0 items only, every node's subtree of each model (glasses and lamps
// included; a part author hides what they do not need). One `usemtl` group per
// material - that is also how the part loader splits a re-imported OBJ into
// draws. Vertices are written per primitive (no dedup): files are a few tens
// of MB, the shapes are exact.
//
// Returns false (with outError) when the car is not loaded or the folder
// could not be created. outFolder gets the written folder on success.
bool ExportCarObj( CinematicCar &car,
                   std::string &outFolder,
                   std::string &outError );
