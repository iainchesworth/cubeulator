# ---------------------------------------------------------------------------
# Tracy.cmake
#
# Defines an INTERFACE target `cube::tracy` that, when CUBEULATOR_ENABLE_TRACY
# is on, links Tracy's client library. The zone macros the sources use
# (cube/profiling.hpp) are a pair of identically-pathed headers under
# src/lib/variants/; src/lib/CMakeLists.txt puts exactly one of them on
# cube::lib's include path, chosen by this option and not by an #ifdef, so
# every instrumented call site compiles unchanged either way and the disabled
# variant is a set of empty macros: a normal build pays nothing for the
# instrumentation and does not even need Tracy present.
#
# Off by default, matching Coverage.cmake's shape. Tracy itself is an OPT-IN
# vcpkg manifest feature ("profiling" in vcpkg.json, not a base dependency);
# the top-level CMakeLists.txt adds it to VCPKG_MANIFEST_FEATURES when this
# option is on, so one switch is the whole story:
#
#   cmake --preset windows-msvc-profiling        # or -linux-gcc- / -macos-clang-
#
# The feature also builds Tracy's command-line tools for the HOST (capture and
# csvexport), so a trace can be recorded and dumped headlessly, without the GUI
# profiler. See docs/profiling.md.
#
# Nothing here is for shipping: a Tracy client opens a TCP listener and answers
# UDP discovery broadcasts, which a released binary must not do. Release
# presets leave this off.
# ---------------------------------------------------------------------------

option(CUBEULATOR_ENABLE_TRACY "Enable Tracy profiler instrumentation" OFF)

add_library(cube_tracy INTERFACE)
add_library(cube::tracy ALIAS cube_tracy)

if(CUBEULATOR_ENABLE_TRACY)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(cube_tracy INTERFACE Tracy::TracyClient)

    # Not an error: someone chasing a bug in the profiling build itself wants
    # exactly this. But a number taken from an AddressSanitizer or coverage
    # build is not a number about the program, and nothing else will say so.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CUBEULATOR_ENABLE_COVERAGE)
        message(WARNING
            "CUBEULATOR_ENABLE_TRACY is on for a Debug or coverage build. "
            "Sanitizer and coverage instrumentation dominate the timings Tracy "
            "reports; use a RelWithDebInfo build (the *-profiling presets) for "
            "numbers worth reading.")
    endif()
endif()
