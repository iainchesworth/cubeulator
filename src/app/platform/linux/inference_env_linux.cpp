#include "interfaces/inference_env.hpp"

#include <cstdlib>

namespace cube::platform::inference_env {

EnvironmentInfo current() {
    std::filesystem::path model_dir;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
        model_dir = std::filesystem::path(xdg) / "cubeulator" / "models";
    } else if (const char* home = std::getenv("HOME")) {
        model_dir = std::filesystem::path(home) / ".local" / "share" / "cubeulator" / "models";
    }
    return EnvironmentInfo{.model_search_path = model_dir, .execution_providers = {"CPU"}};
}

}  // namespace cube::platform::inference_env
