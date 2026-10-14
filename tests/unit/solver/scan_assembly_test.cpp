#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <random>
#include <vector>

#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/scan_assembly.hpp>
#include "../deterministic_rng.hpp"

using cube::model::Facelet;
using cube::model::FacetColor;
using cube::solver::assemble_scanned_faces;
using cube::solver::assemble_scanned_faces_with_mapping;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;

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
        facelets.at(i) = Facelet{.color = to_model_color(colors.at(i)), .confidence = 1.0F};
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
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            rotated.at((3 * row) + col) = face.at((3 * (2 - col)) + row);
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
        for (std::size_t row = 0; row < 3; ++row) {
            std::swap(stickers.at(row * 3), stickers.at((row * 3) + 2));
        }
    }
    return stickers;
}

Cube scrambled_cube(std::mt19937& rng, int move_count) {
    std::uniform_int_distribution<std::size_t> face_dist{0, kAllFaces.size() - 1};
    std::uniform_int_distribution<int> turn_dist{1, 3};
    Cube c = Cube::solved();
    for (int i = 0; i < move_count; ++i) {
        c = c.apply(Move{.face = kAllFaces.at(face_dist(rng)), .turns = turn_dist(rng)});
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
        faces.at(static_cast<std::size_t>(face)) =
            to_model_face(outside_view(face, cube.face(face)));
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
            captured.at(static_cast<std::size_t>(face)) = stickers;
        }

        // Present the six faces in an arbitrary order: input slot i
        // receives the face whose solver-Face index is order[i].
        std::array<std::size_t, 6> order = {0, 1, 2, 3, 4, 5};
        std::shuffle(order.begin(), order.end(), rng);
        std::array<std::array<Facelet, 9>, 6> input{};
        for (std::size_t input_index = 0; input_index < input.size(); ++input_index) {
            input.at(input_index) = to_model_face(captured.at(order.at(input_index)));
        }

        const auto result = assemble_scanned_faces_with_mapping(input);
        REQUIRE(result.has_value());
        REQUIRE(result->cube == original);
        for (std::size_t input_index = 0; input_index < input.size(); ++input_index) {
            REQUIRE(result->input_index_of_face.at(order.at(input_index)) == input_index);
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
// couple of moves of solved"): one U quarter-turn away from solved, captured
// canonically. U and D are uniform (rotation-degenerate), and the four side
// faces - six own-colour stickers plus one displaced row each - admit a
// SECOND joint rotation assignment that builds a different but equally
// solvable cube (the displaced rows re-read as a D-layer turn). Two distinct
// solvable assemblies are rejected, never guessed.
TEST_CASE("faces that put together as two different solvable cubes are rejected",
          "[scan_assembly]") {
    const auto input = model_faces_of(Cube::solved().apply(Move{.face = Face::U, .turns = 1}));
    const auto result = assemble_scanned_faces(input);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error() == cube::CubeError::invalid_cube_state);
}

TEST_CASE("duplicate centre colours are rejected", "[scan_assembly]") {
    auto input = model_faces_of(Cube::solved());
    // Solver-Face input order puts U (Yellow centre) at index 0 and D
    // (White centre) at index 1; forcing U's centre to white gives two
    // White-centred faces - a physically impossible scan.
    input.at(0).at(4).color = FacetColor::white;

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
    std::mt19937 rng = ::cube::test::deterministic_rng(42U);
    const Cube original = scrambled_cube(rng, 25);
    auto captured = model_faces_of(original);

    constexpr std::array<std::size_t, 4> corner_indices = {0, 2, 6, 8};
    constexpr std::array<std::size_t, 4> edge_indices = {1, 3, 5, 7};
    bool swapped = false;
    for (std::size_t corner : corner_indices) {
        for (std::size_t edge : edge_indices) {
            if (captured.at(0).at(corner).color != captured.at(1).at(edge).color) {
                std::swap(captured.at(0).at(corner).color, captured.at(1).at(edge).color);
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

// ---------------------------------------------------------------------------
// The search skips combinations it can tell are no cube; what it finds is what
// trying every one finds. This is the search as it was - all 4,096 turns of the
// six faces, each built and asked - written out from the public cube, and held
// against the real one on a few hundred inputs that are cubes, almost cubes and
// nothing like them.
// ---------------------------------------------------------------------------

namespace {

struct Exhaustive {
    std::vector<Cube> cubes;  // every distinct solvable cube, in the order found
    std::array<std::size_t, 6> input_index_of_face{};
};

std::optional<Color> to_solver_colour(FacetColor colour) {
    switch (colour) {
        case FacetColor::white:
            return Color::White;
        case FacetColor::yellow:
            return Color::Yellow;
        case FacetColor::red:
            return Color::Red;
        case FacetColor::orange:
            return Color::Orange;
        case FacetColor::green:
            return Color::Green;
        case FacetColor::blue:
            return Color::Blue;
        case FacetColor::unknown:
            return std::nullopt;
    }
    return std::nullopt;
}

Face face_of_centre(Color centre) {
    switch (centre) {
        case Color::Yellow:
            return Face::U;
        case Color::White:
            return Face::D;
        case Color::Red:
            return Face::L;
        case Color::Orange:
            return Face::R;
        case Color::Green:
            return Face::F;
        case Color::Blue:
            return Face::B;
    }
    return Face::U;
}

Exhaustive exhaustive_assembly(const std::array<std::array<Facelet, 9>, 6>& input) {
    Exhaustive out;
    std::array<std::array<Color, 9>, 6> base{};
    std::array<bool, 6> taken{};
    for (std::size_t i = 0; i < input.size(); ++i) {
        std::array<Color, 9> converted{};
        for (std::size_t cell = 0; cell < 9; ++cell) {
            const auto colour = to_solver_colour(input.at(i).at(cell).color);
            if (!colour.has_value()) {
                return out;
            }
            converted.at(cell) = *colour;
        }
        const Face face = face_of_centre(converted.at(4));
        const auto at = static_cast<std::size_t>(face);
        if (taken.at(at)) {
            return out;
        }
        taken.at(at) = true;
        base.at(at) = outside_view(face, converted);  // the flip is its own inverse
        out.input_index_of_face.at(at) = i;
    }

    std::array<std::array<std::array<Color, 9>, 4>, 6> rotations{};
    for (std::size_t f = 0; f < 6; ++f) {
        rotations.at(f).at(0) = base.at(f);
        for (std::size_t q = 1; q < 4; ++q) {
            rotations.at(f).at(q) = rotated_clockwise(rotations.at(f).at(q - 1));
        }
    }
    for (unsigned combo = 0; combo < 4096; ++combo) {
        std::array<std::array<Color, 9>, 6> candidate{};
        unsigned digits = combo;
        for (std::size_t f = 0; f < 6; ++f) {
            candidate.at(f) = rotations.at(f).at(digits & 3U);
            digits >>= 2U;
        }
        const auto cube = Cube::from_facelets(candidate);
        if (!cube || !cube->is_solvable()) {
            continue;
        }
        if (std::ranges::find(out.cubes, *cube) == out.cubes.end()) {
            out.cubes.push_back(*cube);
        }
    }
    return out;
}

using Input = std::array<std::array<Facelet, 9>, 6>;

// A cube's own six faces, each at a turn of its own.
Input faces_of_cube(const Cube& cube, std::mt19937& rng) {
    std::uniform_int_distribution<int> any_turn{0, 3};
    Input faces = model_faces_of(cube);
    for (auto& face : faces) {
        std::array<Color, 9> stickers{};
        for (std::size_t cell = 0; cell < 9; ++cell) {
            stickers.at(cell) = to_solver_colour(face.at(cell).color).value_or(Color::White);
        }
        for (int turns = any_turn(rng); turns > 0; --turns) {
            stickers = rotated_clockwise(stickers);
        }
        face = to_model_face(stickers);
    }
    return faces;
}

// Two stickers traded on one face or across two: counts kept, pieces wrong.
Input with_stickers_traded(Input faces, std::mt19937& rng) {
    std::uniform_int_distribution<std::size_t> any_face{0, 5};
    std::uniform_int_distribution<std::size_t> any_cell{0, 8};
    std::swap(faces.at(any_face(rng)).at(any_cell(rng)).color,
              faces.at(any_face(rng)).at(any_cell(rng)).color);
    return faces;
}

// A sticker given a colour that is not its own: the counts are not nine.
Input with_a_sticker_wrong(Input faces, std::mt19937& rng) {
    std::uniform_int_distribution<std::size_t> any_face{0, 5};
    std::uniform_int_distribution<std::size_t> any_cell{0, 7};
    const std::size_t cell = any_cell(rng);
    faces.at(any_face(rng)).at(cell == 4 ? 8 : cell).color = faces.at(any_face(rng)).at(4).color;
    return faces;
}

// The names of two centres traded everywhere: what the scan tries when a
// naming makes no cube.
Input with_names_traded(Input faces, std::mt19937& rng) {
    std::uniform_int_distribution<std::size_t> any_face{0, 5};
    const FacetColor first = faces.at(any_face(rng)).at(4).color;
    const FacetColor second = faces.at(any_face(rng)).at(4).color;
    for (auto& face : faces) {
        for (Facelet& facelet : face) {
            if (facelet.color == first) {
                facelet.color = second;
            } else if (facelet.color == second) {
                facelet.color = first;
            }
        }
    }
    return faces;
}

// An edge flipped in place: every piece there, none solvable.
Cube with_edge_flipped(const Cube& cube) {
    std::array<std::array<Color, 9>, 6> facelets{};
    for (Face face : kAllFaces) {
        facelets.at(static_cast<std::size_t>(face)) = cube.face(face);
    }
    const Cube::EdgeLocation at = cube.locate_edge(Color::White, Color::Green);
    std::swap(
        facelets.at(static_cast<std::size_t>(at.face_a)).at(static_cast<std::size_t>(at.index_a)),
        facelets.at(static_cast<std::size_t>(at.face_b)).at(static_cast<std::size_t>(at.index_b)));
    const auto flipped = Cube::from_facelets(facelets);
    return flipped.value_or(cube);
}

// The stickers that are not centres, shuffled among themselves: nothing like a
// cube, with nine of each colour.
Input shuffled_stickers(std::mt19937& rng) {
    Input faces = model_faces_of(Cube::solved());
    std::vector<FacetColor> colours;
    for (const auto& face : faces) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            if (cell != 4) {
                colours.push_back(face.at(cell).color);
            }
        }
    }
    std::shuffle(colours.begin(), colours.end(), rng);
    std::size_t next = 0;
    for (auto& face : faces) {
        for (std::size_t cell = 0; cell < 9; ++cell) {
            if (cell != 4) {
                face.at(cell).color = colours.at(next++);
            }
        }
    }
    return faces;
}

// Every kind of input the search is given, one after another from a seed.
std::vector<Input> varied_inputs() {
    std::mt19937 rng = ::cube::test::deterministic_rng(2026U);
    std::vector<Input> inputs;
    inputs.reserve(400);
    for (int i = 0; i < 80; ++i) {
        inputs.push_back(faces_of_cube(scrambled_cube(rng, i < 20 ? (i % 3) + 1 : 25), rng));
    }
    inputs.push_back(faces_of_cube(Cube::solved(), rng));
    for (int i = 0; i < 80; ++i) {
        inputs.push_back(with_stickers_traded(faces_of_cube(scrambled_cube(rng, 25), rng), rng));
    }
    for (int i = 0; i < 40; ++i) {
        inputs.push_back(with_a_sticker_wrong(faces_of_cube(scrambled_cube(rng, 25), rng), rng));
    }
    for (int i = 0; i < 80; ++i) {
        inputs.push_back(with_names_traded(faces_of_cube(scrambled_cube(rng, 25), rng), rng));
    }
    for (int i = 0; i < 20; ++i) {
        inputs.push_back(faces_of_cube(with_edge_flipped(scrambled_cube(rng, 25)), rng));
    }
    for (int i = 0; i < 40; ++i) {
        inputs.push_back(shuffled_stickers(rng));
    }
    // Two centres alike, and an unknown colour.
    Input alike = faces_of_cube(scrambled_cube(rng, 25), rng);
    alike.at(1).at(4).color = alike.at(0).at(4).color;
    inputs.push_back(alike);
    Input unknown = faces_of_cube(scrambled_cube(rng, 25), rng);
    unknown.at(2).at(1).color = FacetColor::unknown;
    inputs.push_back(unknown);
    return inputs;
}

}  // namespace

TEST_CASE("the search that skips what is no cube finds what trying every combination finds",
          "[scan_assembly]") {
    std::size_t made_a_cube = 0;
    std::size_t made_none = 0;
    for (const Input& input : varied_inputs()) {
        const Exhaustive reference = exhaustive_assembly(input);

        // Every limit: the cubes the reference found, as many of them as asked
        // for and in its order.
        for (const std::size_t limit :
             {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{16}, std::size_t{257}}) {
            std::vector<Cube> expected = reference.cubes;
            if (expected.size() > limit) {
                expected.resize(limit);
            }
            REQUIRE(cube::solver::assemble_all_scanned_faces(input, limit) == expected);
        }

        const auto result = assemble_scanned_faces_with_mapping(input);
        if (reference.cubes.size() == 1) {
            REQUIRE(result.has_value());
            REQUIRE(result->cube == reference.cubes.front());
            REQUIRE(result->input_index_of_face == reference.input_index_of_face);
            ++made_a_cube;
        } else {
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == cube::CubeError::invalid_cube_state);
            made_none += reference.cubes.empty() ? 1U : 0U;
        }
    }
    // The inputs were of every kind, or the test says little.
    CHECK(made_a_cube > 40);
    CHECK(made_none > 100);
}
