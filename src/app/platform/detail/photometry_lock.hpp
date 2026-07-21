#pragma once

#include "interfaces/camera.hpp"

#include <opencv2/videoio.hpp>

#include <array>
#include <optional>

namespace cube::platform::camera::detail {

// The device-side half of the photometry lock described in
// interfaces/camera.hpp. Split out of opencv_capture_loop.cpp so the pure
// bookkeeping (which control maps to which cv::CAP_PROP_*, how a read-back
// is judged, how an outcome is worded for a user) can be unit-tested against
// a stand-in cv::VideoCapture-free path on every platform, including the
// ones whose backend has no property support at all.

// The values found on the device immediately before a lock was applied, so
// they can be put back. Absent entries were never touched.
//
// "As found", emphatically not "factory defaults": DirectShow property state
// lives in the driver and survives close/open and process exit (the probe
// confirmed a fresh cv::VideoCapture reading back the previous run's
// exposure), so the only honest thing to restore is what this process
// actually found.
struct PhotometrySnapshot {
    std::array<std::optional<double>, kPhotometryControls.size()> values{};
};

// Maps a control onto the OpenCV property id it is written through. Exposed
// for tests; there is no cv::CAP_PROP_* constant anywhere else in the app.
[[nodiscard]] int property_id(PhotometryControl control) noexcept;

// True when a read-back is close enough to the written value to count as
// accepted. Not equality: white-balance-class properties quantise to a
// driver step, so an accepted 6500 can legitimately come back as 6496.
[[nodiscard]] bool read_back_matches(double requested, double readback) noexcept;

// Applies the lock and reports, per control, what the device actually did -
// judged by reading the property back, never by set()'s return value, which
// is unreliable on DirectShow. Records what it overwrote in `as_found`
// (entries already present are left alone, so a second lock in the same
// session still restores the pre-app values).
//
// MUST be called from the capture thread, or from a caller that has proven
// no grab is in flight (stop() does the latter, after joining).
[[nodiscard]] PhotometryLockReport apply_lock(cv::VideoCapture& capture,
                                              const PhotometryLockRequest& request,
                                              PhotometrySnapshot& as_found);

// Writes back everything in the snapshot and clears it. Same threading rule
// as apply_lock().
void restore(cv::VideoCapture& capture, PhotometrySnapshot& as_found);

}  // namespace cube::platform::camera::detail
