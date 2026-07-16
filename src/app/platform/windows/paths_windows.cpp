#include "interfaces/paths.hpp"

#include <cstdlib>

namespace cube::platform::paths {

AppPaths current() {
    AppPaths paths;
    if (const char* appdata = std::getenv("APPDATA")) {
        paths.data_dir = std::filesystem::path(appdata) / "Cubeulator";
    }
    if (const char* local_appdata = std::getenv("LOCALAPPDATA")) {
        paths.cache_dir = std::filesystem::path(local_appdata) / "Cubeulator" / "Cache";
    }
    return paths;
}

}  // namespace cube::platform::paths
