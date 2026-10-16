#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <cube/nxn/cube.hpp>

// The mesh of an N x N x N cube for the layer-lift renderer: no Qt, so that it
// can be checked on its own.
//
// The cube is drawn as the faces of its cubies that can be seen, nothing more.
// A cubie face on the outside of the cube is a black quad the size of the
// cubie face with a coloured sticker over it, a little smaller and a little
// proud of it (the black round each sticker is the gap between stickers). While
// a layer is lifted the cube is drawn as two meshes - the layer, and the rest -
// and the cubie faces where they part are black quads too, so that the cut shows
// as solid when the layer is turned part of the way.
//
// Everything is in cubie spacings, the cube centred on the origin: a cubie is a
// unit cube, the cube reaches N/2 each way. Axes are the library's: x runs L to
// R, y D to U, z B to F.

namespace cube3d {

// Which cubies a mesh holds.
enum class Part : std::uint8_t {
    Whole,  // every cubie
    Rest,   // every cubie but those of the lifted layer
    Layer,  // only the lifted layer
};

struct Rgba {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

struct Palette {
    // By cube::solver::Color.
    std::array<Rgba, 6> sticker{};
    Rgba body{};
};

struct Mesh {
    // Interleaved: position (x y z), normal (x y z), colour (r g b a).
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
};

[[nodiscard]] StickerPlace place_of(int size, cube::nxn::Face face, int row, int col);

// The mesh of `part` of `cube`. `axis` (0 = x, 1 = y, 2 = z) and `layer` (counted
// from the negative end) name the lifted layer, and mean nothing for Whole.
[[nodiscard]] Mesh build_mesh(const cube::nxn::NCube& cube, Part part, int axis, int layer,
                              const Palette& palette);

// The default colours, those of Theme.qml's faces (as they come out of the sRGB
// transfer, since the material treats vertex colour as linear).
[[nodiscard]] Palette default_palette();

}  // namespace cube3d
