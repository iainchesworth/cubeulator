#pragma once

#include <cube/diagnostics/json.hpp>
#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/direct_scan.hpp>

#include <opencv2/core.hpp>

#include <string_view>
#include <vector>

namespace cube::diagnostics {

// What the library knows, as JSON, for the developer diagnostics channel
// (docs/diagnostics.md). Each function turns a value the library already
// computed - the result structs of resolve_colors_explained and
// SideScan::diagnose, the cube - into a Json; none of them computes anything
// of its own, and none is called unless the channel is on.
//
// Names in the output are fixed words (`"red"`, `"ambiguous_scan"`), never
// text meant for a person, so that another program can match on them.

// The word a colour is written as: white, yellow, red, orange, green, blue,
// unknown.
[[nodiscard]] std::string_view name_of(cube::model::FacetColor colour) noexcept;
// The identifier of an error: the enumerator's name.
[[nodiscard]] std::string_view code_of(CubeError error) noexcept;
// The word a solver colour is written as (same words as above).
[[nodiscard]] std::string_view name_of(cube::solver::Color colour) noexcept;
// The side's letter: U, D, L, R, F, B.
[[nodiscard]] std::string_view letter_of(cube::solver::Face face) noexcept;

// [L, a, b].
[[nodiscard]] Json to_json(const cv::Vec3f& lab);
// {"hue_deg", "chroma", "rule", "name", "confidence"}.
[[nodiscard]] Json to_json(const cube::vision::LabClassification& classification);
// What each colour measured in the session: {"white": [L, a, b], ...}.
[[nodiscard]] Json to_json(const cube::vision::SessionColours& colours);
// {"iterations", "match_cost", "hue_override", "hue_changed_names",
//  "clusters": [six of {"cluster", "seed", "mean", "hue", "by_distance",
//  "name"}], "sides": [six of {"cluster": [9], "name": [9],
//  "confidence": [9]}]} - the sides indexed as ScanDiagnosis::sides is.
[[nodiscard]] Json to_json(const cube::vision::ColorResolution& resolution);
// {"path", "candidates", "corner_reads", "corner_fits", "chosen",
//  "direct_error", "exchange", "error"}.
[[nodiscard]] Json to_json(const cube::vision::AssemblyTrace& trace);
// The whole evaluation: {"groups", "captured", "corner_reads", "sides":
// [{"first_seen", "reads", "roll", "in_view", "lab": [9 of [L, a, b]]}],
// "naming", "assembly", "result"}. `naming` is the resolution above, or
// {"error"} when naming failed, or null when fewer than six sides are in.
[[nodiscard]] Json to_json(const cube::vision::ScanDiagnosis& diagnosis);
// {"ok": true, "state", "colours"} or {"ok": false, "error"}; `state` is the
// 54-character string of docs/recording-format.md.
[[nodiscard]] Json to_json(const Result<cube::vision::ScannedCube>& scanned);

// The 54-character state string of a cube (docs/recording-format.md).
[[nodiscard]] Json cube_json(const cube::solver::Cube& cube);
// A move in standard notation ("R", "U2", "F'"), or null for none.
[[nodiscard]] Json move_json(const std::optional<cube::solver::Move>& move);
// Moves as one string of standard notation, space-separated.
[[nodiscard]] Json moves_json(const std::vector<cube::solver::Move>& moves);

}  // namespace cube::diagnostics
