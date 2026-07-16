#include "interfaces/inference_env.hpp"

namespace cube::platform::inference_env {

EnvironmentInfo current() {
    // Resolving NSHomeDirectory()'s Application Support/models path requires
    // an Objective-C++ bridge; lands with the edge-AI integration spec.
    return EnvironmentInfo{.model_search_path = {}, .execution_providers = {"CPU", "CoreML"}};
}

}  // namespace cube::platform::inference_env
