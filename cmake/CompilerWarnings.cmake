# ---------------------------------------------------------------------------
# CompilerWarnings.cmake
#
# Defines an INTERFACE target `cube::warnings` that turns on a strict,
# cross-compiler warning set with "warnings as errors". Link it PRIVATE-ly
# into every first-party target (library, app, tests). Third-party code (Qt,
# vcpkg packages) is pulled in as SYSTEM headers by their package configs, so
# these flags never fire on dependency code.
# ---------------------------------------------------------------------------

add_library(cube_warnings INTERFACE)
add_library(cube::warnings ALIAS cube_warnings)

# The mandated baseline is -Wall -Wextra -Werror; the remaining flags catch the
# classes of mistake (shadowing, dubious conversions, missing overrides) that a
# generator is most likely to introduce.
set(CUBEULATOR_GNU_CLANG_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wformat=2)

set(CUBEULATOR_MSVC_WARNINGS
    /W4
    /WX
    /permissive-)

# clang-cl reports CXX_COMPILER_ID "Clang" (it would otherwise get
# CUBEULATOR_GNU_CLANG_WARNINGS below), but its -Wall alias is bound to
# MSVC's /Wall semantics - "warning level 4 and everything disabled by
# default" - not plain clang's moderate -Wall. That silently turns on
# off-by-default groups like -Wc++98-compat, which then fail the build on
# ordinary C++23 code (scoped enums, [[nodiscard]], nested namespaces, ...).
# /W4 is what clang-cl itself recommends in place of -Wall for this reason.
set(CUBEULATOR_CLANG_CL_WARNINGS
    /W4
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wnon-virtual-dtor
    -Woverloaded-virtual
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wformat=2)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(cube_warnings INTERFACE ${CUBEULATOR_CLANG_CL_WARNINGS})
else()
    target_compile_options(cube_warnings INTERFACE
        "$<$<CXX_COMPILER_ID:MSVC>:${CUBEULATOR_MSVC_WARNINGS}>"
        "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:${CUBEULATOR_GNU_CLANG_WARNINGS}>")
endif()
