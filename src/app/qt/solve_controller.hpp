#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cube/solver/solver.hpp>
#include <cube/solver/stepped_solution.hpp>

#include <optional>

namespace cube::app {
class ScanSession;
}

// Bridges cube::solver's real solve into QML. Owns a
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

public:
    // How the cube is solved: cube::solver::SolveMethod, in the same order.
    // Exposed to QML (SolveController.Advanced) so the screens name a method
    // by what it is, not by a number.
    enum Method {
        Beginner,      // layer by layer, about 100 turns: the steps people learn
        Intermediate,  // CFOP, about 50 turns: how fast solvers do it
        Advanced,      // two-phase search, about 20 turns: the fewest, nothing to learn
    };
    Q_ENUM(Method)

    // The method the next solve uses, and - when it is changed with a cube in
    // hand - the one the rest of this solve switches to. Changing it
    // re-solves from the cube as it stands, so nothing already turned is
    // lost: the new solution starts from here. isSolving is true meanwhile.
    Q_PROPERTY(Method method READ method WRITE setMethod NOTIFY methodChanged)

private:
    // The 3 visible faces' 9 stickers each, row-major, as colour-name
    // strings ("White"/"Yellow"/"Green"/"Blue"/"Orange"/"Red") - QML maps
    // each name to its Theme.qml hex value, keeping the palette itself
    // owned by the UI layer rather than hardcoded here.
    Q_PROPERTY(QVariantList topColors READ topColors NOTIFY stateChanged)
    Q_PROPERTY(QVariantList rightColors READ rightColors NOTIFY stateChanged)
    Q_PROPERTY(QVariantList leftColors READ leftColors NOTIFY stateChanged)

    // Every sticker of the cube as it stands: side letter ("U", "D", "L",
    // "R", "F", "B") -> nine colour names, each side as seen from outside
    // (cube::solver::outside_view). That is the order
    // Cube3DView.faceletColors takes, so the 3D cube is painted from this
    // once the names are swapped for the palette's colours.
    Q_PROPERTY(QVariantMap stickers READ stickers NOTIFY stateChanged)

    // The whole solution, one entry per move: { face: side letter, turns:
    // quarter turns clockwise seen from outside that side, notation }.
    // Changes only when a solve lands, not as it is stepped through.
    Q_PROPERTY(QVariantList moves READ moves NOTIFY solutionChanged)

    // The move about to be performed from the current state, structured
    // for the cube view (side + turn count) as well as display text.
    // Empty/meaningless once atEnd is true.
    Q_PROPERTY(QString currentMoveNotation READ currentMoveNotation NOTIFY stateChanged)
    Q_PROPERTY(QString currentMoveFace READ currentMoveFace NOTIFY stateChanged)
    Q_PROPERTY(int currentMoveTurns READ currentMoveTurns NOTIFY stateChanged)

    Q_PROPERTY(int moveIndex READ moveIndex NOTIFY stateChanged)
    // Notifying, not constant: the solve lands after the solve screen has
    // been created, and a constant is read once - as zero - and never again.
    Q_PROPERTY(int totalMoves READ totalMoves NOTIFY stateChanged)
    Q_PROPERTY(bool atStart READ atStart NOTIFY stateChanged)
    Q_PROPERTY(bool atEnd READ atEnd NOTIFY stateChanged)

    // One entry per stage of this solution, in order: {stageIndex,
    // startIndex, endIndex} - lets the stage sidebar highlight
    // "done"/"current" by comparing moveIndex against a range instead of one
    // entry per move. stageIndex is a cube::solver::Stage value, not a
    // position: the three methods have different stages, and a screen words
    // each by its value (the list's length is how many there are). Empty until
    // the solve lands (see totalMoves).
    Q_PROPERTY(QVariantList stageRanges READ stageRanges NOTIFY stateChanged)
    // The cube::solver::Stage value of the stage the next move belongs to (the
    // last stage's, once solved).
    Q_PROPERTY(int currentStageIndex READ currentStageIndex NOTIFY stateChanged)

    // True from the moment a completed scan is accepted until its
    // background solve() (see solve_controller.cpp) delivers a result.
    // SolveScreen.qml shows a spinner instead of the previous solve's
    // now-stale move data while this is set.
    Q_PROPERTY(bool isSolving READ isSolving NOTIFY stateChanged)

public:
    explicit SolveController(cube::app::ScanSession& scanSession, QObject* parent = nullptr);

    [[nodiscard]] Method method() const noexcept { return method_; }
    void setMethod(Method method);

    // True when the solution now in place is a continuation of the previous
    // one: the same physical cube, in the same hands, solved a different way
    // because the method changed. False for a solution to a newly scanned
    // cube. HeldCube reads it on solutionChanged: a new cube starts from the
    // hold the screen asks for, a continuation keeps the hold it has seen.
    [[nodiscard]] bool solutionContinuesCube() const noexcept { return continuation_; }

    [[nodiscard]] QVariantList topColors() const;
    [[nodiscard]] QVariantList rightColors() const;
    [[nodiscard]] QVariantList leftColors() const;
    [[nodiscard]] QVariantMap stickers() const;
    [[nodiscard]] QVariantList moves() const;

    // The cube as it stands after the moves stepped through so far.
    [[nodiscard]] const cube::solver::Cube& currentCube() const noexcept {
        return stepped_.current_cube();
    }
    // The move expected next, or nothing once the cube is solved.
    [[nodiscard]] std::optional<cube::solver::Move> currentMove() const noexcept {
        return stepped_.at_end() ? std::nullopt : std::optional(stepped_.current_move());
    }

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
    // A different solution is in place: a solve has landed.
    void solutionChanged();
    // The method changed. The solution is replaced when its solve lands
    // (solutionChanged); this is for whoever shows which method is chosen.
    void methodChanged();

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
    // Kicks off the background solve for `scrambled` with the current
    // method. Shared by onScanCompleted(), solveCube() and a method change;
    // the constructor's initial solved-cube placeholder is set directly, not
    // through this path - see the .cpp. `continuation`: this is the cube
    // already being solved, handed over to another method.
    void beginSolve(const cube::solver::Cube& scrambled, bool continuation = false);
    // Invoked on the UI thread once solveWatcher_'s background solve()
    // (kicked off from beginSolve()) finishes.
    void applySolveResult();

    cube::app::ScanSession& scanSession_;
    cube::solver::SteppedSolution stepped_;
    QFutureWatcher<cube::solver::SteppedSolution> solveWatcher_;
    bool isSolving_ = false;
    Method method_ = Beginner;
    // The cube the solve in flight, or the last one to land, was started
    // from; empty until the first. A method change with nothing scanned only
    // records the method.
    std::optional<cube::solver::Cube> pending_;
    bool solving_continuation_ = false;
    bool continuation_ = false;
};
