#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>

#include <cube/solver/stepped_solution.hpp>

namespace cube::app {
class ScanSession;
}

// Bridges cube::solver's real beginner-method solve into QML. Owns a
// SteppedSolution built from the real scanned cube - normally the
// free-form-assembled Cube the ScanSession this controller is constructed
// with exposes via assembledCube(), with cube::solver::cube_from_scan()
// as the fallback when none is set - rather than a random_scramble().
// SolveScreen.qml/
// CubeView.qml read the exposed properties to draw the current stickers and
// an arrow for the exact move about to happen, rather than the old static
// demo data. Deliberately thin: all solving and stepping logic lives in
// cube::solver, which is plain C++ and Catch2-tested independently of Qt.
//
// Constructed directly in main.cpp and registered via
// qmlRegisterSingletonInstance, like CameraController/ScanSession/
// SettingsModel - not QML_ELEMENT/QML_SINGLETON, since it needs a real C++
// reference to the live ScanSession (cube::model::ScanProgress can't cross
// into QML - cube::model stays Qt-free) rather than being engine-owned.
class SolveController : public QObject {
    Q_OBJECT

    // The 3 visible faces' 9 stickers each, row-major, as colour-name
    // strings ("White"/"Yellow"/"Green"/"Blue"/"Orange"/"Red") - QML maps
    // each name to its Theme.qml hex value, keeping the palette itself
    // owned by the UI layer rather than hardcoded here.
    Q_PROPERTY(QVariantList topColors READ topColors NOTIFY stateChanged)
    Q_PROPERTY(QVariantList rightColors READ rightColors NOTIFY stateChanged)
    Q_PROPERTY(QVariantList leftColors READ leftColors NOTIFY stateChanged)

    // The move about to be performed from the current state, structured
    // for CubeView.qml's arrow geometry (face + turn count) as well as
    // display text. Empty/meaningless once atEnd is true.
    Q_PROPERTY(QString currentMoveNotation READ currentMoveNotation NOTIFY stateChanged)
    Q_PROPERTY(QString currentMoveFace READ currentMoveFace NOTIFY stateChanged)
    Q_PROPERTY(int currentMoveTurns READ currentMoveTurns NOTIFY stateChanged)

    Q_PROPERTY(int moveIndex READ moveIndex NOTIFY stateChanged)
    // Notifying, not constant: the solve lands after the solve screen has
    // been created, and a constant is read once - as zero - and never again.
    Q_PROPERTY(int totalMoves READ totalMoves NOTIFY stateChanged)
    Q_PROPERTY(bool atStart READ atStart NOTIFY stateChanged)
    Q_PROPERTY(bool atEnd READ atEnd NOTIFY stateChanged)

    // One entry per solving stage, in order: {stageIndex, startIndex,
    // endIndex} - lets the stage sidebar highlight "done"/"current" by
    // comparing moveIndex against a range instead of one entry per move.
    // Empty until the solve lands (see totalMoves).
    Q_PROPERTY(QVariantList stageRanges READ stageRanges NOTIFY stateChanged)
    Q_PROPERTY(int currentStageIndex READ currentStageIndex NOTIFY stateChanged)

    // True from the moment a completed scan is accepted until its
    // background solve() (see solve_controller.cpp) delivers a result.
    // SolveScreen.qml shows a spinner instead of the previous solve's
    // now-stale move data while this is set.
    Q_PROPERTY(bool isSolving READ isSolving NOTIFY stateChanged)

public:
    explicit SolveController(cube::app::ScanSession& scanSession, QObject* parent = nullptr);

    [[nodiscard]] QVariantList topColors() const;
    [[nodiscard]] QVariantList rightColors() const;
    [[nodiscard]] QVariantList leftColors() const;

    [[nodiscard]] QString currentMoveNotation() const;
    [[nodiscard]] QString currentMoveFace() const;
    [[nodiscard]] int currentMoveTurns() const;

    [[nodiscard]] int moveIndex() const;
    [[nodiscard]] int totalMoves() const;
    [[nodiscard]] bool atStart() const;
    [[nodiscard]] bool atEnd() const;

    [[nodiscard]] QVariantList stageRanges() const;
    [[nodiscard]] int currentStageIndex() const;

    [[nodiscard]] bool isSolving() const;

    // Starts solving a cube handed over already validated - the direct-scan
    // path. isSolving is true until the solution lands. solveReady is not
    // emitted: the caller decides when to show the solution, so a finished
    // scan can stay on screen while this runs.
    void solveCube(const cube::solver::Cube& cube);

public slots:
    void nextMove();
    void previousMove();

signals:
    void stateChanged();

    // Emitted once a completed scan is accepted and its background solve is
    // kicked off (isSolving becomes true at the same point) - AppState.qml
    // navigates to the solve screen on this, not on
    // ScanSession::scanCompleted directly, since a completed scan isn't
    // necessarily a valid one.
    void solveReady();

    // Emitted instead of solveReady when the fallback
    // cube::solver::cube_from_scan() path rejected the completed scan
    // (e.g. a genuine colour misclassification failing
    // Cube::is_physically_valid()); `reason` is cube::to_string(CubeError).
    // scanSession_'s progress is already reset (via restartScan()) by the
    // time this fires. Unreachable when assembledCube() is set - assembly
    // already validated that cube before scanCompleted ever fired.
    void scanRejected(QString reason);

private:
    // Connected to scanSession_'s scanCompleted signal.
    void onScanCompleted();
    // Kicks off the background solve for `scrambled` (the real scanned
    // cube). Shared by onScanCompleted() and the constructor's initial
    // solved-cube placeholder is set directly, not through this path - see
    // the .cpp.
    void beginSolve(const cube::solver::Cube& scrambled);
    // Invoked on the UI thread once solveWatcher_'s background solve()
    // (kicked off from beginSolve()) finishes.
    void applySolveResult();

    cube::app::ScanSession& scanSession_;
    cube::solver::SteppedSolution stepped_;
    QFutureWatcher<cube::solver::SteppedSolution> solveWatcher_;
    bool isSolving_ = false;
};
