#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cube::platform::inference_env {

// Per-platform setup ONNX Runtime's execution providers need: where to find
// bundled models, and which execution providers are available on this host.
struct EnvironmentInfo {
    std::filesystem::path model_search_path;
    std::vector<std::string> execution_providers;
};

// There is exactly one definition of `current()` per platform, living in
// platform/<os>/inference_env_<os>.cpp. CMake compiles only the file
// matching the target OS, so the code contains no preprocessor branching.
[[nodiscard]] EnvironmentInfo current();

}  // namespace cube::platform::inference_env
