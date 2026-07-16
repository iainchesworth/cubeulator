#pragma once

#include <cube/error.hpp>

namespace cube::solver {

// Placeholder entry point for the cube state model + staged/Kociemba solver.
// Real logic lands in a later spec; this exists so the target links and is
// unit-testable end to end.
[[nodiscard]] Result<void> not_yet_implemented();

}  // namespace cube::solver
