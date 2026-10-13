#include <cube/vision/pipeline/face_projection.hpp>

#include <cube/profiling.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace cube::vision::pipeline {

std::vector<ProjectedFace> derive_faces(const cv::Vec3d& rvec, const cv::Vec3d& tvec,
                                        const cv::Matx33d& camera_matrix, double min_frontality) {
    CUBE_ZONE_SCOPED_N("vision.derive_faces");
    std::vector<ProjectedFace> out;
    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);

    const auto to_camera = [&](const cv::Point3f& p) {
        const cv::Matx31d c =
            rotation * cv::Matx31d(static_cast<double>(p.x), static_cast<double>(p.y),
                                   static_cast<double>(p.z)) +
            cv::Matx31d(tvec[0], tvec[1], tvec[2]);
        return cv::Vec3d(c(0, 0), c(1, 0), c(2, 0));
    };
    const auto project = [&](const cv::Point3f& p) {
        const cv::Vec3d c = to_camera(p);
        return cv::Point2f(
            static_cast<float>(camera_matrix(0, 0) * c[0] / c[2] + camera_matrix(0, 2)),
            static_cast<float>(camera_matrix(1, 1) * c[1] / c[2] + camera_matrix(1, 2)));
    };

    for (int face = 0; face < 6; ++face) {
        const int face_axis = face / 2;
        const float sign = (face % 2 == 0) ? 1.0F : -1.0F;
        cv::Point3f normal(0, 0, 0);
        if (face_axis == 0) {
            normal.x = sign;
        } else if (face_axis == 1) {
            normal.y = sign;
        } else {
            normal.z = sign;
        }
        // Frontality: cosine between the outward normal and the ray from
        // the face centre back to the camera. Positive = front-facing.
        const cv::Matx31d normal_c =
            rotation * cv::Matx31d(static_cast<double>(normal.x), static_cast<double>(normal.y),
                                   static_cast<double>(normal.z));
        const cv::Vec3d centre_c =
            to_camera(normal);  // face centre = its unit normal (half-edge 1)
        const double distance = cv::norm(centre_c);
        if (distance <= 1e-9 || centre_c[2] <= 0.0) {
            continue;
        }
        const double frontality = -(normal_c(0, 0) * centre_c[0] + normal_c(1, 0) * centre_c[1] +
                                    normal_c(2, 0) * centre_c[2]) /
                                  distance;
        if (frontality < min_frontality) {
            continue;
        }

        ProjectedFace projected;
        projected.face = face;
        projected.frontality = frontality;
        // Outside-view (u, v): col rightward, row downward exactly as a
        // camera facing the face sees it, i.e. e_u x e_v = -outward
        // normal on EVERY face. The cyclic pair (a+1, a+2) has that
        // handedness only on negative faces; positive faces swap it.
        // One uniform convention is what lets assembly's centre-pinned
        // handedness correction be a fixed per-Face table
        // (scan_assembly.cpp) instead of depending on which body face
        // the gauge happened to assign.
        const int axis_u = sign > 0.0F ? (face_axis + 2) % 3 : (face_axis + 1) % 3;
        const int axis_v = sign > 0.0F ? (face_axis + 1) % 3 : (face_axis + 2) % 3;
        const auto on_face = [&](float u, float v) {
            std::array<float, 3> p{};
            p[static_cast<std::size_t>(face_axis)] = sign;
            p[static_cast<std::size_t>(axis_u)] = u;
            p[static_cast<std::size_t>(axis_v)] = v;
            return cv::Point3f(p[0], p[1], p[2]);
        };
        projected.quad[0] = project(on_face(-1.0F, -1.0F));  // tl
        projected.quad[1] = project(on_face(+1.0F, -1.0F));  // tr
        projected.quad[2] = project(on_face(+1.0F, +1.0F));  // br
        projected.quad[3] = project(on_face(-1.0F, +1.0F));  // bl
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const float u = (static_cast<float>(col) - 1.0F) * (2.0F / 3.0F);
                const float v = (static_cast<float>(row) - 1.0F) * (2.0F / 3.0F);
                projected.centres[static_cast<std::size_t>(row * 3 + col)] = project(on_face(u, v));
                // Cell quads span the STICKER, not the full pitch (see
                // kStickerFraction in the header): gap and neighbour
                // pixels stay out of the sample quad by geometry.
                constexpr float kInset =
                    (1.0F - kStickerFraction) * (1.0F / 3.0F);  // per side, model units
                const float u0 = -1.0F + static_cast<float>(col) * (2.0F / 3.0F) + kInset;
                const float v0 = -1.0F + static_cast<float>(row) * (2.0F / 3.0F) + kInset;
                const float span = 2.0F / 3.0F - 2.0F * kInset;
                auto& cell = projected.cells[static_cast<std::size_t>(row * 3 + col)];
                cell[0] = project(on_face(u0, v0));
                cell[1] = project(on_face(u0 + span, v0));
                cell[2] = project(on_face(u0 + span, v0 + span));
                cell[3] = project(on_face(u0, v0 + span));
            }
        }
        out.push_back(projected);
    }

    std::sort(out.begin(), out.end(), [](const ProjectedFace& a, const ProjectedFace& b) {
        return a.frontality > b.frontality;
    });
    return out;
}

cv::Mat rectify_face(const cv::Mat& bgr_frame, const ProjectedFace& face, int size,
                     cv::Mat* homography_out) {
    CUBE_ZONE_SCOPED_N("vision.rectify_face");
    if (bgr_frame.empty() || size < 3) {
        return {};
    }
    // Degenerate quad (grazing face, garbage pose): a near-zero area
    // makes the homography explode; refuse rather than return noise.
    const double area =
        std::abs(cv::contourArea(std::vector<cv::Point2f>(face.quad.begin(), face.quad.end())));
    if (area < 16.0) {
        return {};
    }
    const std::array<cv::Point2f, 4> destination{
        cv::Point2f(0.0F, 0.0F), cv::Point2f(static_cast<float>(size), 0.0F),
        cv::Point2f(static_cast<float>(size), static_cast<float>(size)),
        cv::Point2f(0.0F, static_cast<float>(size))};
    const cv::Mat homography = cv::getPerspectiveTransform(face.quad.data(), destination.data());
    if (homography_out != nullptr) {
        *homography_out = homography;
    }
    cv::Mat rectified;
    // BORDER_REPLICATE: a quad clipped by the frame edge stretches its
    // border pixels instead of injecting black, which downstream colour
    // sampling would read as a real (dark) observation.
    cv::warpPerspective(bgr_frame, rectified, homography, cv::Size(size, size), cv::INTER_LINEAR,
                        cv::BORDER_REPLICATE);
    return rectified;
}

RefinedGrid refine_cell_grid(const cv::Mat& rectified_bgr) {
    CUBE_ZONE_SCOPED_N("vision.refine_cell_grid");
    RefinedGrid grid;
    if (rectified_bgr.empty() || rectified_bgr.rows != rectified_bgr.cols ||
        rectified_bgr.rows < 24) {
        return grid;
    }
    const int size = rectified_bgr.rows;
    const double pitch = static_cast<double>(size) / 3.0;
    const double reach = 0.2 * pitch;  // the never-re-seat clamp
    cv::Mat grey;
    cv::cvtColor(rectified_bgr, grey, cv::COLOR_BGR2GRAY);

    // 1-D profiles: column means (for x boundaries) and row means (y).
    cv::Mat column_profile;
    cv::Mat row_profile;
    cv::reduce(grey, column_profile, 0, cv::REDUCE_AVG, CV_64F);
    cv::reduce(grey, row_profile, 1, cv::REDUCE_AVG, CV_64F);

    const auto refine_internal = [&](const cv::Mat& profile, double nominal) {
        const auto value_at = [&](int index) {
            return profile.total() > static_cast<std::size_t>(index) && index >= 0
                       ? profile.at<double>(index)
                       : 1e9;
        };
        const int lo = static_cast<int>(std::ceil(nominal - reach));
        const int hi = static_cast<int>(std::floor(nominal + reach));
        int best = -1;
        double best_value = 1e9;
        for (int i = lo; i <= hi; ++i) {
            const double v = value_at(i);
            if (v < best_value) {
                best_value = v;
                best = i;
            }
        }
        if (best < 0) {
            return nominal;
        }
        // Contrast gate: a gap line is DARK against its surround; a flat
        // profile (uniform sticker, washed-out gap) keeps the nominal
        // grid rather than chasing noise.
        const double surround = 0.5 * (value_at(static_cast<int>(nominal - 0.45 * pitch)) +
                                       value_at(static_cast<int>(nominal + 0.45 * pitch)));
        if (surround - best_value < 8.0) {
            return nominal;
        }
        // A close-range gap is a FLAT trough several pixels wide, and
        // "first minimum + parabola" lands on its leading edge, not its
        // centre (measured: 1.5 px off on a 5 px trough). Take the
        // midpoint of the contiguous near-minimum run; the parabola adds
        // sub-pixel only when the trough is sharp enough for it to mean
        // anything.
        int run_lo = best;
        int run_hi = best;
        while (value_at(run_lo - 1) <= best_value + 2.0 && run_lo - 1 >= lo) {
            --run_lo;
        }
        while (value_at(run_hi + 1) <= best_value + 2.0 && run_hi + 1 <= hi) {
            ++run_hi;
        }
        double refined = 0.5 * (run_lo + run_hi);
        if (run_hi == run_lo) {
            const double left = value_at(best - 1);
            const double right = value_at(best + 1);
            const double denominator = left - 2.0 * best_value + right;
            if (std::abs(denominator) > 1e-9) {
                const double delta = 0.5 * (left - right) / denominator;
                if (std::abs(delta) <= 1.0) {
                    refined += delta;
                }
            }
        }
        return std::clamp(refined, nominal - reach, nominal + reach);
    };

    grid.x = {0.0, refine_internal(column_profile, pitch),
              refine_internal(column_profile, 2.0 * pitch), static_cast<double>(size)};
    grid.y = {0.0, refine_internal(row_profile, pitch), refine_internal(row_profile, 2.0 * pitch),
              static_cast<double>(size)};
    grid.valid = true;
    return grid;
}

std::array<std::array<cv::Point2f, 4>, 9> cells_from_grid(const RefinedGrid& grid,
                                                          const cv::Mat& homography) {
    std::array<std::array<cv::Point2f, 4>, 9> cells{};
    const cv::Mat inverse = homography.inv();
    std::vector<cv::Point2f> rectified_points;
    rectified_points.reserve(36);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const double x0 = grid.x[static_cast<std::size_t>(col)];
            const double x1 = grid.x[static_cast<std::size_t>(col + 1)];
            const double y0 = grid.y[static_cast<std::size_t>(row)];
            const double y1 = grid.y[static_cast<std::size_t>(row + 1)];
            const double inset_x = (1.0 - static_cast<double>(kStickerFraction)) * 0.5 * (x1 - x0);
            const double inset_y = (1.0 - static_cast<double>(kStickerFraction)) * 0.5 * (y1 - y0);
            rectified_points.emplace_back(static_cast<float>(x0 + inset_x),
                                          static_cast<float>(y0 + inset_y));
            rectified_points.emplace_back(static_cast<float>(x1 - inset_x),
                                          static_cast<float>(y0 + inset_y));
            rectified_points.emplace_back(static_cast<float>(x1 - inset_x),
                                          static_cast<float>(y1 - inset_y));
            rectified_points.emplace_back(static_cast<float>(x0 + inset_x),
                                          static_cast<float>(y1 - inset_y));
        }
    }
    std::vector<cv::Point2f> image_points;
    cv::perspectiveTransform(rectified_points, image_points, inverse);
    for (std::size_t cell = 0; cell < 9; ++cell) {
        for (std::size_t corner = 0; corner < 4; ++corner) {
            cells[cell][corner] = image_points[cell * 4 + corner];
        }
    }
    return cells;
}

}  // namespace cube::vision::pipeline
