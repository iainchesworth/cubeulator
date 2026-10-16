#include "CubeNModel.hpp"

#include <cube/nxn/geometry.hpp>
#include <cube/solver/cube.hpp>

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

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

// The colour channel as the material takes it: linear light.
float linear(float srgb) {
    return srgb <= 0.04045F ? srgb / 12.92F : std::pow((srgb + 0.055F) / 1.055F, 2.4F);
}

cube::nxn::Rgba linear_of(const QColor& colour) {
    return {
        .r = linear(colour.redF()),
        .g = linear(colour.greenF()),
        .b = linear(colour.blueF()),
        .a = colour.alphaF(),
    };
}

// Where the sticker at (row, col) as seen from outside is in the library's numbering of
// its side: the same picture on U, F and L, mirrored left to right on R and B, and top
// to bottom on D (cube::nxn::outside_view, the other way round).
std::pair<int, int> storage_of(Face face, int size, int row, int col) {
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

// The side length a side of `count` stickers is of, or 0.
int side_length_of(qsizetype count) {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        if (count == static_cast<qsizetype>(size) * size) {
            return size;
        }
    }
    return 0;
}

}  // namespace

CubeNModel::CubeNModel(QObject* parent) : QObject(parent) {
    rebuild();
}

void CubeNModel::setFaceletColors(const QVariant& painting) {
    painting_ = painting;
    rebuild();
}

void CubeNModel::setFill(const QColor& fill) {
    if (fill == fill_) {
        return;
    }
    fill_ = fill;
    rebuild();
}

void CubeNModel::setBody(const QColor& body) {
    if (body == body_) {
        return;
    }
    body_ = body;
    rebuild();
}

void CubeNModel::setDefaultSize(int size) {
    size = std::clamp(size, cube::nxn::kMinSize, cube::nxn::kMaxSize);
    if (size == default_size_) {
        return;
    }
    default_size_ = size;
    rebuild();
}

void CubeNModel::rebuild() {
    const QVariantMap sides = painting_.toMap();
    int size = 0;
    for (const Side& side : kSides) {
        const int found =
            side_length_of(sides.value(QString::fromLatin1(side.letter)).toList().size());
        if (found != 0 && (size == 0 || found == size)) {
            size = found;
        }
    }
    size_ = size != 0 ? size : default_size_;

    const auto n = static_cast<std::size_t>(size_);
    colours_.assign(6 * n * n, fill_);
    for (const auto& [face, letter] : kSides) {
        const QVariantList side = sides.value(QString::fromLatin1(letter)).toList();
        if (static_cast<std::size_t>(side.size()) != n * n) {
            continue;
        }
        for (int row = 0; row < size_; ++row) {
            for (int col = 0; col < size_; ++col) {
                const auto [r, c] = storage_of(face, size_, row, col);
                const auto painted = side.at((row * size_) + col).value<QColor>();
                colours_.at((static_cast<std::size_t>(face) * n * n) +
                            (static_cast<std::size_t>(r) * n) + static_cast<std::size_t>(c)) =
                    painted.isValid() ? painted : fill_;
            }
        }
    }
    linear_.clear();
    linear_.reserve(colours_.size());
    for (const QColor& colour : colours_) {
        linear_.push_back(linear_of(colour));
    }
    linear_body_ = linear_of(body_);
    ++revision_;
    emit changed();
}

void CubeNModel::turn(int axis, int first, int last, int quarterTurns) {
    const int quarters = ((quarterTurns % 4) + 4) % 4;
    if (quarters == 0 || axis < 0 || axis > 2) {
        return;
    }
    const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(size_);
    first = std::clamp(first, 0, size_ - 1);
    last = std::clamp(last, first, size_ - 1);
    for (int layer = first; layer <= last; ++layer) {
        cube::nxn::SliceTurn slice;
        slice.axis = static_cast<cube::nxn::Axis>(axis);
        slice.layer = layer;
        slice.turns = quarters;
        const cube::nxn::Permutation& moved = geometry.turn(slice);
        std::vector<QColor> next(colours_.size());
        for (std::size_t to = 0; to < next.size(); ++to) {
            next.at(to) = colours_.at(moved.at(to));
        }
        colours_ = std::move(next);
    }
    linear_.clear();
    for (const QColor& colour : colours_) {
        linear_.push_back(linear_of(colour));
    }
    ++revision_;
    emit changed();
}

QVariantList CubeNModel::stickerPlacements() const {
    const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(size_);
    const double middle = 0.5 * (size_ - 1);
    QVariantList out;
    out.reserve(static_cast<qsizetype>(colours_.size()));
    for (std::size_t facelet = 0; facelet < colours_.size(); ++facelet) {
        const auto position = geometry.position(facelet);
        const cube::nxn::StickerPlace place =
            cube::nxn::sticker_place(size_, position.face, position.row, position.col);
        const std::array<int, 3> facing = cube::nxn::direction_vector(place.direction);
        QVariantMap entry;
        entry.insert(QStringLiteral("color"), colours_.at(facelet).name());
        QVariantList cubie;
        QVariantList facing_list;
        for (std::size_t k = 0; k < 3; ++k) {
            cubie.append(place.cubie.at(k) - middle);
            facing_list.append(facing.at(k));
        }
        entry.insert(QStringLiteral("cubie"), cubie);
        entry.insert(QStringLiteral("facing"), facing_list);
        out.append(entry);
    }
    return out;
}
