#pragma once

#include <QObject>
#include <QString>

namespace cube::app {

// Read-only capability info about hand detection: whether the model file
// and an inference execution provider are actually present on this device.
// Nothing shows it now: the setting it sat beside drove the previous scan
// pipeline and went with it, and this goes with the hand detector.
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
