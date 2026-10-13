#include "platform/detail/capture_thread_scope.hpp"

namespace cube::platform::camera::detail {

// The capture stacks used here (V4L2, AVFoundation) ask nothing of the
// thread that calls into them, so there is nothing to set up or undo.
CaptureThreadScope::CaptureThreadScope() noexcept = default;

CaptureThreadScope::~CaptureThreadScope() = default;

}  // namespace cube::platform::camera::detail
