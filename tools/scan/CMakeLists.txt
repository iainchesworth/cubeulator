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
