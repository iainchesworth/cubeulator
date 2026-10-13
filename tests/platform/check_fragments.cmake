# ---------------------------------------------------------------------------
# Every platform fragment satisfies the platform contract - on whichever host
# this runs.
#
#   cmake -DREPO_ROOT=<repository root> -P tests/platform/check_fragments.cmake
#
# A build only ever includes the one fragment for the platform it targets, so
# the other four are exercised by their own CI legs alone, and a typo in one
# (a source file that was renamed, a hook that was never defined) would surface
# there, late, if at all. This holds every src/app/platform/*/platform.cmake to
# cmake/PlatformContract.cmake: every variable and hook defined, every file it
# compiles present. It finds the platforms by globbing, so a platform added
# tomorrow is checked without anyone listing it here.
#
# Each fragment is checked in a process of its own (the script re-invokes
# itself with -DFRAGMENT=...): a function a previous fragment defined stays
# defined for the life of a CMake process, which would let a fragment that
# forgot a hook pass on another's.
#
# It also checks the selector (cmake/Platform.cmake) knows each of them, since
# "a directory and a branch in the selector" is what adding a platform is.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.28)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "Pass -DREPO_ROOT=<repository root>.")
endif()

set(PROJECT_SOURCE_DIR "${REPO_ROOT}")
set(_app_dir "${REPO_ROOT}/src/app")
list(APPEND CMAKE_MODULE_PATH "${REPO_ROOT}/cmake")
include(PlatformContract)

# ---- child mode: check one fragment, in this (fresh) process ----------------
if(FRAGMENT)
    get_filename_component(CUBEULATOR_PLATFORM_DIR "${FRAGMENT}" DIRECTORY)
    get_filename_component(CUBEULATOR_PLATFORM "${CUBEULATOR_PLATFORM_DIR}" NAME)

    include("${FRAGMENT}")
    cube_platform_check_contract("${FRAGMENT}" "${_app_dir}")

    list(LENGTH CUBEULATOR_PLATFORM_SOURCES _sources)
    message(STATUS "platform '${CUBEULATOR_PLATFORM}': contract satisfied (${_sources} sources)")
    return()
endif()

# ---- driver mode -------------------------------------------------------------

# The selector's branches, as the platform names they select. Read as text:
# Platform.cmake decides from the toolchain's own flags, which a script run on
# one host cannot set up for another.
file(READ "${REPO_ROOT}/cmake/Platform.cmake" _selector)
string(REGEX MATCHALL "set[(]CUBEULATOR_PLATFORM [a-z0-9_]+[)]" _branches "${_selector}")
set(_selected "")
foreach(_branch IN LISTS _branches)
    string(REGEX REPLACE "set[(]CUBEULATOR_PLATFORM ([a-z0-9_]+)[)]" "\\1" _name "${_branch}")
    list(APPEND _selected "${_name}")
endforeach()
if(NOT _selected)
    message(FATAL_ERROR "Found no branches in ${REPO_ROOT}/cmake/Platform.cmake.")
endif()

file(GLOB _fragments "${_app_dir}/platform/*/platform.cmake")
if(NOT _fragments)
    message(FATAL_ERROR "No src/app/platform/*/platform.cmake found under ${REPO_ROOT}.")
endif()

set(_failures "")
foreach(_fragment IN LISTS _fragments)
    get_filename_component(_dir "${_fragment}" DIRECTORY)
    get_filename_component(_name "${_dir}" NAME)

    if(NOT _name IN_LIST _selected)
        list(APPEND _failures
            "src/app/platform/${_name}/ has a platform.cmake but cmake/Platform.cmake has no branch that selects it.")
        continue()
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DREPO_ROOT=${REPO_ROOT}" "-DFRAGMENT=${_fragment}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error)
    if(_result EQUAL 0)
        string(STRIP "${_output}" _output)
        string(REGEX REPLACE "^-- " "" _output "${_output}")
        message(STATUS "${_output}")
    else()
        string(STRIP "${_error}" _error)
        list(APPEND _failures "${_name}: ${_error}")
    endif()
endforeach()

# And the reverse: a branch in the selector with no directory behind it.
foreach(_name IN LISTS _selected)
    if(NOT EXISTS "${_app_dir}/platform/${_name}/platform.cmake")
        list(APPEND _failures
            "cmake/Platform.cmake selects '${_name}', but src/app/platform/${_name}/platform.cmake does not exist.")
    endif()
endforeach()

if(_failures)
    list(JOIN _failures "\n\n" _report)
    message(FATAL_ERROR "Platform contract not satisfied:\n\n${_report}")
endif()
