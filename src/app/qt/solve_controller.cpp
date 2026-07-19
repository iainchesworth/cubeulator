#include "solve_controller.hpp"

#include <QVariantMap>

#include <cube/solver/notation.hpp>
#include <cube/solver/solver.hpp>

using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::random_scramble;
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

// solve() is called synchronously on construction/restart - fine for now
// with a seed known to solve quickly, but genuinely slow for some seeds
// (see pll.cpp's history: certain scrambles' PLL search can take tens of
// minutes). Blocking the UI thread on an arbitrary seed would freeze the
// app; moving this to a background thread (or pre-computing a small pool
// of known-fast seeds) is a real follow-up, not addressed in this pass.
constexpr unsigned kDefaultScrambleSeed = 0;

}  // namespace

SolveController::SolveController(QObject* parent) : QObject(parent) { restart(kDefaultScrambleSeed); }

void SolveController::restart(unsigned seed) {
    const Cube scrambled = random_scramble(seed, 25);
    const auto solution = solve(scrambled);
    if (solution.has_value()) {
        stepped_ = SteppedSolution::from(scrambled, *solution);
    } else {
        // Should not happen for a physically valid scramble (every real
        // scramble solves) - fall back to an already-solved, zero-move
        // stepped solution so the UI has something consistent to show
        // rather than crashing.
        stepped_ = SteppedSolution::from(Cube::solved(), cube::solver::Solution{});
    }
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

void SolveController::nextMove() {
    stepped_.next();
    emit stateChanged();
}

void SolveController::previousMove() {
    stepped_.previous();
    emit stateChanged();
}
