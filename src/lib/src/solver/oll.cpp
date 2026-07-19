#include <cube/solver/oll.hpp>

#include <array>

#include <cube/solver/middle_edges.hpp>

#include "bfs_search.hpp"

namespace cube::solver {

namespace {

bool u_face_solved(const Cube& cube) noexcept {
    const Color target = canonical_color(Face::U);
    for (int index = 0; index < 9; ++index) {
        if (cube.at(Face::U, index) != target) {
            return false;
        }
    }
    return true;
}

bool u_edges_oriented(const Cube& cube) noexcept {
    const Color target = canonical_color(Face::U);
    return cube.at(Face::U, 1) == target && cube.at(Face::U, 3) == target && cube.at(Face::U, 5) == target &&
           cube.at(Face::U, 7) == target;
}

// The faces a commutator's "setup" half (P, see bfs_search.hpp's
// commutator_search) is built from: every face except D (never needed once
// F2L is solved). U is included here too, not just as the middle toggle -
// real algorithms interleave U turns between the F2L-disturbing moves
// rather than using a single U block, and since U never touches F2L,
// including it in P doesn't weaken the restoration guarantee at all.
// Restricting P to only the 4 truly-disturbing faces was tried first and
// couldn't find a solution even at considerable depth.
constexpr std::array<Face, 5> kDisturbingFaces = {Face::U, Face::L, Face::R, Face::F, Face::B};

}  // namespace

bool oll_solved(const Cube& cube) noexcept { return f2l_solved(cube) && u_face_solved(cube); }

Result<std::vector<Move>> solve_oll(const Cube& start) {
    std::vector<Move> solution;
    Cube cube = start;

    const std::vector<Face> disturbing(kDisturbingFaces.begin(), kDisturbingFaces.end());

    auto edges_postcondition = [](const Cube& c) { return u_edges_oriented(c); };
    Result<std::vector<Move>> edges_step = detail::commutator_search(cube, disturbing, edges_postcondition, 6);
    if (!edges_step.has_value()) {
        edges_step = detail::two_commutator_search(cube, disturbing, edges_postcondition, 4);
    }
    if (!edges_step.has_value()) {
        return std::unexpected(edges_step.error());
    }
    for (const Move& m : *edges_step) {
        cube = cube.apply(m);
        solution.push_back(m);
    }

    auto corners_postcondition = [](const Cube& c) { return u_face_solved(c); };
    Result<std::vector<Move>> corners_step = detail::commutator_search(cube, disturbing, corners_postcondition, 6);
    if (!corners_step.has_value()) {
        corners_step = detail::two_commutator_search(cube, disturbing, corners_postcondition, 4);
    }
    if (!corners_step.has_value()) {
        return std::unexpected(corners_step.error());
    }
    for (const Move& m : *corners_step) {
        cube = cube.apply(m);
        solution.push_back(m);
    }

    return solution;
}

}  // namespace cube::solver
