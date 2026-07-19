#include <cube/vision/auto_advance.hpp>

namespace cube::vision {

bool should_auto_advance(const FaceConfirmSignal& signal, const AutoAdvanceThresholds& thresholds) {
    return signal.color_confidence >= thresholds.min_color_confidence && signal.is_steady &&
           signal.hand_absent;
}

}  // namespace cube::vision
