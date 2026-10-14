# ---------------------------------------------------------------------------
# Coverage.cmake
#
# Defines an INTERFACE target `cube::coverage` that, when
# CUBEULATOR_ENABLE_COVERAGE is on, turns on gcov-style source-based coverage
# instrumentation (GCC/Clang) via --coverage. Off by default: only the
# dedicated linux-gcc-coverage preset turns it on, so normal dev/CI builds
# pay no instrumentation cost.
#
# Link it PRIVATE into every first-party target whose coverage should be
# measured (library, app core, test executables).
# ---------------------------------------------------------------------------

option(CUBEULATOR_ENABLE_COVERAGE "Enable gcov/llvm-cov source coverage instrumentation" OFF)

add_library(cube_coverage INTERFACE)
add_library(cube::coverage ALIAS cube_coverage)

if(CUBEULATOR_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # -fprofile-update=atomic: the test binaries run threads (the solver
        # tests call cfop from several threads, the capture loop and the async
        # recorder have their own), and gcov's default non-atomic counter
        # updates then race. gcov derives the counts of arcs it does not
        # instrument by subtraction, so a lost update shows up as a negative
        # count, and gcovr aborts the report with "NegativeHits ... Got negative
        # hit value" (gcc bug 68080) before the 80% gate is evaluated.
        target_compile_options(cube_coverage INTERFACE --coverage -fprofile-update=atomic -fno-inline)
        target_link_options(cube_coverage INTERFACE --coverage)
    else()
        message(WARNING
            "CUBEULATOR_ENABLE_COVERAGE is on but ${CMAKE_CXX_COMPILER_ID} is not "
            "GCC/Clang; coverage instrumentation is not supported on this "
            "compiler and will be skipped.")
    endif()
endif()
