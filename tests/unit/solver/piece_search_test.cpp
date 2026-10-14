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

    for (std::size_t i = 0; i < kGolden25.size(); ++i) {
        const auto seed = static_cast<unsigned>(i);
        CAPTURE(seed);
        const auto solution = solve(random_scramble(seed, 25));
        REQUIRE(solution.has_value());
        REQUIRE(digest_of(*solution) == kGolden25.at(i));
    }
    for (std::size_t i = 0; i < kGolden100.size(); ++i) {
        const auto seed = static_cast<unsigned>(1000 + i);
        CAPTURE(seed);
        const auto solution = solve(random_scramble(seed, 100));
        REQUIRE(solution.has_value());
        REQUIRE(digest_of(*solution) == kGolden100.at(i));
    }
}
