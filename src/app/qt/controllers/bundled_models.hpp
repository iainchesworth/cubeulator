#pragma once

#include <QByteArray>

namespace cube::app {

// The hand-detection ONNX model's bytes, embedded directly into the
// executable as a Qt resource (see cube_app_controllers' qt_add_resources()
// call) rather than deployed as a loose file next to it. This sidesteps
// the whole "does the working directory / deployment step happen to be
// right" class of problem a filesystem path depends on - confirmed by hand
// during manual verification, where a CWD-relative path reported the model
// missing despite cmake/VisionAssets.cmake having copied it correctly.
// Empty if the resource somehow isn't compiled in (should not happen in a
// normal build).
[[nodiscard]] QByteArray hand_detection_model_bytes();

}  // namespace cube::app
