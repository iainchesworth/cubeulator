#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>

#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/vision/capture_routing.hpp>
#include <cube/vision/orientation_lattice.hpp>

// The test that did not exist anywhere before it: compose the capture
// STORE's convention (each slot in the solver layout of its BODY face,
// kBodyRelabel-closed) with the real free-form assembler, on a scrambled
// cube, under every one of the 24 gauges a tracked session can anchor in.
// Every prior "assembly works" proof either fed solver-layout arrays
// straight in, used a solved cube (uniform faces are flip-invariant), or
// printed instead of asserting - which is how the store's mirror-handed
// D/R/B slots could reach the rotations-only search unconverted, working
// under just 3 of 24 gauges, without any suite going red.

namespace {

using cube::model::Facelet;
using cube::model::FacetColor;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Move;

constexpr cube::solver::Face solver_face(cube::model::Face face) {
    switch (face) {
        case cube::model::Face::U:
            return cube::solver::Face::U;
        case cube::model::Face::R:
            return cube::solver::Face::R;
        case cube::model::Face::F:
            return cube::solver::Face::F;
        case cube::model::Face::D:
            return cube::solver::Face::D;
        case cube::model::Face::L:
            return cube::solver::Face::L;
        case cube::model::Face::B:
            return cube::solver::Face::B;
    }
    return cube::solver::Face::U;
}

constexpr FacetColor model_color(Color color) {
    switch (color) {
        case Color::White:
            return FacetColor::white;
        case Color::Yellow:
            return FacetColor::yellow;
        case Color::Red:
            return FacetColor::red;
        case Color::Orange:
            return FacetColor::orange;
        case Color::Green:
            return FacetColor::green;
        case Color::Blue:
            return FacetColor::blue;
    }
    return FacetColor::unknown;
}

// Colours ride through CapturedFace as synthetic Lab: distinct L per
// Color, decoded by nearest index. Content is a carrier here - the
// geometry (cell permutations) is what the test is about.
cv::Vec3f lab_of(Color color) {
    return {static_cast<float>(color) * 15.0F + 10.0F, 0.0F, 0.0F};
}

Color color_of(const cv::Vec3f& lab) {
    return static_cast<Color>(static_cast<int>(std::lround((lab[0] - 10.0F) / 15.0F)));
}

// A store as it exists under gauge `g`: the identity-gauge slots (body
// face == physical face, content the solver layout of that face) pushed
// through the same relabel_capture the router itself uses on re-anchors.
std::array<cube::vision::CapturedFace, 6> store_under_gauge(const Cube& cube, int gauge) {
    std::array<cube::vision::CapturedFace, 6> slots{};
    for (int p = 0; p < 6; ++p) {
        const auto body = static_cast<cube::model::Face>(p);
        cube::vision::CapturedFace identity_slot;
        const auto colors = cube.face(solver_face(body));
        for (std::size_t k = 0; k < identity_slot.lab.size(); ++k) {
            identity_slot.lab[k] = lab_of(colors[k]);
        }
        const auto& map = cube::vision::kBodyRelabel[static_cast<std::size_t>(gauge)]
                                                    [static_cast<std::size_t>(p)];
        slots[static_cast<std::size_t>(map.body_face)] =
            cube::vision::relabel_capture(identity_slot, body, gauge);
    }
    return slots;
}

std::array<std::array<Facelet, 9>, 6> decode(
    const std::array<cube::vision::CapturedFace, 6>& slots) {
    std::array<std::array<Facelet, 9>, 6> inputs{};
    for (std::size_t b = 0; b < slots.size(); ++b) {
        for (std::size_t k = 0; k < 9; ++k) {
            inputs[b][k] = Facelet{model_color(color_of(slots[b].lab[k])), 1.0F};
        }
    }
    return inputs;
}

Cube scrambled() {
    Cube c = Cube::solved();
    using F = cube::solver::Face;
    for (const Move move : {Move{F::R, 1}, Move{F::U, 2}, Move{F::F, 3}, Move{F::L, 1},
                            Move{F::D, 1}, Move{F::B, 2}, Move{F::U, 1}, Move{F::R, 3},
                            Move{F::F, 1}, Move{F::D, 2}, Move{F::L, 3}, Move{F::B, 1}}) {
        c = c.apply(move);
    }
    return c;
}

}  // namespace

TEST_CASE("a stored scan assembles to the exact cube under every gauge",
          "[capture-routing][scan_assembly]") {
    const Cube cube = scrambled();
    for (int gauge = 0; gauge < static_cast<int>(cube::vision::kLatticeSize); ++gauge) {
        CAPTURE(gauge);
        auto slots = store_under_gauge(cube, gauge);
        for (std::size_t b = 0; b < slots.size(); ++b) {
            cube::vision::slot_to_outside_view(static_cast<cube::model::Face>(b), slots[b]);
        }
        const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(decode(slots));
        REQUIRE(assembled.has_value());
        REQUIRE(assembled->cube == cube);
    }
}

TEST_CASE("without the outside-view seam no gauge assembles", "[capture-routing][scan_assembly]") {
    // The defect this file exists to prevent, pinned as a property: raw
    // store slots (solver layout of their body face) can never satisfy
    // the assembler's outside-view contract - the three slots whose body
    // label is D, R or B arrive double-reflected under EVERY gauge, and
    // no quarter-turn recovers a reflection.
    const Cube cube = scrambled();
    int assembled_count = 0;
    for (int gauge = 0; gauge < static_cast<int>(cube::vision::kLatticeSize); ++gauge) {
        const auto slots = store_under_gauge(cube, gauge);
        const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(decode(slots));
        assembled_count += assembled.has_value() && assembled->cube == cube ? 1 : 0;
    }
    REQUIRE(assembled_count == 0);
}

namespace {

// The centre-colour pinning restated from scan_assembly.hpp's documented
// scheme (White -> D, Yellow -> U, Green -> F, Blue -> B, Red -> L,
// Orange -> R), not lifted from the implementation, so a mis-pinned producer
// cannot agree with a mis-pinned test.
constexpr cube::solver::Face pinned_face(Color centre) {
    switch (centre) {
        case Color::White:
            return cube::solver::Face::D;
        case Color::Yellow:
            return cube::solver::Face::U;
        case Color::Green:
            return cube::solver::Face::F;
        case Color::Blue:
            return cube::solver::Face::B;
        case Color::Red:
            return cube::solver::Face::L;
        case Color::Orange:
            return cube::solver::Face::R;
    }
    return cube::solver::Face::U;
}

// One clockwise quarter-turn in the image plane, derived from the
// (row, col) form - rotated(row, col) = source(2 - col, row) - rather than
// copying scan_assembly.cpp's flat gather table, so a mis-derived table
// would be caught (scan_assembly_test.cpp does the same).
std::array<Color, 9> rotated_clockwise(const std::array<Color, 9>& face) {
    std::array<Color, 9> rotated{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            rotated[static_cast<std::size_t>(3 * row + col)] =
                face[static_cast<std::size_t>(3 * (2 - col) + row)];
        }
    }
    return rotated;
}

// The assembler's handedness correction on a D/R/B-pinned input, restated
// as the column swap it is documented to be.
std::array<Color, 9> flipped_columns(std::array<Color, 9> face) {
    for (int row = 0; row < 3; ++row) {
        std::swap(face[static_cast<std::size_t>(row * 3)],
                  face[static_cast<std::size_t>(row * 3 + 2)]);
    }
    return face;
}

std::array<Color, 9> slot_colors(const cube::vision::CapturedFace& slot) {
    std::array<Color, 9> colors{};
    for (std::size_t k = 0; k < colors.size(); ++k) {
        colors[k] = color_of(slot.lab[k]);
    }
    return colors;
}

// The store is indexed by body face, so slot b's body label IS Face b -
// the live path reads the same labels off the router (face_at).
std::array<cube::model::Face, 6> identity_body_faces() {
    std::array<cube::model::Face, 6> body_faces{};
    for (std::size_t b = 0; b < body_faces.size(); ++b) {
        body_faces[b] = static_cast<cube::model::Face>(b);
    }
    return body_faces;
}

}  // namespace

TEST_CASE("the store hint producer derives digits that re-orient every slot",
          "[capture-routing][scan_assembly]") {
    const Cube cube = scrambled();
    for (int gauge = 0; gauge < static_cast<int>(cube::vision::kLatticeSize); ++gauge) {
        CAPTURE(gauge);
        auto slots = store_under_gauge(cube, gauge);
        for (std::size_t b = 0; b < slots.size(); ++b) {
            cube::vision::slot_to_outside_view(static_cast<cube::model::Face>(b), slots[b]);
        }
        const auto inputs = decode(slots);

        const cube::solver::RotationHint hint =
            cube::solver::rotation_hint_for_store_scan(inputs, identity_body_faces());
        for (std::size_t i = 0; i < slots.size(); ++i) {
            CAPTURE(i);
            REQUIRE(hint.quarter_turns[i] != cube::solver::RotationHint::kNoHint);
            // Independent digit check, straight from the RotationHint
            // contract: digit d rotates the ASSEMBLER-CORRECTED input onto
            // the solver layout of its centre-pinned face. Everything here
            // is restated from documentation, none of it from the
            // producer's geometry.
            std::array<Color, 9> reoriented = slot_colors(slots[i]);
            const cube::solver::Face pinned =
                pinned_face(reoriented[cube::model::center_facelet_index]);
            if (pinned == cube::solver::Face::D || pinned == cube::solver::Face::R ||
                pinned == cube::solver::Face::B) {
                reoriented = flipped_columns(reoriented);
            }
            for (int t = 0; t < hint.quarter_turns[i]; ++t) {
                reoriented = rotated_clockwise(reoriented);
            }
            REQUIRE(reoriented == cube.face(pinned));
        }

        const auto assembled = cube::solver::assemble_scanned_faces_with_mapping(inputs, &hint);
        REQUIRE(assembled.has_value());
        REQUIRE(assembled->cube == cube);
    }
}

TEST_CASE("the store hint degrades to no advice when body labels cannot name a rotation",
          "[capture-routing][scan_assembly]") {
    const Cube cube = scrambled();
    auto slots = store_under_gauge(cube, cube::vision::kIdentityLatticeIndex);
    for (std::size_t b = 0; b < slots.size(); ++b) {
        cube::vision::slot_to_outside_view(static_cast<cube::model::Face>(b), slots[b]);
    }
    const auto inputs = decode(slots);

    SECTION("a repeated body label is not a bijection") {
        auto body_faces = identity_body_faces();
        body_faces[5] = body_faces[0];
        const auto hint = cube::solver::rotation_hint_for_store_scan(inputs, body_faces);
        for (const int digit : hint.quarter_turns) {
            REQUIRE(digit == cube::solver::RotationHint::kNoHint);
        }
    }

    SECTION("swapping the L and R slots' body labels composes a reflection") {
        // Opposite slots hold opposite physical faces under every gauge, so
        // both centre correspondences still agree on the x column's
        // DIRECTION - the opposite-pair check passes - but the columns then
        // form det -1, a mirror no quarter-turn digit can describe.
        auto body_faces = identity_body_faces();
        std::swap(body_faces[static_cast<std::size_t>(cube::model::Face::R)],
                  body_faces[static_cast<std::size_t>(cube::model::Face::L)]);
        const auto hint = cube::solver::rotation_hint_for_store_scan(inputs, body_faces);
        for (const int digit : hint.quarter_turns) {
            REQUIRE(digit == cube::solver::RotationHint::kNoHint);
        }
    }
}

TEST_CASE("the store hint lifts the near-solved ambiguity under every gauge",
          "[capture-routing][scan_assembly]") {
    // scan_assembly_test.cpp's canonical ambiguous fixture (one U quarter
    // turn from solved) expressed through the store convention: U and D are
    // uniform, and the four side faces admit a rival joint rotation that
    // reads the displaced rows as a D-layer turn. The unhinted assembler
    // must refuse, and the store hint's geometric digits must pick the true
    // cube - under all 24 gauges, 23 of them non-identity.
    const Cube cube = Cube::solved().apply(Move{cube::solver::Face::U, 1});
    for (int gauge = 0; gauge < static_cast<int>(cube::vision::kLatticeSize); ++gauge) {
        CAPTURE(gauge);
        auto slots = store_under_gauge(cube, gauge);
        for (std::size_t b = 0; b < slots.size(); ++b) {
            cube::vision::slot_to_outside_view(static_cast<cube::model::Face>(b), slots[b]);
        }
        const auto inputs = decode(slots);

        // The premise: without a hint this scan IS ambiguous. If a solver
        // change ever lifts that, these assertions test nothing and the
        // fixture must be rebuilt (scan_assembly_test.cpp guards the same
        // way).
        const auto unhinted = cube::solver::assemble_scanned_faces_with_mapping(inputs);
        REQUIRE_FALSE(unhinted.has_value());

        const auto hint = cube::solver::rotation_hint_for_store_scan(inputs, identity_body_faces());
        const auto hinted = cube::solver::assemble_scanned_faces_with_mapping(inputs, &hint);
        REQUIRE(hinted.has_value());
        REQUIRE(hinted->cube == cube);
    }
}
