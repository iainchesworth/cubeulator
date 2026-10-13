#include <catch2/catch_test_macros.hpp>

#include <cube/model/color_scheme.hpp>
#include <cube/model/face.hpp>
#include <cube/model/scan_progress.hpp>

using namespace cube::model;

namespace {

std::array<Facelet, facelets_per_face> solid_face(FacetColor color) {
    std::array<Facelet, facelets_per_face> facelets{};
    for (auto& facelet : facelets) {
        facelet = Facelet{.color = color, .confidence = 0.9F};
    }
    return facelets;
}

}  // namespace

TEST_CASE("ScanProgress starts with no faces confirmed", "[model]") {
    const ScanProgress progress;

    REQUIRE(progress.faces_confirmed_count() == 0);
    REQUIRE_FALSE(progress.is_complete());
    REQUIRE_FALSE(progress.is_face_confirmed(Face::U));
}

TEST_CASE("ScanProgress::set_face marks a face confirmed and stores its facelets", "[model]") {
    ScanProgress progress;

    progress.set_face(Face::U, solid_face(FacetColor::white));

    REQUIRE(progress.is_face_confirmed(Face::U));
    REQUIRE(progress.faces_confirmed_count() == 1);
    REQUIRE(progress.face_scan(Face::U).facelets[center_facelet_index].color == FacetColor::white);
    REQUIRE_FALSE(progress.is_face_confirmed(Face::D));
}

TEST_CASE("ScanProgress::is_complete requires all 6 faces", "[model]") {
    ScanProgress progress;

    for (auto face : {Face::U, Face::R, Face::F, Face::D, Face::L}) {
        progress.set_face(face, solid_face(FacetColor::white));
    }
    REQUIRE_FALSE(progress.is_complete());

    progress.set_face(Face::B, solid_face(FacetColor::white));
    REQUIRE(progress.is_complete());
}

TEST_CASE("ScanProgress::observed_centers reports nullopt for unconfirmed or unknown-color faces",
          "[model]") {
    ScanProgress progress;
    progress.set_face(Face::U, solid_face(FacetColor::white));
    progress.set_face(Face::F, solid_face(FacetColor::unknown));

    const auto centers = progress.observed_centers();

    REQUIRE(centers[static_cast<std::size_t>(Face::U)] == FacetColor::white);
    REQUIRE_FALSE(centers[static_cast<std::size_t>(Face::F)].has_value());
    REQUIRE_FALSE(centers[static_cast<std::size_t>(Face::D)].has_value());
}

TEST_CASE("detect_color_scheme reports insufficient_data with no observations", "[model]") {
    const std::array<std::optional<FacetColor>, face_count> centers{};

    const auto result = detect_color_scheme(centers);

    REQUIRE(result.confidence == ColorSchemeConfidence::insufficient_data);
}

TEST_CASE("detect_color_scheme predicts the opposite face from a single observation", "[model]") {
    std::array<std::optional<FacetColor>, face_count> centers{};
    centers[static_cast<std::size_t>(Face::U)] = FacetColor::white;

    const auto result = detect_color_scheme(centers);

    REQUIRE(result.confidence == ColorSchemeConfidence::partial);
    REQUIRE(result.face_colors[static_cast<std::size_t>(Face::D)] == FacetColor::yellow);
    REQUIRE_FALSE(result.face_colors[static_cast<std::size_t>(Face::L)].has_value());
}

TEST_CASE("detect_color_scheme reports confirmed once every axis has an observation", "[model]") {
    std::array<std::optional<FacetColor>, face_count> centers{};
    centers[static_cast<std::size_t>(Face::U)] = FacetColor::white;
    centers[static_cast<std::size_t>(Face::L)] = FacetColor::red;
    centers[static_cast<std::size_t>(Face::F)] = FacetColor::green;

    const auto result = detect_color_scheme(centers);

    REQUIRE(result.confidence == ColorSchemeConfidence::confirmed);
    REQUIRE(result.face_colors[static_cast<std::size_t>(Face::D)] == FacetColor::yellow);
    REQUIRE(result.face_colors[static_cast<std::size_t>(Face::R)] == FacetColor::orange);
    REQUIRE(result.face_colors[static_cast<std::size_t>(Face::B)] == FacetColor::blue);
}

TEST_CASE(
    "detect_color_scheme reports contradictory when opposite faces violate the standard pairing",
    "[model]") {
    std::array<std::optional<FacetColor>, face_count> centers{};
    centers[static_cast<std::size_t>(Face::U)] = FacetColor::white;
    centers[static_cast<std::size_t>(Face::D)] = FacetColor::red;

    const auto result = detect_color_scheme(centers);

    REQUIRE(result.confidence == ColorSchemeConfidence::contradictory);
}
