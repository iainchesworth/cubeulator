#include "interfaces/paths.hpp"

namespace cube::platform::paths {

AppPaths current() {
    // Resolving Context.getFilesDir()/getCacheDir() requires a JNI bridge;
    // lands alongside the native Android shell work.
    return AppPaths{};
}

}  // namespace cube::platform::paths
