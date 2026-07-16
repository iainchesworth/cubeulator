# ---------------------------------------------------------------------------
# MobileDependencies.cmake
#
# vcpkg's own ports for opencv4/onnxruntime/bgfx are not reliably supported
# on the arm64-ios / arm64-android triplets (see docs/dependency-strategy.md
# for the evidence). vcpkg.json therefore scopes those three dependencies to
# desktop triplets only ("platform": "!ios & !android").
#
# For iOS/Android, the plan is to fetch each library's own official
# distribution instead, wrapped as an IMPORTED target behind the SAME
# interface name vision/render code will link against on every platform:
#   - cube::opencv       <- OpenCV's opencv2.framework (iOS) / OpenCV-android-sdk
#   - cube::onnxruntime  <- ONNX Runtime's .xcframework (iOS) / Maven .aar (Android)
#   - cube::bgfx         <- bgfx built via its own GENie `make ios-arm64` /
#                            `make android-arm64`, not vcpkg or bgfx.cmake
#
# None of the three targets are consumed by any code in this bootstrap spec
# (the vision/render/edge-AI specs are what will actually link them), so this
# module is currently a documented placeholder rather than a working fetch
# step - implementing the real download/unpack/IMPORTED-target logic is
# tracked as follow-up work for whichever later spec first needs it on
# mobile.
# ---------------------------------------------------------------------------
function(cube_configure_mobile_dependencies)
    if(NOT (IOS OR ANDROID))
        return()
    endif()

    message(STATUS
        "Mobile target detected (IOS=${IOS}, ANDROID=${ANDROID}): cube::opencv/"
        "cube::onnxruntime/cube::bgfx are not yet wired up for this platform. "
        "See docs/dependency-strategy.md for the intended hybrid approach.")
endfunction()
