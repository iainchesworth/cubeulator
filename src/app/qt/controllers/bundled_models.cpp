#include "controllers/bundled_models.hpp"

#include <QFile>

namespace cube::app {

QByteArray hand_detection_model_bytes() {
    QFile file(QStringLiteral(":/models/palm_detection_mediapipe_2023feb_int8.onnx"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

}  // namespace cube::app
