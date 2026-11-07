#pragma once

// Build watermark: the build stamp (and the tester's name when the launcher
// passes one) drawn top-right so a screenshot or clip identifies the build it
// came from. Always on since the 2026-11-03 workflow session - the owner's bug
// reports need a build id ("we may need to watermark the runs with build ids
// that way we can see what build it was caught"). <userdata>/options.ini is not
// involved: this is a run-time overlay, not a setting.
//
//   -tester <name>   prefix the name: "<name> · build N (hash) · branch"
//   -nowatermark     never draw it (automated pixel-identical A/B runs; the
//                    startup log line, the window title and every screenshot's
//                    file/log stamp keep the identification)

namespace WatermarkOverlay {

// Reads -tester / -nowatermark and logs the run's build + branch one-shot.
// Call once at main-loop start (beside CoordsOverlay::SetVisible).
void Initialize();

// Draws the watermark. Call between ImGui beginFrame / endFrame.
void Draw();

} // namespace WatermarkOverlay
