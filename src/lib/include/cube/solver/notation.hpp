#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// Parses one whitespace-free move token in standard cubing notation: a face
// letter (U/D/L/R/F/B) optionally followed by "'" (counter-clockwise) or "2"
// (half turn). Fails with CubeError::invalid_move_sequence on anything else.
[[nodiscard]] Result<Move> parse_move(std::string_view token);

// Parses a whitespace-separated sequence of move tokens, e.g. "R U R' U'".
[[nodiscard]] Result<std::vector<Move>> parse_algorithm(std::string_view text);

[[nodiscard]] std::string to_string(Move move);
[[nodiscard]] std::string to_string(const std::vector<Move>& moves);

// The move sequence that undoes `moves`: reversed order, each move inverted.
[[nodiscard]] std::vector<Move> inverse(const std::vector<Move>& moves);

}  // namespace cube::solver
