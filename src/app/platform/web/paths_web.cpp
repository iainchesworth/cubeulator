#include "interfaces/paths.hpp"

namespace cube::platform::paths {

AppPaths current() {
    // A page has no filesystem of its own. What C++ sees is Emscripten's
    // in-memory one, rooted at the HOME it gives every program, and it is
    // gone when the tab is reloaded or closed. These are directories a caller
    // can create and write to for the length of the session and nothing
    // more; the app's settings do not go through them (Qt.labs.settings uses
    // the browser's own storage on this platform).
    const std::filesystem::path home{"/home/web_user"};
    AppPaths paths;
    paths.data_dir = home / ".local" / "share" / "cubeulator";
    paths.cache_dir = home / ".cache" / "cubeulator";
    // Session-long like the rest: another process cannot read it, but the
    // page can, and the channel's records are the same.
    paths.diagnostics_dir = home / ".local" / "state" / "cubeulator" / "diagnostics";
    return paths;
}

}  // namespace cube::platform::paths
