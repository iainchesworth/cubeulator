#include "interfaces/paths.hpp"

namespace cube::platform::paths {

AppPaths current() {
    // Resolving Context.getFilesDir()/getCacheDir() requires a JNI bridge;
    // lands alongside the native Android shell work. No diagnostics_dir for
    // the same reason: the developer diagnostics channel reports itself
    // unavailable until then.
    return AppPaths{};
}

}  // namespace cube::platform::paths
