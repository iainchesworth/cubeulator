#include "solve_controller.hpp"

#include "colour_name.hpp"

#include <QVariantMap>
#include <QtConcurrentRun>

#include <cube/diagnostics/describe.hpp>
#include <cube/error.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/cube3.hpp>
#include <cube/nxn/notation.hpp>
#include <cube/nxn/outside_view.hpp>
#include <cube/profiling.hpp>
#include <cube/solver/solver.hpp>

#include <algorithm>
#include <array>
#include <cstddef>

using cube::nxn::NCube;
using cube::nxn::solve_held;
using cube::nxn::SteppedPlan;
using cube::nxn::WideMove;
using cube::solver::Cube;
using cube::solver::Face;
using cube::solver::SolveMethod;

namespace {

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
        colors.append(colour_name(color));
    }
    return colors;
}

// Every sticker of `cube`: side letter -> N x N colour names (nine, for a 3x3),
// each side as seen from outside it.
QVariantMap stickers_of(const NCube& cube) {
    QVariantMap sides;
    for (const Face face : cube::solver::kAllFaces) {
        QVariantList colors;
        colors.reserve(static_cast<qsizetype>(cube.size()) * cube.size());
        for (const cube::solver::Color color : cube::nxn::outside_view(cube, face)) {
            colors.append(colour_name(color));
        }
        sides.insert(face_letter(face), colors);
    }
    return sides;
}

// The 3x3 a cube is, if it is one.
std::optional<Cube> as_cube3(const std::optional<NCube>& cube) {
    if (!cube.has_value() || cube->size() != 3) {
        return std::nullopt;
    }
    if (const auto held = cube->to_cube3_as_held(); held.has_value()) {
        return *held;
    }
    return std::nullopt;
}

// A move of a 3x3 plan as a 3x3 move. A 3x3 plan is turns of the outer layers.
cube::solver::Move move3_of(const WideMove& move) {
    return cube::solver::Move{.face = move.face, .turns = move.turns};
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

// How the moves of a solution are shown. A 3x3 and a 2x2 are shown as the
// library has them, which is what they always were; a bigger cube's adjacent
// layers turned the same way are one move, as a person would say it ("Rw") -
// the layer turns are what the history keeps (docs/nxn-app-plan.md, "Notation").
cube::nxn::Display display_for(int size) {
    return size >= 4 ? cube::nxn::Display::Wide : cube::nxn::Display::Layers;
}

// Runs on a QtConcurrent worker thread, not the UI thread - see beginSolve().
SteppedPlan solve_stepped(const NCube& scrambled, SolveMethod method) {
    CUBE_ZONE_SCOPED_N("app.solve_stepped");
    const auto solution = solve_held(scrambled, method);
    if (solution.has_value()) {
        return SteppedPlan::from(scrambled, *solution, display_for(scrambled.size()));
    }
    // Should not happen for a physically valid cube (every real cube
    // solves) - fall back to an already-solved, zero-move plan so the UI has
    // something consistent to show rather than crashing.
    return SteppedPlan::from(*NCube::solved(scrambled.size()), cube::nxn::Solution{});
}

}  // namespace

SolveController::SolveController(QObject* parent) : QObject(parent) {
    // Safe placeholder so topColors()/etc. never read an empty stepped_
    // before the first real scan completes: a plan of the solved 3x3.
    stepped_ = SteppedPlan{};
    showCurrentCube();
    connect(&solveWatcher_, &QFutureWatcher<SteppedPlan>::finished, this,
            &SolveController::applySolveResult);
}

void SolveController::solveCube(const Cube& cube) {
    solveCube(NCube::from_cube3(cube));
}

void SolveController::solveCube(const NCube& cube) {
    scanned_ = cube;
    beginSolve(cube);
}

std::optional<Cube> SolveController::scannedCube() const {
    return as_cube3(scanned_);
}

std::optional<Cube> SolveController::solutionStart() const {
    return as_cube3(pending_);
}

std::optional<cube::solver::Move> SolveController::currentMove() const noexcept {
    if (stepped_.size() != 3 || stepped_.at_end()) {
        return std::nullopt;
    }
    return move3_of(stepped_.current_move());
}

void SolveController::showCurrentCube() {
    const auto held = stepped_.current_cube().to_cube3_as_held();
    cube3_ = held.has_value() ? *held : Cube::solved();
}

void SolveController::resumeCube(const Cube& scanned, const Cube& from, Method method, int turn) {
    scanned_ = NCube::from_cube3(scanned);
    if (method != method_) {
        // Recorded, not acted on through setMethod(): the cube to solve is
        // `from`, not the one a method change would carry on from.
        method_ = method;
        emit methodChanged();
    }
    beginSolve(NCube::from_cube3(from));
    resume_turn_ = std::max(0, turn);
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
    const NCube from = isSolving_ ? *pending_ : stepped_.current_cube();
    // It continues a solution only if one had landed for this cube. A scan
    // whose solve is still being worked out has none, and what it replaces is
    // still "a new cube to solve" (HeldCube starts that from the stated hold).
    const bool continuation = isSolving_ ? solving_continuation_ : true;
    beginSolve(from, continuation);
}

void SolveController::beginSolve(const NCube& scrambled, bool continuation) {
    // A scan can't complete while a solve is in flight (the scan screen isn't
    // reachable again until this solve's result has been applied - see
    // AppState.qml), but a method change can: it replaces the solve.
    pending_ = scrambled;
    solving_continuation_ = continuation;
    resume_turn_ = 0;
    isSolving_ = true;
    CUBE_MESSAGE("solve.started");
    emit stateChanged();
    solveWatcher_.setFuture(
        QtConcurrent::run(&solve_stepped, scrambled, to_solver_method(method_)));
}

void SolveController::applySolveResult() {
    CUBE_ZONE_SCOPED_N("app.solve.apply_result");
    CUBE_MESSAGE("solve.finished");
    stepped_ = solveWatcher_.result();
    // A solve picked up from the history lands on the turn it was left at.
    for (int i = 0; i < resume_turn_ && !stepped_.at_end(); ++i) {
        stepped_.next();
    }
    showCurrentCube();
    resume_turn_ = 0;
    isSolving_ = false;
    continuation_ = solving_continuation_;
    emit solutionChanged();
    emit stateChanged();
}

QVariantList SolveController::topColors() const {
    return face_colors(cube3_, Face::U);
}
QVariantList SolveController::rightColors() const {
    return face_colors(cube3_, Face::R);
}
QVariantList SolveController::leftColors() const {
    return face_colors(cube3_, Face::F);
}

QVariantMap SolveController::stickers() const {
    return stickers_of(stepped_.current_cube());
}

QVariantMap SolveController::stickersAt(int turn) const {
    return stickers_of(stepped_.cube_at(turn));
}

QVariantList SolveController::moves() const {
    QVariantList moves;
    moves.reserve(stepped_.move_count());
    for (const WideMove& move : stepped_.moves()) {
        moves.append(QVariantMap{
            {QStringLiteral("face"), face_letter(move.face)},
            {QStringLiteral("turns"), move.turns},
            {QStringLiteral("first"), move.first},
            {QStringLiteral("last"), move.last},
            {
                QStringLiteral("notation"),
                QString::fromStdString(cube::nxn::to_string(move, stepped_.size())),
            },
        });
    }
    return moves;
}

QString SolveController::currentMoveNotation() const {
    if (stepped_.at_end()) {
        return QString();
    }
    return QString::fromStdString(cube::nxn::to_string(stepped_.current_move(), stepped_.size()));
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

int SolveController::currentMoveFirst() const {
    return stepped_.at_end() ? 0 : stepped_.current_move().first;
}

int SolveController::currentMoveLast() const {
    return stepped_.at_end() ? 0 : stepped_.current_move().last;
}

int SolveController::size() const {
    return stepped_.size();
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
        entry[QStringLiteral("stageIndex")] = static_cast<int>(range.id);
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
                   : static_cast<int>(stepped_.stage_ranges().back().id);
    }
    return static_cast<int>(stepped_.current_stage());
}

int SolveController::coreStartIndex() const {
    return stepped_.core_start();
}

bool SolveController::canSkipToCore() const {
    return stepped_.size() >= 4 && stepped_.current_index() < stepped_.core_start();
}

bool SolveController::isSolving() const {
    return isSolving_;
}

cube::diagnostics::Json SolveController::diagnosticState() const {
    using cube::diagnostics::Json;
    constexpr std::array<const char*, 3> kMethods = {"beginner", "intermediate", "advanced"};
    Json state = Json::object();
    state.set("method", kMethods.at(static_cast<std::size_t>(method_)));
    state.set("is_solving", isSolving_);
    state.set("continuation", continuation_);
    state.set("move_index", stepped_.current_index());
    state.set("total_moves", stepped_.move_count());
    state.set("at_end", stepped_.at_end());
    std::vector<cube::solver::Move> moves3;
    if (stepped_.size() == 3) {
        moves3.reserve(stepped_.moves().size());
        for (const WideMove& move : stepped_.moves()) {
            moves3.push_back(move3_of(move));
        }
    }
    const std::optional<Cube> started_from = solutionStart();
    state.set("current_move", cube::diagnostics::move_json(currentMove()));
    state.set("moves", cube::diagnostics::moves_json(moves3));
    state.set("cube", cube::diagnostics::cube_json(cube3_));
    state.set("solved", stepped_.current_cube().is_solved());
    state.set("started_from",
              started_from.has_value() ? cube::diagnostics::cube_json(*started_from) : Json());
    state.set("stage", currentStageIndex());
    return state;
}

void SolveController::nextMove() {
    stepped_.next();
    showCurrentCube();
    emit stateChanged();
}

void SolveController::previousMove() {
    stepped_.previous();
    showCurrentCube();
    emit stateChanged();
}

void SolveController::goToMove(int index) {
    if (isSolving_ || index == stepped_.current_index()) {
        return;
    }
    stepped_.seek(index);
    showCurrentCube();
    emit stateChanged();
}

void SolveController::skipToCore() {
    if (canSkipToCore()) {
        goToMove(stepped_.core_start());
    }
}
