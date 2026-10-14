#include <cube/solver/cross.hpp>

#include <cube/profiling.hpp>

#include "piece_search.hpp"

namespace cube::solver {

bool cross_solved(const Cube& cube) noexcept {
    return cube.at(Face::D, 1) == Color::White && cube.at(Face::D, 3) == Color::White &&
           cube.at(Face::D, 5) == Color::White && cube.at(Face::D, 7) == Color::White &&
           cube.at(Face::F, 7) == cube.at(Face::F, 4) &&
           cube.at(Face::R, 7) == cube.at(Face::R, 4) &&
           cube.at(Face::B, 7) == cube.at(Face::B, 4) && cube.at(Face::L, 7) == cube.at(Face::L, 4);
}

Result<std::vector<Move>> solve_cross(const Cube& start) {
    CUBE_ZONE_SCOPED_N("solver.cross");
    const auto signature = [](const Cube& c) {
        return detail::SignatureKey{.cross = detail::cross_key(c), .rest = 0};
    };
    // The cross is solved exactly when its four edges are home, which is
    // where they stand on a solved cube.
    const detail::PieceGoal goal{.rest_codes = 0, .keys = {signature(Cube::solved())}};
    return detail::search(start, std::vector<Face>(kAllFaces.begin(), kAllFaces.end()),
                          cross_solved, signature, 8, goal);
}

}  // namespace cube::solver
