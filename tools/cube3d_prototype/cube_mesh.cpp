#include "cube_mesh.hpp"

#include <algorithm>
#include <cmath>

namespace cube3d {

namespace {

using cube::nxn::Face;

constexpr float kStickerHalf = 0.43F;  // of a cubie face 0.5 on a side: the 0.86 of the app's view
constexpr float kStickerLift = 0.01F;  // how far a sticker stands off the black beneath it

// The six directions, in the order StickerPlace::direction counts them.
constexpr std::array<std::array<int, 3>, 6> kDirections = {{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
}};

// Two tangents for each direction, chosen so that u x v is the direction: a quad
// walked (-u -v), (+u -v), (+u +v), (-u +v) then faces outwards.
constexpr std::array<std::array<std::array<int, 3>, 2>, 6> kTangents = {{
    {{{0, 1, 0}, {0, 0, 1}}},
    {{{0, 0, 1}, {0, 1, 0}}},
    {{{0, 0, 1}, {1, 0, 0}}},
    {{{1, 0, 0}, {0, 0, 1}}},
    {{{1, 0, 0}, {0, 1, 0}}},
    {{{0, 1, 0}, {1, 0, 0}}},
}};

[[nodiscard]] std::size_t flat(int size, int x, int y, int z) {
    return static_cast<std::size_t>((((x * size) + y) * size) + z);
}

class Builder {
public:
    Builder(Mesh& mesh, int size) : mesh_(mesh), size_(size) {}

    // A square of half-width `half` on the face of the cubie at `cubie` that
    // looks along `direction`, `lift` off the face.
    void quad(const std::array<int, 3>& cubie, int direction, float half, float lift,
              const Rgba& colour) {
        const auto& normal = kDirections.at(static_cast<std::size_t>(direction));
        const auto& tangent = kTangents.at(static_cast<std::size_t>(direction));
        const float centre_offset = 0.5F + lift;
        std::array<float, 3> centre{};
        for (std::size_t k = 0; k < 3; ++k) {
            centre.at(k) = static_cast<float>(cubie.at(k)) - (static_cast<float>(size_ - 1) / 2.0F) +
                           (centre_offset * static_cast<float>(normal.at(k)));
        }
        const auto first = static_cast<std::uint32_t>(mesh_.vertices.size() /
                                                      Mesh::kFloatsPerVertex);
        constexpr std::array<std::array<float, 2>, 4> kCorners = {
            {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}}};
        for (const auto& corner : kCorners) {
            for (std::size_t k = 0; k < 3; ++k) {
                mesh_.vertices.push_back(
                    centre.at(k) +
                    (half * ((corner.at(0) * static_cast<float>(tangent.at(0).at(k))) +
                             (corner.at(1) * static_cast<float>(tangent.at(1).at(k))))));
            }
            for (std::size_t k = 0; k < 3; ++k) {
                mesh_.vertices.push_back(static_cast<float>(normal.at(k)));
            }
            mesh_.vertices.insert(mesh_.vertices.end(),
                                  {colour.r, colour.g, colour.b, colour.a});
        }
        mesh_.indices.insert(mesh_.indices.end(),
                             {first, first + 1, first + 2, first, first + 2, first + 3});
    }

private:
    Mesh& mesh_;
    int size_;
};

}  // namespace

StickerPlace place_of(int size, Face face, int row, int col) {
    const int last = size - 1;
    // geometry.cpp's sticker_at, in cubie indices rather than doubled
    // coordinates: U and D run x with col and z with row; R and L run y down
    // with row and z with col; F and B run x with col and y down with row.
    switch (face) {
        case Face::U:
            return {.cubie = {col, last, row}, .direction = 2};
        case Face::D:
            return {.cubie = {col, 0, row}, .direction = 3};
        case Face::R:
            return {.cubie = {last, last - row, col}, .direction = 0};
        case Face::L:
            return {.cubie = {0, last - row, col}, .direction = 1};
        case Face::F:
            return {.cubie = {col, last - row, last}, .direction = 4};
        case Face::B:
            return {.cubie = {col, last - row, 0}, .direction = 5};
    }
    return {};
}

Mesh build_mesh(const cube::nxn::NCube& cube, Part part, int axis, int layer,
                const Palette& palette) {
    const int n = cube.size();
    // The colour of every sticker, by the cubie face it is on.
    std::vector<std::int8_t> colour(static_cast<std::size_t>(n) * static_cast<std::size_t>(n) *
                                        static_cast<std::size_t>(n) * 6,
                                    -1);
    for (const Face face : cube::solver::kAllFaces) {
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                const StickerPlace at = place_of(n, face, row, col);
                colour.at((flat(n, at.cubie.at(0), at.cubie.at(1), at.cubie.at(2)) * 6) +
                          static_cast<std::size_t>(at.direction)) =
                    static_cast<std::int8_t>(cube.at(face, row, col));
            }
        }
    }

    const auto in_layer = [&](int x, int y, int z) {
        const int along = axis == 0 ? x : (axis == 1 ? y : z);
        return along == layer;
    };
    const auto held = [&](int x, int y, int z) {
        switch (part) {
            case Part::Whole:
                return true;
            case Part::Layer:
                return in_layer(x, y, z);
            case Part::Rest:
                return !in_layer(x, y, z);
        }
        return true;
    };

    Mesh mesh;
    Builder builder(mesh, n);
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < n; ++y) {
            for (int z = 0; z < n; ++z) {
                if (!held(x, y, z)) {
                    continue;
                }
                for (int d = 0; d < 6; ++d) {
                    const auto& step = kDirections.at(static_cast<std::size_t>(d));
                    const int nx = x + step.at(0);
                    const int ny = y + step.at(1);
                    const int nz = z + step.at(2);
                    const bool outside = nx < 0 || ny < 0 || nz < 0 || nx >= n || ny >= n || nz >= n;
                    if (outside) {
                        builder.quad({x, y, z}, d, 0.5F, 0.0F, palette.body);
                        const auto shown = colour.at((flat(n, x, y, z) * 6) + static_cast<std::size_t>(d));
                        builder.quad({x, y, z}, d, kStickerHalf, kStickerLift,
                                     palette.sticker.at(static_cast<std::size_t>(shown)));
                    } else if (!held(nx, ny, nz)) {
                        builder.quad({x, y, z}, d, 0.5F, 0.0F, palette.body);  // the cut
                    }
                }
            }
        }
    }
    return mesh;
}

Palette default_palette() {
    // Theme.qml's face colours, linearised.
    const auto lin = [](float c) {
        return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F);
    };
    const auto rgb = [&](int hex) {
        return Rgba{lin(static_cast<float>((hex >> 16) & 0xFF) / 255.0F),
                    lin(static_cast<float>((hex >> 8) & 0xFF) / 255.0F),
                    lin(static_cast<float>(hex & 0xFF) / 255.0F), 1.0F};
    };
    Palette palette;
    using Color = cube::solver::Color;
    palette.sticker.at(static_cast<std::size_t>(Color::White)) = rgb(0xF2F2F2);
    palette.sticker.at(static_cast<std::size_t>(Color::Yellow)) = rgb(0xFFD500);
    palette.sticker.at(static_cast<std::size_t>(Color::Green)) = rgb(0x009B48);
    palette.sticker.at(static_cast<std::size_t>(Color::Blue)) = rgb(0x0046AD);
    palette.sticker.at(static_cast<std::size_t>(Color::Orange)) = rgb(0xFF5800);
    palette.sticker.at(static_cast<std::size_t>(Color::Red)) = rgb(0xB71234);
    palette.body = rgb(0x101010);
    return palette;
}

}  // namespace cube3d
