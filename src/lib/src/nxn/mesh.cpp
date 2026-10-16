#include <cube/nxn/mesh.hpp>

#include <cube/solver/cube.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace cube::nxn {

namespace {

// Of a cubie face 0.5 on a side: 0.86 of it, the sticker inset the 3x3 view has.
constexpr float kStickerHalf = 0.43F;
// How far a sticker stands off the black beneath it.
constexpr float kStickerLift = 0.01F;
// Half a cubie face, which is how big the plastic is.
constexpr float kPlasticHalf = 0.5F;

constexpr int kDirectionCount = 6;

// Two tangents of a direction, chosen so that u x v is the direction: a quad
// walked (-u -v), (+u -v), (+u +v), (-u +v) then faces outwards. Along axis a the
// pair is the next axis and the one after it, the other way round at the negative
// end.
constexpr std::array<std::array<int, 3>, 2> tangents_of(int index) {
    const auto axis = static_cast<std::size_t>(index / 2);
    std::array<int, 3> next{};
    std::array<int, 3> after{};
    next.at((axis + 1) % 3) = 1;
    after.at((axis + 2) % 3) = 1;
    if (index % 2 == 0) {
        return {next, after};
    }
    return {after, next};
}

// The corners of a quad in its tangents, in the order they are walked.
constexpr std::array<float, 2> corner_of(int index) {
    return {index == 1 || index == 2 ? 1.0F : -1.0F, index >= 2 ? 1.0F : -1.0F};
}

[[nodiscard]] std::size_t slot(int size, const std::array<int, 3>& cubie, int direction) {
    const auto n = static_cast<std::size_t>(size);
    const std::size_t flat =
        (((static_cast<std::size_t>(cubie.at(0)) * n) + static_cast<std::size_t>(cubie.at(1))) *
         n) +
        static_cast<std::size_t>(cubie.at(2));
    return (flat * kDirectionCount) + static_cast<std::size_t>(direction);
}

// What a quad is: the face of the cubie that looks along `direction`, a square
// of half-width `half`, `lift` off the face.
struct QuadSpec {
    std::array<int, 3> cubie{};
    int direction = 0;
    float half = kPlasticHalf;
    float lift = 0.0F;
    Rgba colour{};
};

class Builder {
public:
    explicit Builder(int size) : size_(size) {}

    void quad(const QuadSpec& spec) {
        const std::array<int, 3> normal = direction_vector(spec.direction);
        const std::array<std::array<int, 3>, 2> tangent = tangents_of(spec.direction);
        const float centre_offset = kPlasticHalf + spec.lift;
        std::array<float, 3> centre{};
        for (std::size_t k = 0; k < 3; ++k) {
            centre.at(k) = static_cast<float>(spec.cubie.at(k)) -
                           (static_cast<float>(size_ - 1) / 2.0F) +
                           (centre_offset * static_cast<float>(normal.at(k)));
        }
        const auto first =
            static_cast<std::uint32_t>(mesh_.vertices.size() / Mesh::kFloatsPerVertex);
        for (int c = 0; c < 4; ++c) {
            const std::array<float, 2> corner = corner_of(c);
            for (std::size_t k = 0; k < 3; ++k) {
                mesh_.vertices.push_back(
                    centre.at(k) +
                    (spec.half * ((corner.at(0) * static_cast<float>(tangent.at(0).at(k))) +
                                  (corner.at(1) * static_cast<float>(tangent.at(1).at(k))))));
            }
            for (std::size_t k = 0; k < 3; ++k) {
                mesh_.vertices.push_back(static_cast<float>(normal.at(k)));
            }
            mesh_.vertices.insert(mesh_.vertices.end(),
                                  {spec.colour.r, spec.colour.g, spec.colour.b, spec.colour.a});
        }
        mesh_.indices.insert(mesh_.indices.end(),
                             {first, first + 1, first + 2, first, first + 2, first + 3});
    }

    [[nodiscard]] Mesh take() { return std::move(mesh_); }

private:
    Mesh mesh_;
    int size_;
};

// Which cubies a part of the cube holds.
class Holding {
public:
    Holding(MeshPart part, const LiftedLayers& lifted) : part_(part), lifted_(lifted) {}

    [[nodiscard]] bool operator()(const std::array<int, 3>& cubie) const {
        const int along = cubie.at(static_cast<std::size_t>(lifted_.axis));
        const bool in_lifted = along >= lifted_.first && along <= lifted_.last;
        switch (part_) {
            case MeshPart::Whole:
                return true;
            case MeshPart::Layer:
                return in_lifted;
            case MeshPart::Rest:
                return !in_lifted;
        }
        return true;
    }

private:
    MeshPart part_;
    LiftedLayers lifted_;
};

[[nodiscard]] bool outside(int size, const std::array<int, 3>& cubie) {
    return std::ranges::any_of(cubie, [size](int at) { return at < 0 || at >= size; });
}

// The sticker on every cubie face that has one, by cubie face: its facelet number.
[[nodiscard]] std::vector<std::int32_t> facelets_by_place(int size) {
    const auto n = static_cast<std::size_t>(size);
    std::vector<std::int32_t> facelet_at(n * n * n * kDirectionCount, -1);
    for (const Face face : solver::kAllFaces) {
        for (int row = 0; row < size; ++row) {
            for (int col = 0; col < size; ++col) {
                const StickerPlace at = sticker_place(size, face, row, col);
                facelet_at.at(slot(size, at.cubie, at.direction)) = static_cast<std::int32_t>(
                    (static_cast<std::size_t>(face) * n * n) + (static_cast<std::size_t>(row) * n) +
                    static_cast<std::size_t>(col));
            }
        }
    }
    return facelet_at;
}

}  // namespace

StickerPlace sticker_place(int size, Face face, int row, int col) {
    const int last = size - 1;
    // U and D run x with col and z with row; R and L run y down with row and z
    // with col; F and B run x with col and y down with row.
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

Mesh build_mesh(int size, std::span<const Rgba> stickers, const Rgba& body, MeshPart part,
                LiftedLayers lifted) {
    assert(stickers.size() == static_cast<std::size_t>(6 * size) * static_cast<std::size_t>(size));
    const std::vector<std::int32_t> facelet_at = facelets_by_place(size);
    const Holding held(part, lifted);

    Builder builder(size);
    const auto add_sticker_face = [&](const std::array<int, 3>& cubie, int direction) {
        builder.quad({.cubie = cubie, .direction = direction, .colour = body});
        const auto facelet =
            static_cast<std::ptrdiff_t>(facelet_at.at(slot(size, cubie, direction)));
        QuadSpec sticker;
        sticker.cubie = cubie;
        sticker.direction = direction;
        sticker.half = kStickerHalf;
        sticker.lift = kStickerLift;
        sticker.colour = *std::next(stickers.begin(), facelet);
        builder.quad(sticker);
    };
    for (int x = 0; x < size; ++x) {
        for (int y = 0; y < size; ++y) {
            for (int z = 0; z < size; ++z) {
                const std::array<int, 3> cubie = {x, y, z};
                if (!held(cubie)) {
                    continue;
                }
                for (int d = 0; d < kDirectionCount; ++d) {
                    const std::array<int, 3> step = direction_vector(d);
                    std::array<int, 3> next = cubie;
                    for (std::size_t k = 0; k < 3; ++k) {
                        next.at(k) += step.at(k);
                    }
                    if (outside(size, next)) {
                        add_sticker_face(cubie, d);
                    } else if (!held(next)) {
                        // The cut, where the part leaves off.
                        builder.quad({.cubie = cubie, .direction = d, .colour = body});
                    }
                }
            }
        }
    }
    return builder.take();
}

}  // namespace cube::nxn
