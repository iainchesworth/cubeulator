#include "interfaces/platform_info.hpp"

namespace cube::platform::platform_info {

PlatformInfo current() {
    return PlatformInfo{
        .is_mobile = false,
        .graphics_backends =
            {
                {.key = "vulkan", .label = "Vulkan"},
                {.key = "opengl", .label = "GL"},
            },
        .japanese_faces = {"Noto Sans CJK JP", "Noto Sans JP", "Source Han Sans JP"},
        .chinese_faces =
            {
                "Noto Sans CJK SC",
                "Noto Sans SC",
                "Source Han Sans SC",
                "WenQuanYi Micro Hei",
            },
    };
}

}  // namespace cube::platform::platform_info
