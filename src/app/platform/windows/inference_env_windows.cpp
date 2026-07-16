#include "interfaces/inference_env.hpp"

#include <cstdlib>

namespace cube::platform::inference_env {

EnvironmentInfo current() {
    std::filesystem::path model_dir;
    if (const char* local_appdata = std::getenv("LOCALAPPDATA")) {
        model_dir = std::filesystem::path(local_appdata) / "Cubeulator" / "models";
    }
    return EnvironmentInfo{.model_search_path = model_dir,
                           .execution_providers = {"CPU", "DirectML"}};
}

}  // namespace cube::platform::inference_env
