#include "controllers/camera_controller.hpp"
#include "controllers/direct_scan.hpp"
#include "controllers/frame_image_provider.hpp"
#include "controllers/scan_session.hpp"
#include "controllers/settings_model.hpp"
#include "solve_controller.hpp"

#include <cube/solver/solver.hpp>

#include <QtQml/qqml.h>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSettings>

#include <memory>

#ifdef _WIN32
#include <windows.h>
// dbghelp.h requires windows.h first.
#include <dbghelp.h>

#include <cstdio>
#endif

namespace {

#ifdef _WIN32
// Last-resort crash reporting. A hard fault (access violation, stack
// overflow) is a STRUCTURED exception, not a C++ one: MSVC's catch(...)
// under the default /EHsc does not see it, so the try/catch guarding the
// analysis worker cannot report it and the process dies silently - no
// dialog text worth reading, and Windows Error Reporting produced no entry
// at all on this machine, leaving a mid-scan crash with literally zero
// evidence. This filter runs for ANY unhandled fault, writes a minidump
// beside the executable, and prints the exception code and faulting
// address, so the next crash identifies itself instead of being argued
// about. Windows-only and inert unless the process actually dies.
LONG WINAPI writeCrashDump(EXCEPTION_POINTERS* exception) {
    char dump_path[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, dump_path, MAX_PATH);
    // Replace the trailing "cube_app.exe" with the dump name; on failure the
    // path stays as-is and the dump lands next to the binary regardless.
    if (char* last_slash = std::strrchr(dump_path, '\\'); last_slash != nullptr) {
        std::snprintf(last_slash + 1, MAX_PATH - (last_slash + 1 - dump_path), "cube_app_crash.dmp");
    }

    const HANDLE file = ::CreateFileA(dump_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = ::GetCurrentThreadId();
        info.ExceptionPointers = exception;
        info.ClientPointers = FALSE;
        // WithIndirectlyReferencedMemory keeps the dump small while still
        // capturing the objects the faulting frames point at - enough to see
        // which container/optional was empty.
        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file,
                            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                        MiniDumpScanMemory),
                            &info, nullptr, nullptr);
        ::CloseHandle(file);
    }

    if (exception != nullptr && exception->ExceptionRecord != nullptr) {
        std::fprintf(stderr, "CUBE CRASH: code=0x%08lX address=%p dump=%s\n",
                     exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress,
                     dump_path);
        std::fflush(stderr);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

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
#ifdef _WIN32
    // Installed before anything else so a fault during startup is captured
    // too (see writeCrashDump).
    ::SetUnhandledExceptionFilter(writeCrashDump);
#endif

    applyPersistedGraphicsBackend();

    QGuiApplication app(argc, argv);

    // Registered imperatively via qmlRegisterSingletonInstance rather than
    // QML_ELEMENT: cube_app_controllers is a separate target from
    // cube_app_qml specifically so its controllers are linkable into a
    // Catch2 test binary without pulling in QQmlEngine, and QML_ELEMENT's
    // compile-time registration only works for sources compiled directly
    // into a qt_add_qml_module target.
    cube::app::CameraController cameraController;
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "CameraController", &cameraController);

    cube::app::ScanSession scanSession;
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "ScanSession", &scanSession);

    // The scan screen's controller. ScanSession above is the previous scan
    // pipeline, still registered because the settings screen binds to it,
    // but no screen starts it.
    cube::app::DirectScan directScan;
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "DirectScan", &directScan);

    cube::app::SettingsModel settingsModel;
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SettingsModel", &settingsModel);

    // Needs a real reference to scanSession (cube::model::ScanProgress can't
    // cross into QML), so - like the three controllers above - it's
    // constructed directly here rather than being QML-engine-owned; declared
    // after scanSession so it's destroyed first.
    SolveController solveController(scanSession);
    qmlRegisterSingletonInstance("Cubeulator", 1, 0, "SolveController", &solveController);
    QObject::connect(&directScan, &cube::app::DirectScan::scanCompleted, &solveController, [&]() {
        if (const auto cube = directScan.cube(); cube.has_value()) {
            solveController.solveCube(*cube);
        }
    });

    QQmlApplicationEngine engine;
    // addImageProvider takes ownership; directScan outlives engine (engine
    // is declared - and so destroyed first - after directScan).
    engine.addImageProvider("scanFrame", new cube::app::FrameImageProvider(
                                             [&directScan]() { return directScan.latestFrame(); }));
    engine.loadFromModule("Cubeulator", "Main");

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // CUBE_SOLVE_DEMO=<number>: open on the solve screen with a cube
    // scrambled from that seed, no camera and no scan. For working on the
    // solve screen itself - trying how a move is shown does not need a real
    // cube read first.
    bool demo = false;
    const unsigned demo_seed = qEnvironmentVariable("CUBE_SOLVE_DEMO").toUInt(&demo);
    if (demo) {
        solveController.solveCube(cube::solver::random_scramble(demo_seed));
        QObject* app_state = engine.singletonInstance<QObject*>("Cubeulator", "AppState");
        auto show_when_solved = std::make_shared<QMetaObject::Connection>();
        *show_when_solved =
            QObject::connect(&solveController, &SolveController::stateChanged, app_state,
                             [&solveController, app_state, show_when_solved]() {
                                 if (!solveController.isSolving()) {
                                     app_state->setProperty("screen", QStringLiteral("solve"));
                                     QObject::disconnect(*show_when_solved);
                                 }
                             });
    }

    return QGuiApplication::exec();
}
