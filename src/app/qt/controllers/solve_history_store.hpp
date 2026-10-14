#pragma once

#include <cube/solver/cube.hpp>

#include <QString>
#include <QtGlobal>

#include <vector>

namespace cube::app {

// One solve, as it is kept on the device (docs/design/screens/history.md,
// "What is kept"): when it started, the cube as scanned, the turn reached, the
// number of turns and the time taken.
//
// What is NOT here is any picture. A camera frame never reaches this type: the
// only thing that does is the 54 colours the scan settled on. That is what lets
// the privacy statement say that no picture is saved.
//
// The solution is not stored either; it is worked out again from the cube,
// which gives the same answer for the same method - so the method is.
struct HistoryEntry {
    // Never reused while the entry exists; larger is newer.
    qint64 id = 0;
    // Milliseconds since the epoch (UTC) at which the first turn was advanced.
    qint64 startedMs = 0;
    // The cube as the scan read it. What the row's thumbnail is drawn from.
    cube::solver::Cube scanned = cube::solver::Cube::solved();
    // The cube the solution in force starts from. The same as `scanned` until
    // the method is changed part-way through, which solves on from the cube in
    // hand: `turn` counts turns of THAT solution, so it is the cube to solve
    // again to find the place.
    cube::solver::Cube from = cube::solver::Cube::solved();
    // SolveController::Method.
    int method = 0;
    // Turns taken, 0 to `turns`.
    int turn = 0;
    int turns = 0;
    // The time the Complete screen shows: seconds from starting to scan.
    int seconds = 0;
    bool finished = false;
};

// The kept solves, in one file in the app's own data folder.
//
// Newest first; the newest kCapacity are kept and an older one drops off the
// end. A file that cannot be read is set aside and the history starts empty: a
// damaged file must never stop the app, and a file from a later version of the
// app must not be written over (it is renamed, not deleted). Every change is
// written at once (QSaveFile: the old file stays whole until the new one is
// complete), so a crash loses at most the turn being taken.
//
// Nothing here knows about the camera, the screen or the solver beyond Cube.
class SolveHistoryStore {
public:
    static constexpr int kCapacity = 100;
    static constexpr int kFormatVersion = 1;

    // Reads `file` if it is there. It need not exist, and nothing is created
    // until the first change.
    explicit SolveHistoryStore(QString file);

    [[nodiscard]] const std::vector<HistoryEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] const HistoryEntry* find(qint64 id) const noexcept;
    [[nodiscard]] const QString& file() const noexcept { return file_; }
    // The file existed and was not understood (and was set aside). For the
    // controller to say once, and for the tests.
    [[nodiscard]] bool setAsideUnreadableFile() const noexcept { return set_aside_; }
    // The last write failed (a full or read-only disk). The in-memory list is
    // still right, so the history works for the session.
    [[nodiscard]] bool lastWriteFailed() const noexcept { return write_failed_; }

    // Adds `entry` as the newest, giving it an id; returns the id. The oldest
    // entry goes when there are more than kCapacity.
    qint64 add(HistoryEntry entry);
    // Replaces the entry with `entry.id`. False if there is none.
    bool update(const HistoryEntry& entry);
    bool remove(qint64 id);
    // Returns how many there were.
    int clear();

private:
    void load();
    void save();

    QString file_;
    std::vector<HistoryEntry> entries_;
    bool set_aside_ = false;
    bool write_failed_ = false;
};

}  // namespace cube::app
