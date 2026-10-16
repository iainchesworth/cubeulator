// The history of solves (src/app/qt/controllers/solve_history*.{hpp,cpp}):
// what is kept, how many, what happens to a file that cannot be read, and how a
// solve is added to it, followed, finished and picked up again.
//
// Scrambled cubes are made by the solver's own random_scramble; no camera frame
// is anywhere near these tests, and none is ever stored by the code under test.

#include <catch2/catch_test_macros.hpp>

#include "controllers/solve_history.hpp"
#include "controllers/solve_history_store.hpp"
#include "solve_controller.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/cube3.hpp>
#include <cube/nxn/solution.hpp>
#include <cube/nxn/stage.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using cube::app::HistoryEntry;
using cube::app::KeptStage;
using cube::app::SolveHistory;
using cube::app::SolveHistoryStore;
using cube::nxn::NCube;
using cube::solver::Cube;
using cube::solver::MoveCount;
using cube::solver::random_scramble;

namespace {

// The keep switch is a QSettings value. Point the store at a directory of this
// process's own, so that a run neither reads nor leaves a developer's settings.
void isolate_settings() {
    static QTemporaryDir dir;
    static const bool once = [] {
        QCoreApplication::setOrganizationName(QStringLiteral("Cubeulator"));
        QCoreApplication::setApplicationName(QStringLiteral("Cubeulator-history-tests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        return true;
    }();
    (void)once;
    QSettings().remove(QStringLiteral("history/keep"));
}

// A cube the walk's size: ten turns from solved.
Cube scrambled(unsigned seed) {
    return random_scramble(seed, MoveCount{10});
}

HistoryEntry make_entry(unsigned seed, int turn = 3, int turns = 20, bool finished = false) {
    HistoryEntry entry;
    entry.startedMs = 1'700'000'000'000 + static_cast<qint64>(seed);
    entry.scanned = NCube::from_cube3(scrambled(seed));
    entry.from = entry.scanned;
    entry.method = static_cast<int>(seed % 3);
    entry.turn = turn;
    entry.turns = turns;
    entry.seconds = 90 + static_cast<int>(seed);
    entry.finished = finished;
    return entry;
}

QByteArray read_all(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

void write_all(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

bool wait_until_solved(SolveController& controller) {
    QElapsedTimer waited;
    waited.start();
    while (controller.isSolving() && waited.elapsed() < 60'000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return !controller.isSolving();
}

// Scans nothing, solves `cube`, and waits for the solution to land.
void solve_and_wait(SolveController& controller, const Cube& cube) {
    controller.solveCube(cube);
    REQUIRE(wait_until_solved(controller));
}

}  // namespace

// ---- the store ---------------------------------------------------------------

TEST_CASE("a kept solve comes back as it was written", "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));

    HistoryEntry first = make_entry(1, 5, 30, false);
    HistoryEntry second = make_entry(2, 12, 12, true);
    // A solve whose method was changed part-way carries the cube it was
    // changed from.
    HistoryEntry third = make_entry(3, 0, 18, false);
    third.from = NCube::from_cube3(scrambled(30));

    qint64 first_id = 0;
    qint64 second_id = 0;
    qint64 third_id = 0;
    {
        SolveHistoryStore store(file);
        REQUIRE(store.entries().empty());
        first_id = store.add(first);
        second_id = store.add(second);
        third_id = store.add(third);
        REQUIRE_FALSE(store.lastWriteFailed());
    }

    const SolveHistoryStore reloaded(file);
    REQUIRE(reloaded.entries().size() == 3);
    CHECK_FALSE(reloaded.setAsideUnreadableFile());

    // Newest first.
    CHECK(reloaded.entries()[0].id == third_id);
    CHECK(reloaded.entries()[1].id == second_id);
    CHECK(reloaded.entries()[2].id == first_id);

    const HistoryEntry* got = reloaded.find(second_id);
    REQUIRE(got != nullptr);
    CHECK(got->startedMs == second.startedMs);
    CHECK(got->scanned == second.scanned);
    CHECK(got->from == second.from);
    CHECK(got->method == second.method);
    CHECK(got->turn == 12);
    CHECK(got->turns == 12);
    CHECK(got->seconds == second.seconds);
    CHECK(got->finished);

    const HistoryEntry* changed = reloaded.find(third_id);
    REQUIRE(changed != nullptr);
    CHECK(changed->scanned == third.scanned);
    CHECK(changed->from == third.from);
    CHECK_FALSE(changed->from == changed->scanned);
}

TEST_CASE("only the newest hundred are kept", "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    static_assert(SolveHistoryStore::kCapacity == 100);

    qint64 newest = 0;
    qint64 oldest_kept = 0;
    {
        SolveHistoryStore store(file);
        for (unsigned seed = 1; seed <= 105; ++seed) {
            newest = store.add(make_entry(seed));
            if (seed == 6) {
                oldest_kept = newest;  // the 6th is the oldest of the last hundred
            }
        }
        REQUIRE(store.entries().size() == 100);
        CHECK(store.entries().front().id == newest);
        CHECK(store.entries().back().id == oldest_kept);
        CHECK(store.find(oldest_kept - 1) == nullptr);
    }

    // The cap holds across a restart, too.
    const SolveHistoryStore reloaded(file);
    REQUIRE(reloaded.entries().size() == 100);
    CHECK(reloaded.entries().front().id == newest);
    CHECK(reloaded.entries().back().id == oldest_kept);
}

TEST_CASE("deleting one and clearing all", "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    SolveHistoryStore store(file);
    const qint64 a = store.add(make_entry(1));
    const qint64 b = store.add(make_entry(2));
    const qint64 c = store.add(make_entry(3));

    CHECK(store.remove(b));
    CHECK_FALSE(store.remove(b));
    REQUIRE(store.entries().size() == 2);
    CHECK(SolveHistoryStore(file).entries().size() == 2);

    CHECK(store.clear() == 2);
    CHECK(store.entries().empty());
    CHECK(SolveHistoryStore(file).entries().empty());
    CHECK(store.find(a) == nullptr);
    CHECK(store.find(c) == nullptr);
    // Cleared is cleared: nothing comes back from the file.
    CHECK(store.clear() == 0);
}

TEST_CASE("a file that cannot be read is ignored, not fatal", "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));

    SECTION("not JSON at all") {
        write_all(file, "this is not a history\x01\x02");
        SolveHistoryStore store(file);
        CHECK(store.entries().empty());
        CHECK(store.setAsideUnreadableFile());
        // Set aside, not deleted: it is the person's, and may be a later
        // version's. The history works from empty.
        CHECK(QFile::exists(file + QStringLiteral(".unreadable")));
        CHECK_FALSE(QFile::exists(file));
        store.add(make_entry(1));
        CHECK(SolveHistoryStore(file).entries().size() == 1);
    }
    SECTION("JSON that is not a history") {
        write_all(file, "[1, 2, 3]");
        const SolveHistoryStore store(file);
        CHECK(store.entries().empty());
        CHECK(store.setAsideUnreadableFile());
    }
    SECTION("an empty file") {
        write_all(file, "");
        const SolveHistoryStore store(file);
        CHECK(store.entries().empty());
        CHECK(store.setAsideUnreadableFile());
    }
    SECTION("a later version's file is set aside, and not written over") {
        const QByteArray later = R"({"version":3,"entries":[{"anything":"at all"}]})";
        write_all(file, later);
        SolveHistoryStore store(file);
        CHECK(store.entries().empty());
        CHECK(store.setAsideUnreadableFile());
        store.add(make_entry(1));
        CHECK(read_all(file + QStringLiteral(".unreadable")) == later);
    }
    SECTION("one damaged entry is dropped and the rest are kept") {
        {
            SolveHistoryStore store(file);
            store.add(make_entry(1));
            store.add(make_entry(2));
            store.add(make_entry(3));
        }
        QJsonDocument document = QJsonDocument::fromJson(read_all(file));
        QJsonObject root = document.object();
        QJsonArray entries = root.value("entries").toArray();
        REQUIRE(entries.size() == 3);

        // A cube with a letter that is not a colour; a turn past the end;
        // something that is not an object; a cube of one colour. Each is
        // dropped by itself, and the three good entries are kept.
        QJsonObject bad_letter = entries.at(0).toObject();
        bad_letter["cube"] = QString(54, QLatin1Char('X'));
        QJsonObject bad_turn = entries.at(1).toObject();
        bad_turn["turn"] = 9999;
        entries.append(bad_letter);
        entries.append(bad_turn);
        entries.append(QJsonValue(42));
        QJsonObject all_white;
        all_white["id"] = 99;
        all_white["started"] = 1;
        all_white["cube"] = QString(54, QLatin1Char('W'));
        all_white["method"] = 0;
        all_white["turn"] = 0;
        all_white["turns"] = 0;
        all_white["seconds"] = 0;
        all_white["finished"] = false;
        entries.append(all_white);
        root["entries"] = entries;
        write_all(file, QJsonDocument(root).toJson());

        const SolveHistoryStore store(file);
        CHECK_FALSE(store.setAsideUnreadableFile());
        CHECK(store.entries().size() == 3);
    }
}

TEST_CASE("what is written holds the colours and numbers, never a picture", "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    {
        SolveHistoryStore store(file);
        HistoryEntry entry = make_entry(7);
        entry.from = NCube::from_cube3(scrambled(8));  // so that the optional key is there too
        store.add(entry);
    }

    const QByteArray bytes = read_all(file);
    // A picture would be thousands of bytes; this is one small record.
    CHECK(bytes.size() < 1024);

    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    REQUIRE(root.value("entries").toArray().size() == 1);
    const QJsonObject kept = root.value("entries").toArray().first().toObject();
    std::set<QString> keys;
    for (const QString& key : kept.keys()) {
        keys.insert(key);
    }
    const std::set<QString> allowed = {"id",   "started", "cube",    "from",    "method",
                                       "turn", "turns",   "seconds", "finished"};
    CHECK(keys == allowed);
    CHECK(kept.value("cube").toString().size() == 54);
    CHECK(kept.value("from").toString().size() == 54);
}

// ---- the controller ----------------------------------------------------------

TEST_CASE("a solve is added when its first turn is advanced, and followed", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    SolveController solve;
    SolveHistory history(solve, file);
    const Cube cube = scrambled(11);

    solve_and_wait(solve, cube);
    // Scanned, solved, not yet followed: not a solve yet.
    CHECK(history.count() == 0);
    REQUIRE(solve.totalMoves() > 2);

    history.setElapsedSeconds(5);
    solve.nextMove();
    REQUIRE(history.count() == 1);
    const QVariantMap first = history.entries().first().toMap();
    CHECK_FALSE(first.value("finished").toBool());
    CHECK(first.value("turn").toInt() == 1);
    CHECK(first.value("turns").toInt() == solve.totalMoves());
    CHECK(first.value("seconds").toInt() == 5);
    CHECK(first.value("method").toInt() == static_cast<int>(solve.method()));
    CHECK(first.value("started").toLongLong() > 1'600'000'000'000);
    CHECK(first.value("stickers").toList().size() == 9);

    // The turn reached follows, on and back.
    history.setElapsedSeconds(9);
    solve.nextMove();
    solve.nextMove();
    CHECK(history.entries().first().toMap().value("turn").toInt() == 3);
    solve.previousMove();
    CHECK(history.entries().first().toMap().value("turn").toInt() == 2);
    CHECK(history.count() == 1);

    // On disk already: a restart sees it.
    const SolveHistoryStore on_disk(file);
    REQUIRE(on_disk.entries().size() == 1);
    CHECK(on_disk.entries().front().turn == 2);
    CHECK(on_disk.entries().front().scanned == NCube::from_cube3(cube));
    CHECK_FALSE(on_disk.entries().front().finished);
}

TEST_CASE("a solve is finished when the complete screen opens", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    SolveController solve;
    SolveHistory history(solve, dir.filePath(QStringLiteral("h.json")));

    solve_and_wait(solve, scrambled(12));
    history.setElapsedSeconds(30);
    while (!solve.atEnd()) {
        history.setElapsedSeconds(history.elapsedSeconds() + 1);
        solve.nextMove();
    }
    // At the last turn but the screen has not opened: still unfinished.
    CHECK_FALSE(history.entries().first().toMap().value("finished").toBool());

    history.setElapsedSeconds(760);
    history.finish();
    REQUIRE(history.count() == 1);
    const QVariantMap done = history.entries().first().toMap();
    CHECK(done.value("finished").toBool());
    CHECK(done.value("turn").toInt() == done.value("turns").toInt());
    CHECK(done.value("seconds").toInt() == 760);
    CHECK_FALSE(done.value("carryOn").toBool());

    // And once finished it is not touched again, nor added a second time.
    history.finish();
    solve.previousMove();
    solve.nextMove();
    CHECK(history.count() == 1);
    CHECK(history.entries().first().toMap().value("turn").toInt() == done.value("turns").toInt());
}

TEST_CASE("scanning another cube leaves the first unfinished, and only the newest carries on",
          "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    SolveController solve;
    SolveHistory history(solve, dir.filePath(QStringLiteral("h.json")));

    solve_and_wait(solve, scrambled(13));
    solve.nextMove();
    solve.nextMove();
    REQUIRE(history.count() == 1);
    const qlonglong older = history.entries().first().toMap().value("id").toLongLong();

    // Another scan, another solve.
    solve_and_wait(solve, scrambled(14));
    CHECK(history.count() == 1);  // not added until its first turn
    solve.nextMove();
    REQUIRE(history.count() == 2);
    const QVariantMap newer = history.entries().first().toMap();
    const qlonglong newer_id = newer.value("id").toLongLong();
    CHECK(newer_id != older);

    // Both unfinished; the older is still in the list but cannot be carried on.
    CHECK_FALSE(history.entry(older).value("finished").toBool());
    CHECK(history.entry(older).value("turn").toInt() == 2);
    CHECK_FALSE(history.entry(older).value("carryOn").toBool());
    CHECK(newer.value("carryOn").toBool());
    CHECK(history.carryOnId() == newer_id);
    CHECK_FALSE(history.resume(older));
}

TEST_CASE("changing the method part-way carries on the same entry", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    SolveController solve;
    SolveHistory history(solve, dir.filePath(QStringLiteral("h.json")));
    solve.setMethod(SolveController::Beginner);

    const Cube cube = scrambled(15);
    solve_and_wait(solve, cube);
    for (int i = 0; i < 4; ++i) {
        solve.nextMove();
    }
    REQUIRE(history.count() == 1);
    const Cube in_hand = solve.currentCube();
    CHECK_FALSE(in_hand == cube);

    solve.setMethod(SolveController::Advanced);
    REQUIRE(wait_until_solved(solve));

    // Still one entry; it counts the new solution's turns, from the cube in
    // hand, and says which method that is.
    REQUIRE(history.count() == 1);
    const QVariantMap kept = history.entries().first().toMap();
    CHECK(kept.value("method").toInt() == static_cast<int>(SolveController::Advanced));
    CHECK(kept.value("turn").toInt() == 0);
    CHECK(kept.value("turns").toInt() == solve.totalMoves());

    const SolveHistoryStore store(history.store().file());
    REQUIRE(store.entries().size() == 1);
    CHECK(store.entries().front().scanned == NCube::from_cube3(cube));
    CHECK(store.entries().front().from == NCube::from_cube3(in_hand));
}

TEST_CASE("an unfinished solve is picked up where it was left", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("h.json"));

    Cube at_turn_four = Cube::solved();
    int total = 0;
    {
        SolveController solve;
        SolveHistory history(solve, file);
        solve.setMethod(SolveController::Intermediate);
        solve_and_wait(solve, scrambled(16));
        for (int i = 0; i < 4; ++i) {
            solve.nextMove();
        }
        at_turn_four = solve.currentCube();
        total = solve.totalMoves();
        history.setElapsedSeconds(125);
        solve.nextMove();
        solve.previousMove();  // sync the clock into the entry
    }

    // A new launch: nothing in the solve controller, the entry on disk.
    SolveController solve;
    solve.setMethod(SolveController::Beginner);
    SolveHistory history(solve, file);
    REQUIRE(history.count() == 1);
    const qlonglong id = history.carryOnId();
    REQUIRE(id != 0);
    CHECK(history.entry(id).value("seconds").toInt() == 125);

    CHECK_FALSE(history.resume(id + 1000));
    REQUIRE(history.resume(id));
    REQUIRE(wait_until_solved(solve));

    CHECK(solve.method() == SolveController::Intermediate);
    CHECK(solve.totalMoves() == total);
    CHECK(solve.moveIndex() == 4);
    CHECK(solve.currentCube() == at_turn_four);
    CHECK(solve.scannedCube().has_value());
    CHECK(*solve.scannedCube() == scrambled(16));
    // It is the same entry, not a second one, and it goes on being followed.
    CHECK(history.count() == 1);
    solve.nextMove();
    CHECK(history.count() == 1);
    CHECK(history.entry(id).value("turn").toInt() == 5);
}

TEST_CASE("with history off nothing is added, and nothing is deleted", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("h.json"));
    SolveController solve;
    SolveHistory history(solve, file);
    CHECK(history.keep());

    // One kept while it was on.
    solve_and_wait(solve, scrambled(17));
    solve.nextMove();
    REQUIRE(history.count() == 1);

    history.setKeep(false);
    CHECK_FALSE(history.keep());
    // It stays off: the next launch reads it.
    CHECK_FALSE(QSettings().value(QStringLiteral("history/keep"), true).toBool());

    solve_and_wait(solve, scrambled(18));
    solve.nextMove();
    solve.nextMove();
    // Not added; and what was kept is still there.
    CHECK(history.count() == 1);
    CHECK(SolveHistoryStore(file).entries().size() == 1);

    // On again: the next solve is kept.
    history.setKeep(true);
    solve_and_wait(solve, scrambled(19));
    solve.nextMove();
    CHECK(history.count() == 2);
}

TEST_CASE("the keep switch is read back by the next launch", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    SolveController solve;
    {
        SolveHistory history(solve, dir.filePath(QStringLiteral("a.json")));
        CHECK(history.keep());  // on until turned off
        history.setKeep(false);
    }
    SolveHistory later(solve, dir.filePath(QStringLiteral("a.json")));
    CHECK_FALSE(later.keep());
}

TEST_CASE("clearing mid-solve is not undone by the next turn", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    SolveController solve;
    SolveHistory history(solve, dir.filePath(QStringLiteral("h.json")));

    solve_and_wait(solve, scrambled(20));
    solve.nextMove();
    REQUIRE(history.count() == 1);

    CHECK(history.clear() == 1);
    CHECK(history.count() == 0);
    CHECK(history.carryOnId() == 0);  // the unfinished solve goes too
    solve.nextMove();
    CHECK(history.count() == 0);

    // Deleting one entry is the same.
    solve_and_wait(solve, scrambled(21));
    solve.nextMove();
    REQUIRE(history.count() == 1);
    CHECK(history.remove(history.carryOnId()));
    solve.nextMove();
    CHECK(history.count() == 0);
}

TEST_CASE(
    "an entry is reviewed as a solution of its own, and the solve being followed is not moved",
    "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    SolveController solve;
    SolveHistory history(solve, dir.filePath(QStringLiteral("h.json")));

    // A finished solve, and another being followed.
    const Cube reviewed_cube = scrambled(22);
    solve.setMethod(SolveController::Intermediate);
    solve_and_wait(solve, reviewed_cube);
    const int reviewed_turns = solve.totalMoves();
    while (!solve.atEnd()) {
        solve.nextMove();
    }
    history.finish();
    solve.setMethod(SolveController::Beginner);
    solve_and_wait(solve, scrambled(23));
    solve.nextMove();
    solve.nextMove();
    REQUIRE(history.count() == 2);
    const qlonglong finished = history.entries().at(1).toMap().value("id").toLongLong();
    REQUIRE(history.entry(finished).value("finished").toBool());

    QObject* source = history.reviewOf(finished);
    REQUIRE(source != nullptr);
    auto* review = qobject_cast<SolveController*>(source);
    REQUIRE(review != nullptr);
    REQUIRE(review != &solve);
    REQUIRE(wait_until_solved(*review));

    // The shape the solution screen asks of its source, for the cube as it
    // was scanned and the method it was followed with.
    CHECK(review->method() == SolveController::Intermediate);
    CHECK(review->totalMoves() == reviewed_turns);
    CHECK(review->moves().size() == reviewed_turns);
    CHECK_FALSE(review->stageRanges().isEmpty());
    REQUIRE(review->scannedCube().has_value());
    CHECK(*review->scannedCube() == reviewed_cube);
    // Turn 0 is the cube as scanned and the last turn is a solved cube.
    CHECK(review->stickersAt(reviewed_turns) != review->stickersAt(0));
    // At the start of the review, not where the person left it.
    CHECK(review->moveIndex() == 0);

    // The solve being followed is as it was: its cube, method, turn - and no
    // entry was added or changed by opening a review.
    CHECK(solve.method() == SolveController::Beginner);
    CHECK(solve.moveIndex() == 2);
    CHECK(history.count() == 2);
    CHECK(history.entry(history.carryOnId()).value("turn").toInt() == 2);

    // Nothing for an id that is not there.
    CHECK(history.reviewOf(987654) == nullptr);
}

// ---- cubes of any size -------------------------------------------------------

namespace {

// A cube of `size` scrambled by layer turns, and the solution to it as turns of
// single layers: what an entry for a cube bigger than a 3x3 keeps.
struct Big {
    NCube cube;
    cube::nxn::Solution solution;
    std::vector<cube::nxn::LayerMove> moves;
    std::vector<KeptStage> stages;
};

// The stages a solution has, in the form an entry keeps them.
std::vector<KeptStage> stages_of(const cube::nxn::Solution& solution) {
    std::vector<KeptStage> stages;
    for (const cube::nxn::Step& step : solution.steps) {
        stages.push_back({
            .stage = static_cast<int>(cube::nxn::stage_id(step)),
            .count = static_cast<int>(step.moves.size()),
        });
    }
    return stages;
}

Big big_cube(int size, unsigned seed,
             cube::solver::SolveMethod method = cube::solver::SolveMethod::Advanced) {
    NCube cube = cube::nxn::random_scramble({.size = size, .seed = seed, .moves = 40 + (8 * size)});
    const auto solution = cube::nxn::solve_held(cube, method);
    REQUIRE(solution.has_value());
    return Big{
        .cube = std::move(cube),
        .solution = *solution,
        .moves = solution->moves(),
        .stages = stages_of(*solution),
    };
}

HistoryEntry make_big_entry(int size, unsigned seed) {
    Big big = big_cube(size, seed);
    HistoryEntry entry;
    entry.startedMs = 1'700'000'000'000 + static_cast<qint64>(seed);
    entry.scanned = big.cube;
    entry.from = big.cube;
    entry.moves = big.moves;
    entry.stages = big.stages;
    entry.method = 2;
    entry.turn = 5;
    entry.turns = static_cast<int>(big.moves.size());
    entry.seconds = 120;
    return entry;
}

// The first release's file, byte for byte what it wrote for one solved 3x3 that
// has been followed one turn (the cube is the 54 letters of Cube::solved()).
constexpr char kFirstReleasesFile[] =
    R"({"entries":[{"cube":"YYYYYYYYYWWWWWWWWWRRRRRRRRROOOOOOOOOGGGGGGGGGBBBBBBBBB",)"
    R"("finished":false,"id":1,"method":0,"seconds":7,"started":1700000000000,"turn":1,"turns":3}],)"
    R"("version":1})";

}  // namespace

TEST_CASE("the first release's file is read as 3x3s, and stays that file while it can",
          "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    write_all(file, kFirstReleasesFile);

    SolveHistoryStore store(file);
    REQUIRE(store.entries().size() == 1);
    CHECK_FALSE(store.setAsideUnreadableFile());
    const HistoryEntry& kept = store.entries().front();
    CHECK(kept.scanned.size() == 3);
    CHECK(kept.scanned == *NCube::solved(3));
    CHECK(kept.from == kept.scanned);
    CHECK(kept.moves.empty());
    CHECK(kept.turn == 1);
    CHECK(kept.turns == 3);

    // Another 3x3 is added: the file is still the first release's, so that the
    // app that wrote it can go on reading it.
    store.add(make_entry(4));
    const QJsonObject root = QJsonDocument::fromJson(read_all(file)).object();
    CHECK(root.value("version").toInt() == 1);
    for (const QJsonValue& entry : root.value("entries").toArray()) {
        CHECK_FALSE(entry.toObject().contains("size"));
        CHECK_FALSE(entry.toObject().contains("moves"));
    }
}

TEST_CASE("a cube of another size is kept with its size and its solution", "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));

    HistoryEntry five = make_big_entry(5, 1);
    HistoryEntry two = make_big_entry(2, 2);
    qint64 five_id = 0;
    qint64 two_id = 0;
    {
        SolveHistoryStore store(file);
        store.add(make_entry(3));  // a 3x3 beside them
        five_id = store.add(five);
        two_id = store.add(two);
        REQUIRE_FALSE(store.lastWriteFailed());
    }

    const SolveHistoryStore reloaded(file);
    REQUIRE(reloaded.entries().size() == 3);
    CHECK_FALSE(reloaded.setAsideUnreadableFile());

    const HistoryEntry* got = reloaded.find(five_id);
    REQUIRE(got != nullptr);
    CHECK(got->scanned.size() == 5);
    CHECK(got->scanned == five.scanned);
    CHECK(got->from == five.from);
    CHECK(got->moves == five.moves);
    CHECK(got->stages == five.stages);
    CHECK(got->turn == five.turn);
    CHECK(got->turns == five.turns);

    const HistoryEntry* got_two = reloaded.find(two_id);
    REQUIRE(got_two != nullptr);
    CHECK(got_two->scanned.size() == 2);
    CHECK(got_two->moves == two.moves);

    // What is on disk: the second version, with a size and the solution on the
    // entries that need them and nothing extra on the 3x3.
    const QJsonObject root = QJsonDocument::fromJson(read_all(file)).object();
    CHECK(root.value("version").toInt() == 2);
    int with_size = 0;
    for (const QJsonValue& value : root.value("entries").toArray()) {
        const QJsonObject entry = value.toObject();
        if (entry.contains("size")) {
            ++with_size;
            CHECK(entry.value("cube").toString().size() ==
                  6 * entry.value("size").toInt() * entry.value("size").toInt());
            CHECK(entry.value("moves").isString());
        } else {
            CHECK(entry.value("cube").toString().size() == 54);
            CHECK_FALSE(entry.contains("moves"));
        }
    }
    CHECK(with_size == 2);
}

TEST_CASE("the longest solution the library makes is kept", "[qt][history][slow]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    HistoryEntry nine = make_big_entry(9, 1);
    REQUIRE(nine.moves.size() > 400);  // past the first release's cap
    REQUIRE(nine.moves.size() < 2000);
    {
        SolveHistoryStore store(file);
        store.add(nine);
    }
    const SolveHistoryStore reloaded(file);
    REQUIRE(reloaded.entries().size() == 1);
    CHECK(reloaded.entries().front().moves == nine.moves);
    CHECK(reloaded.entries().front().turns == nine.turns);
}

TEST_CASE("a damaged entry of another size is dropped, a damaged solution is not kept",
          "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    const HistoryEntry original = make_big_entry(4, 1);
    {
        SolveHistoryStore store(file);
        store.add(original);
    }
    QJsonObject root = QJsonDocument::fromJson(read_all(file)).object();
    QJsonArray entries = root.value("entries").toArray();
    const QJsonObject good = entries.at(0).toObject();

    // Dropped, each by itself: a size no cube has; a size that does not fit the
    // letters; a cube that is not one turns could make (two stickers swapped);
    // a turn count past what any solve takes.
    QJsonObject no_such_size = good;
    no_such_size["id"] = 11;
    no_such_size["size"] = 1;
    QJsonObject too_big = good;
    too_big["id"] = 12;
    too_big["size"] = 10;
    QJsonObject wrong_size = good;
    wrong_size["id"] = 13;
    wrong_size["size"] = 5;
    QJsonObject not_a_cube = good;
    not_a_cube["id"] = 14;
    QString letters = good.value("cube").toString();
    // A sticker repainted: a colour is one short and another one over.
    letters[0] = letters.at(0) == QLatin1Char('W') ? QLatin1Char('Y') : QLatin1Char('W');
    not_a_cube["cube"] = letters;
    not_a_cube["from"] = letters;
    QJsonObject too_long = good;
    too_long["id"] = 15;
    too_long["turns"] = 2001;
    too_long["turn"] = 3;
    for (const QJsonObject& bad : {no_such_size, too_big, wrong_size, not_a_cube, too_long}) {
        entries.append(bad);
    }

    // Kept, without the solution: one that does not solve the cube it is for.
    QJsonObject wrong_moves = good;
    wrong_moves["id"] = 16;
    wrong_moves["moves"] = QStringLiteral("R U R'");
    entries.append(wrong_moves);
    QJsonObject garbled_moves = good;
    garbled_moves["id"] = 17;
    garbled_moves["moves"] = QStringLiteral("R U Q");
    entries.append(garbled_moves);

    root["entries"] = entries;
    write_all(file, QJsonDocument(root).toJson());

    const SolveHistoryStore store(file);
    CHECK_FALSE(store.setAsideUnreadableFile());
    REQUIRE(store.entries().size() == 3);
    CHECK(store.find(1) != nullptr);
    REQUIRE(store.find(16) != nullptr);
    CHECK(store.find(16)->moves.empty());
    CHECK(store.find(16)->scanned.size() == 4);
    REQUIRE(store.find(17) != nullptr);
    CHECK(store.find(17)->moves.empty());
    CHECK(store.find(1)->moves == original.moves);
}

TEST_CASE("the controller keeps the solution of a cube bigger than a 3x3, and not a 3x3's",
          "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    SolveController solve;
    SolveHistory history(solve, file);

    // A 5x5: the entry has its size, its front side as 25 colours, and the
    // solution in force.
    const Big big = big_cube(5, 3);
    solve.solveCube(big.cube);
    REQUIRE(wait_until_solved(solve));
    REQUIRE(solve.totalMoves() > 2);
    solve.nextMove();
    REQUIRE(history.count() == 1);
    const QVariantMap map = history.entries().first().toMap();
    CHECK(map.value("size").toInt() == 5);
    CHECK(map.value("stickers").toList().size() == 25);
    const HistoryEntry kept = SolveHistoryStore(file).entries().front();
    CHECK(kept.scanned == big.cube);
    CHECK(kept.from == big.cube);
    CHECK(kept.moves == solve.plan().layer_turns());
    CHECK_FALSE(kept.moves.empty());

    // A 3x3 after it: no solution kept, nine colours, size 3.
    solve_and_wait(solve, scrambled(40));
    solve.nextMove();
    REQUIRE(history.count() == 2);
    const QVariantMap three = history.entries().first().toMap();
    CHECK(three.value("size").toInt() == 3);
    CHECK(three.value("stickers").toList().size() == 9);
    CHECK(SolveHistoryStore(file).entries().front().moves.empty());
}

TEST_CASE("the stages of a kept solution are kept with it, and damaged ones spoil it",
          "[qt][history]") {
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));
    const HistoryEntry original = make_big_entry(4, 1);
    REQUIRE(original.stages.size() >= 2);
    {
        SolveHistoryStore store(file);
        store.add(original);
    }
    QJsonObject root = QJsonDocument::fromJson(read_all(file)).object();
    QJsonArray entries = root.value("entries").toArray();
    const QJsonObject good = entries.at(0).toObject();
    // Stage and count, in turn, in one list.
    REQUIRE(good.value("stages").isArray());
    CHECK(good.value("stages").toArray().size() ==
          static_cast<qsizetype>(2 * original.stages.size()));
    CHECK(SolveHistoryStore(file).entries().front().stages == original.stages);

    const auto with_stages = [&good](int id, const QJsonValue& stages) {
        QJsonObject copy = good;
        copy["id"] = id;
        copy["stages"] = stages;
        return copy;
    };
    const auto pairs = [](std::initializer_list<int> numbers) {
        QJsonArray out;
        for (const int number : numbers) {
            out.append(number);
        }
        return out;
    };
    const int turns = static_cast<int>(original.moves.size());
    // Kept without a word of stages: the turns are kept as they are.
    QJsonObject none = good;
    none["id"] = 20;
    none.remove("stages");
    entries.append(none);
    // Each of these cannot be what was kept: an odd list; counts that do not add
    // up to the turns; a stage there is none of; a count below nothing; not a list.
    entries.append(with_stages(21, pairs({9, turns, 10})));
    entries.append(with_stages(22, pairs({9, turns - 1})));
    entries.append(with_stages(23, pairs({9, turns + 1})));
    entries.append(with_stages(24, pairs({12, turns})));
    entries.append(with_stages(25, pairs({9, -1, 10, turns + 1})));
    entries.append(with_stages(26, QStringLiteral("9,5")));
    root["entries"] = entries;
    write_all(file, QJsonDocument(root).toJson());

    const SolveHistoryStore store(file);
    CHECK_FALSE(store.setAsideUnreadableFile());
    REQUIRE(store.find(1) != nullptr);
    CHECK(store.find(1)->stages == original.stages);
    REQUIRE(store.find(20) != nullptr);
    CHECK(store.find(20)->moves == original.moves);
    CHECK(store.find(20)->stages.empty());
    for (const int id : {21, 22, 23, 24, 25, 26}) {
        INFO("entry " << id);
        // The entry stays; the solution that cannot be trusted does not.
        REQUIRE(store.find(id) != nullptr);
        CHECK(store.find(id)->moves.empty());
        CHECK(store.find(id)->stages.empty());
        CHECK(store.find(id)->scanned == original.scanned);
    }
}

TEST_CASE("a bigger cube's solve is carried on with the solution it had, not the one found today",
          "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    const QString file = dir.filePath(QStringLiteral("solve-history.json"));

    // A solve of a 5x5 followed a few turns, with the layer-by-layer method.
    const Big big = big_cube(5, 3, cube::solver::SolveMethod::Beginner);
    {
        SolveController solve;
        SolveHistory history(solve, file);
        solve.setMethod(SolveController::Beginner);
        solve.solveCube(big.cube);
        REQUIRE(wait_until_solved(solve));
        for (int i = 0; i < 4; ++i) {
            solve.nextMove();
        }
        REQUIRE(history.count() == 1);
        const std::vector<KeptStage> stages = SolveHistoryStore(file).entries().front().stages;
        // What was kept says where each stage is, and adds up to every turn.
        REQUIRE_FALSE(stages.empty());
        std::size_t counted = 0;
        for (const KeptStage& stage : stages) {
            counted += static_cast<std::size_t>(stage.count);
        }
        CHECK(counted == solve.plan().layer_turns().size());
    }

    // The solver improves: another solution to the same cube is what it would
    // work out now. Kept in place of the first, it is the one to be followed.
    const Big other = big_cube(5, 3, cube::solver::SolveMethod::Advanced);
    REQUIRE(other.moves != big.moves);
    {
        SolveHistoryStore store(file);
        HistoryEntry entry = store.entries().front();
        entry.moves = other.moves;
        entry.stages = other.stages;
        REQUIRE(store.update(entry));
    }

    SolveController solve;
    SolveHistory history(solve, file);
    const qlonglong id = history.carryOnId();
    REQUIRE(id != 0);
    REQUIRE(history.resume(id));
    // In place at once: no solve to wait for.
    CHECK_FALSE(solve.isSolving());
    CHECK(solve.size() == 5);
    // The solution the controller follows is the kept one, as a bigger cube is
    // followed: in wide moves.
    CHECK(solve.plan().layer_turns() ==
          cube::nxn::SteppedPlan::from(big.cube, other.solution, cube::nxn::Display::Wide)
              .layer_turns());
    CHECK(solve.plan().solution().moves() != big.solution.moves());
    CHECK(solve.moveIndex() == 4);
    NCube expected = big.cube;
    for (int i = 0; i < 4; ++i) {
        expected.apply(solve.plan().layers_at(i));
    }
    CHECK(solve.plan().current_cube() == expected);

    // The stages are the ones kept, in order, and the moves add up.
    const QVariantList ranges = solve.stageRanges();
    REQUIRE(ranges.size() == static_cast<qsizetype>(other.stages.size()));
    int end = 0;
    for (qsizetype i = 0; i < ranges.size(); ++i) {
        const QVariantMap range = ranges.at(i).toMap();
        CHECK(range.value("stageIndex").toInt() ==
              other.stages.at(static_cast<std::size_t>(i)).stage);
        CHECK(range.value("startIndex").toInt() == end);
        end = range.value("endIndex").toInt();
    }
    CHECK(end == solve.totalMoves());

    // And it is the same entry that goes on being kept up to date.
    solve.nextMove();
    CHECK(history.count() == 1);
    CHECK(history.entry(id).value("turn").toInt() == 5);
}

TEST_CASE("a bigger cube's entry is reviewed on the solution it kept, at once", "[qt][history]") {
    isolate_settings();
    QTemporaryDir dir;
    SolveController solve;
    SolveHistory history(solve, dir.filePath(QStringLiteral("h.json")));

    const Big big = big_cube(4, 2);
    solve.solveCube(big.cube);
    REQUIRE(wait_until_solved(solve));
    while (!solve.atEnd()) {
        solve.nextMove();
    }
    history.finish();
    const int turns = solve.totalMoves();
    const qlonglong id = history.entries().first().toMap().value("id").toLongLong();

    auto* review = qobject_cast<SolveController*>(history.reviewOf(id));
    REQUIRE(review != nullptr);
    REQUIRE(review != &solve);
    // There when it is returned: nothing is worked out for it.
    CHECK_FALSE(review->isSolving());
    CHECK(review->size() == 4);
    CHECK(review->totalMoves() == turns);
    CHECK(review->plan().layer_turns() == solve.plan().layer_turns());
    CHECK(review->moveIndex() == 0);
    CHECK(review->plan().current_cube() == big.cube);
    CHECK(review->stageRanges() == solve.stageRanges());

    // A 3x3's is still worked out again.
    solve_and_wait(solve, scrambled(30));
    solve.nextMove();
    const qlonglong three = history.entries().first().toMap().value("id").toLongLong();
    auto* reviewed_three = qobject_cast<SolveController*>(history.reviewOf(three));
    REQUIRE(reviewed_three != nullptr);
    CHECK(reviewed_three->isSolving());
    REQUIRE(wait_until_solved(*reviewed_three));
    CHECK(reviewed_three->size() == 3);
}
