#include "interfaces/paths.hpp"

#include <cstdlib>

namespace cube::platform::paths {

AppPaths current() {
    AppPaths paths;
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
        xdg_config != nullptr && *xdg_config != '\0') {
        paths.data_dir = std::filesystem::path(xdg_config) / "cubeulator";
    } else if (const char* home = std::getenv("HOME")) {
        paths.data_dir = std::filesystem::path(home) / ".config" / "cubeulator";
    }
    if (const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
        xdg_cache != nullptr && *xdg_cache != '\0') {
        paths.cache_dir = std::filesystem::path(xdg_cache) / "cubeulator";
    } else if (const char* home = std::getenv("HOME")) {
        paths.cache_dir = std::filesystem::path(home) / ".cache" / "cubeulator";
    }
    return paths;
}

}  // namespace cube::platform::paths
