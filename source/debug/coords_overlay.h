#pragma once

// Camera coordinate readout and location marks, for comparing positions with the
// Xbox 360 recomp (its F2 / F6 overlay prints the same X / Y / Z / heading).
//
//   F2  show / hide the readout (also on from the start with -coords)
//   F6  append a numbered mark to _marks.txt and the log
//
// Shows the main scene's camera: the player's chase camera while driving, the shot
// camera in a cutscene. Heading is degrees from +Z towards +X, 0..360, the same
// convention as the recomp overlay. [INF] The recomp's convention was derived from
// the camera matrix's forward row; confirm one heading against it before relying
// on the number.

struct Vector3f;

namespace CoordsOverlay {

// Called by the scene setup each frame with the main camera.
void SetCamera( const Vector3f &position, const Vector3f &direction );

void Toggle();
void SetVisible( bool bVisible );
void WriteMark();

// Draws the readout. Call between ImGui beginFrame / endFrame.
void Draw();

} // namespace CoordsOverlay
