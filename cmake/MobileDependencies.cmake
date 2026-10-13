# ---------------------------------------------------------------------------
# MobileDependencies.cmake
#
# Defines cube::opencv and cube::bgfx: the interface names vision/render
# code should link against instead of the upstream package names, so it
# never has to branch on how a dependency was acquired.
#
# vcpkg.json declares opencv4/bgfx as plain (unscoped) dependencies - unlike
# the original bootstrap spec, they are NOT limited to desktop triplets.
# Re-checking the actual vcpkg port manifests (rather than just the upstream
# issues that motivated the original exclusion) showed vcpkg doesn't
# hard-block arm64-ios/arm64-android for either: see docs/dependency-strategy.md
# for the full evidence trail (bgfx.cmake has real iOS Metal/GLES framework
# linking and explicitly excludes ANDROID from its X11 path; opencv4's port
# manifest carries no ios/android restriction either). So this file's job is
# the same on every platform -
# find each package via vcpkg's exported CMake config and alias it to a
# stable cube:: name - rather than mobile needing a different acquisition
# mechanism (a native SDK/framework) from desktop.
#
# The filename/function name predate that finding and are mobile-specific in
# name only at this point; left as-is here to keep this change focused; a
# rename (e.g. ThirdPartyInterfaces.cmake) is reasonable follow-up cleanup.
#
# If vcpkg's mobile build of either library turns out not to actually work
# (unverified from this change - see docs/dependency-strategy.md), the
# fallback is to swap just that library's block below for one that fetches
# its official native distribution (OpenCV's opencv2.framework/Android SDK,
# bgfx's own GENie build) and wraps it as an IMPORTED target under the same
# cube:: name - the call site and the rest of this file wouldn't change.
# ---------------------------------------------------------------------------

function(cube_import_opencv)
    find_package(OpenCV CONFIG REQUIRED)

    add_library(cube_opencv_interface INTERFACE)
    target_link_libraries(cube_opencv_interface INTERFACE ${OpenCV_LIBS})
    target_include_directories(cube_opencv_interface SYSTEM INTERFACE ${OpenCV_INCLUDE_DIRS})
    add_library(cube::opencv ALIAS cube_opencv_interface)
endfunction()

function(cube_import_bgfx)
    find_package(bgfx CONFIG REQUIRED)

    if(TARGET bgfx::bgfx)
        add_library(cube::bgfx ALIAS bgfx::bgfx)
    elseif(TARGET bgfx)
        add_library(cube::bgfx ALIAS bgfx)
    else()
        message(FATAL_ERROR
            "find_package(bgfx) succeeded but neither bgfx::bgfx nor bgfx "
            "is a target - the vcpkg port's exported target name has "
            "changed; update cube_import_bgfx() in "
            "cmake/MobileDependencies.cmake.")
    endif()
endfunction()

function(cube_configure_mobile_dependencies)
    # Whatever the platform needs done before the packages are found (the
    # Android FindThreads workaround is the only one so far) is its own
    # fragment's business: src/app/platform/<name>/platform.cmake.
    cube_platform_prepare_dependencies()

    cube_import_opencv()
    cube_import_bgfx()

    message(STATUS
        "cube::opencv / cube::bgfx configured via vcpkg "
        "(platform: ${CUBEULATOR_PLATFORM}).")
endfunction()
