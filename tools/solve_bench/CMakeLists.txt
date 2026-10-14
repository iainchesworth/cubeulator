# cube_solve_bench: solves a seeded corpus of scrambles with one method and
# reports what each solve cost, plus a digest of every solution so a change
# that claims to keep the solutions byte-identical can say so in one line.
# A developer instrument, never shipped. See main.cpp and docs/performance.md.
add_executable(cube_solve_bench main.cpp)

target_link_libraries(cube_solve_bench
    PRIVATE
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage)

cube_deploy_sanitizer_runtime(cube_solve_bench)
