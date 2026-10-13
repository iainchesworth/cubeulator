#include "app_runtime.hpp"
#include "desktop/desktop_integration.hpp"
#include "interfaces/crash_handler.hpp"

#include <cube/solver/solver.hpp>

#include <QGuiApplication>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSettings>

#include <memory>

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
    // Installed before anything else so a fault during startup is captured
    // too. What it installs, if anything, is the platform's own business
    // (interfaces/crash_handler.hpp).
    cube::platform::crash_handler::install();

    applyPersistedGraphicsBackend();

    QGuiApplication app(argc, argv);

    // E4a - the process's identity (AppUserModelID, desktop file name, window
    // icon) and the remembered window geometry: what only the real application
    // does with its window. Declared before the runtime so that it outlives
    // the engine. The singletons DesktopShell.qml reads are the runtime's.
    cube::app::DesktopIntegration desktop(app);

    // The controllers (cube::app::AppControllers), their registration as QML
    // singletons and the engine that loads Main.qml are one AppRuntime, which
    // tests/system boots the same way.
    cube::app::AppRuntime runtime;
    desktop.prepare(runtime.engine());  // E4a: the window's initial geometry
    if (!runtime.load()) {
        return -1;
    }
    desktop.attach(runtime.engine());  // E4a: start remembering it
    SolveController& solveController = runtime.solve();
    QQmlApplicationEngine& engine = runtime.engine();

    // CUBE_SOLVE_DEMO=<number>: open on the solve screen with a cube
    // scrambled from that seed, no camera and no scan. For working on the
    // solve screen itself - trying how a move is shown does not need a real
    // cube read first. CUBE_SOLVE_DEMO_MOVE=<n> opens it n moves in.
    bool demo = false;
    const unsigned demo_seed = qEnvironmentVariable("CUBE_SOLVE_DEMO").toUInt(&demo);
    if (demo) {
        const int moves_in = qEnvironmentVariable("CUBE_SOLVE_DEMO_MOVE").toInt();
        solveController.solveCube(cube::solver::random_scramble(demo_seed));
        QObject* app_state = engine.singletonInstance<QObject*>("Cubeulator", "AppState");
        app_state->setProperty("solveCamera", false);
        auto show_when_solved = std::make_shared<QMetaObject::Connection>();
        *show_when_solved = QObject::connect(
            &solveController, &SolveController::stateChanged, app_state,
            [&solveController, app_state, show_when_solved, moves_in]() {
                if (solveController.isSolving()) {
                    return;
                }
                QObject::disconnect(*show_when_solved);
                QStringList solution;
                for (const QVariant& move : solveController.moves()) {
                    solution.append(move.toMap().value(QStringLiteral("notation")).toString());
                }
                qInfo("CUBE_SOLVE_DEMO: %s", qPrintable(solution.join(QLatin1Char(' '))));
                for (int i = 0; i < moves_in; ++i) {
                    solveController.nextMove();
                }
                app_state->setProperty("screen", QStringLiteral("solve"));
            });
    }

    return QGuiApplication::exec();
}
