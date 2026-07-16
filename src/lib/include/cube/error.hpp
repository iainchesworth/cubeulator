#pragma once

#include <expected>
#include <string_view>

namespace cube {

// Every fallible operation in the library reports failure through this enum
// rather than by throwing. Combined with `Result<T>` (an alias for
// std::expected) this lets call sites compose with the C++23 monadic
// operations (and_then / transform / or_else) instead of try/catch.
enum class CubeError {
    // Cube state / solver
    invalid_cube_state,
    unsolvable_state,
    invalid_move_sequence,
    no_solution,

    // Vision / capture pipeline
    camera_not_found,
    camera_start_failed,
    no_valid_cube_detected,
    face_capture_incomplete,

    // Edge-AI / inference
    model_load_failed,
    inference_provider_unavailable,
    inference_failed,

    // Rendering
    gpu_surface_unavailable,
    shader_compile_failed,

    // i18n
    locale_resource_not_found,
};

[[nodiscard]] constexpr std::string_view to_string(CubeError error) noexcept {
    switch (error) {
        case CubeError::invalid_cube_state:
            return "the supplied cube state is not physically valid";
        case CubeError::unsolvable_state:
            return "no solution exists for this cube state";
        case CubeError::invalid_move_sequence:
            return "the move sequence contains an invalid move";
        case CubeError::no_solution:
            return "no solution could be found";
        case CubeError::camera_not_found:
            return "no camera device was found";
        case CubeError::camera_start_failed:
            return "the camera could not be started";
        case CubeError::no_valid_cube_detected:
            return "no valid cube was detected in the frame";
        case CubeError::face_capture_incomplete:
            return "not all faces have been captured yet";
        case CubeError::model_load_failed:
            return "the inference model could not be loaded";
        case CubeError::inference_provider_unavailable:
            return "no suitable inference execution provider is available";
        case CubeError::inference_failed:
            return "inference failed to produce a result";
        case CubeError::gpu_surface_unavailable:
            return "no GPU-compatible render surface is available";
        case CubeError::shader_compile_failed:
            return "a shader failed to compile";
        case CubeError::locale_resource_not_found:
            return "a locale resource file could not be found";
    }
    return "unknown error";
}

// The library-wide fallible result type. `Result<void>` is valid and is used
// for operations that either succeed or report a CubeError.
template <typename T>
using Result = std::expected<T, CubeError>;

}  // namespace cube
