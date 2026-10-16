#include "controllers/solve_history.hpp"

#include "colour_name.hpp"
#include "interfaces/paths.hpp"
#include "solve_controller.hpp"

#include <cube/nxn/outside_view.hpp>
#include <cube/nxn/solution.hpp>
#include <cube/nxn/stage.hpp>

#include <QDateTime>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

#include <cstddef>
#include <optional>
#include <utility>

namespace cube::app {

namespace {

// Where the keep switch lives (docs/design/screens/settings.md, "Persistence").
constexpr char kKeepKey[] = "history/keep";
constexpr char kFileName[] = "solve-history.json";

// The solution an entry kept, as a solution again: its turns cut into the
// stages it says. Nothing for an entry that kept none, or kept turns without
// saying which stages they are (those are worked out again, as a 3x3's are).
[[nodiscard]] std::optional<cube::nxn::Solution> kept_solution(const HistoryEntry& entry) {
    if (entry.moves.empty() || entry.stages.empty()) {
        return std::nullopt;
    }
    cube::nxn::Solution solution;
    std::size_t at = 0;
    for (const KeptStage& kept : entry.stages) {
        cube::nxn::Step step = cube::nxn::step_of(static_cast<cube::nxn::StageId>(kept.stage));
        const std::size_t end = at + static_cast<std::size_t>(kept.count);
        if (end > entry.moves.size()) {
            return std::nullopt;
        }
        step.moves.assign(entry.moves.begin() + static_cast<std::ptrdiff_t>(at),
                          entry.moves.begin() + static_cast<std::ptrdiff_t>(end));
        solution.steps.push_back(std::move(step));
        at = end;
    }
    if (at != entry.moves.size()) {
        return std::nullopt;
    }
    return solution;
}

}  // namespace

QString SolveHistory::defaultFile() {
    // The tests' way of keeping out of a person's own history, as
    // CUBEULATOR_LOCALE is for the language: read before anything is shown.
    if (const QString dir = qEnvironmentVariable("CUBEULATOR_DATA_DIR"); !dir.isEmpty()) {
        return QDir(dir).filePath(QLatin1String(kFileName));
    }
    const auto paths = cube::platform::paths::current();
    QString dir;
    if (!paths.data_dir.empty()) {
        dir = QDir::fromNativeSeparators(QString::fromStdU16String(paths.data_dir.u16string()));
    } else {
        // A platform that has not said where its data folder is (Android and
        // iOS resolve it through a bridge that has not landed). Qt knows.
        dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QLatin1String(kFileName));
}

SolveHistory::SolveHistory(SolveController& solve, QString file, QObject* parent)
    : QObject(parent),
      solve_(solve),
      store_(file.isEmpty() ? defaultFile() : std::move(file)),
      review_(this) {
    keep_ = QSettings().value(QLatin1String(kKeepKey), true).toBool();
    connect(&solve_, &SolveController::stateChanged, this, &SolveHistory::onStateChanged);
    connect(&solve_, &SolveController::solutionChanged, this, &SolveHistory::onSolutionChanged);
}

void SolveHistory::setKeep(bool keep) {
    if (keep == keep_) {
        return;
    }
    keep_ = keep;
    QSettings().setValue(QLatin1String(kKeepKey), keep);
    emit keepChanged();
}

void SolveHistory::setElapsedSeconds(int seconds) {
    if (seconds == seconds_) {
        return;
    }
    seconds_ = seconds;
    emit elapsedSecondsChanged();
}

qlonglong SolveHistory::carryOnId() const noexcept {
    // Only the newest unfinished solve can be carried on: starting another
    // scan leaves an older one unfinished, and it stays in the list.
    for (const HistoryEntry& entry : store_.entries()) {
        if (!entry.finished) {
            return entry.id;
        }
    }
    return 0;
}

QVariantMap SolveHistory::toMap(const HistoryEntry& entry, qint64 carry_on) const {
    // The scanned cube's front side as a person sees it: nine colours for a 3x3,
    // N x N for a cube of size N.
    QVariantList stickers;
    stickers.reserve(static_cast<qsizetype>(entry.scanned.size()) * entry.scanned.size());
    for (const cube::solver::Color colour :
         cube::nxn::outside_view(entry.scanned, cube::solver::Face::F)) {
        stickers.append(colour_name(colour));
    }
    return QVariantMap{
        {QStringLiteral("size"), entry.scanned.size()},
        {QStringLiteral("id"), static_cast<qlonglong>(entry.id)},
        {QStringLiteral("started"), static_cast<qlonglong>(entry.startedMs)},
        {QStringLiteral("finished"), entry.finished},
        {QStringLiteral("turn"), entry.turn},
        {QStringLiteral("turns"), entry.turns},
        {QStringLiteral("seconds"), entry.seconds},
        {QStringLiteral("method"), entry.method},
        {QStringLiteral("carryOn"), !entry.finished && entry.id == carry_on},
        {QStringLiteral("stickers"), stickers},
    };
}

QVariantList SolveHistory::entries() const {
    const qint64 carry_on = carryOnId();
    QVariantList list;
    list.reserve(static_cast<qsizetype>(store_.entries().size()));
    for (const HistoryEntry& entry : store_.entries()) {
        list.append(toMap(entry, carry_on));
    }
    return list;
}

QVariantMap SolveHistory::entry(qlonglong id) const {
    const HistoryEntry* found = store_.find(id);
    return found == nullptr ? QVariantMap{} : toMap(*found, carryOnId());
}

bool SolveHistory::remove(qlonglong id) {
    if (!store_.remove(id)) {
        return false;
    }
    if (id == current_) {
        current_ = 0;
    }
    emit entriesChanged();
    return true;
}

int SolveHistory::clear() {
    const int had = store_.clear();
    current_ = 0;
    if (had > 0) {
        emit entriesChanged();
    }
    return had;
}

void SolveHistory::finish() {
    if (current_ == 0) {
        return;
    }
    const HistoryEntry* kept = store_.find(current_);
    if (kept == nullptr) {
        current_ = 0;
        return;
    }
    HistoryEntry done = *kept;
    done.finished = true;
    done.turn = solve_.moveIndex();
    done.turns = solve_.totalMoves();
    done.seconds = seconds_;
    store_.update(done);
    // Over: whatever the controller does next is another solve's.
    current_ = 0;
    emit entriesChanged();
}

QObject* SolveHistory::reviewOf(qlonglong id) {
    const HistoryEntry* kept = store_.find(id);
    if (kept == nullptr) {
        return nullptr;
    }
    // From the cube as scanned, turn 0: what the review shows is the whole
    // solution, not the place the person got to. resumeCube() because it
    // solves exactly this cube with exactly this method and does not carry on
    // from anything the controller held before.
    const auto method = static_cast<SolveController::Method>(kept->method);
    // A bigger cube's solution is the one that was followed, not today's: it is
    // there at once. A 3x3's is worked out again.
    if (const auto solution = kept_solution(*kept); solution.has_value()) {
        review_.resumeCube(kept->scanned, kept->from, method, 0, *solution);
    } else {
        review_.resumeCube(kept->scanned, kept->scanned, method, 0);
    }
    return &review_;
}

bool SolveHistory::resume(qlonglong id) {
    const HistoryEntry* kept = store_.find(id);
    if (kept == nullptr || kept->finished || id != carryOnId()) {
        return false;
    }
    resuming_ = id;
    const auto method = static_cast<SolveController::Method>(kept->method);
    if (const auto solution = kept_solution(*kept); solution.has_value()) {
        solve_.resumeCube(kept->scanned, kept->from, method, kept->turn, *solution);
    } else {
        solve_.resumeCube(kept->scanned, kept->from, method, kept->turn);
    }
    return true;
}

void SolveHistory::keepSolutionIn(HistoryEntry& entry) const {
    entry.moves.clear();
    entry.stages.clear();
    // A 3x3's solution is worked out again from its cube; a bigger cube's is
    // kept (see HistoryEntry).
    if (solve_.plan().size() == 3) {
        return;
    }
    const cube::nxn::Solution solution = solve_.plan().solution();
    entry.moves = solution.moves();
    entry.stages.reserve(solution.steps.size());
    for (const cube::nxn::Step& step : solution.steps) {
        entry.stages.push_back({
            .stage = static_cast<int>(cube::nxn::stage_id(step)),
            .count = static_cast<int>(step.moves.size()),
        });
    }
}

void SolveHistory::onStateChanged() {
    if (solve_.isSolving()) {
        return;
    }
    const int turn = solve_.moveIndex();
    const int turns = solve_.totalMoves();

    if (current_ != 0) {
        // The turn reached follows the person, backwards as well as on.
        const HistoryEntry* kept = store_.find(current_);
        if (kept == nullptr) {
            current_ = 0;
            return;
        }
        if (kept->turn != turn || kept->turns != turns) {
            HistoryEntry changed = *kept;
            changed.turn = turn;
            changed.turns = turns;
            changed.seconds = seconds_;
            store_.update(changed);
            emit entriesChanged();
        }
        return;
    }

    // Added when the first turn is advanced - not when the cube is scanned: a
    // scan that is never followed is not a solve. Switched off, nothing is
    // added; what is already kept is left alone.
    if (!keep_ || recorded_ || turn < 1 || turns < 1) {
        return;
    }
    const auto& scanned = solve_.scannedNCube();
    const auto& from = solve_.solutionStartNCube();
    if (!scanned.has_value() || !from.has_value()) {
        return;
    }
    HistoryEntry started;
    started.startedMs = QDateTime::currentMSecsSinceEpoch();
    started.scanned = *scanned;
    started.from = *from;
    keepSolutionIn(started);
    started.method = static_cast<int>(solve_.method());
    started.turn = turn;
    started.turns = turns;
    started.seconds = seconds_;
    current_ = store_.add(started);
    recorded_ = true;
    emit entriesChanged();
}

void SolveHistory::onSolutionChanged() {
    if (resuming_ != 0) {
        // The solve asked for by resume() has landed, on the turn it was left
        // at. The entry already says everything it should; it is the one to
        // keep up to date, and no new one is made for it.
        current_ = resuming_;
        recorded_ = true;
        resuming_ = 0;
        return;
    }
    if (!solve_.solutionContinuesCube()) {
        // Another cube: the entry before it stays as it was, unfinished.
        current_ = 0;
        recorded_ = false;
        return;
    }
    // The same cube solved on another way from where it stands: the turns an
    // entry counts are those of the new solution, which starts from the cube in
    // hand.
    const HistoryEntry* kept = store_.find(current_);
    if (current_ == 0 || kept == nullptr || !solve_.solutionStartNCube().has_value()) {
        return;
    }
    HistoryEntry changed = *kept;
    changed.from = *solve_.solutionStartNCube();
    keepSolutionIn(changed);
    changed.method = static_cast<int>(solve_.method());
    changed.turn = solve_.moveIndex();
    changed.turns = solve_.totalMoves();
    changed.seconds = seconds_;
    store_.update(changed);
    emit entriesChanged();
}

}  // namespace cube::app
