#pragma once

// Reading a Cube3DView back out of its scene, for the tests that hold what is
// drawn to the solver's own cube.
//
// Nothing here trusts the view's bookkeeping: stickerPlacements() measures
// where each sticker node actually sits and which way it faces, and this
// turns those measurements into the solver's sides and indices by the layout
// documented in cube.hpp, restated here rather than borrowed from the code
// under test.
//
// Everything that reads a view takes the cube's size (3 unless it says
// otherwise): the cubies of an N x N x N cube sit at coordinates from -(N-1)/2
// to (N-1)/2, whole numbers for an odd size and halves for an even one, and
// the layout is the same one cube::nxn::NCube numbers its stickers by, which
// for N = 3 is the solver's.

#include <catch2/catch_test_macros.hpp>

#include <cube/nxn/cube.hpp>
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

// Where an N x N x N cube keeps the sticker at cubie (x, y, z) of a side, as
// row * N + col. With h = (N - 1) / 2: U and D are indexed (row = z + h,
// col = x + h), L and R (row = h - y, col = z + h), F and B (row = h - y,
// col = x + h). Were this restatement wrong for a side, no turn that carries
// stickers on or off that side could match the library's (the test beside it
// holds it to every turn of every layer of every size).
[[nodiscard]] inline int facelet_index(int size, solver::Face face, double x, double y, double z) {
    const double h = 0.5 * (size - 1);
    const auto at = [h](double coordinate) {
        return static_cast<int>(std::lround(coordinate + h));
    };
    switch (face) {
        case solver::Face::U:
        case solver::Face::D:
            return (at(z) * size) + at(x);
        case solver::Face::L:
        case solver::Face::R:
            return ((size - 1 - at(y)) * size) + at(z);
        case solver::Face::F:
        case solver::Face::B:
            return ((size - 1 - at(y)) * size) + at(x);
    }
    return 0;
}

// The same for a 3x3, whose cubies are at -1, 0 and 1.
[[nodiscard]] inline int solver_index(solver::Face face, int x, int y, int z) {
    return facelet_index(3, face, x, y, z);
}

// The stickers as the cube would hold them: [side][row * N + col] -> colour.
using Sides = std::array<std::vector<QColor>, 6>;

// The stickers of a view of an N x N x N cube. Fails unless every sticker sits
// squarely on a cubie position, facing squarely outwards, and all 6 * N * N
// places are filled once each.
[[nodiscard]] inline Sides read(QObject* view, int size = 3) {
    const double h = 0.5 * (size - 1);
    const auto count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    Sides sides;
    std::array<std::vector<int>, 6> seen;
    for (std::size_t side = 0; side < sides.size(); ++side) {
        sides.at(side).resize(count);
        seen.at(side).assign(count, 0);
    }
    for (const Placement& placement : placements(view)) {
        const std::optional<solver::Face> side = side_facing(placement.facing);
        REQUIRE(side.has_value());
        const std::array<double, 3> at = {placement.cubie.x, placement.cubie.y, placement.cubie.z};
        for (const double coordinate : at) {
            REQUIRE(std::abs((coordinate + h) - std::round(coordinate + h)) < 0.01);
            REQUIRE(std::abs(coordinate) <= h + 0.01);
        }
        const auto index =
            static_cast<std::size_t>(facelet_index(size, *side, at.at(0), at.at(1), at.at(2)));
        sides.at(static_cast<std::size_t>(*side)).at(index) = placement.colour;
        ++seen.at(static_cast<std::size_t>(*side)).at(index);
    }
    for (const auto& side : seen) {
        for (const int times : side) {
            REQUIRE(times == 1);
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
[[nodiscard]] inline bool shows(QObject* view, const nxn::NCube& cube,
                                const std::function<QColor(solver::Color)>& paint) {
    const int size = cube.size();
    const Sides sides = read(view, size);
    bool same = true;
    for (const solver::Face face : solver::kAllFaces) {
        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                const auto index = static_cast<std::size_t>((row * size) + col);
                const QColor seen = sides.at(static_cast<std::size_t>(face)).at(index);
                if (seen != paint(cube.at(face, row, col))) {
                    UNSCOPED_INFO("side " << letter(face).toStdString() << " index " << index
                                          << " shows " << seen.name().toStdString()
                                          << ", the solver has "
                                          << paint(cube.at(face, row, col)).name().toStdString());
                    same = false;
                }
            }
        }
    }
    return same;
}

[[nodiscard]] inline bool shows(QObject* view, const solver::Cube& cube,
                                const std::function<QColor(solver::Color)>& paint) {
    return shows(view, nxn::NCube::from_cube3(cube), paint);
}

// Which way each side faces in the scene, where the viewer looks along -z
// with +y up and +x to the right.
[[nodiscard]] inline Vec side_direction(QObject* view, solver::Face side) {
    QVariant result;
    REQUIRE(QMetaObject::invokeMethod(view, "sideDirections", Q_RETURN_ARG(QVariant, result)));
    return vec(result.toMap().value(letter(side)));
}

}  // namespace cube::test
