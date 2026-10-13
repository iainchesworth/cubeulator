# cube_generate_cfop_tables: writes src/lib/src/solver/cfop_case_words.inc,
# the last-layer tables the intermediate (CFOP) solver reads. A developer
# instrument, run by hand when the alphabet changes - never at build time,
# and never shipped. See generate.cpp.
add_executable(cube_generate_cfop_tables generate.cpp)

target_include_directories(cube_generate_cfop_tables
    PRIVATE "${CMAKE_SOURCE_DIR}/src/lib/src/solver")

target_link_libraries(cube_generate_cfop_tables
    PRIVATE
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage)

cube_deploy_sanitizer_runtime(cube_generate_cfop_tables)
