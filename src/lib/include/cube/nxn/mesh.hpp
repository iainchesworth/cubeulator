#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <cube/nxn/geometry.hpp>

// The mesh of an N x N x N cube for the layer-lift renderer: no Qt and no GPU,
// so that it can be checked on its own (tests/unit/nxn/mesh_test.cpp) and so
// that the view built on it is a thin layer over numbers.
//
// The cube is drawn as the faces of its cubies that can be seen, nothing more.
// A cubie face on the outside of the cube is a black quad the size of the cubie
// face with a coloured sticker over it, a little smaller and a little proud of
// it (the black round each sticker is the gap between stickers). While layers
// are lifted the cube is drawn as two meshes - the layers, and the rest - and
// the cubie faces where they part are black quads too, so that the cut shows as
// solid when the layers are turned part of the way. Nothing is built per cubie
// and nothing is bound per sticker, so a cube costs three scene objects at
// every size (docs/performance.md, "The 3D view").
//
// Everything is in cubie spacings, the cube centred on the origin: a cubie is a
// unit cube, the cube reaches N/2 each way. Axes are the library's: x runs L to
// R, y D to U, z B to F.

namespace cube::nxn {

struct Rgba {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;

    friend constexpr bool operator==(const Rgba&, const Rgba&) = default;
};

// Which cubies a mesh holds.
enum class MeshPart : std::uint8_t {
    Whole,  // every cubie
    Rest,   // every cubie but those of the lifted layers
    Layer,  // only the lifted layers
};

// The layers that are lifted: along `axis`, layers `first` to `last` inclusive,
// counted from the negative end like SliceTurn::layer. A wide move lifts several
// at once.
struct LiftedLayers {
    Axis axis = Axis::X;
    int first = 0;
    int last = 0;

    friend constexpr bool operator==(const LiftedLayers&, const LiftedLayers&) = default;
};

// Interleaved vertices and the triangles between them.
struct Mesh {
    // position (x y z), normal (x y z), colour (r g b a)
    static constexpr std::size_t kFloatsPerVertex = 10;
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::size_t quads() const noexcept { return indices.size() / 6; }
};

// Where a sticker is: the cubie it is on, as indices counted from the negative
// end of each axis, and the way it faces (0..5 = +x, -x, +y, -y, +z, -z).
struct StickerPlace {
    std::array<int, 3> cubie{};
    int direction = 0;

    friend constexpr bool operator==(const StickerPlace&, const StickerPlace&) = default;
};

// The unit vector a direction (0..5 = +x, -x, +y, -y, +z, -z) points along.
[[nodiscard]] constexpr std::array<int, 3> direction_vector(int direction) noexcept {
    std::array<int, 3> step{};
    step.at(static_cast<std::size_t>(direction / 2)) = direction % 2 == 0 ? 1 : -1;
    return step;
}

// The sticker at (row, col) of `face`, numbered as Geometry numbers them.
[[nodiscard]] StickerPlace sticker_place(int size, Face face, int row, int col);

// The mesh of `part` of the cube whose stickers are coloured `stickers` (one
// per facelet, numbered face * N^2 + row * N + col) and whose plastic is `body`.
// `lifted` names the layers that are lifted and means nothing for Whole.
[[nodiscard]] Mesh build_mesh(int size, std::span<const Rgba> stickers, const Rgba& body,
                              MeshPart part, LiftedLayers lifted = {});

}  // namespace cube::nxn
