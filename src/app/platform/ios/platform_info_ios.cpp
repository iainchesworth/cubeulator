#include "interfaces/platform_info.hpp"

namespace cube::platform::platform_info {

PlatformInfo current() {
    // iOS effectively has just the one real choice.
    return PlatformInfo{
        .is_mobile = true,
        .graphics_backends = {{.key = "metal", .label = "Metal"}},
        .japanese_faces = {"Hiragino Sans", "Hiragino Kaku Gothic ProN"},
        .chinese_faces = {"PingFang SC", "Heiti SC"},
    };
}

}  // namespace cube::platform::platform_info
