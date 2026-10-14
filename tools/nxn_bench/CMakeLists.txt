# cube_nxn_bench: what the N x N reduction library finds and what it costs,
# per cube size - its cycle libraries, and move counts and times for solving
# seeded scrambles from 2x2 up. A developer instrument, never shipped. See
# main.cpp and docs/nxn-solver.md.
add_executable(cube_nxn_bench main.cpp)

target_link_libraries(cube_nxn_bench
    PRIVATE
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage)

cube_deploy_sanitizer_runtime(cube_nxn_bench)
