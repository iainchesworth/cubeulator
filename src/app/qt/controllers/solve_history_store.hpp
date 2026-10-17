#pragma once

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>

#include <QString>
#include <QtGlobal>

#include <vector>

namespace cube::app {

// A stage of a kept solution: which (a cube::nxn::StageId), and how many of the
// kept turns it is. A stage with no turns is kept too, so that the stages a
// solution has are the stages it has when it is read back.
struct KeptStage {
    int stage = 0;
    int count = 0;

    friend bool operator==(const KeptStage&, const KeptStage&) = default;
};

// One solve, as it is kept on the device (docs/design/screens/history.md,
// "What is kept"): when it started, the cube as scanned, the turn reached, the
// number of turns and the time taken.
//
// What is NOT here is any picture. A camera frame never reaches this type: the
// only thing that does is the colours the scan settled on (54 of them, for a
// 3x3). That is what lets the privacy statement say that no picture is saved.
//
// The cube is of any size. The solution of a 3x3 is not stored; it is worked
// out again from the cube, which gives the same answer for the same method - so
// the method is. A bigger cube's is stored (`moves`), because the solver's
// answer for it improves from one release to the next and a solve being carried
// on must not change under the person doing it.
struct HistoryEntry {
    // Never reused while the entry exists; larger is newer.
    qint64 id = 0;
    // Milliseconds since the epoch (UTC) at which the first turn was advanced.
    qint64 startedMs = 0;
    // The cube as the scan read it. What the row's thumbnail is drawn from.
    cube::nxn::NCube scanned = *cube::nxn::NCube::solved(3);
    // The cube the solution in force starts from. The same as `scanned` until
    // the method is changed part-way through, which solves on from the cube in
    // hand: `turn` counts turns of THAT solution, so it is the cube to solve
    // again to find the place.
    cube::nxn::NCube from = *cube::nxn::NCube::solved(3);
    // SolveController::Method.
    int method = 0;
    // Turns taken, 0 to `turns`.
    int turn = 0;
    int turns = 0;
    // The time the Complete screen shows: seconds from starting to scan.
    int seconds = 0;
    bool finished = false;
    // The solution in force, as turns of single layers from `from`, when it is
    // kept (a cube bigger than a 3x3). Empty when it is not: the solution is
    // worked out again from `from`.
    std::vector<cube::nxn::LayerMove> moves;
    // The stages of `moves`, in order, which add up to all of them: what the
    // solve screen says of where the person is in the solve. Empty when `moves`
    // is, and also for a kept solution that does not say (then the solution is
    // worked out again, as for a 3x3).
    std::vector<KeptStage> stages;
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
// Nothing here knows about the camera, the screen or the solver beyond NCube.
//
// The file is version 1 for as long as every entry is a 3x3 with no kept
// solution, which is the file the first release wrote, so that an app that
// goes back to it still reads it. An entry of another size, or with a kept
// solution, makes it version 2 (a "size" and a "moves" key on that entry); a
// version 1 file is read as 3x3s.
class SolveHistoryStore {
public:
    static constexpr int kCapacity = 100;
    static constexpr int kFormatVersion = 2;

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
