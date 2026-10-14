#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cube/solver/cross.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>

#include "piece_search.hpp"  // private header - see tests/CMakeLists.txt for why it's reachable here

using cube::solver::canonical_color;
using cube::solver::Color;
using cube::solver::cross_solved;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::parse_algorithm;
using cube::solver::random_scramble;
using cube::solver::solve;
using cube::solver::solve_cross;
namespace detail = cube::solver::detail;

namespace {

Cube after(Cube cube, const std::vector<Move>& moves) {
    for (const Move& move : moves) {
        cube = cube.apply(move);
    }
    return cube;
}

// The DFL slot as corners.cpp has it: white on D, and the corner's other two
// stickers matching their centres.
bool dfl_solved(const Cube& cube) {
    return cube.at(Face::D, 6) == Color::White && cube.at(Face::F, 6) == canonical_color(Face::F) &&
           cube.at(Face::L, 8) == canonical_color(Face::L);
}

}  // namespace

TEST_CASE("facelet_map is the table apply() runs on", "[piece_search][facelet_map]") {
    for (unsigned seed = 0; seed < 5; ++seed) {
        CAPTURE(seed);
        const Cube cube = random_scramble(seed, 25);
        for (const Face face : kAllFaces) {
            for (int turns = 1; turns <= 3; ++turns) {
                CAPTURE(face, turns);
                const Move move{.face = face, .turns = turns};
                const Cube turned = cube.apply(move);
                const Cube::FaceletMap& map = Cube::facelet_map(move);
                for (std::size_t from = 0; from < 54; ++from) {
                    const auto source_face = static_cast<Face>(from / 9);
                    const auto target = static_cast<std::size_t>(map.at(from));
                    REQUIRE(target < 54);
                    const auto target_face = static_cast<Face>(target / 9);
                    REQUIRE(turned.at(target_face, static_cast<int>(target % 9)) ==
                            cube.at(source_face, static_cast<int>(from % 9)));
                }
            }
        }
    }
}

TEST_CASE("facelet_map leaves a malformed move's cube alone", "[piece_search][facelet_map]") {
    const Cube::FaceletMap& map = Cube::facelet_map(Move{.face = Face::U, .turns = 0});
    for (std::size_t i = 0; i < map.size(); ++i) {
        REQUIRE(map.at(i) == i);
    }
}

TEST_CASE("the piece search returns what the reference search returns", "[piece_search][slow]") {
    const std::vector<Face> all(kAllFaces.begin(), kAllFaces.end());

    for (unsigned seed = 0; seed < 60; ++seed) {
        CAPTURE(seed);
        const Cube scrambled = random_scramble(seed, 25);

        // The cross: four edges, every face.
        const auto cross_key = [](const Cube& c) {
            return detail::SignatureKey{detail::cross_key(c), 0};
        };
        const detail::PieceGoal cross_goal{.rest_codes = 0, .keys = {cross_key(Cube::solved())}};
        const auto cross_reference = detail::bfs_search(scrambled, all, cross_solved, cross_key, 8);
        const auto cross_fast =
            detail::search(scrambled, all, cross_solved, cross_key, 8, cross_goal);
        REQUIRE(cross_reference.has_value());
        REQUIRE(cross_fast == cross_reference);

        // The first corner (DFL): the cross and one more piece, four faces.
        const Cube crossed = after(scrambled, *cross_reference);
        const auto corner_key = [](const Cube& c) {
            return detail::SignatureKey{
                detail::cross_key(c),
                detail::piece_code_corner(c, Color::White, canonical_color(Face::F),
                                          canonical_color(Face::L))};
        };
        const auto corner_done = [](const Cube& c) { return cross_solved(c) && dfl_solved(c); };
        const detail::PieceGoal corner_goal{.rest_codes = 1, .keys = {corner_key(Cube::solved())}};
        const std::vector<Face> corner_faces = {Face::U, Face::D, Face::F, Face::L};
        const auto corner_reference =
            detail::bfs_search(crossed, corner_faces, corner_done, corner_key, 10);
        const auto corner_fast =
            detail::search(crossed, corner_faces, corner_done, corner_key, 10, corner_goal);
        REQUIRE(corner_reference.has_value());
        REQUIRE(corner_fast == corner_reference);
    }
}

TEST_CASE("the piece search gives up where the reference does, on the same budget",
          "[piece_search]") {
    // A budget too small for the search: both fail the same way.
    const Cube scrambled = random_scramble(3, 25);
    const std::vector<Face> all(kAllFaces.begin(), kAllFaces.end());
    const auto key = [](const Cube& c) { return detail::SignatureKey{detail::cross_key(c), 0}; };
    const detail::PieceGoal goal{.rest_codes = 0, .keys = {key(Cube::solved())}};

    const auto reference = detail::bfs_search(scrambled, all, cross_solved, key, 8, 50);
    const auto fast = detail::search(scrambled, all, cross_solved, key, 8, goal, 50);
    REQUIRE_FALSE(reference.has_value());
    REQUIRE(reference.error() == cube::CubeError::no_solution);
    REQUIRE(fast == reference);
}

TEST_CASE("a cube that is not a real cube keeps the reference search", "[piece_search]") {
    // One corner twisted in place: every colour count and every cubie is
    // possible, so it is physically valid, but no sequence of turns makes it.
    // The cross does not look at corners, so it can still be built - by the
    // search that does not assume each piece is present once.
    std::array<std::array<Color, 9>, 6> facelets{};
    const Cube solved = Cube::solved();
    for (const Face face : kAllFaces) {
        facelets.at(static_cast<std::size_t>(face)) = solved.face(face);
    }
    // The URF corner: U[8], R[2] and F[2] show Yellow, Orange and Green.
    facelets.at(static_cast<std::size_t>(Face::U)).at(8) = Color::Orange;
    facelets.at(static_cast<std::size_t>(Face::R)).at(2) = Color::Green;
    facelets.at(static_cast<std::size_t>(Face::F)).at(2) = Color::Yellow;
    const auto twisted = Cube::from_facelets(facelets);
    REQUIRE(twisted.has_value());
    REQUIRE_FALSE(twisted->is_solvable());

    for (unsigned seed = 0; seed < 10; ++seed) {
        CAPTURE(seed);
        Cube cube = *twisted;
        for (unsigned i = 0; i < 20; ++i) {
            cube = cube.apply(Move{.face = kAllFaces.at((seed + i * 5) % 6),
                                   .turns = static_cast<int>(1 + ((seed + i) % 3))});
        }
        REQUIRE_FALSE(cube.is_solvable());

        const auto solution = solve_cross(cube);
        REQUIRE(solution.has_value());
        REQUIRE(cross_solved(after(cube, *solution)));
    }
}

TEST_CASE("the solutions are the ones the facelet search found", "[piece_search][solve][slow]") {
    // Golden: the digest of each solution (stages and moves, FNV-1a, exactly
    // what `cube_solve_bench --list` prints) from before the first-two-layers
    // search followed pieces instead of cubes. A faster search that is the
    // same search leaves every one of these alone; a change that is meant to
    // alter the solutions regenerates them with that tool.
    //
    // The scrambles are pinned as notation, not drawn: random_scramble goes
    // through std::uniform_int_distribution, whose algorithm is each standard
    // library's own, so its seed 0 is a different cube on libc++ than on
    // libstdc++ and MSVC (which agree). These are what seeds 0 to 39 (25 moves)
    // and 1000 to 1019 (100 moves) draw there, and the digests below are of
    // their solutions.
    constexpr std::array<std::uint64_t, 40> kGolden25 = {
        0xe7e50e835c223f0bULL, 0xc66da365898a5de4ULL, 0x67acf3cbfc9e730fULL, 0x722ede7ced94e4caULL,
        0xe7c8523a809ad49fULL, 0xe267a67373e23cdbULL, 0x8c93d402cb72388cULL, 0xf0e628fc4116882cULL,
        0xf79dd55c0ab19990ULL, 0x8560c3688a22ace1ULL, 0xd046f19838a51dccULL, 0xa8f8294ad87e56daULL,
        0x766e2db470d75e90ULL, 0xb9c01fccfcb4a804ULL, 0x8e4267b9412768b1ULL, 0xefb3665f5d982065ULL,
        0x02f6f4ffb41dcd92ULL, 0xd735bf002d38c1c8ULL, 0xedafc4388d3c9ef6ULL, 0x9f1ed9c74a5a4f59ULL,
        0xe1426994336538d9ULL, 0x51d255772a718470ULL, 0x506e6dc33d12a1e0ULL, 0x4347dfa5b54141fbULL,
        0xd0705c650aeb5cc8ULL, 0x3b727d479a0bbbc3ULL, 0xfceb5b2cb477b2a0ULL, 0x032a339c84c8020bULL,
        0xac619a7d6f1005dbULL, 0xa97e3916069f687aULL, 0x1817c1b4a5844483ULL, 0x6379f0d39b24c380ULL,
        0x510ead9b275c5581ULL, 0x9dd9fe591a3a3565ULL, 0x7a20c03e6a7251cfULL, 0x7db362ec95ebaaacULL,
        0x743c5f88cb8eb80aULL, 0xb95af6ab9e23c6fbULL, 0x6ec0fa7142b43d60ULL, 0x1e3be3dd0775407bULL,
    };
    // Seeds 1000 up, scrambled by 100 moves.
    constexpr std::array<std::uint64_t, 20> kGolden100 = {
        0x38442b7099655ab4ULL, 0xb48c560992dffec8ULL, 0x1b42089b2f97fb61ULL, 0xb53a6207d7334d39ULL,
        0x9c4487660c3d1c85ULL, 0x681136bc4acb7097ULL, 0x3e0376fcf26e230fULL, 0x169befc82d4ce170ULL,
        0x69235948515cd731ULL, 0x410fee141f0e999eULL, 0x80aee10dd284548dULL, 0x2ec8fcfcdc5ed37eULL,
        0x9f6a828c90cc680eULL, 0x9314ff96b888bb35ULL, 0x1561bda1b98776b9ULL, 0xb826940476df0c7eULL,
        0x91527adb260d37c2ULL, 0x55881de1fd5a5094ULL, 0xa53730271f0a3d8cULL, 0x73f0cc914bf7f8f0ULL,
    };

    const std::array<std::string_view, 40> kScrambles25 = {
        "R2 F' R' R' L2 R2 L B B L2 F' R2 R2 B' U2 U2 U2 F' F B' B2 F' L2 F' U'",
        "L' F' U D' U U2 D2 L' L' R' L F2 D2 B U2 F' L2 R2 U' D' F' B' D F2 B'",
        "L U' R' L2 L D D' R D2 D2 R' R' U2 R2 D' F B2 L2 B U' R U L U U2",
        "R F' D R2 B2 B U D U L' U L2 R D2 F2 R' U' R' D2 L2 D F2 L U2 R'",
        "B' R B' F2 F2 D B U' D' L2 F D2 B2 B2 U2 R' U2 L U2 B' L B F' B2 D2",
        "D B' D2 B' L R2 F2 R2 D' D' U2 F' L U' B D L D' R2 R2 R' D D D' D'",
        "B' D F U2 U' R R2 L2 L2 R' L' F2 R R R' B' F2 L' B' F2 U' F F2 F2 F2",
        "U F L' F2 B R R U2 D L2 F' F L2 U' D' B' D2 L' B U' R B' D R' B'",
        "B B B2 R' D2 U2 L' L2 R' L' R R' F2 F' R L2 D2 B' L D U B' U D U2",
        "U2 R2 L U U D' L D U2 L2 D B2 B' U' F' R B F' R2 F' L2 F2 F2 L' B2",
        "F U2 R2 F' L2 D D' F D U F' B' U2 R' F R F' D' B2 F R2 U' L2 F L'",
        "D U2 L F' L L U' L' B B' F' U2 B' B2 U' R' U' U' D' U F' F2 L D U'",
        "U2 F' D R' U2 B B U B' U2 D' R' B2 B' U R R' L' F' U2 F2 U' U' U D'",
        "F2 D' F' B B' L' R2 F' R2 F2 U2 D' U' B2 L2 F2 D2 L' U2 L B' D' D2 B2 U2",
        "R' F2 B2 U D B R D2 R2 D' F2 L R' U F D B2 L' F F' B U D' L' F2",
        "B' D2 U L' D' R D' D' U D' B D2 F2 B' F2 D' D' U' U D2 B' L F L' R",
        "D R R U L' D F2 U' U' B' R U2 F2 U' D2 D' F2 L D L' U2 B R L D'",
        "D R' D' U' F2 R R R2 U' L B' U2 B B U2 R R R L2 D D2 R2 L F L2",
        "R R2 B D' B F R2 B2 D' U' R B2 F2 U U2 D F B2 D2 R2 F' L2 F' F' D",
        "U2 F2 D' U D' U F F B' R2 D' R2 R D' U L2 U' R2 L F2 L2 B2 F2 R' R",
        "R' B' B' F2 U2 F' L' R R' D' D' F' F2 B' F U' U F2 D2 D B B' R' D F2",
        "U' D' F U' D' U D' R2 D' R U2 B U' D2 L2 B' F B2 F2 L L2 F' D' B' B",
        "D' L' L2 B' D' L' D F D2 F' U R F F' D2 U F2 B2 F D' F' F' L R L'",
        "R' B F' D2 D2 F D L R' L2 U' B' B D' R2 B' B' U2 D2 D2 F2 R' U' U' B",
        "B F B D' L2 F' B2 D2 U L2 D L2 F2 B' R' D F2 R2 R' B2 B2 D2 R2 F2 B2",
        "B' R2 D D L U F2 L R L' L' U L2 R2 U2 R2 D2 F L' B L R' L2 B2 R'",
        "D' R F2 F B D D' L' F2 D2 D2 R D F2 D' D2 B B R' L' R2 R2 B2 U' B2",
        "L2 F' F2 B2 L B' B' D2 F2 R2 B2 B' F B2 D D L F' U D' U D F2 U2 U",
        "F R U L2 F R D2 B' B' B2 D' B' D' U' B' B2 D2 U2 L R2 D' L' F' R2 F",
        "B' D2 U' F2 L R F' B' F' F' D F U2 L2 R D D F' B F2 U B2 F' B2 L'",
        "R' L R2 U2 B' L2 B' D' R L2 U R R' F B2 U D B2 D D' R' F F U B2",
        "D B F2 B2 D2 U B' U' U' R U' L2 F L U' D' L U D2 B2 D D2 L2 F R'",
        "B L2 R B' F' F2 U2 B R' R' U L R' L' R' F' R U2 R2 F' B2 U' B2 L2 L",
        "D' L' L' D B' D2 U' B' F L' B L' U2 L U' B' B L L2 L' F' D R U2 D2",
        "U F2 U2 R2 U B' L2 D2 D' L' U' R' B' R2 F2 F' D' B2 D2 U2 F L' R2 L2 B2",
        "L2 D' D D F' U2 R2 D F' F' F' U B' D' D' B2 D' B F2 D' U2 R' L' U2 L2",
        "F R' B' U' F L' D2 D2 R' U' B' B R2 D' D2 U' D2 L2 L2 D' L D2 F L2 R'",
        "B L2 D' R R' F U2 F2 D F F2 R2 L B2 B' D2 R R' F' B' U U' U L' F2",
        "L' B' B' F2 R' R2 D L D' U' D' L2 L' B2 R2 R' L F R2 F2 B2 B D R R'",
        "R F' F2 U' R2 R2 L L' R B' F2 B B2 L' F2 B R2 D2 R2 B' U B' F D L'",
    };
    const std::array<std::string_view, 20> kScrambles100 = {
        "R U2 B L2 B' D U2 L D' B2 D' F' L D F' U' B B' B L2 U2 B' L F2 L' "
        "U' B R2 F' R B B2 U' U' D' U F L' B D' L U2 F' L U' B' B U2 U' L2 "
        "R D2 F' D R R' L U' D' U2 D D R R2 L D R B' B' U D2 L F2 F' L "
        "L2 F2 R' B2 U' L' B2 F2 B D' F' D' R' D2 L' F D2 R' L2 B' R2 R' L R U2",
        "D2 D2 D L2 U2 D' L D2 R2 R' B2 U L' B2 U' L' L' U2 B U' F2 B2 L R' D' "
        "B' F2 F2 D F' D' R F2 L2 U2 B' F L L B U L' R' U' L L2 D R2 L2 B "
        "D' L D2 L R' F F F' F' R2 D2 F2 R L2 D2 U2 U2 F2 R' B D2 B2 U' L2 B2 "
        "F F2 R2 D' F' R R2 B F' B B' U F2 F2 B B2 F' D' B' F2 D F' R F B",
        "U L L R' D L' U2 R F' U' L2 R R' F2 U2 L2 U U2 L' U' F' L D2 D2 B' "
        "D2 L' L' U' B R B2 U' F' L' L' D2 D U R' L B2 D' L B D R' L L' D "
        "U F' B' F' F' L' B' F F2 D2 L' F2 F2 L2 U B' R' L2 B B2 B B2 L2 R D2 "
        "B F' B2 L' L' R' D2 F' L L2 U R' L2 U' D R' D2 R2 F' D R' B2 D2 B2 D2",
        "R L' U R' B2 R U2 B' F' D2 F2 L' D' L D2 L2 L2 D2 D D2 L R' L2 D2 L2 "
        "L2 D2 R' D' B2 F' B' D' L2 R2 L' B D L' D' L D2 L' F' L' U2 F2 L R D' "
        "U2 D2 U R B F' F B2 F L2 B R F F2 L2 U' L' D' L' B D' L' L2 D' B "
        "B B' B' B D' R2 R2 U2 L U2 U U U' D B2 B2 B B D2 F2 B F' B2 U F2",
        "U2 B2 F B' R2 U D' D2 R R U L R B' L2 F' U B' R2 F L U D R2 D' "
        "D2 D B' F L' F' B R D D L' F2 F2 R' R2 L' R' F' F' D U' L D' F2 D "
        "R L2 R2 R R' F' L' U U2 L' B' F L' D D2 F U U' L2 B U F D2 U B "
        "B2 R2 U2 R' B2 U2 L' D2 R' D2 L' L' U2 L R' R D' F U R' L' B2 F' F2 U'",
        "R U L2 B D D' D2 U' F' L' L' B2 R L2 F' L' D' L2 R2 B2 D' L' F2 F U' "
        "B R' R U' F2 R F R' L L' U' U2 D U B' F F2 L B R2 L2 D' F2 L' B2 "
        "B2 U' B2 L U' F U' B' D2 U U2 F' L2 F R L2 U U2 U' D' F2 B2 R' F L2 "
        "R2 B L' U D2 L' D2 D D2 R2 L R2 R2 F' R' L' D2 B' B L2 B2 L2 B2 L2 F2",
        "F D' R' B F2 F2 D' R' D' F2 B' D D' L D2 D' B2 B F' L R2 U' L' R' L "
        "L L2 D2 B2 F2 D2 L' F' B U' B D R' B D' L R' D' D R' R D U U L' "
        "F F2 D L2 F2 D' U2 B' U R U2 R2 B2 U F2 L2 F' U U F2 U B F2 F B "
        "R D2 U' U2 U' D2 D' L2 R' F D B' D' U U R B' U' F' L' L R2 B' U' R2",
        "R' F U2 F R D F' R2 F2 R F2 D2 D R2 L B2 F' U D' L F F F2 U' B "
        "B2 F' D L' L' B D' F2 L2 D' D2 F U2 R U2 B2 B L' D2 D2 B' F2 B R U "
        "D D L B F D' D2 R F' L F B' R R2 L' U' R U L F F' R2 U U R' "
        "R L R F B2 D R B R2 R' B2 F2 D2 B2 L2 L2 U' B' R' R2 U L D2 B2 F2",
        "R' B' D2 R L' U' F' B2 R2 D2 B2 L2 R' L U2 D' B' F2 U L2 D' F' L D' U2 "
        "B' F R U B U2 R' U' R L2 R' F' B' R' B R2 B R' L U B B' D' F B' "
        "R2 F L' U2 L R2 D2 U' U L2 L B2 D2 F2 B' D2 L L B2 B' B F2 L' L' D "
        "D D' U2 L2 D' L B2 L2 U R2 L2 U2 B2 L2 D U2 D' F' D' D' F' B D2 D B'",
        "R U D' D' F' R2 L' B2 L R L F2 U D' U' R F2 U2 U' B2 R D L' F2 R' "
        "U F R' D2 D' U B' F U' U B' R' B' R' D U D2 D D U' L' B' F' B2 B' "
        "D F' L' D' D2 L D2 F' D2 U' B2 U R2 D' L2 U' L' R2 L2 L L D' B U2 B' "
        "B' R U L2 L F' D2 L F2 F' D' U2 B2 U B L2 R2 U' D F D R2 L2 R' U'",
        "L' D2 U2 D L' L2 F R2 L2 R2 D L2 F2 D F2 U L R2 R D2 D2 B D2 F2 L "
        "B2 R L L D' F2 B L2 U B F' U2 F D F L D2 L' B' L2 D F D2 D' L' "
        "B L2 U2 U' R2 L D' R' F' R U2 L2 D U U' B' D U U B F2 R2 D2 L F' "
        "B L' U' L' U L2 R2 B U' F2 R B' U D B F D' F' R2 U2 R' R2 U2 U2 U2",
        "R L R' U2 F' L R F' B F2 U2 L2 U R' L2 F' U U' F' L' D' F2 F L U "
        "D U2 U2 U2 U2 U R D B L2 U B B B L2 R' L' B D2 U' D2 F2 F' B2 D' "
        "D' D2 U2 B2 B' U2 U U' F' B L2 F D2 U2 B' D2 F U2 U' U2 R U2 U2 B' F "
        "B2 R2 B2 L' U2 L D R' L' R2 U2 F' R2 B L D F L F' D R2 R2 L D2 F2",
        "U F D' F D' R R2 R' L2 D2 R' L' F' L' R2 D L D' L2 R' B' D B' U' U "
        "R B' F F B F' B B' D R' F' U2 L B' L' U' D2 D R U2 B' B' F2 U U "
        "L2 L2 U' F U' R2 U2 F' B2 U U' L' L' L' U2 R U R2 U' F' R' L L D' B' "
        "F' U' F2 U' F' D2 B2 L2 R2 R D' U D' D2 R2 D' L2 R' B2 R2 D2 L2 D F' L2",
        "R U' D' B' L U' U D D F2 R2 R2 F2 B2 D D2 L' D' R2 L2 U B L' U' U "
        "B' L' F2 D R2 L F D2 F2 B' D' B R' U2 U U B' F U' R L2 B U' B D' "
        "R R' U' B' B L2 D' F' U2 F L' D F' B' F2 F2 R L D' U D' B2 L2 D' L2 "
        "B2 R R2 F F B2 U D' U' D2 B' R U' D D' R' L D2 U' L' R' U' U U' L",
        "D F D2 U' F' B2 F' B2 F R2 U' B' L' L2 U' R' B B' D' R2 U2 L' R' L' B2 "
        "D2 R L D2 B2 D' U2 L2 B2 L F2 R2 F2 F' D R2 B D' D B2 B F B D' F' "
        "D2 U2 L F2 R2 F F F2 L' L F2 F U' R2 D2 F' F2 L L2 F2 R U' L' B' U' "
        "B' B B B2 L2 U' L R F2 L2 R2 D' F' L D F2 U' F2 F' D' R2 D L' L2 U2",
        "R2 U R R' F2 B2 F2 U' B2 L' B' L2 B2 U' B D2 F' D' L2 R F' D2 B U2 B' "
        "L' B B L2 B' R' L' U R2 F2 L R2 L B2 D2 R' B D' U R F2 L' U2 L' L "
        "D L U' R2 R2 B2 F' B2 D' B' D F' F' U' U' B2 U' F F2 F' L F' F2 B' B2 "
        "U F' B2 R D' B' U F L' B2 R' R F' B' B' D L L' L' U F2 U' L2 B2 D'",
        "B2 F2 F F2 F' D F B L' F' D2 F2 D' F2 D' F2 U2 D' D' U2 D F L' L' F' "
        "F F2 F2 L' L' B F F' U2 R R2 U R R B2 F' B2 L2 D' R' U' D R' L' U2 "
        "B' D' U L' D' U F2 L2 D' R' R F2 B2 B F2 B' L2 F2 R' D' R2 L' U' U2 R "
        "R D2 U' D' D' L B2 U' D' F2 F' U' R2 R2 L D' F D F' U' D2 D' B U L",
        "U2 B L2 R2 F' R2 F L F2 R' R' U' F' F B' F B U B2 L F' U2 R' L' F' "
        "U R' L B2 B2 R U' F2 F F B' D' R' B2 F L' U U' B' L L B' U' R R "
        "R L U' U2 D U2 B' L D F B' R' R B2 R' B D2 F' D L' D' F U2 U U "
        "F2 B' R' U R' F2 R F2 F2 B R' R F U2 U' F' F2 B' U' F2 U2 R' L2 R B'",
        "U B' F2 L U L' L2 R U2 R2 R2 R' L2 D L L L' D' L' F D2 D B' L' F' "
        "D' B B U2 B' R D F' U' B L2 R U U B' B2 U' U L2 B2 U U R' R F' "
        "D' U D' D2 B F' U' D2 B2 B' R' F B' F R D' F U2 L2 F D2 D2 L2 D2 F "
        "U F F B L R2 R2 U B D' F2 F2 D' B2 L2 U R2 D' L2 U' D' U' U2 R' U'",
        "D U' U' B' F' D D' D' D2 U R' F' F2 R2 L B D U R' R' R R2 B2 D B2 "
        "U' F D L' R R' R U2 D' R' D' U L' R' U2 B R L U D2 F2 R L2 B L2 "
        "R B' L' L2 D' L2 U' F2 B' B' U2 D' F2 B' F2 U2 U2 L2 U F' R' F' B D D' "
        "U U F2 F' F' L2 F' B R' L U2 F F2 R' L' B D D B' D' F2 D' B' L2 R",
    };

    const auto digest_of = [](const cube::solver::Solution& solution) {
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        const auto mix = [&hash](std::string_view text) {
            for (const char c : text) {
                hash ^= static_cast<unsigned char>(c);
                hash *= 0x100000001B3ULL;
            }
        };
        for (const auto& step : solution.steps) {
            mix(std::to_string(static_cast<int>(step.stage)));
            mix(":");
            mix(cube::solver::to_string(step.moves));
            mix(";");
        }
        return hash;
    };

    const auto scrambled = [](std::string_view notation) {
        const auto moves = parse_algorithm(notation);
        REQUIRE(moves.has_value());
        Cube cube = Cube::solved();
        for (const Move& move : *moves) {
            cube = cube.apply(move);
        }
        return cube;
    };

    for (std::size_t i = 0; i < kGolden25.size(); ++i) {
        CAPTURE(i, kScrambles25.at(i));
        const auto solution = solve(scrambled(kScrambles25.at(i)));
        REQUIRE(solution.has_value());
        REQUIRE(digest_of(*solution) == kGolden25.at(i));
    }
    for (std::size_t i = 0; i < kGolden100.size(); ++i) {
        CAPTURE(i, kScrambles100.at(i));
        const auto solution = solve(scrambled(kScrambles100.at(i)));
        REQUIRE(solution.has_value());
        REQUIRE(digest_of(*solution) == kGolden100.at(i));
    }
}
