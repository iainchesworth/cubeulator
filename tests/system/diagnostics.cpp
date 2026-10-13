#include "diagnostics.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QtLogging>

#include <cstdio>
#include <utility>

namespace cube::systest {

namespace {

QMutex g_mutex;
std::vector<QmlWarning> g_warnings;
QtMessageHandler g_previous = nullptr;

void capture(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const bool problem = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
    // QML reports against the .qml file and line. Qt's own warnings about the
    // platform (fonts, the window manager) carry the C++ source or none.
    const QString file = QString::fromUtf8(context.file != nullptr ? context.file : "");
    if (problem && (file.endsWith(".qml") || message.contains(".qml:"))) {
        const QMutexLocker lock(&g_mutex);
        g_warnings.push_back({file.isEmpty() ? QString() : file + ":" + QString::number(context.line),
                              message});
    }
    if (g_previous != nullptr) {
        g_previous(type, context, message);
    } else {
        std::fprintf(stderr, "%s\n", qPrintable(message));
    }
}

}  // namespace

void install_warning_capture() {
    g_previous = qInstallMessageHandler(capture);
}

std::vector<QmlWarning> take_qml_warnings() {
    const QMutexLocker lock(&g_mutex);
    return std::exchange(g_warnings, {});
}

}  // namespace cube::systest
