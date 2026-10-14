#include "interfaces/platform_info.hpp"

namespace cube::platform::platform_info {

PlatformInfo current() {
    return PlatformInfo{
        .is_mobile = false,
        .graphics_backends = {{.key = "metal", .label = "Metal"}, {.key = "opengl", .label = "GL"}},
        .japanese_faces = {"Hiragino Sans", "Hiragino Kaku Gothic ProN"},
        .chinese_faces = {"PingFang SC", "Heiti SC"},
    };
}

}  // namespace cube::platform::platform_info
