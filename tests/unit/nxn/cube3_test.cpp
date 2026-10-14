#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/cube3.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/solver/stepped_solution.hpp>

// The app solves through cube::nxn::solve_3x3 now. Everything the app does
// with a solution (the stepped solution, the history, the review) was written
// against what cube::solver::solve returns, so what is checked here is that it
// returns the same thing: the same stages, in the same order, with the same
// moves, or the same error - not merely a solution that solves the cube.

namespace {

using cube::CubeError;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::kAllFaces;
using cube::solver::Move;
using cube::solver::MoveCount;
using cube::solver::SolveMethod;

struct Method {
    const char* name;
    SolveMethod method;
    unsigned scrambles;  // how many scrambles of the corpus: Beginner is the slow one
};

constexpr std::array<Method, 3> kMethods = {
    Method{.name = "beginner", .method = SolveMethod::Beginner, .scrambles = 12},
    Method{.name = "intermediate", .method = SolveMethod::Intermediate, .scrambles = 40},
    Method{.name = "advanced", .method = SolveMethod::Advanced, .scrambles = 40},
};

// `cube` solved both ways, compared stage by stage and move by move.
void check_same(const Cube& cube, SolveMethod method) {
    const auto theirs = cube::solver::solve(cube, method);
    const auto ours = cube::nxn::solve_3x3(cube, method);
    REQUIRE(ours.has_value() == theirs.has_value());
    if (!theirs.has_value()) {
        CHECK(ours.error() == theirs.error());
        return;
    }
    REQUIRE(ours->steps.size() == theirs->steps.size());
    for (std::size_t i = 0; i < theirs->steps.size(); ++i) {
        CHECK(ours->steps.at(i).stage == theirs->steps.at(i).stage);
        CHECK(ours->steps.at(i).moves == theirs->steps.at(i).moves);
    }
}

// A cube from stickers that do not have to have come from turning one.
Cube from_stickers(const std::array<std::array<Color, 9>, 6>& stickers) {
    auto cube = Cube::from_facelets(stickers);
    REQUIRE(cube.has_value());
    return *cube;
}

std::array<std::array<Color, 9>, 6> stickers_of(const Cube& cube) {
    std::array<std::array<Color, 9>, 6> stickers{};
    for (const Face face : kAllFaces) {
        stickers.at(static_cast<std::size_t>(face)) = cube.face(face);
    }
    return stickers;
}

}  // namespace

TEST_CASE("solve_3x3 returns exactly what the 3x3 solver returns, over a corpus", "[nxn][app]") {
    for (const Method& m : kMethods) {
        INFO("method " << m.name);
        for (unsigned seed = 1; seed <= m.scrambles; ++seed) {
            const Cube cube =
                cube::solver::random_scramble(seed, MoveCount{20 + static_cast<int>(seed % 15)});
            check_same(cube, m.method);
        }
    }
}

TEST_CASE("a solved cube, and every single turn of one, solve the same way", "[nxn][app]") {
    for (const Method& m : kMethods) {
        INFO("method " << m.name);
        check_same(Cube::solved(), m.method);
        for (const Face face : kAllFaces) {
            for (int turns = 1; turns <= 3; ++turns) {
                check_same(Cube::solved().apply(Move{.face = face, .turns = turns}), m.method);
            }
        }
    }
}

TEST_CASE("an unsolvable cube is refused with the 3x3 solver's own error, per method",
          "[nxn][app]") {
    // Two edges traded: a cube made of real pieces that no turns produce.
    auto stickers = stickers_of(Cube::solved());
    const auto& g = cube::nxn::Geometry::of(3);
    const auto first = g.mid_edge(0);
    const auto second = g.mid_edge(1);
    for (std::size_t k = 0; k < 2; ++k) {
        std::swap(stickers.at(first.at(k) / 9).at(first.at(k) % 9),
                  stickers.at(second.at(k) / 9).at(second.at(k) % 9));
    }
    const Cube traded = from_stickers(stickers);
    REQUIRE_FALSE(traded.is_solvable());

    for (const Method& m : kMethods) {
        INFO("method " << m.name);
        const auto theirs = cube::solver::solve(traded, m.method);
        REQUIRE_FALSE(theirs.has_value());
        check_same(traded, m.method);
    }
}

TEST_CASE("a cube whose centres are out of order is refused, not read as held another way",
          "[nxn][app]") {
    // The same physical cube with its colours renamed as a turn of the whole
    // cube would: green to orange to blue to red. Its centres are not where the
    // 3x3 solvers' scheme puts them, which is what a scan assembled into the
    // wrong slots looks like - and which they refuse.
    const auto rename = [](Color c) {
        switch (c) {
            case Color::Green:
                return Color::Orange;
            case Color::Orange:
                return Color::Blue;
            case Color::Blue:
                return Color::Red;
            case Color::Red:
                return Color::Green;
            default:
                return c;
        }
    };
    auto stickers = stickers_of(cube::solver::random_scramble(7U, MoveCount{25}));
    for (auto& face : stickers) {
        for (Color& c : face) {
            c = rename(c);
        }
    }
    const Cube turned = from_stickers(stickers);
    REQUIRE_FALSE(turned.is_solvable());

    for (const Method& m : kMethods) {
        INFO("method " << m.name);
        check_same(turned, m.method);
        CHECK_FALSE(cube::nxn::solve_3x3(turned, m.method).has_value());
    }

    // The N x N library's own entry point, which has no fixed centres to
    // honour, does solve it: that is the difference this wrapper exists to keep
    // out of the app.
    const auto orientation_free = cube::nxn::solve(cube::nxn::NCube::from_cube3(turned));
    CHECK(orientation_free.has_value());
}

TEST_CASE("the stepped solution built from solve_3x3 is the one the app always built",
          "[nxn][app]") {
    for (unsigned seed = 1; seed <= 6; ++seed) {
        const Cube cube = cube::solver::random_scramble(seed, MoveCount{25});
        for (const SolveMethod method : {SolveMethod::Intermediate, SolveMethod::Advanced}) {
            const auto theirs = cube::solver::solve(cube, method);
            const auto ours = cube::nxn::solve_3x3(cube, method);
            REQUIRE(theirs.has_value());
            REQUIRE(ours.has_value());
            const auto before = cube::solver::SteppedSolution::from(cube, *theirs);
            const auto after = cube::solver::SteppedSolution::from(cube, *ours);
            CHECK(after.move_count() == before.move_count());
            CHECK(after.moves() == before.moves());
            CHECK(after.stage_ranges().size() == before.stage_ranges().size());
            // Walked to the end, it is the same solved cube.
            CHECK(after.cube_at(after.move_count()) == before.cube_at(before.move_count()));
        }
    }
}

TEST_CASE("prepare builds what a solve of that size needs, any number of times", "[nxn][app]") {
    cube::nxn::prepare(3, SolveMethod::Advanced);
    cube::nxn::prepare(3, SolveMethod::Advanced);
    cube::nxn::prepare(5, SolveMethod::Intermediate);
    // Out-of-range sizes are not an error: there is nothing to build.
    cube::nxn::prepare(1, SolveMethod::Beginner);
    cube::nxn::prepare(cube::nxn::kMaxSize + 3, SolveMethod::Beginner);
    check_same(cube::solver::random_scramble(3U, MoveCount{22}), SolveMethod::Advanced);
}
