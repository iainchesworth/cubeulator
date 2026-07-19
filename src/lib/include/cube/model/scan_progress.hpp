#pragma once

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>

#include <array>
#include <optional>

namespace cube::model {

// Tracks which of the 6 faces have been confirmed during a scan session.
// This is purely the shared scan-state contract cube::vision (producer) and
// the future cube::solver (consumer) both depend on - no capture or solving
// logic lives here.
class ScanProgress {
public:
    void set_face(Face face, std::array<Facelet, facelets_per_face> facelets) noexcept;

    [[nodiscard]] bool is_face_confirmed(Face face) const noexcept;
    [[nodiscard]] const FaceScan& face_scan(Face face) const noexcept;
    [[nodiscard]] int faces_confirmed_count() const noexcept;
    [[nodiscard]] bool is_complete() const noexcept;

    // The confirmed center color of each face, or nullopt where that face
    // hasn't been confirmed yet - the input detect_color_scheme() expects.
    [[nodiscard]] std::array<std::optional<FacetColor>, face_count> observed_centers() const noexcept;

private:
    std::array<FaceScan, face_count> faces_{};
};

}  // namespace cube::model
