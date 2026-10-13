#pragma once

namespace cube::vision {

struct FaceConfirmSignal {
    float color_confidence = 0.0F;
    bool is_steady = false;
    bool hand_absent = true;
};

struct AutoAdvanceThresholds {
    float min_color_confidence = 0.75F;
};

// An AND-of-gates decision, not a weighted score: color_confidence,
// is_steady, and hand_absent are heterogeneous signals that don't collapse
// meaningfully into one number, whereas this stays exhaustively testable
// and easy to explain to a user.
[[nodiscard]] bool should_auto_advance(const FaceConfirmSignal& signal,
                                       const AutoAdvanceThresholds& thresholds = {});

}  // namespace cube::vision
