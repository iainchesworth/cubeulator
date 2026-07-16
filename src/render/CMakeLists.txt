# ---------------------------------------------------------------------------
# cube::gpu_render - bgfx setup, shaders, and the technical-drawing render
# pipeline. Kept separate from src/lib/render/ (shared, GPU-agnostic shading
# logic) and from src/app/ (platform shell) so it can be built/tested without
# either. Stub only: real bgfx wiring lands with the render pipeline spec.
# ---------------------------------------------------------------------------
add_library(cube_gpu_render)
add_library(cube::gpu_render ALIAS cube_gpu_render)

target_sources(cube_gpu_render PRIVATE gpu_render.cpp)

target_include_directories(cube_gpu_render
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

target_compile_features(cube_gpu_render PUBLIC cxx_std_23)

target_link_libraries(cube_gpu_render
    PRIVATE
        cube::warnings
        cube::sanitizers
        cube::coverage
    PUBLIC
        cube::lib)
