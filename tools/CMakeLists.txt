# Desktop-only developer instruments (docs/pipeline-plan.md). Not part of
# any shipping artefact and meaningless on mobile targets, so the whole
# tree is skipped there.
if(NOT ANDROID AND NOT IOS)
    add_subdirectory(runner)
    add_subdirectory(scan)
    add_subdirectory(cfop_tables)
endif()
