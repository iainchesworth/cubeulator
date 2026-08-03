# The offline pipeline runner: replay recordings -> JSONL traces +
# annotated overlay video + metrics (docs/pipeline-plan.md Phase 0). A
# desktop instrument, never shipped: gated out of mobile builds at the
# parent (tools/) level.
add_executable(cube_pipeline_runner main.cpp)

target_link_libraries(cube_pipeline_runner
    PRIVATE
        cube::lib
        cube::warnings
        cube::sanitizers
        cube::coverage)

cube_deploy_sanitizer_runtime(cube_pipeline_runner)
