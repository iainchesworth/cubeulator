#pragma once

#include <filesystem>

namespace cube::platform::paths {

// App data / cache / resource directories, per OS convention.
struct AppPaths {
    std::filesystem::path data_dir;
    std::filesystem::path cache_dir;
    std::filesystem::path resource_dir;
};

// There is exactly one definition of `current()` per platform, living in
// platform/<os>/paths_<os>.cpp. CMake compiles only the file matching the
// target OS, so the code contains no preprocessor branching.
[[nodiscard]] AppPaths current();

}  // namespace cube::platform::paths
