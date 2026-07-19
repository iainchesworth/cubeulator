#pragma once

#include <QObject>
#include <QString>

namespace cube::app {

// Read-only capability info for the Settings screen's hand-detection-AI
// toggle: whether the model file and an inference execution provider are
// actually present on this device, independent of whether the user has it
// enabled - see ScanSession::handDetectionEnabled for the toggle itself,
// and CameraController for the camera-device-picker data this screen also
// shows.
class SettingsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool handDetectionAvailable READ handDetectionAvailable CONSTANT)
    Q_PROPERTY(QString handDetectionUnavailableReason READ handDetectionUnavailableReason CONSTANT)

public:
    explicit SettingsModel(QObject* parent = nullptr);

    [[nodiscard]] bool handDetectionAvailable() const noexcept;
    [[nodiscard]] QString handDetectionUnavailableReason() const;

private:
    bool hand_detection_available_ = false;
    QString hand_detection_unavailable_reason_;
};

}  // namespace cube::app
