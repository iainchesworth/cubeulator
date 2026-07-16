#include "interfaces/gpu_surface.hpp"

namespace cube::platform::gpu_surface {

Result<NativeSurfaceInfo> current(void* native_window_handle) {
    // Resolving a UIView*'s backing CAMetalLayer/scale factor lands with the
    // render pipeline spec.
    return NativeSurfaceInfo{.native_handle = native_window_handle, .scale_factor = 1.0F};
}

}  // namespace cube::platform::gpu_surface
