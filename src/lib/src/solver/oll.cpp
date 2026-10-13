#include <cube/solver/oll.hpp>

#include <vector>

#include <cube/solver/middle_edges.hpp>

#include "last_layer.hpp"

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
    return cube.at(Face::U, 1) == target && cube.at(Face::U, 3) == target &&
           cube.at(Face::U, 5) == target && cube.at(Face::U, 7) == target;
}

}  // namespace

bool oll_solved(const Cube& cube) noexcept {
    return f2l_solved(cube) && u_face_solved(cube);
}

Result<std::vector<Move>> solve_oll(const Cube& start) {
    std::vector<Move> solution;
    Cube cube = start;

    // Stage 1: orient the U edges (yellow cross on top). The edge-flip
    // algorithm plus U alignment reaches every edge-orientation case (dot,
    // L-shape, line) in at most three applications - see last_layer.hpp
    // for why this composition is bounded to milliseconds, unlike the
    // per-scramble commutator search it replaced.
    //
    // Both stage postconditions below also gate on f2l_solved. That is
    // redundant while every alphabet word is F2L-safe (they all are -
    // last_layer_test.cpp proves it), but it means a future broken word
    // degrades to a missed solution or no_solution instead of this stage
    // silently returning a move list that scrambles the first two layers -
    // the U-face-only conditions alone can't tell the difference.
    const std::vector<std::vector<Move>> edge_alphabet = {
        detail::kAdjustU, detail::kAdjustU2, detail::kAdjustUPrime, detail::kEdgeOrientFlip};
    auto edges_postcondition = [](const Cube& c) { return f2l_solved(c) && u_edges_oriented(c); };
    Result<std::vector<Move>> edges_step =
        detail::algorithm_search(cube, edge_alphabet, edges_postcondition, 12);
    if (!edges_step.has_value()) {
        return std::unexpected(edges_step.error());
    }
    for (const Move& m : *edges_step) {
        cube = cube.apply(m);
        solution.push_back(m);
    }

    // Stage 2: orient the U corners. Every corner-orientation case is
    // within two Sune/Anti-Sune applications (plus U alignment) of solved;
    // the emitted sequence occasionally spends a third application where
    // that saves U-alignment words (the search minimises words, and an AUF
    // costs the same as a Sune).
    const std::vector<std::vector<Move>> corner_alphabet = {detail::kAdjustU, detail::kAdjustU2,
                                                            detail::kAdjustUPrime, detail::kSune,
                                                            detail::kAntiSune};
    auto corners_postcondition = [](const Cube& c) { return oll_solved(c); };
    Result<std::vector<Move>> corners_step =
        detail::algorithm_search(cube, corner_alphabet, corners_postcondition, 12);
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
