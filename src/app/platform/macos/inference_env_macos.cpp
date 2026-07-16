#include "interfaces/inference_env.hpp"

#include <cstdlib>

namespace cube::platform::inference_env {

EnvironmentInfo current() {
    std::filesystem::path model_dir;
    if (const char* home = std::getenv("HOME")) {
        model_dir = std::filesystem::path(home) / "Library" / "Application Support" / "Cubeulator" /
                    "models";
    }
    return EnvironmentInfo{.model_search_path = model_dir,
                           .execution_providers = {"CPU", "CoreML"}};
}

}  // namespace cube::platform::inference_env
