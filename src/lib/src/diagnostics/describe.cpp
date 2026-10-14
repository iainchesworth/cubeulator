#include <cube/diagnostics/describe.hpp>

#include <cube/solver/notation.hpp>
#include <cube/vision/recording_v2.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace cube::diagnostics {

namespace {

using cube::model::FacetColor;
using cube::vision::AssemblyPath;
using cube::vision::HueOverride;
using cube::vision::LabRule;

[[nodiscard]] std::string_view name_of(AssemblyPath path) noexcept {
    switch (path) {
        case AssemblyPath::single_candidate:
            return "single_candidate";
        case AssemblyPath::chosen_by_corners:
            return "chosen_by_corners";
        case AssemblyPath::ambiguous:
            return "ambiguous";
        case AssemblyPath::direct:
            return "direct";
        case AssemblyPath::names_exchanged:
            return "names_exchanged";
        case AssemblyPath::failed:
            return "failed";
    }
    return "unknown";
}

[[nodiscard]] std::string_view name_of(HueOverride result) noexcept {
    switch (result) {
        case HueOverride::applied:
            return "applied";
        case HueOverride::collision:
            return "collision";
        case HueOverride::unknown_cluster:
            return "unknown_cluster";
    }
    return "unknown";
}

[[nodiscard]] std::string_view name_of(LabRule rule) noexcept {
    switch (rule) {
        case LabRule::dark_neutral:
            return "dark_neutral";
        case LabRule::neutral_white:
            return "neutral_white";
        case LabRule::warm_cast_white:
            return "warm_cast_white";
        case LabRule::hue_band:
            return "hue_band";
    }
    return "unknown";
}

[[nodiscard]] Json optional_error(const std::optional<CubeError>& error) {
    return error.has_value() ? Json(code_of(*error)) : Json();
}

[[nodiscard]] Json lab_array(const std::array<cv::Vec3f, 9>& labs) {
    Json out = Json::array();
    for (const cv::Vec3f& lab : labs) {
        out.push(to_json(lab));
    }
    return out;
}

}  // namespace

std::string_view name_of(FacetColor colour) noexcept {
    switch (colour) {
        case FacetColor::unknown:
            return "unknown";
        case FacetColor::white:
            return "white";
        case FacetColor::yellow:
            return "yellow";
        case FacetColor::red:
            return "red";
        case FacetColor::orange:
            return "orange";
        case FacetColor::green:
            return "green";
        case FacetColor::blue:
            return "blue";
    }
    return "unknown";
}

std::string_view name_of(cube::solver::Color colour) noexcept {
    switch (colour) {
        case cube::solver::Color::White:
            return "white";
        case cube::solver::Color::Yellow:
            return "yellow";
        case cube::solver::Color::Green:
            return "green";
        case cube::solver::Color::Blue:
            return "blue";
        case cube::solver::Color::Orange:
            return "orange";
        case cube::solver::Color::Red:
            return "red";
    }
    return "unknown";
}

std::string_view letter_of(cube::solver::Face face) noexcept {
    switch (face) {
        case cube::solver::Face::U:
            return "U";
        case cube::solver::Face::D:
            return "D";
        case cube::solver::Face::L:
            return "L";
        case cube::solver::Face::R:
            return "R";
        case cube::solver::Face::F:
            return "F";
        case cube::solver::Face::B:
            return "B";
    }
    return "?";
}

std::string_view code_of(CubeError error) noexcept {
    switch (error) {
        case CubeError::invalid_cube_state:
            return "invalid_cube_state";
        case CubeError::unsolvable_state:
            return "unsolvable_state";
        case CubeError::invalid_move_sequence:
            return "invalid_move_sequence";
        case CubeError::no_solution:
            return "no_solution";
        case CubeError::camera_not_found:
            return "camera_not_found";
        case CubeError::camera_start_failed:
            return "camera_start_failed";
        case CubeError::camera_control_unavailable:
            return "camera_control_unavailable";
        case CubeError::no_valid_cube_detected:
            return "no_valid_cube_detected";
        case CubeError::face_capture_incomplete:
            return "face_capture_incomplete";
        case CubeError::ambiguous_scan:
            return "ambiguous_scan";
        case CubeError::model_load_failed:
            return "model_load_failed";
        case CubeError::inference_provider_unavailable:
            return "inference_provider_unavailable";
        case CubeError::inference_failed:
            return "inference_failed";
        case CubeError::locale_resource_not_found:
            return "locale_resource_not_found";
    }
    return "unknown";
}

Json to_json(const cv::Vec3f& lab) {
    return Json::array().push(lab(0)).push(lab(1)).push(lab(2));
}

Json to_json(const cube::vision::LabClassification& classification) {
    Json out = Json::object();
    out.set("hue_deg", classification.hue_degrees);
    out.set("chroma", classification.chroma);
    out.set("rule", name_of(classification.rule));
    out.set("name", name_of(classification.facelet.color));
    out.set("confidence", classification.facelet.confidence);
    return out;
}

Json to_json(const cube::vision::SessionColours& colours) {
    // Indexed by solver::Color, in the enum's order.
    constexpr std::array<cube::solver::Color, 6> kColours = {
        cube::solver::Color::White, cube::solver::Color::Yellow, cube::solver::Color::Green,
        cube::solver::Color::Blue,  cube::solver::Color::Orange, cube::solver::Color::Red,
    };
    Json out = Json::object();
    for (const cube::solver::Color colour : kColours) {
        out.set(name_of(colour), to_json(colours.at(static_cast<std::size_t>(colour))));
    }
    return out;
}

Json to_json(const cube::vision::ColorResolution& resolution) {
    Json out = Json::object();
    out.set("iterations", resolution.iterations);
    out.set("match_cost", resolution.match_cost);
    out.set("hue_override", name_of(resolution.hue_override));
    out.set("hue_changed_names", resolution.hue_changed_names);

    Json clusters = Json::array();
    for (std::size_t c = 0; c < resolution.clusters.size(); ++c) {
        const cube::vision::ClusterNaming& cluster = resolution.clusters.at(c);
        Json entry = Json::object();
        entry.set("cluster", c);
        entry.set("seed", to_json(cluster.seed));
        entry.set("mean", to_json(cluster.mean));
        entry.set("hue", to_json(cluster.hue));
        entry.set("by_distance", name_of(cluster.by_distance));
        entry.set("name", name_of(cluster.name));
        clusters.push(std::move(entry));
    }
    out.set("clusters", std::move(clusters));

    Json sides = Json::array();
    for (std::size_t side = 0; side < resolution.faces.size(); ++side) {
        Json cluster_of = Json::array();
        Json names = Json::array();
        Json confidence = Json::array();
        for (std::size_t cell = 0; cell < 9; ++cell) {
            cluster_of.push(static_cast<int>(resolution.cluster_of.at(side).at(cell)));
            names.push(name_of(resolution.faces.at(side).at(cell).color));
            confidence.push(resolution.faces.at(side).at(cell).confidence);
        }
        Json entry = Json::object();
        entry.set("cluster", std::move(cluster_of));
        entry.set("name", std::move(names));
        entry.set("confidence", std::move(confidence));
        sides.push(std::move(entry));
    }
    out.set("sides", std::move(sides));
    return out;
}

Json to_json(const cube::vision::AssemblyTrace& trace) {
    Json out = Json::object();
    out.set("path", name_of(trace.path));
    out.set("candidates", trace.candidates);
    out.set("corner_reads", trace.corner_reads);
    Json fits = Json::array();
    for (const std::size_t fit : trace.corner_fits) {
        fits.push(fit);
    }
    out.set("corner_fits", std::move(fits));
    out.set("chosen", trace.chosen.has_value() ? Json(*trace.chosen) : Json());
    out.set("direct_error", optional_error(trace.direct_error));
    if (trace.exchange.has_value()) {
        Json exchange = Json::object();
        exchange.set("first_side", trace.exchange->first_side);
        exchange.set("second_side", trace.exchange->second_side);
        exchange.set("first_name", name_of(trace.exchange->first_name));
        exchange.set("second_name", name_of(trace.exchange->second_name));
        exchange.set("centre_distance", trace.exchange->centre_distance);
        out.set("exchange", std::move(exchange));
    } else {
        out.set("exchange", Json());
    }
    out.set("error", optional_error(trace.error));
    return out;
}

Json to_json(const Result<cube::vision::ScannedCube>& scanned) {
    Json out = Json::object();
    out.set("ok", scanned.has_value());
    if (scanned.has_value()) {
        out.set("state", cube_json(scanned->cube));
        out.set("colours", to_json(scanned->colours));
        out.set("cast", scanned->cast);
    } else {
        out.set("error", code_of(scanned.error()));
    }
    return out;
}

Json to_json(const cube::vision::ScanDiagnosis& diagnosis) {
    Json out = Json::object();
    out.set("groups", diagnosis.groups);
    out.set("captured", diagnosis.captured);
    out.set("corner_reads", diagnosis.corner_reads);

    Json sides = Json::array();
    for (const cube::vision::ScanDiagnosis::Side& side : diagnosis.sides) {
        Json entry = Json::object();
        entry.set("first_seen", side.first_seen);
        entry.set("reads", side.reads);
        entry.set("roll", side.roll);
        entry.set("in_view", side.in_view);
        entry.set("lab", lab_array(side.lab));
        sides.push(std::move(entry));
    }
    out.set("sides", std::move(sides));

    if (diagnosis.white.has_value()) {
        // The light the sides were named in: `side` indexes `sides`, `cast`
        // is the white centre's chroma, `gain` the linear (red, green, blue)
        // factors that took it out.
        Json white = Json::object();
        white.set("side", diagnosis.white->side);
        white.set("cast", diagnosis.white->cast);
        Json gain = Json::array();
        for (const float channel : diagnosis.white->gain) {
            gain.push(Json(channel));
        }
        white.set("gain", std::move(gain));
        out.set("white", std::move(white));
    } else {
        out.set("white", Json());
    }

    if (diagnosis.naming.has_value()) {
        out.set("naming", to_json(*diagnosis.naming));
    } else if (diagnosis.captured >= 6) {
        // Six sides were in and the naming still failed: the result carries
        // the reason.
        Json failed = Json::object();
        failed.set("error",
                   diagnosis.result.has_value() ? Json() : Json(code_of(diagnosis.result.error())));
        out.set("naming", std::move(failed));
    } else {
        out.set("naming", Json());
    }
    out.set("assembly", diagnosis.assembly.has_value() ? to_json(*diagnosis.assembly) : Json());
    out.set("result", to_json(diagnosis.result));
    return out;
}

Json cube_json(const cube::solver::Cube& cube) {
    return Json(cube::vision::recording::state_string(cube));
}

Json move_json(const std::optional<cube::solver::Move>& move) {
    return move.has_value() ? Json(cube::solver::to_string(*move)) : Json();
}

Json moves_json(const std::vector<cube::solver::Move>& moves) {
    return Json(cube::solver::to_string(moves));
}

}  // namespace cube::diagnostics
