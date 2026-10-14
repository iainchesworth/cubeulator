# cube_scan: the direct-scan scoreboard (docs/recovery-plan.md). Replays
# recorded sessions through the reader and reports the cube against the
# session's known state. A desktop instrument, never shipped.
find_package(fmt CONFIG REQUIRED)

add_executable(cube_scan main.cpp)

target_link_libraries(cube_scan
    PRIVATE
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage
        fmt::fmt)

cube_deploy_sanitizer_runtime(cube_scan)

# cube_tumble_sim: a cube turned about one axis and then another, drawn as a
# recorded session, so that cube_scan can replay a tumble of known pose
# (docs/investigations/2026-10-14-scan-tumble.md). Holds no one's face.
add_executable(cube_tumble_sim tumble_sim.cpp)

target_link_libraries(cube_tumble_sim
    PRIVATE
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage
        fmt::fmt)

cube_deploy_sanitizer_runtime(cube_tumble_sim)
