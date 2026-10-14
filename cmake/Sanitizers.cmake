# ---------------------------------------------------------------------------
# Sanitizers.cmake
#
# Defines an INTERFACE target `cube::sanitizers` that, for Debug builds,
# turns on AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) or
# AddressSanitizer (MSVC), plus standard-library hardening assertions. All
# flags are guarded by $<CONFIG:Debug>, so Release builds are unaffected.
#
# Link it PRIVATE into every first-party target (library, app, tests) so both
# the instrumentation (compile) and the runtime (link) are applied.
# ---------------------------------------------------------------------------

option(CUBEULATOR_ENABLE_SANITIZERS "Enable ASan/UBSan for Debug builds" ON)

add_library(cube_sanitizers INTERFACE)
add_library(cube::sanitizers ALIAS cube_sanitizers)

if(CUBEULATOR_ENABLE_SANITIZERS)
    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang-cl: LLVM's bundled ASan runtime on Windows requires the
        # release CRT, which is incompatible with this project's debug-CRT
        # vcpkg/Qt dependencies. Real MSVC (the elseif(MSVC) branch) already
        # supports ASan together with the debug CRT natively.
        message(STATUS
            "ASan/UBSan disabled for clang-cl: LLVM's Windows ASan runtime "
            "requires the release CRT, which is incompatible with this "
            "project's debug-CRT vcpkg/Qt dependencies. Use windows-msvc for "
            "sanitizer coverage.")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(CUBEULATOR_SANITIZER_FLAGS
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all)
        target_compile_options(cube_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:${CUBEULATOR_SANITIZER_FLAGS}>")
        target_link_options(cube_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:-fsanitize=address,undefined>")
    elseif(MSVC)
        # MSVC ships AddressSanitizer; UBSan is not available.
        target_compile_options(cube_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:/fsanitize=address>")
        # MSVC ASan turns on STL container-overflow annotations, which
        # require every linked library to also be ASan-instrumented, or the
        # link fails with LNK2038 mismatches. vcpkg builds its dependencies
        # without ASan, so disable the STL annotations wholesale to match.
        target_compile_definitions(cube_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:_DISABLE_STL_ANNOTATION>")
        # /RTC (runtime checks) and incremental linking are incompatible with
        # ASan, so remove /RTC from the Debug flags and disable incremental link.
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}"
            CACHE STRING "Flags used by the CXX compiler during Debug builds." FORCE)
        target_link_options(cube_sanitizers INTERFACE
            "$<$<CONFIG:Debug>:/INCREMENTAL:NO>")
    endif()
endif()

# Debug hardening: turn on libstdc++/libc++ precondition assertions. Harmless
# where the standard library does not recognise the macro (e.g. MSVC STL).
target_compile_definitions(cube_sanitizers INTERFACE
    "$<$<AND:$<CONFIG:Debug>,$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>>:_GLIBCXX_ASSERTIONS>")

# ---------------------------------------------------------------------------
# The ASan runtime (clang_rt.asan_dynamic-*.dll) isn't in a system directory,
# so executables linked against cube::sanitizers fail to start with
# STATUS_DLL_NOT_FOUND unless run from a Developer Command Prompt. Copy it
# next to the target so it runs standalone, matching what windeployqt does
# for Qt's own DLLs.
# ---------------------------------------------------------------------------
function(cube_deploy_sanitizer_runtime target)
    if(CUBEULATOR_ENABLE_SANITIZERS AND MSVC)
        set(_cube_asan_search_dirs "")

        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
                OUTPUT_VARIABLE _cube_clang_resource_dir
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(_cube_clang_resource_dir)
                list(APPEND _cube_asan_search_dirs "${_cube_clang_resource_dir}/lib/windows")
            endif()
            unset(_cube_clang_resource_dir)
        else()
            get_filename_component(_cube_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            list(APPEND _cube_asan_search_dirs "${_cube_msvc_bin_dir}")
            unset(_cube_msvc_bin_dir)
        endif()

        set(_cube_asan_dll "")
        foreach(_cube_asan_dir IN LISTS _cube_asan_search_dirs)
            file(GLOB _cube_asan_candidates "${_cube_asan_dir}/clang_rt.asan_dynamic-*.dll")
            if(_cube_asan_candidates)
                list(GET _cube_asan_candidates 0 _cube_asan_dll)
                break()
            endif()
        endforeach()

        if(_cube_asan_dll)
            # A separate POST_BUILD copy per target can race when independent
            # targets link in parallel. Make the copy a single shared build
            # edge instead, so ninja only ever runs it once.
            if(NOT TARGET cube_asan_runtime_deploy)
                get_filename_component(_cube_asan_dll_name "${_cube_asan_dll}" NAME)
                set(_cube_asan_dll_dest
                    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${_cube_asan_dll_name}")
                add_custom_command(
                    OUTPUT "${_cube_asan_dll_dest}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_cube_asan_dll}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
                    DEPENDS "${_cube_asan_dll}"
                    COMMENT "Copying ASan runtime into ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
                add_custom_target(cube_asan_runtime_deploy
                    DEPENDS "${_cube_asan_dll_dest}")
                unset(_cube_asan_dll_name)
                unset(_cube_asan_dll_dest)
            endif()
            add_dependencies(${target} cube_asan_runtime_deploy)
        else()
            message(WARNING
                "ASan is enabled but its runtime DLL was not found (searched: "
                "${_cube_asan_search_dirs}); ${target} may fail to start "
                "with STATUS_DLL_NOT_FOUND outside a Developer Command Prompt.")
        endif()

        unset(_cube_asan_search_dirs)
        unset(_cube_asan_dll)
        unset(_cube_asan_candidates)
    endif()
endfunction()
