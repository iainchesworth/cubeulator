# ---------------------------------------------------------------------------
# VisionAssets.cmake
#
# Copies the checked-in edge-AI model assets (assets/models/*.onnx) next to
# a target's executable, mirroring cmake/Sanitizers.cmake's
# cube_deploy_sanitizer_runtime() pattern: a single shared build edge (not a
# separate POST_BUILD copy per target) so parallel builds of multiple
# targets that both need the assets don't race on the same copy.
#
# vcpkg's own applocal-deps mechanism already copies opencv4/onnxruntime's
# runtime DLLs next to each linked executable automatically - this only
# handles the model files themselves, which vcpkg has no visibility into.
# ---------------------------------------------------------------------------

function(cube_deploy_vision_assets target)
    if(NOT TARGET cube_vision_assets_deploy)
        set(_cube_vision_assets_src_dir "${PROJECT_SOURCE_DIR}/assets/models")
        set(_cube_vision_assets_dest_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/models")

        file(GLOB _cube_vision_assets "${_cube_vision_assets_src_dir}/*.onnx")

        set(_cube_vision_assets_outputs "")
        foreach(_cube_vision_asset IN LISTS _cube_vision_assets)
            get_filename_component(_cube_vision_asset_name "${_cube_vision_asset}" NAME)
            set(_cube_vision_asset_dest "${_cube_vision_assets_dest_dir}/${_cube_vision_asset_name}")
            add_custom_command(
                OUTPUT "${_cube_vision_asset_dest}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cube_vision_assets_dest_dir}"
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_cube_vision_asset}" "${_cube_vision_asset_dest}"
                DEPENDS "${_cube_vision_asset}"
                COMMENT "Copying ${_cube_vision_asset_name} into ${_cube_vision_assets_dest_dir}")
            list(APPEND _cube_vision_assets_outputs "${_cube_vision_asset_dest}")
        endforeach()

        add_custom_target(cube_vision_assets_deploy DEPENDS ${_cube_vision_assets_outputs})

        unset(_cube_vision_assets_src_dir)
        unset(_cube_vision_assets_dest_dir)
        unset(_cube_vision_assets)
        unset(_cube_vision_assets_outputs)
        unset(_cube_vision_asset_name)
        unset(_cube_vision_asset_dest)
    endif()

    add_dependencies(${target} cube_vision_assets_deploy)
endfunction()
