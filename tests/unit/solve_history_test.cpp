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

#include <memory>
#include <set>

using cube::app::HistoryEntry;
using cube::app::SolveHistory;
using cube::app::SolveHistoryStore;
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
    entry.scanned = scrambled(seed);
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
    third.from = scrambled(30);

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
        const QByteArray later = R"({"version":2,"entries":[{"anything":"at all"}]})";
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
        entry.from = scrambled(8);  // so that the optional key is there too
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
    CHECK(on_disk.entries().front().scanned == cube);
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
    CHECK(store.entries().front().scanned == cube);
    CHECK(store.entries().front().from == in_hand);
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
