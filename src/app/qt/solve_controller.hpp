#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cube/diagnostics/json.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/stepped_plan.hpp>
#include <cube/solver/solver.hpp>

#include <optional>
#include <vector>

// Bridges the real solve - in whichever of its three methods is chosen - into
// QML. Owns a SteppedPlan built from the cube the scan read (solveCube). The
// solve screen reads the exposed properties to draw the current stickers and
// the move about to happen. Deliberately thin: all solving and stepping logic
// lives in cube::nxn, which is plain C++ and Catch2-tested independently of
// Qt.
//
// The cube and its moves are held as cube::nxn types, a 3x3 being an NCube of
// size 3 (docs/nxn-app-plan.md, decision 1). The accessors that return a
// cube::solver::Cube or Move - currentCube(), currentMove(), scannedCube(),
// solutionStart() - are the 3x3 view of that, which is what HeldCube, the
// history and the diagnostics are written in; they have a value only while the
// cube is a 3x3.
//
// Constructed in C++ and registered via qmlRegisterSingletonInstance, like
// the other controllers - not QML_ELEMENT/QML_SINGLETON, since the scan
// controller hands it a cube::solver::Cube, which cannot cross into QML.
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
    explicit SolveController(QObject* parent = nullptr);

    [[nodiscard]] QVariantList topColors() const;
    [[nodiscard]] QVariantList rightColors() const;
    [[nodiscard]] QVariantList leftColors() const;
    [[nodiscard]] QVariantMap stickers() const;
    // The cube as it stands after `turn` turns of this solution - `stickers`
    // for any position, not only the one the solve screen has stepped to.
    // For the screen that reviews the solution, which has a position of its
    // own and must not move the solve's. 0 is the cube as the solve began;
    // totalMoves is the cube solved. Clamped to that range.
    Q_INVOKABLE QVariantMap stickersAt(int turn) const;
    [[nodiscard]] QVariantList moves() const;

    // The solution being followed, for whoever needs the cube at any size.
    [[nodiscard]] const cube::nxn::SteppedPlan& plan() const noexcept { return stepped_; }

    // The cube as it stands after the moves stepped through so far, as a 3x3.
    // A solved 3x3 while the cube is of another size.
    [[nodiscard]] const cube::solver::Cube& currentCube() const noexcept { return cube3_; }
    // The move expected next as a 3x3 move, or nothing once the cube is solved
    // (or when it is not a 3x3).
    [[nodiscard]] std::optional<cube::solver::Move> currentMove() const noexcept;

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

    // The solve as it stands, for a diagnostics record
    // (docs/diagnostics.md): method, the cube now, the whole move list, which
    // move is next and how far through.
    [[nodiscard]] cube::diagnostics::Json diagnosticState() const;

    [[nodiscard]] Method method() const noexcept { return method_; }
    void setMethod(Method method);

    // True when the solution now in place is a continuation of the previous
    // one: the same physical cube, in the same hands, solved a different way
    // because the method changed. False for a solution to a newly scanned
    // cube. HeldCube reads it on solutionChanged: a new cube starts from the
    // hold the screen asks for, a continuation keeps the hold it has seen.
    [[nodiscard]] bool solutionContinuesCube() const noexcept { return continuation_; }

    // Starts solving a cube handed over already validated - the direct-scan
    // path. isSolving is true until the solution lands. Nothing is
    // announced beyond that: the caller decides when to show the solution,
    // so a finished scan can stay on screen while this runs.
    void solveCube(const cube::solver::Cube& cube);
    // The same for a cube of any size.
    void solveCube(const cube::nxn::NCube& cube);

    // The cube as the scan handed it over: what the solve being followed is a
    // solve OF. Set by solveCube(); a method change part-way does not move it
    // (solutionStart() is what moves). Empty until the first scan.
    [[nodiscard]] const std::optional<cube::nxn::NCube>& scannedNCube() const noexcept {
        return scanned_;
    }
    // The cube the solution in force - or the one in flight - starts from. The
    // scanned cube until the method is changed part-way, then the cube in hand
    // at that moment. The turn moveIndex() counts is a turn of THIS solution.
    [[nodiscard]] const std::optional<cube::nxn::NCube>& solutionStartNCube() const noexcept {
        return pending_;
    }
    // Those two as 3x3s, which the history and HeldCube are written in: empty
    // when there is no such cube, or it is of another size.
    [[nodiscard]] std::optional<cube::solver::Cube> scannedCube() const;
    [[nodiscard]] std::optional<cube::solver::Cube> solutionStart() const;

    // Picks a solve up where it was left: `from` is solved with `method` and,
    // when the solution lands, stepped on `turn` turns, so the screen opens on
    // the turn that was reached. `scanned` is kept as the cube the solve is of
    // (see scannedCube()). It is a new cube as far as HeldCube is concerned
    // (solutionContinuesCube() is false): it starts from the hold the screen
    // asks for.
    void resumeCube(const cube::solver::Cube& scanned, const cube::solver::Cube& from,
                    Method method, int turn);
    // The same for a cube of any size.
    void resumeCube(const cube::nxn::NCube& scanned, const cube::nxn::NCube& from, Method method,
                    int turn);

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

private:
    // Kicks off the background solve for `scrambled` with the current method.
    // Shared by solveCube() and a method change; the constructor's
    // solved-cube placeholder is set directly, not through this path - see
    // the .cpp. `continuation`: this is the cube already being solved, handed
    // over to another method.
    void beginSolve(const cube::nxn::NCube& scrambled, bool continuation = false);
    // Invoked on the UI thread once solveWatcher_'s background solve()
    // (kicked off from beginSolve()) finishes.
    void applySolveResult();
    // Brings cube3_ to the cube the plan now shows.
    void showCurrentCube();

    cube::nxn::SteppedPlan stepped_;
    QFutureWatcher<cube::nxn::SteppedPlan> solveWatcher_;
    // The cube on show as a 3x3 (currentCube()); a solved one for any other size.
    cube::solver::Cube cube3_ = cube::solver::Cube::solved();
    bool isSolving_ = false;
    Method method_ = Beginner;
    // The cube the solve in flight, or the last one to land, was started
    // from; empty until the first. A method change with nothing scanned only
    // records the method.
    std::optional<cube::nxn::NCube> pending_;
    // The cube the scan read, whatever the method has done since.
    std::optional<cube::nxn::NCube> scanned_;
    // Turns to step on when the solve in flight lands (resumeCube); 0 for any
    // other solve.
    int resume_turn_ = 0;
    bool solving_continuation_ = false;
    bool continuation_ = false;
};
