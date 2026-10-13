#include "interfaces/platform_info.hpp"

namespace cube::platform::platform_info {

PlatformInfo current() {
    return PlatformInfo{
        .is_mobile = true,
        .graphics_backends = {{.key = "vulkan", .label = "Vulkan"},
                              {.key = "opengl", .label = "GLES"}},
    };
}

}  // namespace cube::platform::platform_info
