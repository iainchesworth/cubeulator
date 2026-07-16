#pragma once

#include <cube/error.hpp>

namespace cube::platform::gpu_surface {

// A native window/view handle resolved to a bgfx-compatible surface, plus
// the native scale factor (DPI) it was created at.
struct NativeSurfaceInfo {
    void* native_handle = nullptr;
    float scale_factor = 1.0F;
};

// There is exactly one definition of `current()` per platform, living in
// platform/<os>/gpu_surface_<os>.cpp. CMake compiles only the file matching
// the target OS, so the code contains no preprocessor branching.
[[nodiscard]] Result<NativeSurfaceInfo> current(void* native_window_handle);

}  // namespace cube::platform::gpu_surface
