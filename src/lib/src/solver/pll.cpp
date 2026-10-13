#include <cube/solver/pll.hpp>

#include <array>
#include <vector>

#include <cube/profiling.hpp>
#include <cube/solver/oll.hpp>

#include "last_layer.hpp"

namespace cube::solver {

namespace {

struct CornerSlot {
    int u_index;
    Face side_a;
    int side_a_index;
    Face side_b;
    int side_b_index;
};

// U's corner index is (row=z+1)*3+(col=x+1); each side face's U-adjacent
// corner index is (row=1-y)*3+col with y=1, i.e. always row 0 - see
// cube.cpp's layout derivation.
constexpr std::array<CornerSlot, 4> kCornerSlots = {{
    {.u_index = 8,
     .side_a = Face::F,
     .side_a_index = 2,
     .side_b = Face::R,
     .side_b_index = 2},  // UFR
    {.u_index = 6,
     .side_a = Face::F,
     .side_a_index = 0,
     .side_b = Face::L,
     .side_b_index = 2},  // UFL
    {.u_index = 2,
     .side_a = Face::B,
     .side_a_index = 2,
     .side_b = Face::R,
     .side_b_index = 0},  // UBR
    {.u_index = 0,
     .side_a = Face::B,
     .side_a_index = 0,
     .side_b = Face::L,
     .side_b_index = 0},  // UBL
}};

bool corner_slot_permuted(const Cube& cube, const CornerSlot& slot) noexcept {
    return cube.at(slot.side_a, slot.side_a_index) == canonical_color(slot.side_a) &&
           cube.at(slot.side_b, slot.side_b_index) == canonical_color(slot.side_b);
}

bool corners_permuted(const Cube& cube) noexcept {
    if (!oll_solved(cube)) {
        return false;
    }
    for (const CornerSlot& slot : kCornerSlots) {
        if (!corner_slot_permuted(cube, slot)) {
            return false;
        }
    }
    return true;
}

}  // namespace

Result<std::vector<Move>> solve_pll(const Cube& start) {
    CUBE_ZONE_SCOPED_N("solver.pll");
    std::vector<Move> solution;
    Cube cube = start;

    // Stage 1: permute the U corners into their solved slots. The A-perm
    // corner 3-cycle plus U alignment reaches every corner permutation -
    // see last_layer.hpp for why this composition is bounded to
    // milliseconds, unlike the per-scramble commutator search it replaced
    // (whose worst PLL cases ran minutes-to-hours).
    const std::vector<std::vector<Move>> corner_alphabet = {
        detail::kAdjustU, detail::kAdjustU2, detail::kAdjustUPrime, detail::kCornerCycle};
    Result<std::vector<Move>> corners_step =
        detail::algorithm_search(cube, corner_alphabet, corners_permuted, 12);
    if (!corners_step.has_value()) {
        return std::unexpected(corners_step.error());
    }
    for (const Move& m : *corners_step) {
        cube = cube.apply(m);
        solution.push_back(m);
    }

    // Stage 2: permute the U edges, finishing the cube. With corners
    // already solved the remaining edge permutation is always even (a
    // legal cube's corner and edge parities match), so the U-perm edge
    // 3-cycle plus U alignment always completes it.
    auto fully_solved = [](const Cube& c) { return c.is_solved(); };
    const std::vector<std::vector<Move>> edge_alphabet = {
        detail::kAdjustU, detail::kAdjustU2, detail::kAdjustUPrime, detail::kEdgeCycle};
    Result<std::vector<Move>> edges_step =
        detail::algorithm_search(cube, edge_alphabet, fully_solved, 12);
    if (!edges_step.has_value()) {
        return std::unexpected(edges_step.error());
    }
    for (const Move& m : *edges_step) {
        cube = cube.apply(m);
        solution.push_back(m);
    }

    return solution;
}

}  // namespace cube::solver
