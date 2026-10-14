#include "controllers/solve_history_store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <algorithm>
#include <array>

namespace cube::app {

namespace {

using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::kAllFaces;

constexpr char kColourLetters[] = {'W', 'Y', 'G', 'B', 'O', 'R'};  // Color's order
constexpr int kStickers = 54;
// A solution is a few dozen turns; the longest the beginner method has made is
// well under this. A value past it is a damaged file, not a long solve.
constexpr int kMostTurns = 400;
// A day is 86 400; a solve open for a year is not a solve.
constexpr int kMostSeconds = 366 * 86'400;

[[nodiscard]] QString encode(const Cube& cube) {
    QString text;
    text.reserve(kStickers);
    for (const auto face : kAllFaces) {
        for (const Color colour : cube.face(face)) {
            text.append(QLatin1Char(kColourLetters[static_cast<int>(colour)]));
        }
    }
    return text;
}

// The 54 letters back to a cube, or nothing if the text is not one that a real
// cube makes: a damaged file must be dropped, not handed to the solver.
[[nodiscard]] bool decode(const QString& text, Cube& out) {
    if (text.size() != kStickers) {
        return false;
    }
    std::array<std::array<Color, 9>, 6> facelets{};
    for (int i = 0; i < kStickers; ++i) {
        const QChar letter = text.at(i);
        int colour = -1;
        for (int c = 0; c < 6; ++c) {
            if (letter == QLatin1Char(kColourLetters[c])) {
                colour = c;
            }
        }
        if (colour < 0) {
            return false;
        }
        facelets.at(static_cast<std::size_t>(i / 9)).at(static_cast<std::size_t>(i % 9)) =
            static_cast<Color>(colour);
    }
    const auto cube = Cube::from_facelets(facelets);
    if (!cube.has_value()) {
        return false;
    }
    out = *cube;
    return true;
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
    // Written only when the method was changed part-way, which is rare.
    if (!(entry.from == entry.scanned)) {
        object.insert("from", encode(entry.from));
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

    if (!object.value("cube").isString() ||
        !decode(object.value("cube").toString(), entry.scanned)) {
        return false;
    }
    entry.from = entry.scanned;
    if (object.contains("from") && (!object.value("from").isString() ||
                                    !decode(object.value("from").toString(), entry.from))) {
        return false;
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
    for (const HistoryEntry& entry : entries_) {
        array.append(to_json(entry));
    }
    const QJsonDocument document(QJsonObject{{"version", kFormatVersion}, {"entries", array}});

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
