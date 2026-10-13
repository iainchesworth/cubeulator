#include <cube/vision/face_grid_fit.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace cube::vision::detail {

namespace {

[[nodiscard]] double det3(const std::array<double, 9>& m) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
}

}  // namespace

float estimate_face_pitch(const std::vector<cv::Point2f>& centers) {
    if (centers.size() < 2) {
        return 0.0F;
    }
    std::vector<float> nearest_distances;
    nearest_distances.reserve(centers.size());
    for (std::size_t i = 0; i < centers.size(); ++i) {
        float best = std::numeric_limits<float>::max();
        for (std::size_t j = 0; j < centers.size(); ++j) {
            if (i == j) {
                continue;
            }
            const cv::Point2f diff = centers[i] - centers[j];
            best = std::min(best, std::hypot(diff.x, diff.y));
        }
        nearest_distances.push_back(best);
    }
    const auto mid =
        nearest_distances.begin() + static_cast<std::ptrdiff_t>(nearest_distances.size() / 2);
    std::ranges::nth_element(nearest_distances, mid);
    return *mid;
}

SlotAssignment classify_to_slots(const std::vector<cv::Point2f>& centers, cv::Point2f anchor,
                                 cv::Point2f col_step, cv::Point2f row_step) {
    SlotAssignment assignment;
    assignment.slots.fill(-1);
    std::array<float, 9> residuals{};
    residuals.fill(std::numeric_limits<float>::max());

    const double det =
        static_cast<double>(col_step.x) * row_step.y - static_cast<double>(col_step.y) * row_step.x;
    if (std::abs(det) < 1e-6) {
        return assignment;
    }

    for (std::size_t idx = 0; idx < centers.size(); ++idx) {
        const cv::Point2f d = centers[idx] - anchor;
        const double u =
            (static_cast<double>(d.x) * row_step.y - static_cast<double>(d.y) * row_step.x) / det;
        const double v =
            (static_cast<double>(col_step.x) * d.y - static_cast<double>(col_step.y) * d.x) / det;
        const double ru = std::round(u);
        const double rv = std::round(v);
        if (ru < -1.0 || ru > 1.0 || rv < -1.0 || rv > 1.0) {
            continue;
        }
        const auto residual = static_cast<float>(std::hypot(u - ru, v - rv));
        if (residual > kMaxLatticeResidualRatio) {
            continue;
        }
        const auto slot =
            static_cast<std::size_t>((static_cast<int>(rv) + 1) * 3 + (static_cast<int>(ru) + 1));
        if (residual < residuals[slot]) {
            residuals[slot] = residual;
            assignment.slots[slot] = static_cast<int>(idx);
        }
    }
    for (const int idx : assignment.slots) {
        if (idx >= 0) {
            ++assignment.inlier_count;
        }
    }
    return assignment;
}

std::optional<FaceFit> refit_affine(const std::vector<cv::Point2f>& centers,
                                    const std::array<int, 9>& slots) {
    double n = 0;
    double su = 0;
    double sv = 0;
    double suu = 0;
    double suv = 0;
    double svv = 0;
    std::array<double, 3> bx{};
    std::array<double, 3> by{};

    for (int slot = 0; slot < 9; ++slot) {
        const int idx = slots[static_cast<std::size_t>(slot)];
        if (idx < 0) {
            continue;
        }
        const double u = (slot % 3) - 1;
        const double v = (slot / 3) - 1;
        const cv::Point2f p = centers[static_cast<std::size_t>(idx)];
        n += 1.0;
        su += u;
        sv += v;
        suu += u * u;
        suv += u * v;
        svv += v * v;
        bx[0] += p.x;
        bx[1] += u * p.x;
        bx[2] += v * p.x;
        by[0] += p.y;
        by[1] += u * p.y;
        by[2] += v * p.y;
    }

    const std::array<double, 9> m = {n, su, sv, su, suu, suv, sv, suv, svv};
    const double det = det3(m);
    if (std::abs(det) < 1e-9) {
        return std::nullopt;
    }

    const auto solve = [&](const std::array<double, 3>& rhs) {
        const std::array<double, 9> m0 = {rhs[0], su, sv, rhs[1], suu, suv, rhs[2], suv, svv};
        const std::array<double, 9> m1 = {n, rhs[0], sv, su, rhs[1], suv, sv, rhs[2], svv};
        const std::array<double, 9> m2 = {n, su, rhs[0], su, suu, rhs[1], sv, suv, rhs[2]};
        return std::array<double, 3>{det3(m0) / det, det3(m1) / det, det3(m2) / det};
    };
    const auto x = solve(bx);
    const auto y = solve(by);

    return FaceFit{
        .anchor = cv::Point2f(static_cast<float>(x[0]), static_cast<float>(y[0])),
        .col_step = cv::Point2f(static_cast<float>(x[1]), static_cast<float>(y[1])),
        .row_step = cv::Point2f(static_cast<float>(x[2]), static_cast<float>(y[2])),
    };
}

std::optional<FaceFit> fit_face_grid(const std::vector<cv::Point2f>& centers, cv::Point2f col_dir,
                                     cv::Point2f row_dir, std::size_t min_candidates,
                                     int min_inliers) {
    if (centers.size() < min_candidates) {
        return std::nullopt;
    }
    const float pitch = estimate_face_pitch(centers);
    if (pitch <= 0.0F) {
        return std::nullopt;
    }

    cv::Point2f anchor(0.0F, 0.0F);
    for (const auto& center : centers) {
        anchor += center;
    }
    anchor /= static_cast<float>(centers.size());

    const cv::Point2f col_step = pitch * col_dir;
    const cv::Point2f row_step = pitch * row_dir;
    const auto provisional = classify_to_slots(centers, anchor, col_step, row_step);
    if (provisional.inlier_count < 3) {
        return std::nullopt;
    }

    FaceFit fit{.anchor = anchor, .col_step = col_step, .row_step = row_step};
    if (auto refined = refit_affine(centers, provisional.slots)) {
        fit = *refined;
    }
    const auto final_assignment =
        classify_to_slots(centers, fit.anchor, fit.col_step, fit.row_step);
    if (final_assignment.inlier_count < min_inliers) {
        return std::nullopt;
    }
    fit.inlier_count = final_assignment.inlier_count;
    return fit;
}

}  // namespace cube::vision::detail
