#include "interfaces/platform_info.hpp"

namespace cube::platform::platform_info {

PlatformInfo current() {
    return PlatformInfo{
        .is_mobile = false,
        .graphics_backends =
            {
                {.key = "d3d11", .label = "D3D11"},
                {.key = "d3d12", .label = "D3D12"},
                {.key = "vulkan", .label = "Vulkan"},
                {.key = "opengl", .label = "GL"},
            },
        .japanese_faces = {"Yu Gothic UI", "Meiryo UI", "MS UI Gothic"},
        .chinese_faces = {"Microsoft YaHei UI", "Microsoft YaHei"},
    };
}

}  // namespace cube::platform::platform_info
