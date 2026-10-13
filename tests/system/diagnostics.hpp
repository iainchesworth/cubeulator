#pragma once

// Qt prints a QML error - a binding that threw, a property that does not
// exist, a binding loop - as a warning and carries on, so a screen can be
// broken while every assertion about it passes. This collects what the app's
// own QML said while the suite ran, so a test can insist on silence.

#include <QString>

#include <vector>

namespace cube::systest {

struct QmlWarning {
    QString where;    // qrc:/qt/qml/Cubeulator/DirectScanScreen.qml:73:13
    QString message;
};

// Installs the Qt message handler (chaining to the default one, so output
// still reaches the log). Call once, before the engine exists.
void install_warning_capture();

// Warnings from QML sources since the last call.
[[nodiscard]] std::vector<QmlWarning> take_qml_warnings();

}  // namespace cube::systest
