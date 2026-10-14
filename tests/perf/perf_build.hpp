#pragma once

// What kind of build the performance tests are running in, as far as
// measuring it goes. CMake decides (tests/CMakeLists.txt, the cube_perf_tests
// block) and passes the facts as compile definitions; nothing here asks the
// compiler or the OS, so no preprocessor conditional is needed.
//
//   kOptimised         the configuration is Release, RelWithDebInfo or
//                      MinSizeRel. Everything in this tier is sized for an
//                      optimised build (a full corpus is seconds); in Debug
//                      it is minutes, and a Debug build has nothing to add -
//                      the node counts are the same in every build.
//   kInstrumented      coverage or a sanitizer is compiled in. The code is
//                      what it was, but a clock reading is not about it.
//   kTimingMeaningful  a stopwatch can be read as a statement about the
//                      program.

#include <string_view>

namespace cube::perf {

inline constexpr bool kOptimised = CUBE_PERF_OPTIMISED != 0;
inline constexpr bool kInstrumented = CUBE_PERF_INSTRUMENTED != 0;
inline constexpr bool kTimingMeaningful = kOptimised && !kInstrumented;

// The configuration name, for the message a skipped case prints.
inline constexpr std::string_view kBuildType = CUBE_PERF_BUILD_TYPE;

}  // namespace cube::perf
