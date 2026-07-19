#include <catch2/catch_test_macros.hpp>

#include <cube/vision/auto_advance.hpp>

using namespace cube::vision;

TEST_CASE("should_auto_advance requires every gate to pass", "[vision]") {
    constexpr AutoAdvanceThresholds thresholds{.min_color_confidence = 0.75F};

    REQUIRE(should_auto_advance(FaceConfirmSignal{.color_confidence = 0.9F, .is_steady = true,
                                                    .hand_absent = true},
                                 thresholds));

    REQUIRE_FALSE(should_auto_advance(FaceConfirmSignal{.color_confidence = 0.5F, .is_steady = true,
                                                          .hand_absent = true},
                                       thresholds));
    REQUIRE_FALSE(should_auto_advance(FaceConfirmSignal{.color_confidence = 0.9F, .is_steady = false,
                                                          .hand_absent = true},
                                       thresholds));
    REQUIRE_FALSE(should_auto_advance(FaceConfirmSignal{.color_confidence = 0.9F, .is_steady = true,
                                                          .hand_absent = false},
                                       thresholds));
}

TEST_CASE("should_auto_advance treats the confidence threshold as inclusive", "[vision]") {
    constexpr AutoAdvanceThresholds thresholds{.min_color_confidence = 0.75F};

    REQUIRE(should_auto_advance(
        FaceConfirmSignal{.color_confidence = 0.75F, .is_steady = true, .hand_absent = true}, thresholds));
}
