#include <cube/solver/scan_conversion.hpp>

#include <array>
#include <cstddef>
#include <optional>

namespace cube::solver {

namespace {

constexpr Face to_solver_face(model::Face face) noexcept {
    switch (face) {
        case model::Face::U:
            return Face::U;
        case model::Face::R:
            return Face::R;
        case model::Face::F:
            return Face::F;
        case model::Face::D:
            return Face::D;
        case model::Face::L:
            return Face::L;
        case model::Face::B:
            return Face::B;
    }
    return Face::U;
}

constexpr std::optional<Color> to_solver_color(model::FacetColor color) noexcept {
    switch (color) {
        case model::FacetColor::white:
            return Color::White;
        case model::FacetColor::yellow:
            return Color::Yellow;
        case model::FacetColor::red:
            return Color::Red;
        case model::FacetColor::orange:
            return Color::Orange;
        case model::FacetColor::green:
            return Color::Green;
        case model::FacetColor::blue:
            return Color::Blue;
        case model::FacetColor::unknown:
            return std::nullopt;
    }
    return std::nullopt;
}

// Per-face camera-row-major -> solver-row-major facelet index permutation,
// derived from cube.cpp's position_of() under the holding convention
// documented in scan_conversion.hpp. Every table is a self-inverse
// (involution) - scan_conversion_test.cpp's round-trip test reuses these
// same tables to go the other way.
using Permutation = std::array<int, 9>;
constexpr Permutation kIdentity = {0, 1, 2, 3, 4, 5, 6, 7, 8};
constexpr Permutation kRotate180 = {8, 7, 6, 5, 4, 3, 2, 1, 0};
constexpr Permutation kFlipVertical = {6, 7, 8, 3, 4, 5, 0, 1, 2};
constexpr Permutation kFlipHorizontal = {2, 1, 0, 5, 4, 3, 8, 7, 6};

constexpr const Permutation& camera_to_solver_permutation(model::Face face) noexcept {
    switch (face) {
        case model::Face::U:
            return kRotate180;
        case model::Face::D:
            return kFlipVertical;
        case model::Face::F:
            return kIdentity;
        case model::Face::L:
            return kIdentity;
        case model::Face::B:
            return kFlipHorizontal;
        case model::Face::R:
            return kFlipHorizontal;
    }
    return kIdentity;
}

}  // namespace

Result<Cube> cube_from_scan(const model::ScanProgress& scan) noexcept {
    if (!scan.is_complete()) {
        return std::unexpected(CubeError::face_capture_incomplete);
    }

    std::array<std::array<Color, 9>, 6> facelets{};
    for (std::size_t i = 0; i < model::face_count; ++i) {
        const auto model_face = static_cast<model::Face>(i);
        auto& dest = facelets[static_cast<std::size_t>(to_solver_face(model_face))];
        const auto& scan_facelets = scan.face_scan(model_face).facelets;
        const auto& permutation = camera_to_solver_permutation(model_face);

        for (std::size_t cam_index = 0; cam_index < model::facelets_per_face; ++cam_index) {
            const auto color = to_solver_color(scan_facelets[cam_index].color);
            if (!color) {
                return std::unexpected(CubeError::invalid_cube_state);
            }
            dest[static_cast<std::size_t>(permutation[cam_index])] = *color;
        }
    }

    return Cube::from_facelets(facelets);
}

}  // namespace cube::solver
