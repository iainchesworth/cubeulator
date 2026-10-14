#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <cube/nxn/geometry.hpp>
#include <cube/solver/solver.hpp>

namespace cube::nxn {

// The stage a step of a solution belongs to.
enum class Stage : std::uint8_t {
    // The odd inner-layer turns that make the wings' permutation even.
    Parity,
    Centres,
    Edges,
    // The 3x3's solution; Step::core_stage says which of its stages.
    Core,
    // A 2x2 solved as one search.
    Corners,
};

struct Step {
    Stage stage = Stage::Centres;
    // Only meaningful when stage is Core.
    solver::Stage core_stage = solver::Stage::Cross;
    std::vector<LayerMove> moves;
};

struct Solution {
    std::vector<Step> steps;

    [[nodiscard]] std::size_t move_count() const noexcept {
        std::size_t total = 0;
        for (const Step& step : steps) {
            total += step.moves.size();
        }
        return total;
    }
    [[nodiscard]] std::vector<LayerMove> moves() const;
};

// The same turns merged where they meet: adjacent turns of the one layer
// become one (a quarter and its inverse vanish), looking past turns of other
// layers of that axis because those commute with it.
[[nodiscard]] std::vector<LayerMove> merged(std::span<const LayerMove> moves, int size);

// The same, over a whole solution: a turn that merges backwards across a step
// boundary stays with the earlier step, so each turn belongs to exactly one.
// Only the end of the solution is guaranteed to be where it was: a merge that
// crosses a boundary changes the cube *at* that boundary.
void merge_steps(std::vector<Step>& steps, int size);

}  // namespace cube::nxn
