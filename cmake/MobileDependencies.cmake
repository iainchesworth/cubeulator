# ---------------------------------------------------------------------------
# MobileDependencies.cmake
#
# Defines cube::opencv: the interface name vision code should link against
# instead of the upstream package name, so it never has to branch on how the
# dependency was acquired.
#
# vcpkg.json declares opencv4 as a plain (unscoped) dependency - unlike the
# original bootstrap spec, it is NOT limited to desktop triplets.
# Re-checking the actual vcpkg port manifest (rather than just the upstream
# issues that motivated the original exclusion) showed vcpkg doesn't
# hard-block arm64-ios/arm64-android for it: see docs/dependency-strategy.md
# for the full evidence trail (opencv4's port manifest carries no ios/android
# restriction). So this file's job is the same on every platform - find the
# package via vcpkg's exported CMake config and alias it to a stable cube::
# name - rather than mobile needing a different acquisition mechanism (a
# native SDK/framework) from desktop.
#
# The filename/function name predate that finding and are mobile-specific in
# name only at this point; left as-is here to keep this change focused; a
# rename (e.g. ThirdPartyInterfaces.cmake) is reasonable follow-up cleanup.
#
# If vcpkg's mobile build of OpenCV turns out not to actually work
# (unverified from this change - see docs/dependency-strategy.md), the
# fallback is to swap the block below for one that fetches its official
# native distribution (OpenCV's opencv2.framework/Android SDK) and wraps it
# as an IMPORTED target under the same cube:: name - the call site and the
# rest of this file wouldn't change.
# ---------------------------------------------------------------------------

function(cube_import_opencv)
    find_package(OpenCV CONFIG REQUIRED)

    add_library(cube_opencv_interface INTERFACE)
    target_link_libraries(cube_opencv_interface INTERFACE ${OpenCV_LIBS})
    target_include_directories(cube_opencv_interface SYSTEM INTERFACE ${OpenCV_INCLUDE_DIRS})
    add_library(cube::opencv ALIAS cube_opencv_interface)
endfunction()

function(cube_configure_mobile_dependencies)
    # Whatever the platform needs done before the packages are found (the
    # Android FindThreads workaround is the only one so far) is its own
    # fragment's business: src/app/platform/<name>/platform.cmake.
    cube_platform_prepare_dependencies()

    cube_import_opencv()

    message(STATUS
        "cube::opencv configured via vcpkg "
        "(platform: ${CUBEULATOR_PLATFORM}).")
endfunction()
