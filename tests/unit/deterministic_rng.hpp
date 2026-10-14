#pragma once

#include <cstdint>
#include <random>

namespace cube::test {

// A Mersenne twister on a seed the test names, on purpose: the same sequence
// on every run and every standard library (the engine is specified exactly;
// only the distributions are not), so a failure reproduces. Written as a
// function because a constant seed written into a declaration is what
// bugprone-random-generator-seed exists to flag in production code, where it
// would be a bug; here the constant is the point.
[[nodiscard]] inline std::mt19937 deterministic_rng(std::uint32_t seed) {
    return std::mt19937{seed};
}

}  // namespace cube::test
