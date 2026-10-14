#pragma once

#include <filesystem>

namespace cube::platform::paths {

// App data / cache / resource directories, per OS convention.
struct AppPaths {
    std::filesystem::path data_dir;
    std::filesystem::path cache_dir;
    std::filesystem::path resource_dir;
    // Where the developer diagnostics channel writes (docs/diagnostics.md): a
    // directory of the current user's, outside every repository, that a person
    // or another process can read while the app is running. A platform's log
    // convention, not its settings or cache one. Empty where the platform has
    // no such place yet; the channel then says it is unavailable.
    std::filesystem::path diagnostics_dir;
};

// There is exactly one definition of `current()` per platform, living in
// platform/<os>/paths_<os>.cpp. CMake compiles only the file matching the
// target OS, so the code contains no preprocessor branching.
[[nodiscard]] AppPaths current();

}  // namespace cube::platform::paths
