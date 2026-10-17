#include "controllers/colour_entry.hpp"

#include "colour_name.hpp"
#include "solve_controller.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/validate.hpp>
#include <cube/solver/cube.hpp>

#include <QVariantList>

#include <algorithm>
#include <array>
#include <cstddef>
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

constexpr int kColourCount = 6;
constexpr int kUnpainted = -1;

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

// Where the sticker at (row, col) as seen from outside is in the library's
// numbering of its side, and the other way round: the same picture on U, F and L,
// mirrored left to right on R and B and top to bottom on D, which undoes itself
// (cube::nxn::outside_view).
[[nodiscard]] std::pair<int, int> mirrored(Face face, int size, int row, int col) {
    switch (face) {
        case Face::R:
        case Face::B:
            return {row, size - 1 - col};
        case Face::D:
            return {size - 1 - row, col};
        case Face::U:
        case Face::F:
        case Face::L:
            break;
    }
    return {row, col};
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

ColourEntry::ColourEntry(SolveController& solve, QObject* parent) : QObject(parent), solve_(solve) {
    begin(3);
}

void ColourEntry::begin(int size) {
    size_ = std::clamp(size, cube::nxn::kMinSize, cube::nxn::kMaxSize);
    const auto n = static_cast<std::size_t>(size_);
    colours_.assign(6 * n * n, static_cast<std::int8_t>(kUnpainted));
    emit changed();
}

void ColourEntry::paint(const QString& side, int index, int colour) {
    const std::optional<Face> face = face_of(side);
    if (!face.has_value() || index < 0 || index >= size_ * size_ || colour < kUnpainted ||
        colour >= kColourCount) {
        return;
    }
    const auto [row, col] = mirrored(*face, size_, index / size_, index % size_);
    const auto n = static_cast<std::size_t>(size_);
    const std::size_t facelet = (static_cast<std::size_t>(*face) * n * n) +
                                (static_cast<std::size_t>(row) * n) + static_cast<std::size_t>(col);
    if (colours_.at(facelet) == colour) {
        return;
    }
    colours_.at(facelet) = static_cast<std::int8_t>(colour);
    emit changed();
}

void ColourEntry::paintSide(const QString& side, int colour) {
    const std::optional<Face> face = face_of(side);
    if (!face.has_value() || colour < kUnpainted || colour >= kColourCount) {
        return;
    }
    const auto n = static_cast<std::size_t>(size_);
    const auto first =
        colours_.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(*face) * n * n);
    std::fill(first, first + static_cast<std::ptrdiff_t>(n * n), static_cast<std::int8_t>(colour));
    emit changed();
}

void ColourEntry::clearAll() {
    std::ranges::fill(colours_, static_cast<std::int8_t>(kUnpainted));
    emit changed();
}

int ColourEntry::paintedOn(const QString& side) const {
    const std::optional<Face> face = face_of(side);
    if (!face.has_value()) {
        return 0;
    }
    const auto n = static_cast<std::size_t>(size_);
    const auto first =
        colours_.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(*face) * n * n);
    return static_cast<int>(std::count_if(first, first + static_cast<std::ptrdiff_t>(n * n),
                                          [](std::int8_t colour) { return colour != kUnpainted; }));
}

bool ColourEntry::touched() const {
    return std::ranges::any_of(colours_, [](std::int8_t colour) { return colour != kUnpainted; });
}

QVariantMap ColourEntry::sides() const {
    const auto n = static_cast<std::size_t>(size_);
    QVariantMap out;
    for (const Side& side : kSides) {
        QVariantList stickers;
        stickers.reserve(static_cast<qsizetype>(n * n));
        for (int row = 0; row < size_; ++row) {
            for (int col = 0; col < size_; ++col) {
                const auto [r, c] = mirrored(side.face, size_, row, col);
                const std::int8_t colour =
                    colours_.at((static_cast<std::size_t>(side.face) * n * n) +
                                (static_cast<std::size_t>(r) * n) + static_cast<std::size_t>(c));
                stickers.append(colour == kUnpainted
                                    ? QString()
                                    : colour_name(static_cast<cube::solver::Color>(colour)));
            }
        }
        out.insert(QString::fromLatin1(side.letter), stickers);
    }
    return out;
}

QVariantMap ColourEntry::status() const {
    const auto n = static_cast<std::size_t>(size_);
    const int per_colour = size_ * size_;
    std::array<int, kColourCount> counts{};
    int to_go = 0;
    for (const std::int8_t colour : colours_) {
        if (colour == kUnpainted) {
            ++to_go;
        } else {
            ++counts.at(static_cast<std::size_t>(colour));
        }
    }

    QVariantMap out;
    out.insert(QStringLiteral("toGo"), to_go);
    if (static_cast<std::size_t>(to_go) == 6 * n * n) {
        out.insert(QStringLiteral("state"), QStringLiteral("empty"));
        return out;
    }
    if (to_go > 0) {
        out.insert(QStringLiteral("state"), QStringLiteral("partial"));
        return out;
    }

    QVariantList over;
    QVariantList under;
    for (int colour = 0; colour < kColourCount; ++colour) {
        const int count = counts.at(static_cast<std::size_t>(colour));
        if (count == per_colour) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("colour"),
                     colour_name(static_cast<cube::solver::Color>(colour)));
        entry.insert(QStringLiteral("count"), count);
        (count > per_colour ? over : under).append(entry);
    }
    if (!over.isEmpty() || !under.isEmpty()) {
        out.insert(QStringLiteral("state"), QStringLiteral("miscount"));
        out.insert(QStringLiteral("over"), over);
        out.insert(QStringLiteral("under"), under);
        return out;
    }

    std::vector<cube::solver::Color> stickers;
    stickers.reserve(colours_.size());
    for (const std::int8_t colour : colours_) {
        stickers.push_back(static_cast<cube::solver::Color>(colour));
    }
    const cube::nxn::Verdict verdict = cube::nxn::validate(size_, stickers);
    if (verdict.ok()) {
        out.insert(QStringLiteral("state"), QStringLiteral("valid"));
        return out;
    }
    out.insert(QStringLiteral("state"), QStringLiteral("impossible"));
    out.insert(QStringLiteral("fault"), QString::fromLatin1(fault_code(verdict.fault)));
    // A sticker of the piece at fault, as seen from outside, when it is about one.
    const bool about_a_piece = verdict.fault == cube::nxn::Fault::TwistedCorner ||
                               verdict.fault == cube::nxn::Fault::FlippedEdge ||
                               verdict.fault == cube::nxn::Fault::UnknownPiece ||
                               verdict.fault == cube::nxn::Fault::RepeatedPiece;
    if (about_a_piece) {
        const auto position = cube::nxn::Geometry::of(size_).position(verdict.facelet);
        const auto [row, col] = mirrored(position.face, size_, position.row, position.col);
        QVariantMap suspect;
        suspect.insert(QStringLiteral("side"), QString::fromLatin1(letter_of(position.face)));
        suspect.insert(QStringLiteral("index"), (row * size_) + col);
        out.insert(QStringLiteral("suspect"), suspect);
    }
    return out;
}

QVariantMap ColourEntry::solve() {
    QVariantMap out;
    if (status().value(QStringLiteral("state")).toString() != QLatin1String("valid")) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("notACube"));
        return out;
    }
    std::vector<cube::solver::Color> stickers;
    stickers.reserve(colours_.size());
    for (const std::int8_t colour : colours_) {
        stickers.push_back(static_cast<cube::solver::Color>(colour));
    }
    const auto cube = cube::nxn::NCube::from_facelets(size_, stickers);
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
