# cube::track_m3t - the M3T-backed PoseTracker implementation behind the
# GL-free pipeline seam (pose_tracker.hpp). Desktop-only for the same
# reason third_party/m3t is: GLEW/GLFW have no mobile story, and the
# phone port swaps this implementation, not the seam.
if(CUBEULATOR_PLATFORM_DESKTOP)
    add_library(cube_track_m3t STATIC
        src/m3t_pose_tracker.cpp)
    add_library(cube::track_m3t ALIAS cube_track_m3t)

    target_include_directories(cube_track_m3t
        PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

    target_compile_features(cube_track_m3t PUBLIC cxx_std_23)

    target_link_libraries(cube_track_m3t
        PUBLIC
            cube::lib
        PRIVATE
            cube::m3t
            cube::warnings
            cube::sanitizers
            cube::coverage)
endif()
