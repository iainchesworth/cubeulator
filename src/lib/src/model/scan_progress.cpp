#include <cube/model/scan_progress.hpp>

namespace cube::model {

void ScanProgress::set_face(Face face, std::array<Facelet, facelets_per_face> facelets) noexcept {
    auto& scan = faces_[static_cast<std::size_t>(face)];
    scan.facelets = facelets;
    scan.confirmed = true;
}

bool ScanProgress::is_face_confirmed(Face face) const noexcept {
    return faces_[static_cast<std::size_t>(face)].confirmed;
}

const FaceScan& ScanProgress::face_scan(Face face) const noexcept {
    return faces_[static_cast<std::size_t>(face)];
}

int ScanProgress::faces_confirmed_count() const noexcept {
    int count = 0;
    for (const auto& scan : faces_) {
        if (scan.confirmed) {
            ++count;
        }
    }
    return count;
}

bool ScanProgress::is_complete() const noexcept {
    return faces_confirmed_count() == static_cast<int>(face_count);
}

std::array<std::optional<FacetColor>, face_count> ScanProgress::observed_centers() const noexcept {
    std::array<std::optional<FacetColor>, face_count> centers{};
    for (std::size_t i = 0; i < face_count; ++i) {
        const auto& scan = faces_[i];
        const auto center_color = scan.facelets[center_facelet_index].color;
        if (scan.confirmed && center_color != FacetColor::unknown) {
            centers[i] = center_color;
        }
    }
    return centers;
}

}  // namespace cube::model
