#include <cube/solver/pll.hpp>

#include <array>

#include <cube/solver/oll.hpp>

#include "bfs_search.hpp"

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
    {8, Face::F, 2, Face::R, 2},  // UFR
    {6, Face::F, 0, Face::L, 2},  // UFL
    {2, Face::B, 2, Face::R, 0},  // UBR
    {0, Face::B, 0, Face::L, 0},  // UBL
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

// The faces a commutator's setup half is built from - see oll.cpp for why
// U is included alongside the truly-disturbing faces, and why D is
// excluded (never needed once F2L is solved).
constexpr std::array<Face, 5> kDisturbingFaces = {Face::U, Face::L, Face::R, Face::F, Face::B};

}  // namespace

Result<std::vector<Move>> solve_pll(const Cube& start) {
    std::vector<Move> solution;
    Cube cube = start;

    const std::vector<Face> disturbing(kDisturbingFaces.begin(), kDisturbingFaces.end());

    Result<std::vector<Move>> corners_step = detail::commutator_search(cube, disturbing, corners_permuted, 7);
    if (!corners_step.has_value()) {
        corners_step = detail::two_commutator_search(cube, disturbing, corners_permuted, 5);
    }
    if (!corners_step.has_value()) {
        return std::unexpected(corners_step.error());
    }
    for (const Move& m : *corners_step) {
        cube = cube.apply(m);
        solution.push_back(m);
    }

    auto fully_solved = [](const Cube& c) { return c.is_solved(); };
    Result<std::vector<Move>> edges_step = detail::commutator_search(cube, disturbing, fully_solved, 7);
    if (!edges_step.has_value()) {
        edges_step = detail::two_commutator_search(cube, disturbing, fully_solved, 5);
    }
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
