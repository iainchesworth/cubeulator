#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include <cube/nxn/commutator.hpp>
#include <cube/nxn/strategy.hpp>

#include "centres.hpp"
#include "two_by_two.hpp"

namespace cube::nxn {

namespace {

constexpr std::size_t kColourCount = 6;

// A wing's identity as one number: its primary sticker's colour, then its
// secondary's. Two wings of an orbit always differ in this (a wing's two
// stickers are told apart by orbit, so the pair is ordered), which is what
// lets a wing be put in a particular slot rather than just a kind of one.
[[nodiscard]] constexpr int wing_label(Color primary, Color secondary) noexcept {
    return (static_cast<int>(primary) * static_cast<int>(kColourCount)) +
           static_cast<int>(secondary);
}

[[nodiscard]] Color shown_on(const EdgeDisplay& display, const EdgeLocation& location,
                             Face face) noexcept {
    return face == location.a ? display.on_a : display.on_b;
}

// The wing label a slot must hold for its edge to show its display.
[[nodiscard]] int target_label(const Geometry& geometry, const WingSlot& slot,
                               const EdgeDisplays& displays) noexcept {
    const EdgeLocation& location = geometry.edge_location(slot.location);
    const EdgeDisplay& display = displays.at(slot.location);
    return wing_label(shown_on(display, location, geometry.face_of(slot.primary)),
                      shown_on(display, location, geometry.face_of(slot.secondary)));
}

[[nodiscard]] EdgeDisplays home_displays(const Scheme& scheme, const Geometry& geometry) noexcept {
    EdgeDisplays displays{};
    for (std::size_t loc = 0; loc < displays.size(); ++loc) {
        const EdgeLocation& where = geometry.edge_location(loc);
        displays.at(loc) = EdgeDisplay{
            .on_a = scheme.at(static_cast<std::size_t>(where.a)),
            .on_b = scheme.at(static_cast<std::size_t>(where.b)),
        };
    }
    return displays;
}

[[nodiscard]] EdgeDisplays mid_edge_displays(const NCube& cube) noexcept {
    const Geometry& geometry = cube.geometry();
    EdgeDisplays displays{};
    for (std::size_t loc = 0; loc < displays.size(); ++loc) {
        const EdgeLocation& where = geometry.edge_location(loc);
        EdgeDisplay display;
        for (const std::uint16_t f : geometry.mid_edge(loc)) {
            if (geometry.face_of(f) == where.a) {
                display.on_a = cube.facelet(f);
            } else {
                display.on_b = cube.facelet(f);
            }
        }
        displays.at(loc) = display;
    }
    return displays;
}

// Would the cube be a solvable 3x3 once its centres show `scheme` and its
// wings show `displays`? Asked of a copy that is made to look that way,
// because the corners (which no 3-cycle of centres or wings touches) and the
// middle cubies are what decide it.
[[nodiscard]] bool reduces_to_solvable(const NCube& cube, const Scheme& scheme,
                                       const EdgeDisplays& displays) {
    const Geometry& geometry = cube.geometry();
    std::vector<Color> facelets(cube.facelets().begin(), cube.facelets().end());
    for (std::size_t i = 0; i < facelets.size(); ++i) {
        if (geometry.role(i) == Role::Centre) {
            facelets.at(i) = scheme.at(static_cast<std::size_t>(geometry.face_of(i)));
        }
    }
    for (const std::vector<WingSlot>& orbit : geometry.wing_orbits()) {
        for (const WingSlot& slot : orbit) {
            const EdgeLocation& location = geometry.edge_location(slot.location);
            const EdgeDisplay& display = displays.at(slot.location);
            for (const std::uint16_t f : {slot.primary, slot.secondary}) {
                facelets.at(f) = shown_on(display, location, geometry.face_of(f));
            }
        }
    }
    const auto hypothetical = NCube::from_facelets(cube.size(), facelets);
    if (!hypothetical) {
        return false;
    }
    const auto as_3x3 = hypothetical->to_cube3();
    return as_3x3.has_value() && as_3x3->is_solvable();
}

[[nodiscard]] Labels wing_labels(const NCube& cube, const std::vector<WingSlot>& orbit) {
    Labels labels;
    labels.reserve(orbit.size());
    for (const WingSlot& slot : orbit) {
        labels.push_back(wing_label(cube.facelet(slot.primary), cube.facelet(slot.secondary)));
    }
    return labels;
}

[[nodiscard]] Labels wing_targets(const Geometry& geometry, const std::vector<WingSlot>& orbit,
                                  const EdgeDisplays& displays) {
    Labels labels;
    labels.reserve(orbit.size());
    for (const WingSlot& slot : orbit) {
        labels.push_back(target_label(geometry, slot, displays));
    }
    return labels;
}

// The sign of the permutation that takes the wings of one orbit from where
// they are to where they must be; 0 if the two are not the same set.
[[nodiscard]] int wing_sign(const Labels& current, const Labels& target) {
    Permutation gather(target.size());
    for (std::size_t k = 0; k < target.size(); ++k) {
        const auto from = std::ranges::find(current, target.at(k));
        if (from == current.end()) {
            return 0;
        }
        gather.at(k) = static_cast<std::uint16_t>(from - current.begin());
    }
    return sign(gather);
}

void turn(Context& context, SliceTurn t, Step& into) {
    context.cube.apply(t);
    into.moves.push_back(to_layer_move(t, context.cube.size()));
}

[[nodiscard]] Result<const Targets*> targets_of(const Context& context) {
    if (!context.targets) {
        // A strategy that reaches a stage that needs targets without having
        // planned them is a mistake in the strategy, not in the cube.
        return std::unexpected(CubeError::no_solution);
    }
    return &*context.targets;
}

}  // namespace

// --- Target policies --------------------------------------------------------

Result<Scheme> FixedCentres::scheme(const NCube& cube) {
    return scheme_of(cube);
}

EdgeDisplays FixedCentres::reference_displays(const NCube& cube, const Scheme& /*scheme*/) {
    return mid_edge_displays(cube);
}

Result<EdgeDisplays> FixedCentres::displays(const NCube& cube, const Scheme& scheme) {
    const EdgeDisplays mid = mid_edge_displays(cube);
    if (reduces_to_solvable(cube, scheme, mid)) {
        return mid;
    }
    return std::unexpected(CubeError::unsolvable_state);
}

Result<Scheme> FreeCentres::scheme(const NCube& cube) {
    return scheme_of(cube);
}

EdgeDisplays FreeCentres::reference_displays(const NCube& cube, const Scheme& scheme) {
    return home_displays(scheme, cube.geometry());
}

Result<EdgeDisplays> FreeCentres::displays(const NCube& cube, const Scheme& scheme) {
    const EdgeDisplays home = home_displays(scheme, cube.geometry());
    if (reduces_to_solvable(cube, scheme, home)) {
        return home;
    }
    // Two edges trade places, each in whichever orientation: one of the four
    // is a solvable 3x3 when the cube's corners are an odd permutation away.
    for (int flips = 0; flips < 4; ++flips) {
        EdgeDisplays traded = home;
        const EdgeDisplay first = home.at(0);
        const EdgeDisplay second = home.at(1);
        traded.at(1) = flips % 2 != 0 ? EdgeDisplay{.on_a = first.on_b, .on_b = first.on_a} : first;
        traded.at(0) =
            flips / 2 != 0 ? EdgeDisplay{.on_a = second.on_b, .on_b = second.on_a} : second;
        if (reduces_to_solvable(cube, scheme, traded)) {
            return traded;
        }
    }
    return std::unexpected(CubeError::unsolvable_state);
}

// --- Parity -----------------------------------------------------------------

ParityHandler inner_layer_parity(Axis axis) {
    return [axis](const Geometry& geometry, std::size_t group) -> Result<SliceTurn> {
        const auto& orbits = geometry.wing_orbits();
        for (int layer = 1; layer < geometry.size() - 1; ++layer) {
            const SliceTurn candidate{.axis = axis, .layer = layer, .turns = 1};
            const Permutation& moves = geometry.turn(candidate);
            std::vector<bool> touches(orbits.size(), false);
            for (std::size_t g = 0; g < orbits.size(); ++g) {
                for (const WingSlot& slot : orbits.at(g)) {
                    touches.at(g) = touches.at(g) || moves.at(slot.primary) != slot.primary;
                }
            }
            if (std::ranges::count(touches, true) == 1 && touches.at(group)) {
                return candidate;
            }
        }
        return std::unexpected(CubeError::no_solution);
    };
}

// --- Stages -----------------------------------------------------------------

namespace detail {

Result<void> plan_scheme(Context& context, const Scheme& scheme, const EdgeDisplays& reference) {
    context.targets = Targets{.scheme = scheme, .reference = reference, .displays = std::nullopt};
    return {};
}

Result<void> plan_displays(Context& context, Result<EdgeDisplays> displays) {
    if (!displays) {
        return std::unexpected(displays.error());
    }
    if (!context.targets) {
        return std::unexpected(CubeError::no_solution);
    }
    context.targets->displays = *displays;
    return {};
}

}  // namespace detail

StageFn make_wings_even(ParityHandler handler) {
    return [handler = std::move(handler)](Context& context) -> Result<void> {
        const Result<const Targets*> targets = targets_of(context);
        if (!targets) {
            return std::unexpected(targets.error());
        }
        const Geometry& geometry = context.cube.geometry();
        Step step{.stage = Stage::Parity, .core_stage = solver::Stage::Cross, .moves = {}};
        const auto& orbits = geometry.wing_orbits();
        for (std::size_t g = 0; g < orbits.size(); ++g) {
            const int parity =
                wing_sign(wing_labels(context.cube, orbits.at(g)),
                          wing_targets(geometry, orbits.at(g), (*targets)->reference));
            if (parity == 0) {
                return std::unexpected(CubeError::invalid_cube_state);
            }
            if (parity > 0) {
                continue;
            }
            const Result<SliceTurn> odd = handler(geometry, g);
            if (!odd) {
                return std::unexpected(odd.error());
            }
            turn(context, *odd, step);
        }
        if (!step.moves.empty()) {
            context.steps.push_back(std::move(step));
        }
        return {};
    };
}

StageFn solve_centres() {
    return [](Context& context) -> Result<void> {
        const Result<const Targets*> targets = targets_of(context);
        if (!targets) {
            return std::unexpected(targets.error());
        }
        Result<Word> word = detail::solve_centre_stickers(context.cube, (*targets)->scheme);
        if (!word) {
            return std::unexpected(word.error());
        }
        Step step{.stage = Stage::Centres, .core_stage = solver::Stage::Cross, .moves = {}};
        step.moves.reserve(word->size());
        for (const SliceTurn t : *word) {
            step.moves.push_back(to_layer_move(t, context.cube.size()));
        }
        if (!step.moves.empty()) {
            context.steps.push_back(std::move(step));
        }
        return {};
    };
}

StageFn solve_edges() {
    return [](Context& context) -> Result<void> {
        const Result<const Targets*> targets = targets_of(context);
        if (!targets) {
            return std::unexpected(targets.error());
        }
        if (!(*targets)->displays) {
            // plan_edges() has not run: a mistake in the strategy.
            return std::unexpected(CubeError::no_solution);
        }
        const EdgeDisplays& displays = *(*targets)->displays;
        const Geometry& geometry = context.cube.geometry();
        const CycleLibrary& library = CycleLibrary::of(context.cube.size());
        Step step{.stage = Stage::Edges, .core_stage = solver::Stage::Cross, .moves = {}};
        const auto& orbits = geometry.wing_orbits();
        for (std::size_t g = 0; g < orbits.size(); ++g) {
            std::vector<std::uint16_t> cells;
            for (const WingSlot& slot : orbits.at(g)) {
                cells.push_back(slot.primary);
            }
            Labels current = wing_labels(context.cube, orbits.at(g));
            const Labels target = wing_targets(geometry, orbits.at(g), displays);
            const Result<Word> word = arrange(cells, library.wings(g), current, target);
            if (!word) {
                return std::unexpected(word.error());
            }
            for (const SliceTurn t : *word) {
                turn(context, t, step);
            }
        }
        if (!step.moves.empty()) {
            context.steps.push_back(std::move(step));
        }
        return {};
    };
}

StageFn solve_core() {
    return [](Context& context) -> Result<void> {
        const Result<solver::Cube> core_cube = context.cube.to_cube3();
        if (!core_cube) {
            return std::unexpected(core_cube.error());
        }
        Result<solver::Solution> core = solver::solve(*core_cube, context.method);
        if (!core) {
            return std::unexpected(core.error());
        }
        for (const solver::SolveStep& core_step : core->steps) {
            Step step{.stage = Stage::Core, .core_stage = core_step.stage, .moves = {}};
            for (const solver::Move move : core_step.moves) {
                const LayerMove layer{.face = move.face, .depth = 0, .turns = move.turns};
                context.cube.apply(layer);
                step.moves.push_back(layer);
            }
            context.steps.push_back(std::move(step));
        }
        return {};
    };
}

StageFn solve_corners() {
    return [](Context& context) -> Result<void> {
        Result<std::vector<LayerMove>> moves = detail::solve_two_by_two(context.cube);
        if (!moves) {
            return std::unexpected(moves.error());
        }
        context.cube.apply(std::span<const LayerMove>{*moves});
        context.steps.push_back(Step{
            .stage = Stage::Corners,
            .core_stage = solver::Stage::Cross,
            .moves = std::move(*moves),
        });
        return {};
    };
}

}  // namespace cube::nxn
