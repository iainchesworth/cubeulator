#pragma once

// The capture-gate and hold-window constants the GUI-thread commit path
// (src/app/qt/controllers/scan_session.cpp) applies, exported so the
// offline replay harnesses ([.cross-session], [.track-replay]) measure the
// SHIPPING policy by construction instead of hand-transcribing it. Before
// this header existed both harnesses carried "must be mirrored by hand"
// copies — exactly the drift trap frame_recording.hpp was created to close
// for the recording format (see that header's rationale).
//
// Header-only and Qt/OpenCV-free on purpose: consumed by the Qt app and by
// the Qt-free test binaries alike.
//
// These are POLICY constants: tuning one changes what the app commits, so
// any change must be re-measured on the corpus (docs/pipeline-plan.md
// ground rules) — the point of this header is that the harnesses pick the
// change up automatically and the before/after tables stay honest.

#include <cstdint>

namespace cube::vision::gates {

// Primary commit gate: commits require a LOCKED track whose identity has
// this many consecutive accepted resolutions behind it — a fresh re-lock
// must re-prove itself before its routing is trusted with a write.
inline constexpr int kMinCaptureAccepts = 3;

// Per-identity staleness: identity-keyed aggregation windows legitimately
// pause while the user shows OTHER faces — only a gap long enough for
// lighting to have drifted resets a face's window, because the identity
// key already guarantees the samples belong to the same physical face.
inline constexpr std::int64_t kIdentityAggStaleMs = 5000;

// Frontal-ness: beyond ~35 degrees a face view foreshortens too much to
// trust colours; a corner view reads its flank faces obliquely by
// construction, so faces whose window took corner samples get the looser
// corner bound instead.
inline constexpr float kMaxFaceCaptureDeg = 35.0F;
inline constexpr float kMaxCornerFlankDeg = 70.0F;

// Motion: a commit mid-turn samples blur. The resolved per-pass rotation
// bounds actual cube motion; the stability score (threshold well above its
// 0.02 steady floor) bounds scene motion the pose can't see.
inline constexpr float kMaxCaptureRotDeg = 8.0F;
inline constexpr float kMaxCaptureMotionScore = 0.05F;

// AR-overlay/seed persistence window: held geometry (and the pose seed +
// corner/edge hints derived from it) outlives a detection blip by this
// long. The replay harnesses hold seeds for the same window so their
// detector cascade sees the hints the live app's would.
inline constexpr std::int64_t kOverlayHoldMs = 450;

}  // namespace cube::vision::gates
