# ---------------------------------------------------------------------------
# GenerateVersion.cmake
#
# Stamps the semantic version and git provenance into a generated header.
# Used two ways:
#   * include()'d at configure time so the header exists for the first build;
#   * run with `cmake -P` as a build-time step so the stamp tracks new commits
#     without needing to reconfigure.
#
# Required variables:
#   CUBEULATOR_VERSION - the semver string, e.g. "0.1.0"
#   SRC                - path to version.hpp.in
#   DST                - path to the generated version.hpp
#   WORKDIR            - repository root (for running git)
#   GIT_EXECUTABLE     - path to git (optional; falls back to "unknown" fields)
# ---------------------------------------------------------------------------

set(CUBEULATOR_VERSION_MAJOR 0)
set(CUBEULATOR_VERSION_MINOR 0)
set(CUBEULATOR_VERSION_PATCH 0)
if(CUBEULATOR_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CUBEULATOR_VERSION_MAJOR "${CMAKE_MATCH_1}")
    set(CUBEULATOR_VERSION_MINOR "${CMAKE_MATCH_2}")
    set(CUBEULATOR_VERSION_PATCH "${CMAKE_MATCH_3}")
endif()

# Git provenance defaults (used when git is unavailable, e.g. a source tarball).
set(CUBEULATOR_GIT_COMMIT "unknown")
set(CUBEULATOR_GIT_COMMIT_FULL "unknown")
set(CUBEULATOR_GIT_DESCRIBE "v${CUBEULATOR_VERSION}")
set(CUBEULATOR_GIT_BRANCH "unknown")
set(CUBEULATOR_GIT_DIRTY "false")

if(DEFINED GIT_EXECUTABLE AND GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${WORKDIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE CUBEULATOR_GIT_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE CUBEULATOR_GIT_RESULT)

    if(CUBEULATOR_GIT_RESULT EQUAL 0)
        set(CUBEULATOR_GIT_COMMIT "${CUBEULATOR_GIT_SHORT}")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${WORKDIR}" rev-parse HEAD
            OUTPUT_VARIABLE CUBEULATOR_GIT_COMMIT_FULL
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

        # No --dirty suffix here: dirty state is tracked separately via
        # CUBEULATOR_GIT_DIRTY so callers aren't stuck parsing it back out of
        # a string when they want to render it as e.g. a standalone UI badge.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${WORKDIR}" describe --tags --always
            OUTPUT_VARIABLE CUBEULATOR_GIT_DESCRIBE_RAW
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
            RESULT_VARIABLE CUBEULATOR_DESCRIBE_RESULT)
        if(CUBEULATOR_DESCRIBE_RESULT EQUAL 0)
            set(CUBEULATOR_GIT_DESCRIBE "${CUBEULATOR_GIT_DESCRIBE_RAW}")
        else()
            set(CUBEULATOR_GIT_DESCRIBE "${CUBEULATOR_GIT_COMMIT}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${WORKDIR}" rev-parse --abbrev-ref HEAD
            OUTPUT_VARIABLE CUBEULATOR_GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

        # Ignore untracked files so a populated build tree does not read "dirty".
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${WORKDIR}" status --porcelain --untracked-files=no
            OUTPUT_VARIABLE CUBEULATOR_GIT_STATUS
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT CUBEULATOR_GIT_STATUS STREQUAL "")
            set(CUBEULATOR_GIT_DIRTY "true")
        endif()
    endif()
endif()

configure_file("${SRC}" "${DST}" @ONLY)
