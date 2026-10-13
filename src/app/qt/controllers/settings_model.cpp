#include "controllers/settings_model.hpp"

#include "controllers/bundled_models.hpp"
#include "interfaces/inference_env.hpp"

#include <cube/vision/hand_presence.hpp>

namespace cube::app {

SettingsModel::SettingsModel(QObject* parent) : QObject(parent) {
    const auto env = cube::platform::inference_env::current();
    const QByteArray model_bytes = hand_detection_model_bytes();
    const auto capability = cube::vision::check_hand_detection_capability(
        !model_bytes.isEmpty(), env.execution_providers, /*user_enabled=*/true);
    hand_detection_available_ =
        capability.model_file_present && capability.execution_provider_available;

    if (!capability.model_file_present) {
        hand_detection_unavailable_reason_ =
            QStringLiteral("Hand-detection model not found on this device.");
    } else if (!capability.execution_provider_available) {
        hand_detection_unavailable_reason_ = QStringLiteral("No AI inference hardware detected.");
    }
}

bool SettingsModel::handDetectionAvailable() const noexcept {
    return hand_detection_available_;
}

QString SettingsModel::handDetectionUnavailableReason() const {
    return hand_detection_unavailable_reason_;
}

}  // namespace cube::app
