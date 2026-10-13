#include "controllers/live_track.hpp"

namespace cube::app::live_track {

// No tracker on this platform: the vendored M3T library's GLEW/GLFW offscreen
// renderer has no mobile story, and the phone port swaps the tracker
// implementation, not the seam. The scan session therefore runs the legacy
// cascade, exactly as it does under CUBE_VISION_PIPELINE=legacy.
bool track_mode() {
    return false;
}

Sample step(const cv::Mat& /*analysis_frame*/, float /*scale_back*/) {
    return {};
}

}  // namespace cube::app::live_track
