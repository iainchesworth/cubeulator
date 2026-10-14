#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <cube/nxn/cube.hpp>
#include <cube/nxn/state.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

#include "../must_have.hpp"

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
    CHECK(view.mapping().is_exhaustive());
    for (std::size_t face = 0; face < 6; ++face) {
        for (std::size_t i = 0; i < 9; ++i) {
            CHECK(view[face, i / 3, i % 3] == cube.facelet((face * 9) + i));
        }
    }
}

TEST_CASE("a reduced cube's virtual 3x3 is the 3x3 it converts to, renamed", "[nxn][state]") {
    // Reduce a 5x5 without its core, then compare: to_cube3 is the virtual
    // view with every colour renamed to its face's canonical one.
    const NCube scrambled = cube::nxn::random_scramble({.size = 5, .seed = 21U, .moves = 80});
    Context context{
        .cube = scrambled,
        .steps = {},
        .method = cube::solver::SolveMethod::Advanced,
        .targets = std::nullopt,
    };
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
    const Names reduction = {"plan", "parity", "centres", "plan_edges", "edges", "core"};
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
        .then("plan_edges", cube::nxn::plan_edges(cube::nxn::FreeCentres{}))
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
    no_plan.then("centres", cube::nxn::solve_centres())
        .then("plan_edges", cube::nxn::plan_edges(cube::nxn::FreeCentres{}));
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
            const auto& orbits = g.wing_orbits();
            std::size_t touched = 0;
            bool ours = false;
            for (std::size_t k = 0; k < orbits.size(); ++k) {
                for (const auto& slot : orbits.at(k)) {
                    if (moves.at(slot.primary) != slot.primary) {
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
            .then("plan_edges", cube::nxn::plan_edges(cube::nxn::FreeCentres{}))
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
        .then("plan_edges", cube::nxn::plan_edges(cube::nxn::FreeCentres{}))
        .then("edges", cube::nxn::solve_edges())
        .then("core", cube::nxn::solve_core());
    const auto solution = cube::nxn::solve(cube, strategy);
    CHECK_FALSE(solution.has_value());
}

// ---------------------------------------------------------------------------
// The centre stage: what it may do to the rest of the cube, and what it must
// leave alone.
// ---------------------------------------------------------------------------

namespace {

// Runs the stages of the default strategy up to (not including) `stop`.
Context run_until(const NCube& cube, std::string_view stop) {
    Context context{
        .cube = cube,
        .steps = {},
        .method = cube::solver::SolveMethod::Advanced,
        .targets = std::nullopt,
    };
    for (const auto& entry : cube::nxn::default_strategy(cube.size()).stages()) {
        if (entry.name == stop) {
            break;
        }
        REQUIRE(entry.run(context).has_value());
    }
    return context;
}

// The sign of each wing orbit's permutation from where its wings are to
// where `displays` says they go: +1 when 3-cycles alone could do it.
std::vector<int> wing_signs(const NCube& cube, const cube::nxn::EdgeDisplays& displays) {
    const auto& g = cube.geometry();
    std::vector<int> signs;
    for (const auto& orbit : g.wing_orbits()) {
        const auto label = [](Color a, Color b) {
            return (static_cast<int>(a) * 6) + static_cast<int>(b);
        };
        std::vector<int> current;
        std::vector<int> target;
        for (const auto& slot : orbit) {
            current.push_back(label(cube.facelet(slot.primary), cube.facelet(slot.secondary)));
            const auto& where = g.edge_location(slot.location);
            const auto& d = displays.at(slot.location);
            const auto shown = [&](std::uint16_t f) {
                return g.face_of(f) == where.a ? d.on_a : d.on_b;
            };
            target.push_back(label(shown(slot.primary), shown(slot.secondary)));
        }
        cube::nxn::Permutation gather(target.size());
        for (std::size_t k = 0; k < target.size(); ++k) {
            const auto from = std::ranges::find(current, target.at(k));
            REQUIRE(from != current.end());
            gather.at(k) = static_cast<std::uint16_t>(from - current.begin());
        }
        signs.push_back(cube::nxn::sign(gather));
    }
    return signs;
}

}  // namespace

TEST_CASE("after the centre stage every centre sticker shows its face's colour", "[nxn][centres]") {
    for (int size = 4; size <= 7; ++size) {
        const NCube scrambled =
            cube::nxn::random_scramble({.size = size, .seed = 31U, .moves = 80});
        const Context context = run_until(scrambled, "plan_edges");
        const auto& targets = cube::test::must_have(context.targets);
        const auto& g = context.cube.geometry();
        for (std::size_t f = 0; f < g.facelet_count(); ++f) {
            if (g.role(f) == cube::nxn::Role::Centre) {
                CHECK(context.cube.facelet(f) ==
                      targets.scheme.at(static_cast<std::size_t>(g.face_of(f))));
            }
        }
    }
}

TEST_CASE("the centre stage never moves the middle cubie of a face", "[nxn][centres]") {
    // An odd size reads its colour scheme from those cubies; a search that
    // moved one would be solving to a different cube.
    for (const int size : {5, 7}) {
        for (unsigned seed = 1; seed <= 4; ++seed) {
            const NCube scrambled =
                cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 90});
            const Context before = run_until(scrambled, "centres");
            const Context after = run_until(scrambled, "plan_edges");
            const auto& g = before.cube.geometry();
            for (std::size_t f = 0; f < g.facelet_count(); ++f) {
                if (g.role(f) == cube::nxn::Role::FaceCentre) {
                    CHECK(after.cube.facelet(f) == before.cube.facelet(f));
                }
            }
        }
    }
}

TEST_CASE("the centre stage leaves every wing orbit's parity as it found it", "[nxn][centres]") {
    // The parity decision is made before the centres are built, so a word that
    // changed it would send the edges stage a cube it cannot arrange.
    for (int size = 4; size <= 7; ++size) {
        for (unsigned seed = 1; seed <= 4; ++seed) {
            const NCube scrambled =
                cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 90});
            const Context before = run_until(scrambled, "centres");
            const Context after = run_until(scrambled, "plan_edges");
            const auto& targets = cube::test::must_have(before.targets);
            // Same reference for both: signs are compared like with like.
            const auto reference = targets.reference;
            if (size % 2 == 1) {
                // An odd size's reference is its middle edges, which the
                // centre stage may move; compare against the ones now there.
                const auto now =
                    cube::nxn::FixedCentres::reference_displays(after.cube, targets.scheme);
                CHECK(wing_signs(after.cube, now) == wing_signs(before.cube, reference));
            } else {
                CHECK(wing_signs(after.cube, reference) == wing_signs(before.cube, reference));
            }
            // And the parity stage left them all even.
            for (const int s : wing_signs(before.cube, reference)) {
                CHECK(s == 1);
            }
        }
    }
}

TEST_CASE("centres are built in far fewer moves than 3-cycles alone would take", "[nxn][centres]") {
    // 3-cycles fix at most three stickers for eight or more moves; the search
    // over words that may disturb the wings must do clearly better, or it is
    // not earning its place. 5x5: 48 stickers off a scramble, about 120 moves
    // by 3-cycles, about 70 now.
    const NCube scrambled = cube::nxn::random_scramble({.size = 5, .seed = 7U, .moves = 100});
    const Context after = run_until(scrambled, "plan_edges");
    std::size_t moves = 0;
    for (const auto& step : after.steps) {
        if (step.stage == Stage::Centres) {
            moves += step.moves.size();
        }
    }
    CHECK(moves > 0);
    CHECK(moves < 100);
}
