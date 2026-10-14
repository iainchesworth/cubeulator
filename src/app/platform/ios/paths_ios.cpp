#include "interfaces/paths.hpp"

namespace cube::platform::paths {

AppPaths current() {
    // Resolving NSHomeDirectory()/NSSearchPathForDirectoriesInDomains requires
    // an Objective-C++ bridge; lands alongside the native iOS shell work. No
    // diagnostics_dir for the same reason: the developer diagnostics channel
    // reports itself unavailable until then.
    return AppPaths{};
}

}  // namespace cube::platform::paths
