#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <cube/solver/stepped_solution.hpp>

// Bridges cube::solver's real beginner-method solve into QML. Owns a
// SteppedSolution (built from a random_scramble() - there's no vision
// pipeline yet, see docs/architecture.md) and forwards per-move stepping to
// it; SolveScreen.qml/CubeView.qml read the exposed properties to draw the
// current stickers and an arrow for the exact move about to happen, rather
// than the old static demo data. Deliberately thin: all solving and
// stepping logic lives in cube::solver, which is plain C++ and
// Catch2-tested independently of Qt.
class SolveController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

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
    Q_PROPERTY(int totalMoves READ totalMoves CONSTANT)
    Q_PROPERTY(bool atStart READ atStart NOTIFY stateChanged)
    Q_PROPERTY(bool atEnd READ atEnd NOTIFY stateChanged)

    // One entry per solving stage, in order: {stageIndex, startIndex,
    // endIndex} - lets the stage sidebar highlight "done"/"current" by
    // comparing moveIndex against a range instead of one entry per move.
    Q_PROPERTY(QVariantList stageRanges READ stageRanges CONSTANT)
    Q_PROPERTY(int currentStageIndex READ currentStageIndex NOTIFY stateChanged)

    // True from restart() until its background solve() (see
    // solve_controller.cpp) delivers a result. SolveScreen.qml shows a
    // spinner instead of the previous solve's now-stale move data while
    // this is set.
    Q_PROPERTY(bool isSolving READ isSolving NOTIFY stateChanged)

public:
    explicit SolveController(QObject* parent = nullptr);

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

public slots:
    void nextMove();
    void previousMove();

    // Discards progress and starts over on a fresh scramble - used by the
    // "scan another" flow once wired up; also what the constructor calls
    // to seed the first solve.
    void restart(unsigned seed);

signals:
    void stateChanged();

private:
    // Invoked on the UI thread once solveWatcher_'s background solve()
    // (kicked off from restart()) finishes.
    void applySolveResult();

    cube::solver::SteppedSolution stepped_;
    QFutureWatcher<cube::solver::SteppedSolution> solveWatcher_;
    bool isSolving_ = false;
};
