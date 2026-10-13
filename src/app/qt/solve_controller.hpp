#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cube/solver/stepped_solution.hpp>

#include <optional>

// Bridges cube::solver's real beginner-method solve into QML. Owns a
// SteppedSolution built from the cube the scan read (solveCube). The solve
// screen reads the exposed properties to draw the current stickers and the
// move about to happen. Deliberately thin: all solving and stepping logic
// lives in cube::solver, which is plain C++ and Catch2-tested independently
// of Qt.
//
// Constructed in C++ and registered via qmlRegisterSingletonInstance, like
// the other controllers - not QML_ELEMENT/QML_SINGLETON, since the scan
// controller hands it a cube::solver::Cube, which cannot cross into QML.
class SolveController : public QObject {
    Q_OBJECT

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
    explicit SolveController(QObject* parent = nullptr);

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
    // path. isSolving is true until the solution lands. Nothing is
    // announced beyond that: the caller decides when to show the solution,
    // so a finished scan can stay on screen while this runs.
    void solveCube(const cube::solver::Cube& cube);

public slots:
    void nextMove();
    void previousMove();

signals:
    void stateChanged();
    // A different solution is in place: a solve has landed.
    void solutionChanged();

private:
    // Kicks off the background solve for `scrambled` (the scanned cube).
    // The constructor's solved-cube placeholder is set directly, not through
    // this path - see the .cpp.
    void beginSolve(const cube::solver::Cube& scrambled);
    // Invoked on the UI thread once solveWatcher_'s background solve()
    // (kicked off from beginSolve()) finishes.
    void applySolveResult();

    cube::solver::SteppedSolution stepped_;
    QFutureWatcher<cube::solver::SteppedSolution> solveWatcher_;
    bool isSolving_ = false;
};
