#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

#include <cube/nxn/geometry.hpp>
#include <cube/nxn/mesh.hpp>
#include <cube/nxn/permutation.hpp>
#include <cube/solver/cube.hpp>

// The mesh the layer-lift view draws, held to the library: every sticker is a
// quad where the library says that sticker is, a lifted layer is the stickers
// a turn of that layer moves, and the layers and the rest together are the
// cube and its cuts. A mesh is checked without a GPU by giving every sticker a
// colour of its own - its facelet number - and reading the quads back.

namespace {

using cube::nxn::Axis;
using cube::nxn::Face;
using cube::nxn::Geometry;
using cube::nxn::LiftedLayers;
using cube::nxn::Mesh;
using cube::nxn::MeshPart;
using cube::nxn::Rgba;
using cube::nxn::SliceTurn;
using cube::nxn::StickerPlace;

constexpr float kIdScale = 1024.0F;

// Every sticker its own colour: red carries the facelet number plus one, so that
// no sticker is red zero. The plastic is the only green.
std::vector<Rgba> numbered(int size) {
    std::vector<Rgba> stickers;
    const auto count = static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 6;
    stickers.reserve(count);
    for (std::size_t id = 0; id < count; ++id) {
        stickers.push_back(Rgba{.r = static_cast<float>(id + 1) / kIdScale, .g = 0.0F, .b = 0.0F});
    }
    return stickers;
}

constexpr Rgba kBody{.r = 0.0F, .g = 1.0F, .b = 0.0F, .a = 1.0F};

// A quad read back out of a mesh.
struct Quad {
    std::array<float, 3> centre{};
    std::array<float, 3> normal{};
    bool plastic = false;
    std::size_t facelet = 0;  // of a sticker
};

Quad quad_at(const Mesh& mesh, std::size_t q) {
    constexpr std::size_t kStride = Mesh::kFloatsPerVertex;
    Quad quad;
    std::set<std::uint32_t> corners;
    for (std::size_t k = 0; k < 6; ++k) {
        corners.insert(mesh.indices.at((6 * q) + k));
    }
    REQUIRE(corners.size() == 4);
    for (const std::uint32_t corner : corners) {
        const std::size_t at = static_cast<std::size_t>(corner) * kStride;
        REQUIRE(at + kStride <= mesh.vertices.size());
        for (std::size_t k = 0; k < 3; ++k) {
            quad.centre.at(k) += mesh.vertices.at(at + k) / 4.0F;
            quad.normal.at(k) = mesh.vertices.at(at + 3 + k);
        }
        quad.plastic = mesh.vertices.at(at + 7) > 0.5F;
        if (!quad.plastic) {
            quad.facelet =
                static_cast<std::size_t>(std::lround(mesh.vertices.at(at + 6) * kIdScale)) - 1;
        }
    }
    return quad;
}

std::vector<Quad> quads_of(const Mesh& mesh) {
    REQUIRE(mesh.indices.size() % 6 == 0);
    std::vector<Quad> quads;
    quads.reserve(mesh.quads());
    for (std::size_t q = 0; q < mesh.quads(); ++q) {
        quads.push_back(quad_at(mesh, q));
    }
    return quads;
}

std::set<std::size_t> stickers_in(const std::vector<Quad>& quads) {
    std::set<std::size_t> found;
    for (const Quad& quad : quads) {
        if (!quad.plastic) {
            CHECK(found.insert(quad.facelet).second);  // each sticker once
        }
    }
    return found;
}

std::size_t plastic_in(const std::vector<Quad>& quads) {
    return static_cast<std::size_t>(
        std::ranges::count_if(quads, [](const Quad& quad) { return quad.plastic; }));
}

// A quarter turn clockwise as seen from the positive end of `axis`: -90 degrees about it.
std::array<int, 3> quarter_turned(Axis axis, const std::array<int, 3>& v) {
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

// The cubie in doubled coordinates, -(N-1) to N-1 by 2, so that halves are whole.
std::array<int, 3> doubled(int size, const StickerPlace& place) {
    std::array<int, 3> at{};
    for (std::size_t k = 0; k < 3; ++k) {
        at.at(k) = (2 * place.cubie.at(k)) - (size - 1);
    }
    return at;
}

StickerPlace place_of(int size, std::size_t facelet) {
    const auto position = Geometry::of(size).position(facelet);
    return cube::nxn::sticker_place(size, position.face, position.row, position.col);
}

// Every sticker's place on a cube of `size`, by facelet number.
std::vector<StickerPlace> places_of(int size) {
    std::vector<StickerPlace> places;
    const std::size_t count = Geometry::of(size).facelet_count();
    places.reserve(count);
    for (std::size_t facelet = 0; facelet < count; ++facelet) {
        places.push_back(place_of(size, facelet));
    }
    return places;
}

// How many stickers a turn of the library puts somewhere other than where
// turning them with their layer, in space, would.
int misplaced_by(int size, const std::vector<StickerPlace>& places, SliceTurn turn) {
    const cube::nxn::Permutation& moved = Geometry::of(size).turn(turn);
    int wrong = 0;
    for (std::size_t to = 0; to < places.size(); ++to) {
        // What lands at `to` came from moved[to], turned with its layer.
        const StickerPlace& from = places.at(moved.at(to));
        std::array<int, 3> cubie = doubled(size, from);
        std::array<int, 3> facing = cube::nxn::direction_vector(from.direction);
        if (cubie.at(static_cast<std::size_t>(turn.axis)) == (2 * turn.layer) - (size - 1)) {
            for (int quarter = 0; quarter < turn.turns; ++quarter) {
                cubie = quarter_turned(turn.axis, cubie);
                facing = quarter_turned(turn.axis, facing);
            }
        }
        const StickerPlace& there = places.at(to);
        const bool right =
            cubie == doubled(size, there) && facing == cube::nxn::direction_vector(there.direction);
        wrong += right ? 0 : 1;
    }
    return wrong;
}

// The facelets whose stickers sit in the layers `lifted` names.
std::set<std::size_t> in_lifted_layers(const std::vector<StickerPlace>& places,
                                       const LiftedLayers& lifted) {
    std::set<std::size_t> found;
    for (std::size_t facelet = 0; facelet < places.size(); ++facelet) {
        const int along = places.at(facelet).cubie.at(static_cast<std::size_t>(lifted.axis));
        if (along >= lifted.first && along <= lifted.last) {
            found.insert(facelet);
        }
    }
    return found;
}

// Every single layer of every axis, and the blocks of adjacent layers: all of them
// on a small cube, the ones that matter on a big one.
std::vector<LiftedLayers> lifts_for(int size) {
    std::vector<LiftedLayers> lifts;
    for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
        for (int first = 0; first < size; ++first) {
            for (int last = first; last < size; ++last) {
                const bool wanted =
                    first == last || size <= 5 || last == first + 1 || last == size - 1;
                if (wanted) {
                    lifts.push_back(LiftedLayers{.axis = axis, .first = first, .last = last});
                }
            }
        }
    }
    return lifts;
}

// The place of a sticker as a quad has it: where its cubie is and a little proud of
// the plastic along the way it faces.
bool sits_where_the_library_puts_it(int size, const Quad& quad) {
    const StickerPlace place = place_of(size, quad.facelet);
    const std::array<int, 3> normal = cube::nxn::direction_vector(place.direction);
    bool right = true;
    for (std::size_t k = 0; k < 3; ++k) {
        const float expected = static_cast<float>(place.cubie.at(k)) -
                               (static_cast<float>(size - 1) / 2.0F) +
                               ((0.5F + 0.01F) * static_cast<float>(normal.at(k)));
        right = right && std::abs(quad.centre.at(k) - expected) < 1e-4F &&
                quad.normal.at(k) == static_cast<float>(normal.at(k));
    }
    return right;
}

// What the mesh of a part of the cube has: the stickers in it, and every quad.
struct Part {
    std::vector<Quad> quads;
    std::set<std::size_t> stickers;
};

void read_part(Part& found, int size, MeshPart part, const LiftedLayers& lifted) {
    found.quads = quads_of(cube::nxn::build_mesh(size, numbered(size), kBody, part, lifted));
    found.stickers = stickers_in(found.quads);
}

}  // namespace

TEST_CASE("a direction is the unit vector along its axis", "[nxn][mesh]") {
    using Vector = std::array<int, 3>;
    CHECK(cube::nxn::direction_vector(0) == Vector{1, 0, 0});
    CHECK(cube::nxn::direction_vector(1) == Vector{-1, 0, 0});
    CHECK(cube::nxn::direction_vector(2) == Vector{0, 1, 0});
    CHECK(cube::nxn::direction_vector(3) == Vector{0, -1, 0});
    CHECK(cube::nxn::direction_vector(4) == Vector{0, 0, 1});
    CHECK(cube::nxn::direction_vector(5) == Vector{0, 0, -1});
}

TEST_CASE("the whole cube is a plastic quad and a sticker for every sticker", "[nxn][mesh]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const Mesh mesh = cube::nxn::build_mesh(size, numbered(size), kBody, MeshPart::Whole);
        const auto n = static_cast<std::size_t>(size);
        CHECK(mesh.quads() == 12 * n * n);
        CHECK(mesh.vertices.size() == 4 * mesh.quads() * Mesh::kFloatsPerVertex);
        CHECK(std::ranges::all_of(mesh.indices, [&mesh](std::uint32_t index) {
            return index < mesh.vertices.size() / Mesh::kFloatsPerVertex;
        }));

        const std::vector<Quad> quads = quads_of(mesh);
        CHECK(plastic_in(quads) == 6 * n * n);
        CHECK(stickers_in(quads).size() == 6 * n * n);

        // Each sticker is where the library puts it, standing a little off the plastic.
        for (const Quad& quad : quads) {
            CHECK((quad.plastic || sits_where_the_library_puts_it(size, quad)));
        }
    }
}

TEST_CASE("a sticker's place is where the library's turns carry it", "[nxn][mesh]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const std::vector<StickerPlace> places = places_of(size);

        // No two stickers in one place.
        std::set<std::pair<std::array<int, 3>, int>> distinct;
        for (const StickerPlace& place : places) {
            distinct.insert({place.cubie, place.direction});
        }
        CHECK(distinct.size() == places.size());

        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            for (int layer = 0; layer < size; ++layer) {
                for (int turns = 1; turns <= 3; ++turns) {
                    CAPTURE(static_cast<int>(axis), layer, turns);
                    CHECK(misplaced_by(size, places,
                                       SliceTurn{.axis = axis, .layer = layer, .turns = turns}) ==
                          0);
                }
            }
        }
    }
}

TEST_CASE("lifted layers and the rest are the cube and its cuts", "[nxn][mesh]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const auto n = static_cast<std::size_t>(size);
        const std::vector<StickerPlace> places = places_of(size);

        for (const LiftedLayers& lifted : lifts_for(size)) {
            CAPTURE(static_cast<int>(lifted.axis), lifted.first, lifted.last);
            Part layers;
            Part rest;
            read_part(layers, size, MeshPart::Layer, lifted);
            read_part(rest, size, MeshPart::Rest, lifted);

            // The layers hold exactly the stickers in them, the rest the others.
            CHECK(layers.stickers == in_lifted_layers(places, lifted));
            std::set<std::size_t> everything = layers.stickers;
            everything.insert(rest.stickers.begin(), rest.stickers.end());
            CHECK(everything.size() == 6 * n * n);
            CHECK(layers.stickers.size() + rest.stickers.size() == 6 * n * n);

            // Where they part there is a cut on each side, N x N a boundary: the two
            // meshes together are the cube and its cuts.
            const int boundaries = (lifted.first > 0 ? 1 : 0) + (lifted.last < size - 1 ? 1 : 0);
            CHECK(layers.quads.size() + rest.quads.size() ==
                  (12 * n * n) + (2 * static_cast<std::size_t>(boundaries) * n * n));
        }
    }
}

TEST_CASE("what a turn of one layer moves is what the mesh lifts", "[nxn][mesh]") {
    for (int size = cube::nxn::kMinSize; size <= cube::nxn::kMaxSize; ++size) {
        CAPTURE(size);
        const std::vector<StickerPlace> places = places_of(size);
        for (const Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
            for (int layer = 0; layer < size; ++layer) {
                CAPTURE(static_cast<int>(axis), layer);
                std::set<std::size_t> moved;
                for (const std::uint16_t f : cube::nxn::support(Geometry::of(size).turn(
                         SliceTurn{.axis = axis, .layer = layer, .turns = 1}))) {
                    moved.insert(f);
                }
                const std::set<std::size_t> lifted = in_lifted_layers(
                    places, LiftedLayers{.axis = axis, .first = layer, .last = layer});
                // The lift holds everything the turn moves, and of what it holds only
                // the middle of the face the turn is about stays where it is.
                CHECK(std::ranges::includes(lifted, moved));
                CHECK(lifted.size() - moved.size() <= 1);
            }
        }
    }
}
