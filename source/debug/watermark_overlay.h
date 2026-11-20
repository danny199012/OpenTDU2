#pragma once

// Build watermark: the build stamp (and the tester's name when the launcher
// passes one) drawn top-right so a screenshot or clip identifies the build it
// came from. Always on since the 2026-11-03 workflow session - the owner's bug
// reports need a build id ("we may need to watermark the runs with build ids
// that way we can see what build it was caught"). <userdata>/options.ini is not
// involved: this is a run-time overlay, not a setting.
//
//   -tester <name>   prefix the name: "<name> · build N (hash) · branch"
//   -showfps         adds a frame-rate line (FPS, average and worst frame time over
//                    half a second); the Android launcher has a switch for it
//   -nowatermark     never draw it (automated pixel-identical A/B runs; the
//                    startup log line, the window title and every screenshot's
//                    file/log stamp keep the identification)

namespace WatermarkOverlay {

// Reads -tester / -nowatermark and logs the run's build + branch one-shot.
// Call once at main-loop start (beside CoordsOverlay::SetVisible).
void Initialize();

// The last frame's section times (ms), shown under the FPS line with -showfps: where the
// time goes (update = game logic + streaming, offscreen / main = scene passes, present = the
// wait for the GPU). Call once per frame after the frame is done.
void SetFrameTimes( double updateMs, double offscreenMs, double mainMs, double uiMs, double endMs );

// Draws the watermark. Call between ImGui beginFrame / endFrame.
void Draw();

} // namespace WatermarkOverlay
