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
        // Local, not roaming: a log of this machine's camera has no business
        // following the user to another one.
        paths.diagnostics_dir = std::filesystem::path(local_appdata) / "Cubeulator" / "Diagnostics";
    }
    return paths;
}

}  // namespace cube::platform::paths
