#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <cube/nxn/commutator.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>

namespace {

using cube::CubeError;
using cube::nxn::Axis;
using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::nxn::SliceTurn;
using cube::nxn::Stage;
using cube::solver::Color;
using cube::solver::Face;
using cube::solver::SolveMethod;

NCube solved_of(int size) {
    auto cube = NCube::solved(size);
    REQUIRE(cube.has_value());
    return *cube;
}

// Solves `cube` and checks the solution does what it says on the cube it was
// given, whatever the stages were.
cube::nxn::Solution solve_and_check(const NCube& cube, SolveMethod method = SolveMethod::Advanced) {
    const auto solution = cube::nxn::solve(cube, method);
    REQUIRE(solution.has_value());
    NCube replay = cube;
    for (const auto& step : solution->steps) {
        replay.apply(std::span<const LayerMove>{step.moves});
    }
    CHECK(replay.is_solved());
    // Every move names a layer the cube has.
    for (const LayerMove& move : solution->moves()) {
        CHECK(move.depth >= 0);
        CHECK(move.depth < cube.size());
        CHECK(move.turns >= 1);
        CHECK(move.turns <= 3);
    }
    return *solution;
}

// Turns the whole cube about an axis: every layer, the same way.
NCube rotated_about(NCube cube, Axis axis, int turns) {
    for (int layer = 0; layer < cube.size(); ++layer) {
        cube.apply(SliceTurn{.axis = axis, .layer = layer, .turns = turns});
    }
    return cube;
}

}  // namespace

TEST_CASE("every size from 2 to 7 solves scrambled cubes", "[nxn][solve]") {
    for (int size = 2; size <= 7; ++size) {
        for (unsigned seed = 1; seed <= 6; ++seed) {
            const NCube scrambled =
                cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 80});
            INFO("size " << size << " seed " << seed);
            solve_and_check(scrambled);
        }
    }
}

TEST_CASE("an already solved cube of any size has an empty solution", "[nxn][solve]") {
    for (int size = 2; size <= 9; ++size) {
        const auto solution = cube::nxn::solve(solved_of(size));
        REQUIRE(solution.has_value());
        CHECK(solution->move_count() == 0);
    }
}

TEST_CASE("a cube held in another orientation is just as solved or solvable", "[nxn][solve]") {
    for (const int size : {2, 3, 4, 5, 6}) {
        // Turned as a whole, a solved cube shows six uniform faces in another
        // arrangement of the scheme, and needs no moves.
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            const auto solution = cube::nxn::solve(rotated_about(solved_of(size), axis, 1));
            REQUIRE(solution.has_value());
            CHECK(solution->move_count() == 0);
        }
        // A scrambled cube held in another orientation is solved the same way.
        const NCube scrambled =
            cube::nxn::random_scramble({.size = size, .seed = 11U, .moves = 70});
        solve_and_check(rotated_about(scrambled, Axis::Y, 1));
        solve_and_check(rotated_about(scrambled, Axis::X, 2));
    }
}

TEST_CASE("one inner quarter turn on its own is solved, at every size and depth",
          "[nxn][solve][parity]") {
    // The smallest scramble that needs the odd permutation of a wing orbit on
    // an even size, and the smallest that disturbs centres and wings on an odd
    // one.
    for (int size = 4; size <= 7; ++size) {
        for (int layer = 1; layer < size - 1; ++layer) {
            NCube cube = solved_of(size);
            cube.apply(SliceTurn{.axis = Axis::X, .layer = layer, .turns = 1});
            INFO("size " << size << " layer " << layer);
            const auto solution = solve_and_check(cube);
            // The middle layer of an odd cube holds no wings (it is the
            // layer of the middle edges and the face centres), so it needs no
            // odd permutation of any wing orbit.
            const bool holds_wings = size % 2 == 0 || layer != (size - 1) / 2;
            const bool uses_parity = std::ranges::any_of(
                solution.steps, [](const auto& s) { return s.stage == Stage::Parity; });
            CHECK(uses_parity == holds_wings);
        }
    }
}

TEST_CASE("every layer turn at every size is solved", "[nxn][solve]") {
    for (int size = 2; size <= 6; ++size) {
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            for (int layer = 0; layer < size; ++layer) {
                for (int turns = 1; turns <= 3; ++turns) {
                    NCube cube = solved_of(size);
                    cube.apply(SliceTurn{.axis = axis, .layer = layer, .turns = turns});
                    INFO("size " << size << " layer " << layer << " turns " << turns);
                    solve_and_check(cube);
                }
            }
        }
    }
}

TEST_CASE("at size 3 the solution is exactly the 3x3 solver's, for every method", "[nxn][solve]") {
    for (const SolveMethod method :
         {SolveMethod::Beginner, SolveMethod::Intermediate, SolveMethod::Advanced}) {
        for (unsigned seed = 1; seed <= 3; ++seed) {
            const auto cube3 = cube::solver::random_scramble(seed, cube::solver::MoveCount{30});
            const auto theirs = cube::solver::solve(cube3, method);
            REQUIRE(theirs.has_value());
            const auto ours = cube::nxn::solve(NCube::from_cube3(cube3), method);
            REQUIRE(ours.has_value());

            std::vector<LayerMove> expected;
            for (const auto& step : theirs->steps) {
                for (const auto& m : step.moves) {
                    expected.push_back(LayerMove{.face = m.face, .depth = 0, .turns = m.turns});
                }
            }
            CHECK(ours->moves() == expected);
            CHECK(ours->steps.size() == theirs->steps.size());
        }
    }
}

TEST_CASE("the core method is the caller's choice at every size", "[nxn][solve]") {
    const NCube scrambled = cube::nxn::random_scramble({.size = 5, .seed = 3U, .moves = 90});
    for (const SolveMethod method :
         {SolveMethod::Beginner, SolveMethod::Intermediate, SolveMethod::Advanced}) {
        solve_and_check(scrambled, method);
    }
    // The reduction is the same whatever the core is; only the core differs,
    // and a method that teaches takes more moves than one that is short.
    const auto beginner = cube::nxn::solve(scrambled, SolveMethod::Beginner);
    const auto advanced = cube::nxn::solve(scrambled, SolveMethod::Advanced);
    REQUIRE(beginner.has_value());
    REQUIRE(advanced.has_value());
    CHECK(beginner->move_count() > advanced->move_count());
}

TEST_CASE("3x3 algorithms run unchanged on a cube of any size", "[nxn][algorithm]") {
    // R U R' U' has order 6 on every cube: six repeats are the identity.
    const auto sexy = cube::solver::parse_algorithm("R U R' U'");
    REQUIRE(sexy.has_value());
    for (int size = 2; size <= 8; ++size) {
        NCube cube = solved_of(size);
        for (int i = 0; i < 6; ++i) {
            cube::nxn::run_algorithm(cube, std::span<const cube::solver::Move>{*sexy});
            if (i < 5) {
                CHECK_FALSE(cube.is_solved());
            }
        }
        CHECK(cube.is_solved());
    }
}

TEST_CASE("one algorithm, applied through the shared concept, does the same on Cube and NCube",
          "[nxn][algorithm]") {
    static_assert(cube::nxn::FaceTurnable<cube::solver::Cube>);
    static_assert(cube::nxn::FaceTurnable<NCube>);

    const auto sune = cube::solver::parse_algorithm("R U R' U R U2 R' F' L F L' B2 D' L2");
    REQUIRE(sune.has_value());
    cube::solver::Cube three = cube::solver::random_scramble(17U, cube::solver::MoveCount{25});
    NCube wide = NCube::from_cube3(three);
    cube::nxn::run_algorithm(three, std::span<const cube::solver::Move>{*sune});
    cube::nxn::run_algorithm(wide, std::span<const cube::solver::Move>{*sune});
    CHECK(wide == NCube::from_cube3(three));
}

TEST_CASE("a 3x3 solution still solves a cube once it is reduced", "[nxn][algorithm]") {
    // The core's moves are outer-layer turns, which is why the reduction can
    // hand over to the 3x3 solver at all: they turn a reduced big cube
    // exactly as they turn the 3x3 it shows.
    for (int size = 3; size <= 7; ++size) {
        const NCube scrambled = cube::nxn::random_scramble({.size = size, .seed = 5U, .moves = 60});
        const auto solution = solve_and_check(scrambled);
        NCube reduced = scrambled;
        for (const auto& step : solution.steps) {
            if (step.stage != Stage::Core) {
                reduced.apply(std::span<const LayerMove>{step.moves});
            }
        }
        if (size >= 3) {
            CHECK(reduced.is_reduced());
        }
    }
}

TEST_CASE("cubes that cannot be solved are refused, not guessed at", "[nxn][solve][error]") {
    for (const int size : {2, 3, 4, 5}) {
        // A corner turned in place on its own is unreachable.
        const auto& geometry = cube::nxn::Geometry::of(size);
        NCube good = solved_of(size);
        std::vector<Color> facelets(good.facelets().begin(), good.facelets().end());
        const auto& corner = geometry.corners().front();
        const Color first = facelets.at(corner.at(0));
        facelets.at(corner.at(0)) = facelets.at(corner.at(1));
        facelets.at(corner.at(1)) = facelets.at(corner.at(2));
        facelets.at(corner.at(2)) = first;
        const auto twisted = NCube::from_facelets(size, facelets);
        REQUIRE(twisted.has_value());
        const auto solution = cube::nxn::solve(*twisted);
        REQUIRE_FALSE(solution.has_value());
        CHECK((solution.error() == CubeError::unsolvable_state ||
               solution.error() == CubeError::invalid_cube_state));
    }
}

TEST_CASE("stickers that cannot be a cube's are refused", "[nxn][solve][error]") {
    const NCube good = solved_of(4);
    std::vector<Color> facelets(good.facelets().begin(), good.facelets().end());
    facelets.at(0) = Color::Red;  // one colour now appears 17 times
    CHECK_FALSE(NCube::from_facelets(4, facelets).has_value());
    CHECK_FALSE(NCube::from_facelets(4, std::span<const Color>{facelets}.first(50)).has_value());
}

TEST_CASE("merged() cancels and combines turns, even past commuting layers", "[nxn][solve]") {
    const int size = 5;
    const auto layer = [](Face face, int depth, int turns) {
        return LayerMove{.face = face, .depth = depth, .turns = turns};
    };
    // R R' vanishes; 2R sits between and commutes with R, so R 2R R' is 2R.
    const std::array<LayerMove, 3> past = {layer(Face::R, 0, 1), layer(Face::R, 1, 1),
                                           layer(Face::R, 0, 3)};
    const auto reduced = cube::nxn::merged(past, size);
    REQUIRE(reduced.size() == 1);
    CHECK(reduced.front() == layer(Face::R, 1, 1));

    // The same layer from the other end is the same layer.
    const std::array<LayerMove, 2> ends = {layer(Face::R, 0, 1), layer(Face::L, 4, 3)};
    CHECK(cube::nxn::merged(ends, size).size() == 1);
    CHECK(cube::nxn::merged(ends, size).front().turns == 2);

    // Different axes do not commute, so nothing may merge across them.
    const std::array<LayerMove, 3> blocked = {layer(Face::R, 0, 1), layer(Face::U, 0, 1),
                                              layer(Face::R, 0, 3)};
    CHECK(cube::nxn::merged(blocked, size).size() == 3);
}

// ---------------------------------------------------------------------------
// The commutator engine's own contract.
// ---------------------------------------------------------------------------

namespace {

cube::nxn::Permutation word_permutation(const cube::nxn::Geometry& geometry,
                                        const cube::nxn::Word& word) {
    cube::nxn::Permutation p = cube::nxn::identity_permutation(geometry.facelet_count());
    for (const SliceTurn t : word) {
        p = cube::nxn::then(p, geometry.turn(t));
    }
    return p;
}

}  // namespace

TEST_CASE("every cycle in the library does exactly what it says and nothing else",
          "[nxn][commutator]") {
    for (const int size : {4, 5, 6}) {
        const auto& geometry = cube::nxn::Geometry::of(size);
        const auto& library = cube::nxn::CycleLibrary::of(size);
        const auto check = [&](const cube::nxn::ThreeCycle& cycle, bool wing) {
            const auto p = word_permutation(geometry, cycle.word);
            const auto moved = cube::nxn::support(p);
            CHECK(moved.size() == (wing ? 6U : 3U));
            // gather: what shows at i came from p[i]. The cycle says content
            // at cells[k] ends at cells[k+1].
            for (std::size_t k = 0; k < 3; ++k) {
                CHECK(p[cycle.cells.at((k + 1) % 3)] == cycle.cells.at(k));
            }
            // And a solved cube is moved by it nowhere else.
            for (const std::uint16_t f : moved) {
                const bool named = std::ranges::find(cycle.cells, f) != cycle.cells.end();
                if (!wing) {
                    CHECK(named);
                }
            }
        };
        // Every 40th, to keep the test quick: the property is per cycle and
        // the sample covers every orbit.
        for (std::size_t k = 0; k < geometry.centre_orbits().size(); ++k) {
            const auto cycles = library.centres(k);
            for (std::size_t i = 0; i < cycles.size(); i += 40) {
                check(cycles[i], false);
            }
        }
        for (std::size_t k = 0; k < geometry.wing_orbits().size(); ++k) {
            const auto cycles = library.wings(k);
            for (std::size_t i = 0; i < cycles.size(); i += 40) {
                check(cycles[i], true);
            }
        }
    }
}

TEST_CASE("the cycle library holds every 3-cycle of every orbit", "[nxn][commutator]") {
    // 24 pieces per orbit: C(24,3) triples, each in two directions.
    constexpr std::size_t kAll = 24 * 23 * 22 / 3;
    for (const int size : {4, 5, 6, 7}) {
        const auto& geometry = cube::nxn::Geometry::of(size);
        const auto& library = cube::nxn::CycleLibrary::of(size);
        for (std::size_t k = 0; k < geometry.centre_orbits().size(); ++k) {
            CHECK(library.centres(k).size() == kAll);
        }
        for (std::size_t k = 0; k < geometry.wing_orbits().size(); ++k) {
            CHECK(library.wings(k).size() == kAll);
        }
    }
}

TEST_CASE("arrange sorts by 3-cycles and refuses what 3-cycles cannot do", "[nxn][commutator]") {
    const auto& geometry = cube::nxn::Geometry::of(4);
    const auto& library = cube::nxn::CycleLibrary::of(4);
    const auto cells = geometry.orbit_facelets(geometry.centre_orbits().front());
    const std::vector<std::uint16_t> cell_list(cells.begin(), cells.end());

    // 24 distinct labels, rotated by three places among the first nine.
    cube::nxn::Labels target(24);
    for (std::size_t i = 0; i < 24; ++i) {
        target[i] = static_cast<int>(i);
    }
    cube::nxn::Labels current = target;
    std::rotate(current.begin(), current.begin() + 3, current.begin() + 9);
    const auto word = cube::nxn::arrange(cell_list, library.centres(0), current, target);
    REQUIRE(word.has_value());
    CHECK(current == target);
    CHECK_FALSE(word->empty());

    // Two labels swapped is an odd permutation; no 3-cycle makes it.
    cube::nxn::Labels odd = target;
    std::swap(odd[0], odd[1]);
    const auto refused = cube::nxn::arrange(cell_list, library.centres(0), odd, target);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error() == CubeError::no_solution);

    // Labels that are not the same set can never be arranged into each other.
    cube::nxn::Labels other = target;
    other[0] = 99;
    const auto mismatch = cube::nxn::arrange(cell_list, library.centres(0), other, target);
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error() == CubeError::invalid_cube_state);
}
