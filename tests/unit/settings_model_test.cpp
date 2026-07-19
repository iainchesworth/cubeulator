#include <catch2/catch_test_macros.hpp>

#include "controllers/settings_model.hpp"

using cube::app::SettingsModel;

TEST_CASE("SettingsModel reports a consistent hand-detection availability state", "[qt][controllers]") {
    SettingsModel settings;

    // Whichever way the real capability check lands on this machine, the
    // reason string should only be populated when the capability is
    // unavailable, never both or neither.
    REQUIRE(settings.handDetectionAvailable() != !settings.handDetectionUnavailableReason().isEmpty());
}
