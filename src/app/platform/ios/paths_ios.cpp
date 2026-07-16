#include "interfaces/paths.hpp"

namespace cube::platform::paths {

AppPaths current() {
    // Resolving NSHomeDirectory()/NSSearchPathForDirectoriesInDomains requires
    // an Objective-C++ bridge; lands alongside the native iOS shell work.
    return AppPaths{};
}

}  // namespace cube::platform::paths
