#pragma once

// Reading a Cube3DView back out of its scene, for the tests that hold what is
// drawn to the solver's own cube.
//
// Nothing here trusts the view's bookkeeping: stickerPlacements() measures
// where each sticker node actually sits and which way it faces, and this
// turns those measurements into the solver's sides and indices by the layout
// documented in cube.hpp, restated here rather than borrowed from the code
// under test.

#include <catch2/catch_test_macros.hpp>

#include <cube/solver/cube.hpp>
#include <cube/solver/outside_view.hpp>

#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJSEngine>
#include <QJSValue>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace cube::test {

struct Vec {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

[[nodiscard]] inline Vec vec(const QVariant& list) {
    const QVariantList parts = list.toList();
    return {parts.at(0).toDouble(), parts.at(1).toDouble(), parts.at(2).toDouble()};
}

[[nodiscard]] inline double dot(const Vec& a, const Vec& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
[[nodiscard]] inline Vec operator-(const Vec& a, const Vec& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] inline QString letter(solver::Face face) {
    switch (face) {
        case solver::Face::U:
            return QStringLiteral("U");
        case solver::Face::D:
            return QStringLiteral("D");
        case solver::Face::L:
            return QStringLiteral("L");
        case solver::Face::R:
            return QStringLiteral("R");
        case solver::Face::F:
            return QStringLiteral("F");
        case solver::Face::B:
            return QStringLiteral("B");
    }
    return {};
}

[[nodiscard]] inline std::vector<solver::Move> all_moves() {
    std::vector<solver::Move> moves;
    for (const solver::Face face : solver::kAllFaces) {
        for (int turns = 1; turns <= 3; ++turns) {
            moves.push_back(solver::Move{face, turns});
        }
    }
    return moves;
}

// Runs the event loop until `done` or the time is up: animations and queued
// calls only happen while it runs.
template <typename Done>
[[nodiscard]] bool wait_for(Done done, int milliseconds = 5000) {
    QElapsedTimer waited;
    waited.start();
    while (!done() && waited.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
}

struct Placement {
    QColor colour;
    Vec cubie;   // which cubie position the sticker is on: x L to R, y D to U, z B to F
    Vec facing;  // which way it faces
};

// Every sticker of a Cube3DView as it sits in the scene.
[[nodiscard]] inline std::vector<Placement> placements(QObject* view) {
    QVariant result;
    REQUIRE(QMetaObject::invokeMethod(view, "stickerPlacements", Q_RETURN_ARG(QVariant, result)));
    std::vector<Placement> out;
    for (const QVariant& entry : result.toList()) {
        const QVariantMap map = entry.toMap();
        out.push_back({QColor(map.value("color").toString()), vec(map.value("cubie")),
                       vec(map.value("facing"))});
    }
    return out;
}

// The side a sticker is on, when it faces squarely along an axis.
[[nodiscard]] inline std::optional<solver::Face> side_facing(const Vec& facing) {
    constexpr double kSquare = 0.999;
    if (facing.x > kSquare) {
        return solver::Face::R;
    }
    if (facing.x < -kSquare) {
        return solver::Face::L;
    }
    if (facing.y > kSquare) {
        return solver::Face::U;
    }
    if (facing.y < -kSquare) {
        return solver::Face::D;
    }
    if (facing.z > kSquare) {
        return solver::Face::F;
    }
    if (facing.z < -kSquare) {
        return solver::Face::B;
    }
    return std::nullopt;
}

// Where the solver keeps the sticker at cubie (x, y, z) of a side: U and D
// are indexed (row = z + 1, col = x + 1), L and R (row = 1 - y, col = z + 1),
// F and B (row = 1 - y, col = x + 1). Were this restatement wrong for a
// side, no turn that carries stickers on or off that side could match
// Cube::apply.
[[nodiscard]] inline int solver_index(solver::Face face, int x, int y, int z) {
    switch (face) {
        case solver::Face::U:
        case solver::Face::D:
            return (z + 1) * 3 + (x + 1);
        case solver::Face::L:
        case solver::Face::R:
            return (1 - y) * 3 + (z + 1);
        case solver::Face::F:
        case solver::Face::B:
            return (1 - y) * 3 + (x + 1);
    }
    return 0;
}

// The stickers as the solver would hold them: [side][index] -> colour. Fails
// unless every sticker sits squarely on a cubie position, facing squarely
// outwards, and all 54 places are filled once each.
[[nodiscard]] inline std::array<std::array<QColor, 9>, 6> read(QObject* view) {
    std::array<std::array<QColor, 9>, 6> sides{};
    std::array<std::array<int, 9>, 6> seen{};
    for (const Placement& placement : placements(view)) {
        const std::optional<solver::Face> side = side_facing(placement.facing);
        REQUIRE(side.has_value());
        const std::array<double, 3> at = {placement.cubie.x, placement.cubie.y, placement.cubie.z};
        std::array<int, 3> cell{};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            cell[axis] = static_cast<int>(std::lround(at[axis]));
            REQUIRE(std::abs(at[axis] - cell[axis]) < 0.01);
            REQUIRE(std::abs(cell[axis]) <= 1);
        }
        const auto index = static_cast<std::size_t>(solver_index(*side, cell[0], cell[1], cell[2]));
        sides[static_cast<std::size_t>(*side)][index] = placement.colour;
        ++seen[static_cast<std::size_t>(*side)][index];
    }
    for (const auto& side : seen) {
        for (const int count : side) {
            REQUIRE(count == 1);
        }
    }
    return sides;
}

// The painting a screen hands to Cube3DView.faceletColors for `cube`: each side
// as seen from outside, each colour drawn as `paint` gives it.
//
// It is a fresh JavaScript object on every call, because that is what the
// screens really assign (SolveScreen.painted(), ScanScreen.scanFaceletColors),
// and because the view's contract needs it: a new painting - even of the cube as
// it already stands - puts every layer back. A QVariantMap would be compared by
// value, and since Qt 6.12 an assignment from C++ that equals the current value
// emits no change signal (Qt 6.8 did), so repainting an unchanged cube would
// leave a turned layer where it was. Assign it as QVariant::fromValue(...).
[[nodiscard]] inline QJSValue painting(QJSEngine& engine, const solver::Cube& cube,
                                       const std::function<QColor(solver::Color)>& paint) {
    QJSValue sides = engine.newObject();
    for (const solver::Face face : solver::kAllFaces) {
        QJSValue colours = engine.newArray();
        quint32 index = 0;
        for (const solver::Color colour : solver::outside_view(cube, face)) {
            colours.setProperty(index++, paint(colour).name());
        }
        sides.setProperty(letter(face), colours);
    }
    return sides;
}

// True when the view shows `cube`, each colour drawn as `paint` gives it.
[[nodiscard]] inline bool shows(QObject* view, const solver::Cube& cube,
                                const std::function<QColor(solver::Color)>& paint) {
    const auto sides = read(view);
    bool same = true;
    for (const solver::Face face : solver::kAllFaces) {
        for (int index = 0; index < 9; ++index) {
            const QColor seen =
                sides[static_cast<std::size_t>(face)][static_cast<std::size_t>(index)];
            if (seen != paint(cube.at(face, index))) {
                UNSCOPED_INFO("side " << letter(face).toStdString() << " index " << index
                                      << " shows " << seen.name().toStdString()
                                      << ", the solver has "
                                      << paint(cube.at(face, index)).name().toStdString());
                same = false;
            }
        }
    }
    return same;
}

// Which way each side faces in the scene, where the viewer looks along -z
// with +y up and +x to the right.
[[nodiscard]] inline Vec side_direction(QObject* view, solver::Face side) {
    QVariant result;
    REQUIRE(QMetaObject::invokeMethod(view, "sideDirections", Q_RETURN_ARG(QVariant, result)));
    return vec(result.toMap().value(letter(side)));
}

}  // namespace cube::test
