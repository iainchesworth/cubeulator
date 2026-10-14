#include "interfaces/paths.hpp"

#include <cstdlib>

namespace cube::platform::paths {

AppPaths current() {
    AppPaths paths;
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path home_dir(home);
        paths.data_dir = home_dir / "Library" / "Application Support" / "Cubeulator";
        paths.cache_dir = home_dir / "Library" / "Caches" / "Cubeulator";
        paths.diagnostics_dir = home_dir / "Library" / "Logs" / "Cubeulator" / "Diagnostics";
    }
    return paths;
}

}  // namespace cube::platform::paths
