#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <vector>

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
using cube::solver::rotation_hint_for_tracked_scan;
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
        facelets[i] = Facelet{.color = to_model_color(colors[i]), .confidence = 1.0F};
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
            rotated[static_cast<std::size_t>((3 * row) + col)] =
                face[static_cast<std::size_t>((3 * (2 - col)) + row)];
        }
    }
    return rotated;
}

// A capture as an outside viewer - a camera pointed at the face - sees it.
// The solver's cross-unfolded layout is camera-handed on U, F, L and
// MIRROR-handed on D, R, B (assembly reverses this exact flip after
// pinning each input's face by centre), so the outside view of those three
// faces is the column-flipped solver array.
std::array<Color, 9> outside_view(Face face, std::array<Color, 9> stickers) {
    if (face == Face::D || face == Face::R || face == Face::B) {
        for (int row = 0; row < 3; ++row) {
            std::swap(stickers[static_cast<std::size_t>(row * 3)],
                      stickers[static_cast<std::size_t>((row * 3) + 2)]);
        }
    }
    return stickers;
}

Cube scrambled_cube(std::mt19937& rng, int move_count) {
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};
    Cube c = Cube::solved();
    for (int i = 0; i < move_count; ++i) {
        c = c.apply(Move{.face = kAllFaces[face_dist(rng)], .turns = turn_dist(rng)});
    }
    return c;
}

// The cube's six faces as model facelets in solver-Face input order with no
// extra rotation, in the assembler's outside-view input convention - the
// "camera happened to capture everything canonically" baseline the
// free-form tests then perturb.
std::array<std::array<Facelet, 9>, 6> model_faces_of(const Cube& cube) {
    std::array<std::array<Facelet, 9>, 6> faces{};
    for (Face face : kAllFaces) {
        faces[static_cast<std::size_t>(face)] = to_model_face(outside_view(face, cube.face(face)));
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

        // Capture each face as the camera might: the outside view, rotated
        // by an arbitrary number of quarter turns, independently per face.
        std::uniform_int_distribution<int> rotation_dist{0, 3};
        std::array<std::array<Color, 9>, 6> captured{};
        for (Face face : kAllFaces) {
            std::array<Color, 9> stickers = outside_view(face, original.face(face));
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

TEST_CASE("a solved cube's uniform faces assemble despite degenerate rotations",
          "[scan_assembly]") {
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
    return model_faces_of(Cube::solved().apply(Move{.face = Face::U, .turns = 1}));
}
}  // namespace

TEST_CASE("a rotation hint lifts near-solved assembly ambiguity", "[scan_assembly]") {
    const Cube original = Cube::solved().apply(Move{.face = Face::U, .turns = 1});
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
    for (std::size_t face_index = 0; face_index < hinted->input_index_of_face.size();
         ++face_index) {
        REQUIRE(hinted->input_index_of_face[face_index] == face_index);
    }
}

TEST_CASE("a rotation hint can select the rival survivor instead", "[scan_assembly]") {
    const Cube original = Cube::solved().apply(Move{.face = Face::U, .turns = 1});
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

TEST_CASE("a hint that cannot separate the survivors leaves the ambiguity standing",
          "[scan_assembly]") {
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

namespace {

// The 24 proper rotations of the cube as signed axis permutations,
// column[a] = the image of body axis +e_a in the solver frame. Generated
// from all 6 axis permutations x 8 sign choices, keeping determinant +1.
std::vector<std::array<std::array<int, 3>, 3>> all_body_orientations() {
    std::vector<std::array<std::array<int, 3>, 3>> orientations;
    std::array<int, 3> perm = {0, 1, 2};
    do {
        for (int signs = 0; signs < 8; ++signs) {
            std::array<std::array<int, 3>, 3> column{};
            for (int a = 0; a < 3; ++a) {
                column[static_cast<std::size_t>(a)]
                      [static_cast<std::size_t>(perm[static_cast<std::size_t>(a)])] =
                          ((signs >> a) & 1) != 0 ? -1 : 1;
            }
            const auto& cx = column[0];
            const auto& cy = column[1];
            const auto& cz = column[2];
            const int det = (cx[0] * ((cy[1] * cz[2]) - (cy[2] * cz[1]))) -
                            (cy[0] * ((cx[1] * cz[2]) - (cx[2] * cz[1]))) +
                            (cz[0] * ((cx[1] * cy[2]) - (cx[2] * cy[1])));
            if (det == 1) {
                orientations.push_back(column);
            }
        }
    } while (std::ranges::next_permutation(perm).found);
    return orientations;
}

Face face_of_direction(const std::array<int, 3>& d) {
    if (d[0] == 1) {
        return Face::R;
    }
    if (d[0] == -1) {
        return Face::L;
    }
    if (d[1] == 1) {
        return Face::U;
    }
    if (d[1] == -1) {
        return Face::D;
    }
    return d[2] == 1 ? Face::F : Face::B;
}

// The facelet layout documented in cube.hpp, restated here from that spec
// (not lifted from any implementation) so a mis-derived producer cannot
// agree with a mis-derived test.
int solver_index_of(Face face, const std::array<int, 3>& q) {
    switch (face) {
        case Face::U:
        case Face::D:
            return ((q[2] + 1) * 3) + (q[0] + 1);
        case Face::R:
        case Face::L:
            return ((1 - q[1]) * 3) + (q[2] + 1);
        case Face::F:
        case Face::B:
            return ((1 - q[1]) * 3) + (q[0] + 1);
    }
    return 0;
}

}  // namespace

TEST_CASE("the tracked-scan hint producer derives digits that re-orient every capture",
          "[scan_assembly]") {
    // For each of the 24 ways the tracked body can sit in the solver frame,
    // sample a scrambled cube through the body-frame cell convention
    // (face_projection.cpp's: col along +axis(a+1), row along +axis(a+2)),
    // then require the produced digit to rotate each capture onto the
    // solver facelet layout exactly, and hinted assembly to round-trip.
    std::mt19937 rng{20260804};
    const Cube cube = scrambled_cube(rng, 30);
    for (const auto& column : all_body_orientations()) {
        std::array<std::array<Facelet, 9>, 6> captured{};
        std::array<std::array<Color, 9>, 6> captured_colors{};
        std::array<int, 6> body_ids{};
        std::array<Face, 6> shown{};
        for (int id = 0; id < 6; ++id) {
            const int body_axis = id / 2;
            const int sign = (id % 2 == 0) ? 1 : -1;
            std::array<int, 3> outward{};
            for (int k = 0; k < 3; ++k) {
                outward[static_cast<std::size_t>(k)] =
                    sign * column[static_cast<std::size_t>(body_axis)][static_cast<std::size_t>(k)];
            }
            const Face face = face_of_direction(outward);
            // face_projection.cpp's outside-view axes: cyclic pair on
            // negative body faces, swapped on positive ones.
            const int axis_u = (sign > 0) ? (body_axis + 2) % 3 : (body_axis + 1) % 3;
            const int axis_v = (sign > 0) ? (body_axis + 1) % 3 : (body_axis + 2) % 3;
            std::array<Color, 9> colors{};
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    std::array<int, 3> p{};
                    p[static_cast<std::size_t>(body_axis)] = sign;
                    p[static_cast<std::size_t>(axis_u)] = col - 1;
                    p[static_cast<std::size_t>(axis_v)] = row - 1;
                    std::array<int, 3> q{};
                    for (int k = 0; k < 3; ++k) {
                        q[static_cast<std::size_t>(k)] =
                            (column[0][static_cast<std::size_t>(k)] * p[0]) +
                            (column[1][static_cast<std::size_t>(k)] * p[1]) +
                            (column[2][static_cast<std::size_t>(k)] * p[2]);
                    }
                    colors[static_cast<std::size_t>((row * 3) + col)] =
                        cube.face(face)[static_cast<std::size_t>(solver_index_of(face, q))];
                }
            }
            captured_colors[static_cast<std::size_t>(id)] = colors;
            captured[static_cast<std::size_t>(id)] = to_model_face(colors);
            body_ids[static_cast<std::size_t>(id)] = id;
            shown[static_cast<std::size_t>(id)] = face;
        }

        const RotationHint hint = rotation_hint_for_tracked_scan(captured, body_ids);
        for (std::size_t i = 0; i < captured.size(); ++i) {
            REQUIRE(hint.quarter_turns[i] >= 0);
            // Digits are defined over the search's arrays, i.e. AFTER
            // assembly's handedness flip on D/R/B - outside_view applies
            // that same self-inverse flip.
            std::array<Color, 9> reoriented = outside_view(shown[i], captured_colors[i]);
            for (int k = 0; k < hint.quarter_turns[i]; ++k) {
                reoriented = rotated_clockwise(reoriented);
            }
            REQUIRE(reoriented == cube.face(shown[i]));
        }

        const auto assembled = assemble_scanned_faces_with_mapping(captured, &hint);
        REQUIRE(assembled.has_value());
        REQUIRE(assembled->cube == cube);
    }
}

TEST_CASE("the hint producer degrades to no advice on inputs that cannot name a rotation",
          "[scan_assembly]") {
    std::mt19937 rng{20260804};
    const Cube cube = scrambled_cube(rng, 30);
    const auto faces = model_faces_of(cube);  // input order = Face enum: U,D,L,R,F,B
    // Body ids that make the body frame coincide with the solver frame:
    // +x shows R, -x L, +y U, -y D, +z F, -z B.
    const std::array<int, 6> identity_ids = {2, 3, 1, 0, 4, 5};

    const RotationHint good = rotation_hint_for_tracked_scan(faces, identity_ids);
    for (const int digit : good.quarter_turns) {
        REQUIRE(digit != RotationHint::kNoHint);
    }

    SECTION("a repeated body id is not a bijection") {
        std::array<int, 6> repeated = identity_ids;
        repeated[5] = repeated[0];
        const RotationHint hint = rotation_hint_for_tracked_scan(faces, repeated);
        for (const int digit : hint.quarter_turns) {
            REQUIRE(digit == RotationHint::kNoHint);
        }
    }

    SECTION("swapping which body axis shows L and R composes a reflection") {
        // Opposite-face ids swapped: both centre correspondences still
        // agree on the x column's DIRECTION (-x), so the pair check
        // passes - but the columns then form det -1, a mirror.
        std::array<int, 6> mirrored = identity_ids;
        std::swap(mirrored[2], mirrored[3]);
        const RotationHint hint = rotation_hint_for_tracked_scan(faces, mirrored);
        for (const int digit : hint.quarter_turns) {
            REQUIRE(digit == RotationHint::kNoHint);
        }
    }

    SECTION("an unknown centre colour cannot pin a cube position") {
        auto unknown_centre = faces;
        unknown_centre[0][4].color = FacetColor::unknown;
        const RotationHint hint = rotation_hint_for_tracked_scan(unknown_centre, identity_ids);
        for (const int digit : hint.quarter_turns) {
            REQUIRE(digit == RotationHint::kNoHint);
        }
    }
}
