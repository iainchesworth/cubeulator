#include "controllers/colour_entry.hpp"

#include "colour_name.hpp"
#include "controllers/direct_scan.hpp"
#include "solve_controller.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/validate.hpp>
#include <cube/solver/cube.hpp>

#include <QVariantList>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace cube::app {

namespace {

using cube::nxn::Face;

struct Side {
    Face face;
    const char* letter;
};

constexpr std::array<Side, 6> kSides = {
    Side{.face = Face::U, .letter = "U"}, Side{.face = Face::D, .letter = "D"},
    Side{.face = Face::L, .letter = "L"}, Side{.face = Face::R, .letter = "R"},
    Side{.face = Face::F, .letter = "F"}, Side{.face = Face::B, .letter = "B"},
};

[[nodiscard]] std::optional<Face> face_of(const QString& letter) {
    for (const Side& side : kSides) {
        if (letter == QLatin1String(side.letter)) {
            return side.face;
        }
    }
    return std::nullopt;
}

[[nodiscard]] const char* letter_of(Face face) {
    for (const Side& side : kSides) {
        if (side.face == face) {
            return side.letter;
        }
    }
    return "U";
}

[[nodiscard]] const char* fault_code(cube::nxn::Fault fault) {
    using cube::nxn::Fault;
    switch (fault) {
        case Fault::NoScheme:
            return "noScheme";
        case Fault::CentreColours:
            return "centreColours";
        case Fault::UnknownPiece:
            return "unknownPiece";
        case Fault::RepeatedPiece:
            return "repeatedPiece";
        case Fault::TwistedCorner:
            return "twistedCorner";
        case Fault::FlippedEdge:
            return "flippedEdge";
        case Fault::SwappedPieces:
            return "swappedPieces";
        case Fault::None:
        case Fault::NotACube:
        case Fault::ColourCounts:
            break;
    }
    return "unknownPiece";
}

[[nodiscard]] QString name_of(std::int8_t colour) {
    return colour == kUnpainted ? QString() : colour_name(static_cast<cube::solver::Color>(colour));
}

[[nodiscard]] QVariantList counts(const std::vector<ColourCount>& entries) {
    QVariantList out;
    for (const ColourCount& entry : entries) {
        QVariantMap map;
        map.insert(QStringLiteral("colour"), name_of(entry.colour));
        map.insert(QStringLiteral("count"), entry.count);
        out.append(map);
    }
    return out;
}

// The cube turned as a whole until each side's colour is the one the solver's
// scheme gives it - yellow on top, green in front - when there is such a way to
// hold it (the colours of a cube made the usual way always have one). The solve
// screen then says "hold yellow on top, green facing you", which is the way the
// cube is shown, and the stages worded for that scheme ("white cross") are true of
// it. The cube is the same cube; only which side is called the top has changed. A
// cube whose colours are in some other scheme is solved as it is.
[[nodiscard]] cube::nxn::NCube held_like_the_solver(const cube::nxn::NCube& cube) {
    const cube::nxn::Geometry& geometry = cube.geometry();
    for (std::size_t rotation = 0; rotation < geometry.rotations().size(); ++rotation) {
        cube::nxn::NCube turned = cube;
        turned.apply(geometry.rotation_permutation(rotation));
        const auto scheme = cube::nxn::scheme_of(turned);
        if (!scheme.has_value()) {
            break;
        }
        const bool same = std::ranges::all_of(cube::solver::kAllFaces, [&](Face face) {
            return scheme->at(static_cast<std::size_t>(face)) ==
                   cube::solver::canonical_color(face);
        });
        if (same) {
            return turned;
        }
    }
    return cube;
}

}  // namespace

ColourEntry::ColourEntry(SolveController& solve, DirectScan* scan, QObject* parent)
    : QObject(parent), solve_(solve), scan_(scan), edit_(3) {}

bool ColourEntry::beginFromScan() {
    if (scan_ == nullptr) {
        return false;
    }
    std::optional<Reading> reading = scan_->reviewReading();
    if (!reading.has_value()) {
        return false;
    }
    loadReading(std::move(*reading));
    return true;
}

void ColourEntry::begin(int size) {
    edit_.begin(size);
    emit changed();
}

void ColourEntry::loadReading(Reading reading) {
    edit_.load(std::move(reading));
    emit changed();
}

void ColourEntry::paint(const QString& side, int index, int colour) {
    const std::optional<Face> face = face_of(side);
    if (!face.has_value()) {
        return;
    }
    if (edit_.paint(*face, index, static_cast<std::int8_t>(colour))) {
        emit changed();
    }
}

void ColourEntry::paintSide(const QString& side, int colour) {
    const std::optional<Face> face = face_of(side);
    if (!face.has_value()) {
        return;
    }
    if (edit_.fill(*face, static_cast<std::int8_t>(colour))) {
        emit changed();
    }
}

void ColourEntry::clearAll() {
    if (edit_.clear_all()) {
        emit changed();
    }
}

int ColourEntry::paintedOn(const QString& side) const {
    const std::optional<Face> face = face_of(side);
    return face.has_value() ? edit_.painted_on(*face) : 0;
}

QVariantMap ColourEntry::describeSide(const QString& side, const QString& text) {
    QVariantMap out;
    const std::optional<Face> face = face_of(side);
    if (!face.has_value()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("noSuchSide"));
        out.insert(QStringLiteral("at"), 0);
        return out;
    }
    const std::string latin = text.toStdString();
    const SideText parsed = parse_side(latin, edit_.size());
    if (!parsed.error.empty()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(
            QStringLiteral("error"),
            QString::fromLatin1(parsed.error.data(), static_cast<qsizetype>(parsed.error.size())));
        out.insert(QStringLiteral("at"), parsed.at);
        return out;
    }
    if (edit_.set_side(*face, parsed.colours)) {
        emit changed();
    }
    out.insert(QStringLiteral("ok"), true);
    return out;
}

QString ColourEntry::sideText(const QString& side) const {
    const std::optional<Face> face = face_of(side);
    if (!face.has_value()) {
        return {};
    }
    const int n = edit_.size();
    QString out;
    for (int row = 0; row < n; ++row) {
        if (row > 0) {
            out.append(QLatin1Char('/'));
        }
        for (int col = 0; col < n; ++col) {
            out.append(QLatin1Char(colour_letter(edit_.colour(*face, (row * n) + col))));
        }
    }
    return out;
}

void ColourEntry::rotateSide(const QString& side, int quarterTurns) {
    const std::optional<Face> face = face_of(side);
    if (face.has_value() && edit_.rotate_side(*face, quarterTurns)) {
        emit changed();
    }
}

void ColourEntry::beginStroke() {
    edit_.begin_stroke();
}

void ColourEntry::endStroke() {
    edit_.end_stroke();
}

void ColourEntry::undo() {
    if (edit_.undo()) {
        emit changed();
    }
}

void ColourEntry::redo() {
    if (edit_.redo()) {
        emit changed();
    }
}

void ColourEntry::revert() {
    if (edit_.revert()) {
        emit changed();
    }
}

int ColourEntry::doubtCount() const {
    int total = 0;
    for (const Side& side : kSides) {
        total += edit_.doubtful_on(side.face);
    }
    return total;
}

QVariantMap ColourEntry::sides() const {
    const int n = edit_.size();
    QVariantMap out;
    for (const Side& side : kSides) {
        QVariantList stickers;
        stickers.reserve(static_cast<qsizetype>(n) * n);
        for (int i = 0; i < n * n; ++i) {
            stickers.append(name_of(edit_.colour(side.face, i)));
        }
        out.insert(QString::fromLatin1(side.letter), stickers);
    }
    return out;
}

QVariantMap ColourEntry::doubts() const {
    const int n = edit_.size();
    QVariantMap out;
    for (const Side& side : kSides) {
        QVariantList flags;
        flags.reserve(static_cast<qsizetype>(n) * n);
        for (int i = 0; i < n * n; ++i) {
            flags.append(edit_.doubtful(side.face, i));
        }
        out.insert(QString::fromLatin1(side.letter), flags);
    }
    return out;
}

QVariantMap ColourEntry::centres() const {
    QVariantMap out;
    for (const Side& side : kSides) {
        out.insert(QString::fromLatin1(side.letter), name_of(edit_.centre(side.face)));
    }
    return out;
}

QVariantMap ColourEntry::status() const {
    const EditStatus now = edit_.status();
    QVariantMap out;
    out.insert(QStringLiteral("toGo"), now.to_go);
    switch (now.state) {
        case EditState::Empty:
            out.insert(QStringLiteral("state"), QStringLiteral("empty"));
            break;
        case EditState::Partial:
            out.insert(QStringLiteral("state"), QStringLiteral("partial"));
            break;
        case EditState::Miscount:
            out.insert(QStringLiteral("state"), QStringLiteral("miscount"));
            out.insert(QStringLiteral("over"), counts(now.over));
            out.insert(QStringLiteral("under"), counts(now.under));
            break;
        case EditState::Impossible:
            out.insert(QStringLiteral("state"), QStringLiteral("impossible"));
            out.insert(QStringLiteral("fault"), QString::fromLatin1(fault_code(now.fault)));
            if (now.suspect.has_value()) {
                QVariantMap suspect;
                suspect.insert(QStringLiteral("side"),
                               QString::fromLatin1(letter_of(now.suspect->face)));
                suspect.insert(QStringLiteral("index"), now.suspect->index);
                out.insert(QStringLiteral("suspect"), suspect);
            }
            break;
        case EditState::Valid:
            out.insert(QStringLiteral("state"), QStringLiteral("valid"));
            break;
    }
    return out;
}

QVariantMap ColourEntry::solve() {
    QVariantMap out;
    if (edit_.status().state != EditState::Valid) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("notACube"));
        return out;
    }
    std::vector<cube::solver::Color> stickers;
    stickers.reserve(edit_.stickers().size());
    for (const std::int8_t colour : edit_.stickers()) {
        stickers.push_back(static_cast<cube::solver::Color>(colour));
    }
    const auto cube = cube::nxn::NCube::from_facelets(edit_.size(), stickers);
    if (!cube.has_value()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("notACube"));
        return out;
    }
    solve_.solveCube(held_like_the_solver(*cube));
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("solved"), cube->is_solved());
    return out;
}

}  // namespace cube::app
