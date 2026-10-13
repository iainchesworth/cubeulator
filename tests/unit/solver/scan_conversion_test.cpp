#include <catch2/catch_test_macros.hpp>

#include <cube/solver/scan_conversion.hpp>

#include <array>
#include <cstddef>

using cube::CubeError;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::cube_from_scan;
using cube::solver::Face;
using cube::solver::Move;

namespace {

using Permutation = std::array<int, 9>;
constexpr Permutation kIdentity = {0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr Permutation kRotate180 = {8, 7, 6, 5, 4, 3, 2, 1, 0};
constexpr Permutation kFlipVertical = {6, 7, 8, 3, 4, 5, 0, 1, 2};
constexpr Permutation kFlipHorizontal = {2, 1, 0, 5, 4, 3, 8, 7, 6};

// Independently restates scan_conversion.cpp's own (anonymous-namespace,
// so not reachable from here) permutation tables. Every table is a
// self-inverse (involution), which is what lets the same table used to
// synthesize a ScanProgress from a Cube also be the one cube_from_scan()
// uses to map back - see the round-trip test below.
const Permutation& permutation_for(cube::model::Face face) {
    switch (face) {
        case cube::model::Face::U:
            return kRotate180;
        case cube::model::Face::D:
            return kFlipVertical;
        case cube::model::Face::F:
            return kIdentity;
        case cube::model::Face::L:
            return kIdentity;
        case cube::model::Face::B:
            return kFlipHorizontal;
        case cube::model::Face::R:
            return kFlipHorizontal;
    }
    return kIdentity;
}

Face to_solver_face(cube::model::Face face) {
    switch (face) {
        case cube::model::Face::U:
            return Face::U;
        case cube::model::Face::R:
            return Face::R;
        case cube::model::Face::F:
            return Face::F;
        case cube::model::Face::D:
            return Face::D;
        case cube::model::Face::L:
            return Face::L;
        case cube::model::Face::B:
            return Face::B;
    }
    return Face::U;
}

cube::model::FacetColor to_model_color(Color color) {
    switch (color) {
        case Color::White:
            return cube::model::FacetColor::white;
        case Color::Yellow:
            return cube::model::FacetColor::yellow;
        case Color::Red:
            return cube::model::FacetColor::red;
        case Color::Orange:
            return cube::model::FacetColor::orange;
        case Color::Green:
            return cube::model::FacetColor::green;
        case Color::Blue:
            return cube::model::FacetColor::blue;
    }
    return cube::model::FacetColor::unknown;
}

// Inverts cube_from_scan()'s own mapping to build a ScanProgress a correct
// implementation must map back to `cube` exactly.
cube::model::ScanProgress synthesize_scan(const Cube& cube) {
    cube::model::ScanProgress progress;
    for (std::size_t i = 0; i < cube::model::face_count; ++i) {
        const auto model_face = static_cast<cube::model::Face>(i);
        const Face solver_face = to_solver_face(model_face);
        const auto& permutation = permutation_for(model_face);
        const auto solver_facelets = cube.face(solver_face);

        std::array<cube::model::Facelet, 9> facelets{};
        for (std::size_t cam_index = 0; cam_index < 9; ++cam_index) {
            const auto sol_index = static_cast<std::size_t>(permutation[cam_index]);
            facelets[cam_index] = cube::model::Facelet{
                .color = to_model_color(solver_facelets[sol_index]), .confidence = 1.0F};
        }
        progress.set_face(model_face, facelets);
    }
    return progress;
}

}  // namespace

TEST_CASE("cube_from_scan round-trips a synthesized scan back to the original cube",
          "[scan_conversion]") {
    const Cube scrambled =
        Cube::solved().apply(Move{Face::R, 1}).apply(Move{Face::U, 1}).apply(Move{Face::F, 2});
    for (const Cube& original :
         {Cube::solved(), Cube::solved().apply(Move{Face::R, 1}), scrambled}) {
        const auto result = cube_from_scan(synthesize_scan(original));
        REQUIRE(result.has_value());
        REQUIRE(*result == original);
    }
}

TEST_CASE("cube_from_scan fails on an incomplete scan", "[scan_conversion]") {
    const cube::model::ScanProgress progress;
    const auto result = cube_from_scan(progress);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == CubeError::face_capture_incomplete);
}

TEST_CASE("cube_from_scan rejects a scan with a contradictory colour arrangement",
          "[scan_conversion]") {
    cube::model::ScanProgress progress = synthesize_scan(Cube::solved());
    // Forcing U to also read all-White (alongside D's genuine all-White)
    // pushes White's count to 18 and Yellow's to 0 - a real vision
    // misclassification is_physically_valid() must catch.
    std::array<cube::model::Facelet, 9> all_white{};
    all_white.fill(
        cube::model::Facelet{.color = cube::model::FacetColor::white, .confidence = 1.0F});
    progress.set_face(cube::model::Face::U, all_white);

    const auto result = cube_from_scan(progress);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == CubeError::invalid_cube_state);
}
