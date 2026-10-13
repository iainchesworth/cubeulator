#include <cube/solver/cross.hpp>

#include "bfs_search.hpp"

namespace cube::solver {

bool cross_solved(const Cube& cube) noexcept {
    return cube.at(Face::D, 1) == Color::White && cube.at(Face::D, 3) == Color::White &&
           cube.at(Face::D, 5) == Color::White && cube.at(Face::D, 7) == Color::White &&
           cube.at(Face::F, 7) == cube.at(Face::F, 4) &&
           cube.at(Face::R, 7) == cube.at(Face::R, 4) &&
           cube.at(Face::B, 7) == cube.at(Face::B, 4) && cube.at(Face::L, 7) == cube.at(Face::L, 4);
}

Result<std::vector<Move>> solve_cross(const Cube& start) {
    return detail::bfs_search(
        start, std::vector<Face>(kAllFaces.begin(), kAllFaces.end()), cross_solved,
        [](const Cube& c) { return detail::SignatureKey{detail::cross_key(c), 0}; }, 8);
}

}  // namespace cube::solver
