#pragma once

#include "controllers/solve_history_store.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class SolveController;

namespace cube::app {

// The list of solves kept on this device (docs/design/screens/history.md), and
// the part of the app that keeps it.
//
// It watches the solve controller and writes the history as a person solves:
//
//   - a solve is ADDED when its first turn is advanced, marked unfinished;
//   - the turn reached follows every turn taken or taken back;
//   - it is FINISHED when the complete screen opens (finish());
//   - a method change part-way carries on the same entry (the solution it
//     counts turns of is a different one from then on);
//   - scanning another cube leaves the entry unfinished and starts a new one.
//
// "Keep a history of solves" off stops all of that - nothing is added or
// changed - and deletes nothing; clear() deletes.
//
// What is written is the 54 colours, never a picture (solve_history_store.hpp).
// Wording is QML's: this exposes numbers, flags and colour names.
class SolveHistory : public QObject {
    Q_OBJECT

    // history/keep in the settings store; on until a person turns it off.
    Q_PROPERTY(bool keep READ keep WRITE setKeep NOTIFY keepChanged)
    // Newest first. Each entry is a map: id, started (ms since the epoch),
    // finished, turn, turns, seconds, method, carryOn (the one entry that can
    // be carried on) and stickers (nine colour names, the scanned cube's front
    // side, row by row).
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)
    // The newest unfinished solve - the only one that can be carried on - or 0.
    Q_PROPERTY(qlonglong carryOnId READ carryOnId NOTIFY entriesChanged)
    // The clock the complete screen shows, which AppState owns and binds here:
    // what an entry's time is taken from.
    Q_PROPERTY(
        int elapsedSeconds READ elapsedSeconds WRITE setElapsedSeconds NOTIFY elapsedSecondsChanged)

public:
    // `file` is where the history is kept; empty for the app's own data folder
    // (defaultFile()). The tests give a file of their own.
    explicit SolveHistory(SolveController& solve, QString file = {}, QObject* parent = nullptr);

    // <data folder>/solve-history.json. The folder is the platform's
    // (cube::platform::paths), unless CUBEULATOR_DATA_DIR names one - which is
    // how the system tests keep out of a person's real history - and the
    // platform's own application-data location where the platform has not
    // said (Android and iOS today).
    [[nodiscard]] static QString defaultFile();

    [[nodiscard]] bool keep() const noexcept { return keep_; }
    void setKeep(bool keep);

    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] int count() const noexcept { return static_cast<int>(store_.entries().size()); }
    [[nodiscard]] qlonglong carryOnId() const noexcept;

    [[nodiscard]] int elapsedSeconds() const noexcept { return seconds_; }
    void setElapsedSeconds(int seconds);

    [[nodiscard]] const SolveHistoryStore& store() const noexcept { return store_; }

    // One entry as a map (see `entries`), or an empty map for an id that is
    // not there.
    Q_INVOKABLE QVariantMap entry(qlonglong id) const;
    // Deletes one entry. False if there is none.
    Q_INVOKABLE bool remove(qlonglong id);
    // Deletes every entry, an unfinished one included. Returns how many.
    Q_INVOKABLE int clear();
    // Marks the solve being followed as finished, with the time now showing.
    // Called when the complete screen opens; harmless when nothing is being
    // kept.
    Q_INVOKABLE void finish();
    // Puts the newest unfinished solve back in the solve controller: its cube,
    // its method and the turn reached. The solution is worked out again from
    // the cube, so the screen opens when it lands. False for any entry that is
    // not the one that can be carried on.
    Q_INVOKABLE bool resume(qlonglong id);

signals:
    void keepChanged();
    void entriesChanged();
    void elapsedSecondsChanged();

private:
    void onStateChanged();
    void onSolutionChanged();
    [[nodiscard]] QVariantMap toMap(const HistoryEntry& entry, qint64 carry_on) const;

    SolveController& solve_;
    SolveHistoryStore store_;
    bool keep_ = true;
    int seconds_ = 0;
    // The entry the solve now being followed is written to, or 0.
    qint64 current_ = 0;
    // An entry has been made for the solution in force (and may since have
    // been deleted): it is not made again for the same solve, so that
    // clearing the history mid-solve is not undone by the next turn.
    bool recorded_ = false;
    // Set by resume() until the solution it asked for lands.
    qint64 resuming_ = 0;
};

}  // namespace cube::app
