#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cube/error.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/solution.hpp>
#include <cube/solver/solver.hpp>

// How a cube of a given size is solved, as data.
//
// A Strategy is an ordered list of stages, each a function of the state being
// solved. Which stages a size has is decided in one place (default_strategy),
// so "skip to the right stage for this size" is not a branch inside a solver
// but the shape of the list:
//
//   size 2       Corners                                     a search
//   size 3       Core                                        no reduction
//   even >= 4    Plan, Parity, Centres, Edges, Core          free centres
//   odd  >= 5    Plan, Parity, Centres, Edges, Core          fixed centres
//
// The even and odd lists differ in one thing, the TargetPolicy that Plan
// uses: with an even size nothing fixes the colour of a face until its centre
// is solved, so the scheme is read off the corners and the edges are free to
// settle wherever the corners need them; an odd size has centre and middle-edge
// cubies that never move relative to the faces, and the rest is solved to
// match them. Everything after Plan is the same engine.
//
// Stages are callable values (StageLike), so a different one - a quicker
// centres method for one size, an algorithm table for a parity case - is a
// drop-in for the stage it replaces, in a Strategy built by hand.

namespace cube::nxn {

// What each edge of the cube must show: the colour on each of its two faces.
// Paired wings, and the middle cubie when the size has one, all show it.
struct EdgeDisplay {
    Color on_a = Color::White;
    Color on_b = Color::White;

    friend constexpr bool operator==(const EdgeDisplay&, const EdgeDisplay&) = default;
};

using EdgeDisplays = std::array<EdgeDisplay, Geometry::kEdgeLocations>;
using Scheme = std::array<Color, 6>;

// Where the pieces are to end up. The colour of every face is worked out
// once, first, and the centres are built to it. What every edge shows is
// worked out twice, because it depends on the corners and the stage that
// builds the centres turns them: `reference` is what an edge shows when
// everything is home, which is all the parity decision needs (it asks only how
// odd the wings' permutation is), and `displays` is what each edge will show
// once the 3x3 the cube becomes is known to be solvable, set after the
// centres by plan_edges().
struct Targets {
    Scheme scheme{};
    EdgeDisplays reference{};
    std::optional<EdgeDisplays> displays;
};

// The state a Strategy's stages work on.
struct Context {
    NCube cube;                      // the working copy, turned by each stage
    std::vector<Step> steps;         // the moves made so far, by stage
    solver::SolveMethod method;      // which 3x3 method the Core stage uses
    std::optional<Targets> targets;  // set by Plan
};

// How the targets of a reduction are chosen for a cube. See the table above.
template <typename P>
concept TargetPolicy = requires(const P& policy, const NCube& cube, const Scheme& scheme) {
    { policy.scheme(cube) } -> std::same_as<Result<Scheme>>;
    { policy.reference_displays(cube, scheme) } -> std::same_as<EdgeDisplays>;
    { policy.displays(cube, scheme) } -> std::same_as<Result<EdgeDisplays>>;
};

// An odd size: the middle of each face and of each edge are fixed in relation
// to one another, so the faces' colours and the edges' displays are read from
// them, and the centres and wings are solved to match.
struct FixedCentres {
    [[nodiscard]] static Result<Scheme> scheme(const NCube& cube);
    [[nodiscard]] static EdgeDisplays reference_displays(const NCube& cube, const Scheme& scheme);
    [[nodiscard]] static Result<EdgeDisplays> displays(const NCube& cube, const Scheme& scheme);
};

// An even size: nothing is fixed. The scheme comes from the corners, the
// edges go home - or, when the corners are an odd arrangement of edges away
// from home, two edges trade places - whichever makes the reduced cube a
// solvable 3x3.
struct FreeCentres {
    [[nodiscard]] static Result<Scheme> scheme(const NCube& cube);
    [[nodiscard]] static EdgeDisplays reference_displays(const NCube& cube, const Scheme& scheme);
    [[nodiscard]] static Result<EdgeDisplays> displays(const NCube& cube, const Scheme& scheme);
};

static_assert(TargetPolicy<FixedCentres>);
static_assert(TargetPolicy<FreeCentres>);

// A 3-cycle cannot make an odd permutation of the wings of an orbit; a
// quarter turn of a layer that holds that orbit's wings does. This is asked
// for each orbit that needs one and must return a turn that moves the wings of
// that orbit - and of no other - so that the orbit's permutation changes
// parity. The default finds one by looking; a size whose parity is known to
// have a shorter fix can replace it.
using ParityHandler = std::function<Result<SliceTurn>(const Geometry&, std::size_t wing_orbit)>;

// The first quarter turn of an inner layer perpendicular to `axis` that moves
// exactly the wings of `wing_orbit`.
[[nodiscard]] ParityHandler inner_layer_parity(Axis axis = Axis::X);

// The stage signature, and what may be one.
using StageFn = std::function<Result<void>(Context&)>;

template <typename S>
concept StageLike = std::copy_constructible<S> && requires(const S& stage, Context& context) {
    { stage(context) } -> std::same_as<Result<void>>;
};

// Stages. Each is a factory for a callable value; compose them in a Strategy.

// Chooses the colour of every face, and the reference for what an edge shows,
// with `policy`. Does not turn the cube.
template <TargetPolicy P>
[[nodiscard]] StageFn plan(P policy);

// Chooses what every edge will show, with `policy`, from the cube as it is:
// run after the centres, which may have moved the corners and with them what
// the cube needs to be a solvable 3x3. Does not turn the cube.
template <TargetPolicy P>
[[nodiscard]] StageFn plan_edges(P policy);

// Makes each wing orbit's permutation even, with `handler`.
[[nodiscard]] StageFn make_wings_even(ParityHandler handler = inner_layer_parity());

// Puts every centre sticker on its face's colour. See solve_centre_stickers():
// a search over short words that may disturb the wings and corners, which the
// stages after this one put right.
[[nodiscard]] StageFn solve_centres();

// Puts every wing where its edge shows the display it should, by 3-cycles.
[[nodiscard]] StageFn solve_edges();

// How the core stage reads the reduced cube as a 3x3.
enum class CoreView : std::uint8_t {
    // With every colour renamed so each centre shows its face's canonical
    // colour (NCube::to_cube3): a cube held in any orientation is solved. What
    // a size above 3 needs, since nothing fixes its colours' places.
    Renamed,
    // Exactly as held (NCube::to_cube3_as_held, size 3 only): a cube whose
    // centres are not where the 3x3 solvers' scheme puts them is refused, as
    // the app has always refused a scan assembled into the wrong slots.
    AsHeld,
};

// Solves the 3x3 the reduced cube is, with Context::method.
[[nodiscard]] StageFn solve_core(CoreView view = CoreView::Renamed);

// Solves a 2x2 as one search.
[[nodiscard]] StageFn solve_corners();

class Strategy {
public:
    struct Entry {
        std::string name;
        StageFn run;
    };

    Strategy() = default;
    explicit Strategy(std::string name) : name_(std::move(name)) {}

    template <StageLike S>
    Strategy& then(std::string stage_name, S stage) {
        entries_.push_back(Entry{.name = std::move(stage_name), .run = StageFn(std::move(stage))});
        return *this;
    }

    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] std::span<const Entry> stages() const noexcept { return entries_; }

private:
    std::string name_;
    std::vector<Entry> entries_;
};

// The strategy for a cube of `size`: the table at the top of this file.
[[nodiscard]] Strategy default_strategy(int size);

// Runs `strategy` on `scrambled` and returns the moves, checked: the moves are
// applied to a copy of the cube and it must come out solved.
//
// Fails with invalid_cube_state when the stickers cannot be a cube's, and
// unsolvable_state when they are valid but unreachable (a twisted corner, say).
[[nodiscard]] Result<Solution> solve(const NCube& scrambled, const Strategy& strategy,
                                     solver::SolveMethod method = solver::SolveMethod::Advanced);

// Solves with the default strategy for the cube's size. For sizes 3 and up,
// `method` picks the 3x3 method of the last stage, so a reduction's output is
// read the way any other 3x3 solution is.
[[nodiscard]] Result<Solution> solve(const NCube& scrambled,
                                     solver::SolveMethod method = solver::SolveMethod::Advanced);

// --- template definitions ----------------------------------------------------

namespace detail {

[[nodiscard]] Result<void> plan_scheme(Context& context, const Scheme& scheme,
                                       const EdgeDisplays& reference);
[[nodiscard]] Result<void> plan_displays(Context& context, Result<EdgeDisplays> displays);

}  // namespace detail

template <TargetPolicy P>
StageFn plan(P policy) {
    return [policy = std::move(policy)](Context& context) -> Result<void> {
        const Result<Scheme> scheme = policy.scheme(context.cube);
        if (!scheme) {
            return std::unexpected(scheme.error());
        }
        const EdgeDisplays reference = policy.reference_displays(context.cube, *scheme);
        return detail::plan_scheme(context, *scheme, reference);
    };
}

template <TargetPolicy P>
StageFn plan_edges(P policy) {
    return [policy = std::move(policy)](Context& context) -> Result<void> {
        if (!context.targets) {
            return std::unexpected(CubeError::no_solution);
        }
        return detail::plan_displays(context,
                                     policy.displays(context.cube, context.targets->scheme));
    };
}

}  // namespace cube::nxn
