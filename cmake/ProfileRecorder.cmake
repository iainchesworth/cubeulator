# ---------------------------------------------------------------------------
# ProfileRecorder.cmake
#
# CUBEULATOR_ENABLE_PROFILE_RECORDER selects the in-process recorder as the
# implementation of the CUBE_ZONE_* / CUBE_PLOT / CUBE_FRAME_MARK macros
# (cube/profiling.hpp), in place of Tracy's. It is what profiling means on a
# target Tracy's client cannot run on - a browser tab has no TCP listener to
# offer a profiler - and what to turn on to get the same zones as a Chrome
# trace or a CSV with nothing attached (docs/profiling.md, "In the browser").
#
# Like Tracy it is a choice of directory, not an #ifdef: src/lib/CMakeLists.txt
# puts src/lib/variants/profiling-recorder on cube::lib's include path ahead of
# the no-op sibling. The recorder itself (cube/profile/recorder.hpp) is plain
# C++ with no dependency and is compiled into cube::lib on every build, so it is
# unit-tested natively whichever variant a build selects.
#
# Not for shipping, for the same reason as Tracy: a release build has no use
# for a ring buffer it never reads. The web presets that publish the demo leave
# it off; the web-profiling presets turn it on.
# ---------------------------------------------------------------------------

option(CUBEULATOR_ENABLE_PROFILE_RECORDER
    "Record zones in-process (Chrome trace / CSV) instead of Tracy" OFF)

if(CUBEULATOR_ENABLE_PROFILE_RECORDER AND CUBEULATOR_ENABLE_TRACY)
    message(FATAL_ERROR
        "CUBEULATOR_ENABLE_PROFILE_RECORDER and CUBEULATOR_ENABLE_TRACY are "
        "both on. They are two implementations of the same macros; choose one "
        "(Tracy where a profiler can connect, the recorder where it cannot).")
endif()
