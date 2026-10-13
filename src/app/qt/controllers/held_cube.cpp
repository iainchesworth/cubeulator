#include "controllers/held_cube.hpp"

#include <QMatrix3x3>

#include <array>
#include <cstddef>

namespace cube::app {

namespace {

using cube::solver::Face;
using cube::solver::Place;

[[nodiscard]] QString side_letter(Face face) {
    switch (face) {
        case Face::U:
            return QStringLiteral("U");
        case Face::D:
            return QStringLiteral("D");
        case Face::L:
            return QStringLiteral("L");
        case Face::R:
            return QStringLiteral("R");
        case Face::F:
            return QStringLiteral("F");
        case Face::B:
            return QStringLiteral("B");
    }
    return QStringLiteral("U");
}

[[nodiscard]] QString place_name(Place place) {
    switch (place) {
        case Place::Top:
            return QStringLiteral("top");
        case Place::Bottom:
            return QStringLiteral("bottom");
        case Place::Left:
            return QStringLiteral("left");
        case Place::Right:
            return QStringLiteral("right");
        case Place::Front:
            return QStringLiteral("front");
        case Place::Back:
            return QStringLiteral("back");
    }
    return QStringLiteral("top");
}

}  // namespace

HeldCube::HeldCube(QObject* parent) : QObject(parent) {}

void HeldCube::setHold(const cube::solver::Hold& hold) {
    if (!cube::solver::is_valid(hold) || hold == hold_) {
        return;
    }
    hold_ = hold;
    emit holdChanged();
}

QString HeldCube::up() const {
    return side_letter(hold_.up);
}
QString HeldCube::front() const {
    return side_letter(hold_.front);
}

QVariantMap HeldCube::places() const {
    QVariantMap places;
    for (const Face side : cube::solver::kAllFaces) {
        places.insert(side_letter(side), place_name(cube::solver::place_of(hold_, side)));
    }
    return places;
}

QQuaternion HeldCube::rotation() const {
    const std::array<int, 9> turn = cube::solver::rotation(hold_);
    std::array<float, 9> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = static_cast<float>(turn[i]);
    }
    return QQuaternion::fromRotationMatrix(QMatrix3x3(values.data()));
}

}  // namespace cube::app
