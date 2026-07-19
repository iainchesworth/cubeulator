#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>

#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>

using cube::model::Facelet;
using cube::model::FacetColor;
using cube::solver::assemble_scanned_faces;
using cube::solver::assemble_scanned_faces_with_mapping;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::RotationHint;

namespace {

constexpr FacetColor to_model_color(Color color) noexcept {
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

std::array<Facelet, 9> to_model_face(const std::array<Color, 9>& colors) {
    std::array<Facelet, 9> facelets{};
    for (std::size_t i = 0; i < colors.size(); ++i) {
        facelets[i] = Facelet{to_model_color(colors[i]), 1.0F};
    }
    return facelets;
}

// One clockwise quarter-turn in the image plane, derived here from the
// (row, col) form - rotated(row, col) = source(2 - col, row) - rather than
// copying scan_assembly.cpp's flat gather table, so the test would catch a
// mis-derived table. (Which direction "clockwise" is doesn't matter to the
// round-trip tests: assembly searches all four rotations either way.)
std::array<Color, 9> rotated_clockwise(const std::array<Color, 9>& face) {
    std::array<Color, 9> rotated{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            rotated[static_cast<std::size_t>(3 * row + col)] = face[static_cast<std::size_t>(3 * (2 - col) + row)];
        }
    }
    return rotated;
}

Cube scrambled_cube(std::mt19937& rng, int move_count) {
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};
    Cube c = Cube::solved();
    for (int i = 0; i < move_count; ++i) {
        c = c.apply(Move{kAllFaces[face_dist(rng)], turn_dist(rng)});
    }
    return c;
}

// The cube's six faces as model facelets in solver-Face input order with no
// extra rotation - the "camera happened to capture everything canonically"
// baseline the free-form tests then perturb.
std::array<std::array<Facelet, 9>, 6> model_faces_of(const Cube& cube) {
    std::array<std::array<Facelet, 9>, 6> faces{};
    for (Face face : kAllFaces) {
        faces[static_cast<std::size_t>(face)] = to_model_face(cube.face(face));
    }
    return faces;
}

}  // namespace

TEST_CASE("assembly round-trips scrambled cubes through per-face rotation and order shuffling",
          "[scan_assembly]") {
    for (unsigned seed = 1; seed <= 10; ++seed) {
        CAPTURE(seed);
        std::mt19937 rng{seed};
        const Cube original = scrambled_cube(rng, 25);

        // Capture each face as the camera might: rotated by an arbitrary
        // number of quarter turns, independently per face.
        std::uniform_int_distribution<int> rotation_dist{0, 3};
        std::array<std::array<Color, 9>, 6> captured{};
        for (Face face : kAllFaces) {
            std::array<Color, 9> stickers = original.face(face);
            const int rotations = rotation_dist(rng);
            for (int r = 0; r < rotations; ++r) {
                stickers = rotated_clockwise(stickers);
            }
            captured[static_cast<std::size_t>(face)] = stickers;
        }

        // Present the six faces in an arbitrary order: input slot i
        // receives the face whose solver-Face index is order[i].
        std::array<std::size_t, 6> order = {0, 1, 2, 3, 4, 5};
        std::shuffle(order.begin(), order.end(), rng);
        std::array<std::array<Facelet, 9>, 6> input{};
        for (std::size_t input_index = 0; input_index < input.size(); ++input_index) {
            input[input_index] = to_model_face(captured[order[input_index]]);
        }

        const auto result = assemble_scanned_faces_with_mapping(input);
        REQUIRE(result.has_value());
        REQUIRE(result->cube == original);
        for (std::size_t input_index = 0; input_index < input.size(); ++input_index) {
            REQUIRE(result->input_index_of_face[order[input_index]] == input_index);
        }
    }
}

TEST_CASE("a solved cube's uniform faces assemble despite degenerate rotations", "[scan_assembly]") {
    // Every rotation of a uniform 9-sticker face is that same face, so all
    // 4096 combinations build the identical solved cube - the search must
    // treat that as one (degenerate) survivor, not as ambiguity.
    const auto result = assemble_scanned_faces(model_faces_of(Cube::solved()));
    REQUIRE(result.has_value());
    REQUIRE(*result == Cube::solved());
}

// The canonical near-solved ambiguity (scan_assembly.hpp's "within a
// couple of moves of solved"): one U quarter-turn away from solved,
// captured canonically. U and D are uniform (rotation-degenerate), and the
// four side faces - six own-colour stickers plus one displaced row each -
// admit a SECOND joint rotation assignment that builds a different but
// equally solvable cube (the displaced rows re-read as a D-layer turn), so
// the unhinted search must reject the scan as ambiguous. Shared fixture
// for every hint test below.
namespace {
std::array<std::array<Facelet, 9>, 6> ambiguous_near_solved_input() {
    return model_faces_of(Cube::solved().apply(Move{Face::U, 1}));
}
}  // namespace

TEST_CASE("a rotation hint lifts near-solved assembly ambiguity", "[scan_assembly]") {
    const Cube original = Cube::solved().apply(Move{Face::U, 1});
    const auto input = ambiguous_near_solved_input();

    // Establish the premise: without a hint this scan IS ambiguous - if it
    // ever stops being (a solver change), every hint test here is testing
    // nothing and must be rebuilt on a new fixture.
    const auto unhinted = assemble_scanned_faces_with_mapping(input);
    REQUIRE_FALSE(unhinted.has_value());
    REQUIRE(unhinted.error() == cube::CubeError::invalid_cube_state);

    // Canonical capture means the true assembly applies zero quarter turns
    // to every face; the rival survivor needs the side faces re-rotated,
    // so an all-zeros hint scores the true cube strictly highest.
    RotationHint hint;
    hint.quarter_turns.fill(0);
    const auto hinted = assemble_scanned_faces_with_mapping(input, &hint);
    REQUIRE(hinted.has_value());
    REQUIRE(hinted->cube == original);
    // Mapping bookkeeping is hint-independent (centres pin it before the
    // search runs): canonical input order means the identity mapping.
    for (std::size_t face_index = 0; face_index < hinted->input_index_of_face.size(); ++face_index) {
        REQUIRE(hinted->input_index_of_face[face_index] == face_index);
    }
}

TEST_CASE("a rotation hint can select the rival survivor instead", "[scan_assembly]") {
    const Cube original = Cube::solved().apply(Move{Face::U, 1});
    const auto input = ambiguous_near_solved_input();

    // Hint the four side faces at a half-turn (input order is solver-Face
    // order here: U,D,L,R,F,B), leaving the degenerate U/D unhinted: the
    // displaced rows land on the D layer and the OTHER solvable assembly
    // must win - proof the hint genuinely selects rather than merely
    // rubber-stamping the assembly the unhinted search would have found
    // first.
    RotationHint hint;
    hint.quarter_turns = {RotationHint::kNoHint, RotationHint::kNoHint, 2, 2, 2, 2};
    const auto hinted = assemble_scanned_faces_with_mapping(input, &hint);
    REQUIRE(hinted.has_value());
    REQUIRE(hinted->cube != original);
    REQUIRE(hinted->cube.is_solvable());
}

TEST_CASE("a hint never vetoes a unique survivor", "[scan_assembly]") {
    // A scrambled cube's assembly is unique; a hint - even one that
    // contradicts the true rotations - must not be able to reject it.
    // Geometry outvotes advice.
    std::mt19937 rng{7};
    const Cube original = scrambled_cube(rng, 25);
    const auto input = model_faces_of(original);

    RotationHint wrong_hint;
    wrong_hint.quarter_turns.fill(3);  // canonical capture: true digits are all 0
    const auto hinted = assemble_scanned_faces_with_mapping(input, &wrong_hint);
    REQUIRE(hinted.has_value());
    REQUIRE(hinted->cube == original);
}

TEST_CASE("an unusable hint behaves exactly like no hint", "[scan_assembly]") {
    const auto input = ambiguous_near_solved_input();

    // Default-constructed (all kNoHint) and out-of-range entries alike
    // carry no expectation: the ambiguity must fail exactly as unhinted.
    RotationHint empty_hint;
    const auto with_empty = assemble_scanned_faces_with_mapping(input, &empty_hint);
    REQUIRE_FALSE(with_empty.has_value());
    REQUIRE(with_empty.error() == cube::CubeError::invalid_cube_state);

    RotationHint malformed_hint;
    malformed_hint.quarter_turns = {7, -5, 4, 99, RotationHint::kNoHint, -1};
    const auto with_malformed = assemble_scanned_faces_with_mapping(input, &malformed_hint);
    REQUIRE_FALSE(with_malformed.has_value());
    REQUIRE(with_malformed.error() == cube::CubeError::invalid_cube_state);

    const auto with_null = assemble_scanned_faces_with_mapping(input, nullptr);
    REQUIRE_FALSE(with_null.has_value());
    REQUIRE(with_null.error() == cube::CubeError::invalid_cube_state);
}

TEST_CASE("a hint that cannot separate the survivors leaves the ambiguity standing", "[scan_assembly]") {
    const auto input = ambiguous_near_solved_input();

    // Hinting only the uniform U face is worthless evidence: every
    // survivor reaches digit 0 on a rotationally symmetric face, so all
    // scores tie and the scan must still fail - a hint may only ever win
    // STRICTLY.
    RotationHint hint;
    hint.quarter_turns[0] = 0;
    const auto hinted = assemble_scanned_faces_with_mapping(input, &hint);
    REQUIRE_FALSE(hinted.has_value());
    REQUIRE(hinted.error() == cube::CubeError::invalid_cube_state);
}

TEST_CASE("duplicate centre colours are rejected", "[scan_assembly]") {
    auto input = model_faces_of(Cube::solved());
    // Solver-Face input order puts U (Yellow centre) at index 0 and D
    // (White centre) at index 1; forcing U's centre to white gives two
    // White-centred faces - a physically impossible scan.
    input[0][4].color = FacetColor::white;

    const auto result = assemble_scanned_faces(input);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == cube::CubeError::invalid_cube_state);
}

TEST_CASE("a cross-face sticker swap that keeps colour counts is rejected", "[scan_assembly]") {
    // Simulates the one failure resolve_colors() can't catch: colours
    // resolved consistently (still exactly 9 stickers per colour) but with
    // two stickers' classes exchanged. Swapping a CORNER sticker on one
    // face with a differently coloured EDGE sticker on another guarantees
    // no rotation combination can be solvable: rotations map corner
    // positions to corner positions and edge positions to edge positions,
    // and after the swap one colour occupies five edge slots and three
    // corner slots - no real cube (4 of each per colour) matches that.
    std::mt19937 rng{42};
    const Cube original = scrambled_cube(rng, 25);
    auto captured = model_faces_of(original);

    constexpr std::array<std::size_t, 4> corner_indices = {0, 2, 6, 8};
    constexpr std::array<std::size_t, 4> edge_indices = {1, 3, 5, 7};
    bool swapped = false;
    for (std::size_t corner : corner_indices) {
        for (std::size_t edge : edge_indices) {
            if (captured[0][corner].color != captured[1][edge].color) {
                std::swap(captured[0][corner].color, captured[1][edge].color);
                swapped = true;
                break;
            }
        }
        if (swapped) {
            break;
        }
    }
    // A pair of differently coloured stickers must exist unless face 0's
    // corners and face 1's edges all share one colour - impossible here,
    // and guarded so a silent no-swap can't fake a pass.
    REQUIRE(swapped);

    const auto result = assemble_scanned_faces(captured);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == cube::CubeError::invalid_cube_state);
}
