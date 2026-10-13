# Desktop-only developer instruments (docs/recovery-plan.md). Not part of
# any shipping artefact and meaningless on mobile targets, so the whole
# tree is skipped there.
if(CUBEULATOR_PLATFORM_DESKTOP)
    add_subdirectory(scan)
    add_subdirectory(cfop_tables)
    add_subdirectory(solve_bench)
endif()
