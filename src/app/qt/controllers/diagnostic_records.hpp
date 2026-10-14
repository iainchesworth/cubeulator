#pragma once

#include "interfaces/camera.hpp"

#include <cube/diagnostics/json.hpp>

#include <string_view>

namespace cube::app::records {

// The pieces of the app's diagnostic records that describe the platform's
// own types, as JSON (docs/diagnostics.md). The library describes the scan
// and the cube (cube/diagnostics/describe.hpp); these are what only the app
// layer can see.

// The identifier a control or an outcome is written as.
[[nodiscard]] std::string_view id_of(cube::platform::camera::PhotometryControl control) noexcept;
[[nodiscard]] std::string_view id_of(cube::platform::camera::ControlOutcome outcome) noexcept;

// What the driver accepted of a photometry lock: for each control the outcome,
// the value asked for and the value it read back, the white balance kelvin
// held, and the one-line summary the app would show.
//
//   {"platform_supported", "exposure_locked", "fully_applied",
//    "auto_white_balance": {"outcome", "requested", "readback"},
//    "exposure": {...}, "brightness": {...}, "saturation": {...},
//    "white_balance_kelvin": number or null, "summary"}
[[nodiscard]] cube::diagnostics::Json photometry(
    const cube::platform::camera::PhotometryLockReport& report);

}  // namespace cube::app::records
