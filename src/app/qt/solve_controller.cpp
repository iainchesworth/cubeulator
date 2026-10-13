#include "solve_controller.hpp"

#include "controllers/scan_session.hpp"

#include <QVariantMap>
#include <QtConcurrentRun>

#include <cube/error.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/outside_view.hpp>
#include <cube/solver/scan_conversion.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::cube_from_scan;
using cube::solver::Face;
using cube::solver::solve;
using cube::solver::SolveMethod;
using cube::solver::SteppedSolution;

namespace {

QString color_name(cube::solver::Color color) {
    switch (color) {
        case cube::solver::Color::White:
            return QStringLiteral("White");
        case cube::solver::Color::Yellow:
            return QStringLiteral("Yellow");
        case cube::solver::Color::Green:
            return QStringLiteral("Green");
        case cube::solver::Color::Blue:
            return QStringLiteral("Blue");
        case cube::solver::Color::Orange:
            return QStringLiteral("Orange");
        case cube::solver::Color::Red:
            return QStringLiteral("Red");
    }
    return QStringLiteral("White");
}

QString face_letter(Face face) {
    switch (face) {
        case Face::U:
            return QStringLiteral("U");
        case Face::D:
            return QStringLiteral("D");
        case Face::L:
            return QStringLiteral("L");
        case Face::R:
            return QStringLiteral("R");
        case Face::F:
            return QStringLiteral("F");
        case Face::B:
            return QStringLiteral("B");
    }
    return QStringLiteral("U");
}

QVariantList face_colors(const Cube& cube, Face face) {
    QVariantList colors;
    colors.reserve(9);
    for (cube::solver::Color color : cube.face(face)) {
        colors.append(color_name(color));
    }
    return colors;
}

SolveMethod to_solver_method(SolveController::Method method) {
    switch (method) {
        case SolveController::Beginner:
            return SolveMethod::Beginner;
        case SolveController::Intermediate:
            return SolveMethod::Intermediate;
        case SolveController::Advanced:
            return SolveMethod::Advanced;
    }
    return SolveMethod::Beginner;
}

// Every method completes in seconds or far less on any scramble (the beginner
// method's last layer uses fixed 2-look algorithms - see
// src/lib/src/solver/last_layer.hpp; it once ranged up to tens of minutes
// when that stage was searched per-scramble - and the other two take
// milliseconds). beginSolve() below still runs it on a background thread via
// QtConcurrent::run rather than the UI thread, so even those seconds never
// affect app responsiveness.

// Runs on a QtConcurrent worker thread, not the UI thread - see beginSolve().
SteppedSolution solve_stepped(Cube scrambled, SolveMethod method) {
    const auto solution = solve(scrambled, method);
    if (solution.has_value()) {
        return SteppedSolution::from(scrambled, *solution);
    }
    // Should not happen for a physically valid cube (every real cube
    // solves) - fall back to an already-solved, zero-move stepped solution
    // so the UI has something consistent to show rather than crashing.
    return SteppedSolution::from(Cube::solved(), cube::solver::Solution{});
}

}  // namespace

SolveController::SolveController(cube::app::ScanSession& scanSession, QObject* parent)
    : QObject(parent), scanSession_(scanSession) {
    // Safe placeholder so topColors()/etc. never read an empty stepped_
    // before the first real scan completes.
    stepped_ = SteppedSolution::from(Cube::solved(), cube::solver::Solution{});
    connect(&solveWatcher_, &QFutureWatcher<SteppedSolution>::finished, this,
            &SolveController::applySolveResult);
    connect(&scanSession_, &cube::app::ScanSession::scanCompleted, this,
            &SolveController::onScanCompleted);
}

void SolveController::onScanCompleted() {
    // Free-form scans arrive pre-assembled: ScanSession only emits
    // scanCompleted once cube::solver::assemble_scanned_faces() has
    // produced a validated Cube, exposed via assembledCube() - consume
    // that directly, skipping the holding-convention cube_from_scan()
    // conversion entirely. The fallback below only runs when no assembled
    // cube is set (a ScanSession whose progress was populated without the
    // free-form pipeline), keeping the legacy construction path - and its
    // rejection handling - intact.
    if (const auto assembled = scanSession_.assembledCube(); assembled.has_value()) {
        beginSolve(*assembled);
        emit solveReady();
        return;
    }

    const auto scanned_cube = cube_from_scan(scanSession_.progress());
    if (!scanned_cube.has_value()) {
        scanSession_.restartScan();
        emit scanRejected(QString::fromStdString(std::string(to_string(scanned_cube.error()))));
        return;
    }
    beginSolve(*scanned_cube);
    emit solveReady();
}

void SolveController::solveCube(const Cube& cube) {
    beginSolve(cube);
}

void SolveController::setMethod(Method method) {
    if (method == method_) {
        return;
    }
    method_ = method;
    emit methodChanged();

    // Nothing scanned yet: the method is only recorded, and the next solve
    // uses it.
    if (!pending_.has_value()) {
        return;
    }
    // Carry on from the cube as it stands. A solve still in flight is
    // replaced - its cube is the one to solve - rather than waited for: the
    // QFutureWatcher drops the old future's result when it is given a new one.
    const Cube from = isSolving_ ? *pending_ : stepped_.current_cube();
    // It continues a solution only if one had landed for this cube. A scan
    // whose solve is still being worked out has none, and what it replaces is
    // still "a new cube to solve" (HeldCube starts that from the stated hold).
    const bool continuation = isSolving_ ? solving_continuation_ : true;
    beginSolve(from, continuation);
}

void SolveController::beginSolve(const Cube& scrambled, bool continuation) {
    // A scan can't complete while a solve is in flight (the scan screen isn't
    // reachable again until this solve's result has been applied - see
    // AppState.qml), but a method change can: it replaces the solve.
    pending_ = scrambled;
    solving_continuation_ = continuation;
    isSolving_ = true;
    emit stateChanged();
    solveWatcher_.setFuture(
        QtConcurrent::run(&solve_stepped, scrambled, to_solver_method(method_)));
}

void SolveController::applySolveResult() {
    stepped_ = solveWatcher_.result();
    isSolving_ = false;
    continuation_ = solving_continuation_;
    emit solutionChanged();
    emit stateChanged();
}

QVariantList SolveController::topColors() const {
    return face_colors(stepped_.current_cube(), Face::U);
}
QVariantList SolveController::rightColors() const {
    return face_colors(stepped_.current_cube(), Face::R);
}
QVariantList SolveController::leftColors() const {
    return face_colors(stepped_.current_cube(), Face::F);
}

QVariantMap SolveController::stickers() const {
    QVariantMap sides;
    for (const Face face : cube::solver::kAllFaces) {
        QVariantList colors;
        colors.reserve(9);
        for (const cube::solver::Color color :
             cube::solver::outside_view(stepped_.current_cube(), face)) {
            colors.append(color_name(color));
        }
        sides.insert(face_letter(face), colors);
    }
    return sides;
}

QVariantList SolveController::moves() const {
    QVariantList moves;
    moves.reserve(stepped_.move_count());
    for (const cube::solver::Move& move : stepped_.moves()) {
        moves.append(QVariantMap{
            {QStringLiteral("face"), face_letter(move.face)},
            {QStringLiteral("turns"), move.turns},
            {QStringLiteral("notation"), QString::fromStdString(cube::solver::to_string(move))}});
    }
    return moves;
}

QString SolveController::currentMoveNotation() const {
    if (stepped_.at_end()) {
        return QString();
    }
    return QString::fromStdString(cube::solver::to_string(stepped_.current_move()));
}

QString SolveController::currentMoveFace() const {
    if (stepped_.at_end()) {
        return QString();
    }
    return face_letter(stepped_.current_move().face);
}

int SolveController::currentMoveTurns() const {
    return stepped_.at_end() ? 0 : stepped_.current_move().turns;
}

int SolveController::moveIndex() const {
    return stepped_.current_index();
}
int SolveController::totalMoves() const {
    return stepped_.move_count();
}
bool SolveController::atStart() const {
    return stepped_.at_start();
}
bool SolveController::atEnd() const {
    return stepped_.at_end();
}

QVariantList SolveController::stageRanges() const {
    QVariantList ranges;
    for (const auto& range : stepped_.stage_ranges()) {
        QVariantMap entry;
        entry[QStringLiteral("stageIndex")] = static_cast<int>(range.stage);
        entry[QStringLiteral("startIndex")] = range.start_index;
        entry[QStringLiteral("endIndex")] = range.end_index;
        ranges.append(entry);
    }
    return ranges;
}

int SolveController::currentStageIndex() const {
    if (stepped_.at_end()) {
        return stepped_.stage_ranges().empty()
                   ? 0
                   : static_cast<int>(stepped_.stage_ranges().back().stage);
    }
    return static_cast<int>(stepped_.current_stage());
}

bool SolveController::isSolving() const {
    return isSolving_;
}

void SolveController::nextMove() {
    stepped_.next();
    emit stateChanged();
}

void SolveController::previousMove() {
    stepped_.previous();
    emit stateChanged();
}
