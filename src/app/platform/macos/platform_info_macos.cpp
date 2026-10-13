#include "interfaces/platform_info.hpp"

namespace cube::platform::platform_info {

PlatformInfo current() {
    return PlatformInfo{
        .is_mobile = false,
        .graphics_backends = {{.key = "metal", .label = "Metal"}, {.key = "opengl", .label = "GL"}},
    };
}

}  // namespace cube::platform::platform_info
