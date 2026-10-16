// The helper that reads a view of a cube back out of its scene
// (cube_view_reading.hpp), for a cube of any size: that the layout it restates
// is the library's - every turn of every layer of every size from 2 to 9 moves
// the stickers it says it does - and that a view whose stickers sit where a cube
// of that size has them reads back as that cube.
//
// There is no view of a 4x4 or a 9x9 to read yet, so the view here is a
// QML object that answers stickerPlacements() from a table the test builds.
#include <catch2/catch_test_macros.hpp>

#include "cube_view_reading.hpp"

#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/permutation.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>

#include <QColor>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace {

using cube::nxn::Axis;
using cube::nxn::Geometry;
using cube::nxn::NCube;
using cube::nxn::SliceTurn;
using cube::solver::Color;
using cube::solver::Face;

// A sticker as the scene has it: the cubie it is on, doubled so that the
// halves of an even size are whole numbers, and the way it faces.
struct Sticker {
    std::array<int, 3> cubie{};
    std::array<int, 3> facing{};

    friend bool operator==(const Sticker&, const Sticker&) = default;
};

[[nodiscard]] std::array<int, 3> facing_of(Face face) {
    switch (face) {
        case Face::R:
            return {1, 0, 0};
        case Face::L:
            return {-1, 0, 0};
        case Face::U:
            return {0, 1, 0};
        case Face::D:
            return {0, -1, 0};
        case Face::F:
            return {0, 0, 1};
        case Face::B:
            return {0, 0, -1};
    }
    return {};
}

// Every sticker of a cube of `size`, by the facelet number the library gives it,
// found by walking the cubies of each side and asking the helper for its index.
[[nodiscard]] std::vector<Sticker> stickers_of(int size) {
    std::vector<Sticker> found(static_cast<std::size_t>(6 * size * size));
    std::vector<bool> filled(found.size(), false);
    for (const Face face : cube::solver::kAllFaces) {
        const std::array<int, 3> facing = facing_of(face);
        // The cubie coordinates along each axis, doubled: from -(N-1) to N-1 by 2.
        std::array<std::vector<int>, 3> along;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (facing.at(axis) != 0) {
                along.at(axis) = {facing.at(axis) * (size - 1)};
                continue;
            }
            for (int at = -(size - 1); at <= size - 1; at += 2) {
                along.at(axis).push_back(at);
            }
        }
        for (const int x : along.at(0)) {
            for (const int y : along.at(1)) {
                for (const int z : along.at(2)) {
                    const int index =
                        cube::test::facelet_index(size, face, 0.5 * x, 0.5 * y, 0.5 * z);
                    const auto facelet =
                        (static_cast<std::size_t>(face) * static_cast<std::size_t>(size * size)) +
                        static_cast<std::size_t>(index);
                    REQUIRE(facelet < found.size());
                    REQUIRE_FALSE(filled.at(facelet));
                    filled.at(facelet) = true;
                    found.at(facelet) = Sticker{.cubie = {x, y, z}, .facing = facing};
                }
            }
        }
    }
    REQUIRE(std::ranges::all_of(filled, [](bool done) { return done; }));
    return found;
}

// A quarter turn clockwise as seen from the positive end of `axis`, which is
// -90 degrees about it.
[[nodiscard]] std::array<int, 3> quarter_turned(Axis axis, const std::array<int, 3>& v) {
    switch (axis) {
        case Axis::X:
            return {v.at(0), v.at(2), -v.at(1)};
        case Axis::Y:
            return {-v.at(2), v.at(1), v.at(0)};
        case Axis::Z:
            return {v.at(1), -v.at(0), v.at(2)};
    }
    return v;
}

// How many stickers a turn of the library puts somewhere other than where
// turning them with their layer, in the scene's coordinates, would.
[[nodiscard]] int misplaced_by(const std::vector<Sticker>& stickers, int size, SliceTurn turn) {
    const cube::nxn::Permutation& moved = Geometry::of(size).turn(turn);
    int wrong = 0;
    for (std::size_t to = 0; to < stickers.size(); ++to) {
        // The sticker that ends up at `to` came from `moved[to]`; turned with its
        // layer, it must be where `to` is.
        Sticker turned = stickers.at(moved.at(to));
        const int along = turned.cubie.at(static_cast<std::size_t>(turn.axis));
        if (along == (2 * turn.layer) - (size - 1)) {
            for (int quarter = 0; quarter < turn.turns; ++quarter) {
                turned.cubie = quarter_turned(turn.axis, turned.cubie);
                turned.facing = quarter_turned(turn.axis, turned.facing);
            }
        }
        wrong += turned == stickers.at(to) ? 0 : 1;
    }
    return wrong;
}

}  // namespace

TEST_CASE("the layout the reader restates holds every sticker once, at every size",
          "[cube_view_reading]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const std::vector<Sticker> stickers = stickers_of(size);
        CHECK(stickers.size() == 6 * Geometry::of(size).face_area());
    }
}

TEST_CASE("a 3x3's index is the one it always was", "[cube_view_reading]") {
    // U and D by (z + 1, x + 1), L and R by (1 - y, z + 1), F and B by (1 - y, x + 1).
    CHECK(cube::test::solver_index(Face::U, -1, 1, -1) == 0);
    CHECK(cube::test::solver_index(Face::U, 1, 1, 1) == 8);
    CHECK(cube::test::solver_index(Face::D, 1, -1, -1) == 2);
    CHECK(cube::test::solver_index(Face::L, -1, 1, -1) == 0);
    CHECK(cube::test::solver_index(Face::R, 1, -1, 1) == 8);
    CHECK(cube::test::solver_index(Face::F, -1, 1, 1) == 0);
    CHECK(cube::test::solver_index(Face::B, 1, -1, -1) == 8);
}

TEST_CASE("every turn of every layer moves the stickers the library says it does",
          "[cube_view_reading]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const std::vector<Sticker> stickers = stickers_of(size);
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            for (int layer = 0; layer < size; ++layer) {
                for (int turns = 1; turns <= 3; ++turns) {
                    CAPTURE(static_cast<int>(axis), layer, turns);
                    CHECK(misplaced_by(stickers, size,
                                       SliceTurn{.axis = axis, .layer = layer, .turns = turns}) ==
                          0);
                }
            }
        }
    }
}

namespace {

// A view that answers stickerPlacements() with the table it is given: the
// placements of a cube of `size`, coloured as `cube` is.
class FakeView {
public:
    FakeView() {
        QQmlComponent component(&engine_);
        component.setData(
            "import QtQml\n"
            "QtObject {\n"
            "    property var table: []\n"
            "    function stickerPlacements() { return table }\n"
            "}\n",
            QUrl());
        object_.reset(component.create());
        REQUIRE(object_ != nullptr);
    }

    [[nodiscard]] QObject* object() const { return object_.get(); }

    void show(const NCube& cube, const std::function<QColor(Color)>& paint) {
        const int size = cube.size();
        const std::vector<Sticker> stickers = stickers_of(size);
        QVariantList table;
        for (std::size_t facelet = 0; facelet < stickers.size(); ++facelet) {
            const Sticker& sticker = stickers.at(facelet);
            const auto to_list = [](const std::array<int, 3>& v, double scale) {
                return QVariantList{scale * v.at(0), scale * v.at(1), scale * v.at(2)};
            };
            QVariantMap placement;
            placement.insert("color", paint(cube.facelet(facelet)).name());
            placement.insert("cubie", to_list(sticker.cubie, 0.5));
            placement.insert("facing", to_list(sticker.facing, 1.0));
            table.append(placement);
        }
        object_->setProperty("table", table);
    }

private:
    QQmlEngine engine_;
    std::unique_ptr<QObject> object_;
};

[[nodiscard]] QColor paint(Color colour) {
    switch (colour) {
        case Color::White:
            return QColor::fromRgb(255, 255, 255);
        case Color::Yellow:
            return QColor::fromRgb(255, 255, 0);
        case Color::Red:
            return QColor::fromRgb(255, 0, 0);
        case Color::Orange:
            return QColor::fromRgb(255, 128, 0);
        case Color::Green:
            return QColor::fromRgb(0, 255, 0);
        case Color::Blue:
            return QColor::fromRgb(0, 0, 255);
    }
    return {};
}

}  // namespace

TEST_CASE("a view of a cube of any size reads back as that cube", "[cube_view_reading]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const NCube cube = cube::nxn::random_scramble(
            {.size = size, .seed = static_cast<unsigned>(size), .moves = 40});
        FakeView view;
        view.show(cube, paint);
        CHECK(cube::test::shows(view.object(), cube, paint));

        // A different cube is not what it shows.
        NCube other = cube;
        other.apply(cube::nxn::LayerMove{.face = Face::R, .depth = 0, .turns = 1});
        CHECK_FALSE(cube::test::shows(view.object(), other, paint));
    }
}

TEST_CASE("a 3x3 view reads back through the 3x3 overload", "[cube_view_reading]") {
    const cube::solver::Cube cube = cube::solver::random_scramble(3U);
    FakeView view;
    view.show(NCube::from_cube3(cube), paint);
    CHECK(cube::test::shows(view.object(), cube, paint));
}
