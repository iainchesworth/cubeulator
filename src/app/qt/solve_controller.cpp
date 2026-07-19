#include "solve_controller.hpp"

#include "controllers/scan_session.hpp"

#include <QVariantMap>
#include <QtConcurrentRun>

#include <cube/error.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/scan_conversion.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::cube_from_scan;
using cube::solver::Face;
using cube::solver::solve;
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

// solve() can take anywhere from milliseconds to tens of minutes depending
// on the cube (see pll.cpp's history for a scramble whose PLL search runs
// long) - beginSolve() below runs it on a background thread via
// QtConcurrent::run rather than the UI thread, so a slow cube never freezes
// the app.

// Runs on a QtConcurrent worker thread, not the UI thread - see beginSolve().
SteppedSolution solve_stepped(Cube scrambled) {
    const auto solution = solve(scrambled);
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
    connect(&solveWatcher_, &QFutureWatcher<SteppedSolution>::finished, this, &SolveController::applySolveResult);
    connect(&scanSession_, &cube::app::ScanSession::scanCompleted, this, &SolveController::onScanCompleted);
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

void SolveController::beginSolve(const Cube& scrambled) {
    // isSolving_ can't already be true here: onScanCompleted() (the only
    // caller) fires from ScanSession::scanCompleted, which fires once per
    // completed scan, and the scan screen isn't reachable again until this
    // solve's result has been applied (see AppState.qml).
    isSolving_ = true;
    emit stateChanged();
    solveWatcher_.setFuture(QtConcurrent::run(&solve_stepped, scrambled));
}

void SolveController::applySolveResult() {
    stepped_ = solveWatcher_.result();
    isSolving_ = false;
    emit stateChanged();
}

QVariantList SolveController::topColors() const { return face_colors(stepped_.current_cube(), Face::U); }
QVariantList SolveController::rightColors() const { return face_colors(stepped_.current_cube(), Face::R); }
QVariantList SolveController::leftColors() const { return face_colors(stepped_.current_cube(), Face::F); }

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

int SolveController::currentMoveTurns() const { return stepped_.at_end() ? 0 : stepped_.current_move().turns; }

int SolveController::moveIndex() const { return stepped_.current_index(); }
int SolveController::totalMoves() const { return stepped_.move_count(); }
bool SolveController::atStart() const { return stepped_.at_start(); }
bool SolveController::atEnd() const { return stepped_.at_end(); }

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
        return stepped_.stage_ranges().empty() ? 0 : static_cast<int>(stepped_.stage_ranges().back().stage);
    }
    return static_cast<int>(stepped_.current_stage());
}

bool SolveController::isSolving() const { return isSolving_; }

void SolveController::nextMove() {
    stepped_.next();
    emit stateChanged();
}

void SolveController::previousMove() {
    stepped_.previous();
    emit stateChanged();
}
