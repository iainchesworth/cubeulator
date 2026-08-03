# ---------------------------------------------------------------------------
# Vendored third-party code. Every subtree here documents its provenance,
# licence and local patch list in its own README.md; nothing in this
# directory is first-party code, so none of it links cube::warnings.
# ---------------------------------------------------------------------------

# --- M3T: region-based 6-DoF model tracker (docs/pipeline-plan.md Phase 2) --
# Desktop-only, mirroring the tools/ gate: the tracker's GLEW/GLFW offscreen
# renderer is a desktop dependency stack, and mobile integration is a later
# phase's problem (vcpkg would also need the GL ports for the mobile triplets).
if(NOT ANDROID AND NOT IOS)
    # The vendored CMakeLists declares its options under `cmake_minimum_required
    # (VERSION 3.5)` policies, where option() would clobber a plain variable set
    # here (CMP0077 OLD). Seed the cache instead: an existing cache entry always
    # wins over option()'s default.
    #
    # M3T_WIN32_STATIC OFF: use dynamic vcpkg x64-windows libraries and the /MD
    # runtime like the rest of this project (SPIKE PATCH 1 made it an option).
    # The device-camera and gtest integrations are dead weight here: frames are
    # fed in-memory by the caller, and the vendored subset carries no test/ dir.
    set(M3T_WIN32_STATIC OFF CACHE BOOL "Static GLEW/OpenCV + /MT runtime on Windows")
    set(USE_AZURE_KINECT OFF CACHE BOOL "Use Azure Kinect")
    set(USE_REALSENSE OFF CACHE BOOL "Use RealSense D435")
    set(USE_GTEST OFF CACHE BOOL "Use gtest")

    # SYSTEM: consumers see m3t's interface headers (and its Eigen/OpenCV/GL
    # includes) as system includes, so cube::warnings' /W4 /WX never fires on
    # them - the same posture the vcpkg toolchain applies to every dependency.
    # EXCLUDE_FROM_ALL: the library is built only when a consumer links it,
    # and the subtree's install() rules stay out of this project's packages.
    # m3t itself compiles with its upstream (default) warning level, again
    # matching how vcpkg dependency code is treated.
    add_subdirectory(m3t SYSTEM EXCLUDE_FROM_ALL)

    # The one first-party seam: link cube::m3t, never the raw m3t target.
    # M3T's public headers require C++17 in consuming TUs (their filesystem
    # shim probes the language level; MSVC would otherwise default to C++14),
    # which cxx_std_17 encodes as a hard floor - a no-op for this project's
    # global C++23, but it keeps the requirement attached to the target rather
    # than to tribal knowledge.
    add_library(cube_m3t INTERFACE)
    add_library(cube::m3t ALIAS cube_m3t)
    target_link_libraries(cube_m3t INTERFACE m3t)
    target_compile_features(cube_m3t INTERFACE cxx_std_17)
endif()
