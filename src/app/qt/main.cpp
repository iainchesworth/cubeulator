#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>

namespace {

// The graphics API a QQuickWindow uses is fixed for the process's lifetime
// (QQuickWindow::setGraphicsApi() only has an effect before the first
// QGuiApplication/QQuickWindow exists), so the "stats for nerds" backend
// picker in SettingsScreen can only persist a preference for next launch,
// not hot-swap it. This reads that preference from the same QSettings store
// Qt.labs.settings' Settings QML type writes to (same organization/
// application name), applied here before anything else runs.
void applyPersistedGraphicsBackend() {
    QCoreApplication::setOrganizationName("Cubeulator");
    QCoreApplication::setApplicationName("Cubeulator");

    const QSettings settings;
    const auto backend = settings.value("rendering/graphicsBackend", "auto").toString();
    if (backend == "d3d11") {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    } else if (backend == "d3d12") {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
    } else if (backend == "vulkan") {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    } else if (backend == "opengl") {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    } else if (backend == "metal") {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    }
    // "auto" (or an unrecognised value): leave Qt's own per-platform default.
    // Values are only ever written by SettingsState.qml's
    // availableGraphicsBackends, which already filters to what's valid on
    // the current platform (Direct3D: Windows only; Metal: macOS/iOS only;
    // Vulkan/OpenGL: wherever Qt ships a backend for them) - no extra
    // per-platform guarding needed here.
}

}  // namespace

int main(int argc, char* argv[]) {
    applyPersistedGraphicsBackend();

    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    engine.loadFromModule("Cubeulator", "Main");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return QGuiApplication::exec();
}
