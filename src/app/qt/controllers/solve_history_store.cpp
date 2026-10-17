#include "controllers/solve_history_store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <cube/nxn/notation.hpp>
#include <cube/nxn/stage.hpp>
#include <cube/nxn/validate.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <utility>
#include <vector>

namespace cube::app {

namespace {

using cube::nxn::LayerMove;
using cube::nxn::NCube;
using cube::solver::Color;

constexpr char kColourLetters[] = {'W', 'Y', 'G', 'B', 'O', 'R'};  // Color's order
// A 3x3 is the size an entry has when it does not say.
constexpr int kDefaultSize = 3;
// A solution is a few dozen turns at a 3x3 and a few hundred at the biggest
// cube the library solves (740 on average for a 9x9, 766 the most seen); this
// leaves room past that. A value past it is a damaged file, not a long solve.
constexpr int kMostTurns = 2000;
// A day is 86 400; a solve open for a year is not a solve.
constexpr int kMostSeconds = 366 * 86'400;

// The cube's stickers as letters, face by face, row by row: 54 for a 3x3, 6 N^2
// for any size.
[[nodiscard]] QString encode(const NCube& cube) {
    QString text;
    text.reserve(static_cast<qsizetype>(cube.facelets().size()));
    for (const Color colour : cube.facelets()) {
        text.append(QLatin1Char(kColourLetters[static_cast<int>(colour)]));
    }
    return text;
}

// The letters back to a cube of `size`, or nothing if the text is not one that
// a real cube makes: a damaged file must be dropped, not handed to the solver.
[[nodiscard]] bool decode(const QString& text, int size, NCube& out) {
    if (size < cube::nxn::kMinSize || size > cube::nxn::kMaxSize ||
        text.size() != 6 * size * size) {
        return false;
    }
    std::vector<Color> facelets;
    facelets.reserve(static_cast<std::size_t>(text.size()));
    for (const QChar letter : text) {
        int colour = -1;
        for (int c = 0; c < 6; ++c) {
            if (letter == QLatin1Char(kColourLetters[c])) {
                colour = c;
            }
        }
        if (colour < 0) {
            return false;
        }
        facelets.push_back(static_cast<Color>(colour));
    }
    const auto cube = NCube::from_facelets(size, facelets);
    if (!cube.has_value() || !cube::nxn::validate(*cube).ok()) {
        return false;
    }
    out = *cube;
    return true;
}

// Whether the entry needs the second version of the file to be said.
[[nodiscard]] bool needs_version_2(const HistoryEntry& entry) {
    return entry.scanned.size() != kDefaultSize || entry.from.size() != kDefaultSize ||
           !entry.moves.empty();
}

// The turns as notation, one token to a layer turn.
[[nodiscard]] QString encode_moves(const std::vector<LayerMove>& moves, int size) {
    return QString::fromStdString(cube::nxn::to_string(std::span<const LayerMove>{moves}, size));
}

// The turns back, or nothing if the text is not turns of layers the cube has
// that solve it: a solution that does not solve its cube is not kept.
[[nodiscard]] bool decode_moves(const QString& text, const NCube& from,
                                std::vector<LayerMove>& out) {
    const auto parsed = cube::nxn::parse(text.toStdString(), from.size());
    if (!parsed.has_value()) {
        return false;
    }
    std::vector<LayerMove> moves = cube::nxn::layers_of(*parsed);
    NCube replay = from;
    replay.apply(moves);
    if (!replay.is_solved()) {
        return false;
    }
    out = std::move(moves);
    return true;
}

// The stages as the file has them: stage and turn count, in turn, one list.
[[nodiscard]] QJsonArray encode_stages(const std::vector<KeptStage>& stages) {
    QJsonArray out;
    for (const KeptStage& stage : stages) {
        out.append(stage.stage);
        out.append(stage.count);
    }
    return out;
}

[[nodiscard]] QJsonObject to_json(const HistoryEntry& entry) {
    QJsonObject object{
        {"id", static_cast<double>(entry.id)},
        {"started", static_cast<double>(entry.startedMs)},
        {"cube", encode(entry.scanned)},
        {"method", entry.method},
        {"turn", entry.turn},
        {"turns", entry.turns},
        {"seconds", entry.seconds},
        {"finished", entry.finished},
    };
    // Only for a cube that is not a 3x3, which is what a version 1 file holds.
    if (entry.scanned.size() != kDefaultSize) {
        object.insert("size", entry.scanned.size());
    }
    // Written only when the method was changed part-way, which is rare.
    if (!(entry.from == entry.scanned)) {
        object.insert("from", encode(entry.from));
    }
    if (!entry.moves.empty()) {
        object.insert("moves", encode_moves(entry.moves, entry.from.size()));
        if (!entry.stages.empty()) {
            object.insert("stages", encode_stages(entry.stages));
        }
    }
    return object;
}

// A whole number held in a JSON double, or nothing.
[[nodiscard]] bool whole(const QJsonValue& value, qint64 least, qint64 most, qint64& out) {
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!(number >= static_cast<double>(least) && number <= static_cast<double>(most)) ||
        number != static_cast<double>(static_cast<qint64>(number))) {
        return false;
    }
    out = static_cast<qint64>(number);
    return true;
}

// The stages back, or nothing if they are not stages of a solution of `turns`
// turns: stage numbers there are, counts that are not negative and add up to all
// of the turns.
[[nodiscard]] bool decode_stages(const QJsonValue& value, std::size_t turns,
                                 std::vector<KeptStage>& out) {
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray list = value.toArray();
    if (list.size() % 2 != 0) {
        return false;
    }
    constexpr int kStages = static_cast<int>(cube::nxn::StageId::Corners) + 1;
    std::vector<KeptStage> stages;
    std::size_t counted = 0;
    for (qsizetype i = 0; i < list.size(); i += 2) {
        qint64 stage = 0;
        qint64 count = 0;
        if (!whole(list.at(i), 0, kStages - 1, stage) ||
            !whole(list.at(i + 1), 0, static_cast<qint64>(turns), count)) {
            return false;
        }
        counted += static_cast<std::size_t>(count);
        stages.push_back({.stage = static_cast<int>(stage), .count = static_cast<int>(count)});
    }
    if (counted != turns) {
        return false;
    }
    out = std::move(stages);
    return true;
}

[[nodiscard]] bool from_json(const QJsonObject& object, HistoryEntry& out) {
    HistoryEntry entry;
    qint64 value = 0;
    if (!whole(object.value("id"), 1, (qint64{1} << 52), entry.id) ||
        !whole(object.value("started"), 0, (qint64{1} << 52), entry.startedMs)) {
        return false;
    }
    if (!object.value("finished").isBool()) {
        return false;
    }
    entry.finished = object.value("finished").toBool();

    if (!whole(object.value("method"), 0, 2, value)) {
        return false;
    }
    entry.method = static_cast<int>(value);
    if (!whole(object.value("turns"), 0, kMostTurns, value)) {
        return false;
    }
    entry.turns = static_cast<int>(value);
    if (!whole(object.value("turn"), 0, entry.turns, value)) {
        return false;
    }
    entry.turn = static_cast<int>(value);
    if (!whole(object.value("seconds"), 0, kMostSeconds, value)) {
        return false;
    }
    entry.seconds = static_cast<int>(value);

    // The size is said only when it is not a 3x3's.
    qint64 size = kDefaultSize;
    if (object.contains("size") &&
        !whole(object.value("size"), cube::nxn::kMinSize, cube::nxn::kMaxSize, size)) {
        return false;
    }
    const int cube_size = static_cast<int>(size);

    if (!object.value("cube").isString() ||
        !decode(object.value("cube").toString(), cube_size, entry.scanned)) {
        return false;
    }
    entry.from = entry.scanned;
    if (object.contains("from") &&
        (!object.value("from").isString() ||
         !decode(object.value("from").toString(), cube_size, entry.from))) {
        return false;
    }
    // A kept solution that does not solve its cube is dropped, and the entry
    // with it kept: the solution is worked out again.
    if (object.contains("moves") &&
        (!object.value("moves").isString() ||
         !decode_moves(object.value("moves").toString(), entry.from, entry.moves))) {
        entry.moves.clear();
    }
    // Stages that do not fit the turns spoil the kept solution as much as turns
    // that do not solve the cube; a solution kept with no stages is kept as it is,
    // and used by nothing that needs them.
    if (!entry.moves.empty() && object.contains("stages") &&
        !decode_stages(object.value("stages"), entry.moves.size(), entry.stages)) {
        entry.moves.clear();
    }
    out = entry;
    return true;
}

}  // namespace

SolveHistoryStore::SolveHistoryStore(QString file) : file_(std::move(file)) {
    load();
}

const HistoryEntry* SolveHistoryStore::find(qint64 id) const noexcept {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [id](const HistoryEntry& e) { return e.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

qint64 SolveHistoryStore::add(HistoryEntry entry) {
    qint64 newest = 0;
    for (const HistoryEntry& existing : entries_) {
        newest = std::max(newest, existing.id);
    }
    entry.id = newest + 1;
    entries_.insert(entries_.begin(), entry);
    if (entries_.size() > static_cast<std::size_t>(kCapacity)) {
        entries_.resize(static_cast<std::size_t>(kCapacity));
    }
    save();
    return entry.id;
}

bool SolveHistoryStore::update(const HistoryEntry& entry) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&entry](const HistoryEntry& e) { return e.id == entry.id; });
    if (it == entries_.end()) {
        return false;
    }
    *it = entry;
    save();
    return true;
}

bool SolveHistoryStore::remove(qint64 id) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [id](const HistoryEntry& e) { return e.id == id; });
    if (it == entries_.end()) {
        return false;
    }
    entries_.erase(it);
    save();
    return true;
}

int SolveHistoryStore::clear() {
    const int had = static_cast<int>(entries_.size());
    entries_.clear();
    save();
    return had;
}

void SolveHistoryStore::load() {
    QFile file(file_);
    if (file_.isEmpty() || !file.exists()) {
        return;
    }
    QByteArray bytes;
    bool readable = file.open(QIODevice::ReadOnly);
    if (readable) {
        bytes = file.readAll();
        file.close();
    }

    QJsonParseError error;
    const QJsonDocument document =
        readable ? QJsonDocument::fromJson(bytes, &error) : QJsonDocument{};
    qint64 version = 0;
    if (!readable || error.error != QJsonParseError::NoError || !document.isObject() ||
        !whole(document.object().value("version"), 1, kFormatVersion, version) ||
        !document.object().value("entries").isArray()) {
        // Set aside rather than deleted: it may be a later version's, and the
        // next write would otherwise be on top of it. What is left is an
        // empty history, which is the right thing to show for a file nobody
        // can read.
        const QString aside = file_ + QStringLiteral(".unreadable");
        QFile::remove(aside);
        QFile::rename(file_, aside);
        set_aside_ = true;
        qWarning("history: %s could not be read; it was set aside as %s", qUtf8Printable(file_),
                 qUtf8Printable(aside));
        return;
    }

    for (const QJsonValue& item : document.object().value("entries").toArray()) {
        HistoryEntry entry;
        // One bad entry is dropped; the rest are good.
        if (!item.isObject() || !from_json(item.toObject(), entry) ||
            std::any_of(entries_.begin(), entries_.end(),
                        [&entry](const HistoryEntry& e) { return e.id == entry.id; })) {
            continue;
        }
        entries_.push_back(entry);
    }
    // Newest first whatever order the file had, and no more than the cap.
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const HistoryEntry& a, const HistoryEntry& b) { return a.id > b.id; });
    if (entries_.size() > static_cast<std::size_t>(kCapacity)) {
        entries_.resize(static_cast<std::size_t>(kCapacity));
    }
}

void SolveHistoryStore::save() {
    write_failed_ = true;
    if (file_.isEmpty()) {
        return;
    }
    QJsonArray array;
    bool second_version = false;
    for (const HistoryEntry& entry : entries_) {
        array.append(to_json(entry));
        second_version = second_version || needs_version_2(entry);
    }
    // Version 1 for as long as nothing in the file needs more, so that the app
    // that wrote it before can still read it.
    const QJsonDocument document(
        QJsonObject{{"version", second_version ? kFormatVersion : 1}, {"entries", array}});

    if (!QDir().mkpath(QFileInfo(file_).absolutePath())) {
        qWarning("history: cannot make the folder for %s", qUtf8Printable(file_));
        return;
    }
    QSaveFile out(file_);
    if (!out.open(QIODevice::WriteOnly) || out.write(document.toJson(QJsonDocument::Compact)) < 0 ||
        !out.commit()) {
        qWarning("history: cannot write %s", qUtf8Printable(file_));
        return;
    }
    write_failed_ = false;
}

}  // namespace cube::app
