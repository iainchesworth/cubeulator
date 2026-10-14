#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/state.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

namespace {

using cube::CubeError;
using cube::nxn::Axis;
using cube::nxn::Context;
using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::nxn::SliceTurn;
using cube::nxn::Stage;
using cube::nxn::Strategy;
using cube::solver::Color;
using cube::solver::Face;

NCube solved_of(int size) {
    auto cube = NCube::solved(size);
    REQUIRE(cube.has_value());
    return *cube;
}

std::vector<std::string> stage_names(const Strategy& strategy) {
    std::vector<std::string> names;
    for (const auto& entry : strategy.stages()) {
        names.push_back(entry.name);
    }
    return names;
}

}  // namespace

// ---------------------------------------------------------------------------
// The concepts say what a state is, once.
// ---------------------------------------------------------------------------

static_assert(cube::nxn::FaceletState<NCube>);
static_assert(cube::nxn::FaceletState<cube::solver::Cube>);
static_assert(!cube::nxn::FaceletState<int>);
static_assert(cube::nxn::TargetPolicy<cube::nxn::FixedCentres>);
static_assert(cube::nxn::TargetPolicy<cube::nxn::FreeCentres>);
static_assert(!cube::nxn::TargetPolicy<int>);
static_assert(cube::nxn::StageLike<cube::nxn::StageFn>);

TEST_CASE("range algorithms over the concept work on both kinds of state", "[nxn][state]") {
    const cube::solver::Cube three = cube::solver::random_scramble(5U, cube::solver::MoveCount{20});
    const NCube wide = NCube::from_cube3(three);

    CHECK(cube::nxn::puzzle_size(three) == 3);
    CHECK(cube::nxn::puzzle_size(wide) == 3);
    CHECK(std::ranges::equal(cube::nxn::facelets_of(three), cube::nxn::facelets_of(wide)));
    CHECK(cube::nxn::colour_counts(three) == cube::nxn::colour_counts(wide));
    CHECK_FALSE(cube::nxn::has_uniform_faces(three));
    CHECK(cube::nxn::has_uniform_faces(cube::solver::Cube::solved()));

    for (int size = 2; size <= 7; ++size) {
        const NCube solved = solved_of(size);
        CHECK(cube::nxn::has_uniform_faces(solved));
        CHECK(solved.is_solved() == cube::nxn::has_uniform_faces(solved));
        for (const int count : cube::nxn::colour_counts(solved)) {
            CHECK(count == size * size);
        }
    }
}

// ---------------------------------------------------------------------------
// A big cube seen as a 3x3.
// ---------------------------------------------------------------------------

TEST_CASE("the virtual 3x3 of a cube is its corners, edges and centres, uncopied", "[nxn][state]") {
    for (int size = 3; size <= 8; ++size) {
        const NCube cube = cube::nxn::random_scramble({.size = size, .seed = 9U, .moves = 60});
        const auto view = cube::nxn::virtual_3x3(cube);
        REQUIRE(view.extent(0) == 6);
        REQUIRE(view.extent(1) == 3);
        REQUIRE(view.extent(2) == 3);
        const int middle = (size - 1) / 2;
        const std::array<int, 3> line = {0, middle, size - 1};
        for (std::size_t face = 0; face < 6; ++face) {
            for (std::size_t row = 0; row < 3; ++row) {
                for (std::size_t col = 0; col < 3; ++col) {
                    CHECK(view[face, row, col] ==
                          cube.at(static_cast<Face>(face), line.at(row), line.at(col)));
                }
            }
        }
        // No copy: the view reads the cube's own storage.
        CHECK(&view[0, 0, 0] == cube.facelets().data());
    }
}

TEST_CASE("at size 3 the virtual 3x3 is the cube itself", "[nxn][state]") {
    const NCube cube = cube::nxn::random_scramble({.size = 3, .seed = 4U, .moves = 30});
    const auto view = cube::nxn::virtual_3x3(cube);
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t i = 0; i < 9; ++i) {
            CHECK(view[face, i / 3, i % 3] == cube.facelets()[(face * 9) + i]);
        }
    }
}

TEST_CASE("a reduced cube's virtual 3x3 is the 3x3 it converts to, renamed", "[nxn][state]") {
    // Reduce a 5x5 without its core, then compare: to_cube3 is the virtual
    // view with every colour renamed to its face's canonical one.
    const NCube scrambled = cube::nxn::random_scramble({.size = 5, .seed = 21U, .moves = 80});
    Context context{.cube = scrambled,
                    .steps = {},
                    .method = cube::solver::SolveMethod::Advanced,
                    .targets = std::nullopt};
    const Strategy full = cube::nxn::default_strategy(5);
    for (const auto& entry : full.stages()) {
        if (entry.name == "core") {
            break;
        }
        REQUIRE(entry.run(context).has_value());
    }
    REQUIRE(context.cube.is_reduced());
    const auto three = context.cube.to_cube3();
    REQUIRE(three.has_value());

    const auto view = cube::nxn::virtual_3x3(context.cube);
    // The same partition of stickers into equal colours, whatever they are
    // called: two stickers match in one exactly when they match in the other.
    for (std::size_t a = 0; a < 54; ++a) {
        for (std::size_t b = a + 1; b < 54; ++b) {
            const bool same_view =
                view[a / 9, (a % 9) / 3, a % 3] == view[b / 9, (b % 9) / 3, b % 3];
            const bool same_cube =
                three->at(static_cast<Face>(a / 9), static_cast<int>((a % 9) / 3),
                          static_cast<int>(a % 3)) == three->at(static_cast<Face>(b / 9),
                                                                static_cast<int>((b % 9) / 3),
                                                                static_cast<int>(b % 3));
            CHECK(same_view == same_cube);
        }
    }
}

// ---------------------------------------------------------------------------
// Strategy: the stages a size has, and swapping them.
// ---------------------------------------------------------------------------

TEST_CASE("each size gets the stages it needs and no others", "[nxn][strategy]") {
    using Names = std::vector<std::string>;
    CHECK(stage_names(cube::nxn::default_strategy(2)) == Names{"corners"});
    CHECK(stage_names(cube::nxn::default_strategy(3)) == Names{"core"});
    const Names reduction = {"plan", "parity", "centres", "edges", "core"};
    for (int size = 4; size <= 9; ++size) {
        CHECK(stage_names(cube::nxn::default_strategy(size)) == reduction);
    }
    CHECK(cube::nxn::default_strategy(4).name() != cube::nxn::default_strategy(5).name());
}

TEST_CASE("a 3x3 skips the whole reduction, so it makes no reduction moves", "[nxn][strategy]") {
    const NCube scrambled = cube::nxn::random_scramble({.size = 3, .seed = 8U, .moves = 40});
    const auto solution = cube::nxn::solve(scrambled);
    REQUIRE(solution.has_value());
    for (const auto& step : solution->steps) {
        CHECK(step.stage == Stage::Core);
    }
}

TEST_CASE("a strategy built by hand runs its stages in order", "[nxn][strategy]") {
    // The default list for an even size, written out, gives the default answer.
    const NCube scrambled = cube::nxn::random_scramble({.size = 4, .seed = 13U, .moves = 70});
    Strategy by_hand{"by hand"};
    by_hand.then("plan", cube::nxn::plan(cube::nxn::FreeCentres{}))
        .then("parity", cube::nxn::make_wings_even())
        .then("centres", cube::nxn::solve_centres())
        .then("edges", cube::nxn::solve_edges())
        .then("core", cube::nxn::solve_core());
    const auto manual = cube::nxn::solve(scrambled, by_hand);
    const auto standard = cube::nxn::solve(scrambled);
    REQUIRE(manual.has_value());
    REQUIRE(standard.has_value());
    CHECK(manual->moves() == standard->moves());
}

TEST_CASE("a stage that needs targets, run without planning them, fails cleanly",
          "[nxn][strategy]") {
    Strategy no_plan{"no plan"};
    no_plan.then("centres", cube::nxn::solve_centres());
    const auto result =
        cube::nxn::solve(cube::nxn::random_scramble({.size = 4, .seed = 1U, .moves = 30}), no_plan);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == CubeError::no_solution);
}

TEST_CASE("a stage can be any callable that takes the context", "[nxn][strategy]") {
    // A stage of the caller's own: here, one that counts how often it ran and
    // makes no move. StageLike asks only for copyable and callable on Context.
    int runs = 0;
    Strategy counting{"counting"};
    counting.then("note", [&runs](Context&) -> cube::Result<void> {
        ++runs;
        return {};
    });
    const auto result = cube::nxn::solve(solved_of(5), counting);
    REQUIRE(result.has_value());
    CHECK(runs == 1);
}

TEST_CASE("the parity fix can be injected, and any valid choice solves the cube",
          "[nxn][strategy][parity]") {
    // A one-turn scramble needs an odd wing permutation. The handler is asked
    // for which turn to make; here three different ones are supplied: the
    // default's X axis, the same search on Z, and a hand-written rule that
    // takes the deepest layer.
    NCube cube = solved_of(6);
    cube.apply(SliceTurn{.axis = Axis::Y, .layer = 1, .turns = 1});

    int asked = 0;
    const cube::nxn::ParityHandler counting = [&asked](const cube::nxn::Geometry& g,
                                                       std::size_t orbit) {
        ++asked;
        return cube::nxn::inner_layer_parity(Axis::Z)(g, orbit);
    };
    const cube::nxn::ParityHandler deepest = [](const cube::nxn::Geometry& g,
                                                std::size_t orbit) -> cube::Result<SliceTurn> {
        // Last inner layer along Y that moves only this orbit's wings.
        for (int layer = g.size() - 2; layer >= 1; --layer) {
            const SliceTurn candidate{.axis = Axis::Y, .layer = layer, .turns = 1};
            const auto& moves = g.turn(candidate);
            const auto orbits = g.wing_orbits();
            std::size_t touched = 0;
            bool ours = false;
            for (std::size_t k = 0; k < orbits.size(); ++k) {
                for (const auto& slot : orbits[k]) {
                    if (moves[slot.primary] != slot.primary) {
                        ++touched;
                        ours = ours || k == orbit;
                        break;
                    }
                }
            }
            if (touched == 1 && ours) {
                return candidate;
            }
        }
        return std::unexpected(CubeError::no_solution);
    };

    for (const cube::nxn::ParityHandler& handler :
         {cube::nxn::inner_layer_parity(Axis::X), counting, deepest}) {
        Strategy strategy{"injected parity"};
        strategy.then("plan", cube::nxn::plan(cube::nxn::FreeCentres{}))
            .then("parity", cube::nxn::make_wings_even(handler))
            .then("centres", cube::nxn::solve_centres())
            .then("edges", cube::nxn::solve_edges())
            .then("core", cube::nxn::solve_core());
        const auto solution = cube::nxn::solve(cube, strategy);
        REQUIRE(solution.has_value());
        NCube replay = cube;
        replay.apply(std::span<const LayerMove>{solution->moves()});
        CHECK(replay.is_solved());
    }
    CHECK(asked >= 1);
}

TEST_CASE("a handler that fixes nothing is caught, not trusted", "[nxn][strategy][parity]") {
    // A handler that returns a turn that moves no wing does not change the
    // permutation's parity; the arrangement then cannot be reached by
    // 3-cycles, and the solve says so instead of returning moves that do not
    // solve the cube.
    NCube cube = solved_of(4);
    cube.apply(SliceTurn{.axis = Axis::X, .layer = 1, .turns = 1});
    const cube::nxn::ParityHandler useless = [](const cube::nxn::Geometry&, std::size_t) {
        return cube::Result<SliceTurn>{SliceTurn{.axis = Axis::X, .layer = 0, .turns = 2}};
    };
    Strategy strategy{"useless parity"};
    strategy.then("plan", cube::nxn::plan(cube::nxn::FreeCentres{}))
        .then("parity", cube::nxn::make_wings_even(useless))
        .then("centres", cube::nxn::solve_centres())
        .then("edges", cube::nxn::solve_edges())
        .then("core", cube::nxn::solve_core());
    const auto solution = cube::nxn::solve(cube, strategy);
    CHECK_FALSE(solution.has_value());
}
