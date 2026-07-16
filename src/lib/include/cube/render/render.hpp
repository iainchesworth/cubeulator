#pragma once

#include <cube/error.hpp>

namespace cube::render {

// Placeholder entry point for shared render logic (shading model, step
// layout) that is agnostic of the actual GPU calls made in src/render/.
[[nodiscard]] Result<void> not_yet_implemented();

}  // namespace cube::render
