#include <cube/vision/pipeline/pose_refiner.hpp>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace cube::vision::pipeline {

namespace {

// A model-space line segment to measure against, with the faces that
// decide its visibility. Face ids 0..5 = +x, -x, +y, -y, +z, -z
// (R, L, U, D, F, B in the Singmaster body frame). Outer edges carry
// their two adjacent faces; lattice lines carry one face and -1.
struct ModelEdge {
    cv::Point3f a;
    cv::Point3f b;
    int face_a;
    int face_b;
};

cv::Point3f face_normal(int face) {
    const float sign = (face % 2 == 0) ? 1.0F : -1.0F;
    switch (face / 2) {
        case 0:
            return {sign, 0.0F, 0.0F};
        case 1:
            return {0.0F, sign, 0.0F};
        default:
            return {0.0F, 0.0F, sign};
    }
}

// The measurement geometry, built once: 12 outer edges (every corner
// pair differing in exactly one coordinate) and 24 sticker-gap lattice
// lines (per face, two per tangent direction at +-1/3).
const std::vector<ModelEdge>& model_edges() {
    static const std::vector<ModelEdge> edges = [] {
        std::vector<ModelEdge> out;
        const auto face_of = [](float coord, int axis) {
            return axis * 2 + (coord > 0.0F ? 0 : 1);
        };
        // Outer edges: fix two axes at +-1, run the third from -1 to 1.
        for (int run_axis = 0; run_axis < 3; ++run_axis) {
            const int axis_1 = (run_axis + 1) % 3;
            const int axis_2 = (run_axis + 2) % 3;
            for (const float sign_1 : {1.0F, -1.0F}) {
                for (const float sign_2 : {1.0F, -1.0F}) {
                    std::array<float, 3> lo{};
                    lo[static_cast<std::size_t>(run_axis)] = -1.0F;
                    lo[static_cast<std::size_t>(axis_1)] = sign_1;
                    lo[static_cast<std::size_t>(axis_2)] = sign_2;
                    std::array<float, 3> hi = lo;
                    hi[static_cast<std::size_t>(run_axis)] = 1.0F;
                    out.push_back({{lo[0], lo[1], lo[2]},
                                   {hi[0], hi[1], hi[2]},
                                   face_of(sign_1, axis_1),
                                   face_of(sign_2, axis_2)});
                }
            }
        }
        // Lattice lines: on each face, the two internal gap lines per
        // tangent direction, at the surface.
        for (int face = 0; face < 6; ++face) {
            const int face_axis = face / 2;
            const float face_sign = (face % 2 == 0) ? 1.0F : -1.0F;
            const int axis_u = (face_axis + 1) % 3;
            const int axis_v = (face_axis + 2) % 3;
            for (const int fixed_axis : {axis_u, axis_v}) {
                const int run_axis = fixed_axis == axis_u ? axis_v : axis_u;
                for (const float fixed : {1.0F / 3.0F, -1.0F / 3.0F}) {
                    std::array<float, 3> lo{};
                    lo[static_cast<std::size_t>(face_axis)] = face_sign;
                    lo[static_cast<std::size_t>(fixed_axis)] = fixed;
                    lo[static_cast<std::size_t>(run_axis)] = -1.0F;
                    std::array<float, 3> hi = lo;
                    hi[static_cast<std::size_t>(run_axis)] = 1.0F;
                    out.push_back({{lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]}, face, -1});
                }
            }
        }
        return out;
    }();
    return edges;
}

double rotation_angle_deg(const cv::Vec3d& rvec_a, const cv::Vec3d& rvec_b) {
    cv::Matx33d rot_a;
    cv::Matx33d rot_b;
    cv::Rodrigues(rvec_a, rot_a);
    cv::Rodrigues(rvec_b, rot_b);
    cv::Vec3d delta;
    cv::Rodrigues(rot_a.t() * rot_b, delta);
    return cv::norm(delta) * 180.0 / CV_PI;
}

// One pose's worth of edge measurements.
struct Measurement {
    std::vector<cv::Point3f> model_points;
    std::vector<cv::Point2f> image_points;
    std::vector<double> displacements;
    std::vector<cv::Point2d> normals;
    int samples = 0;
};

Measurement measure_edges(const cv::Mat& grey, const cv::Vec3d& rvec, const cv::Vec3d& tvec,
                          const cv::Matx33d& camera_matrix, const PoseRefineParams& params) {
    Measurement m;
    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);

    const auto to_camera = [&](const cv::Point3f& p) {
        const cv::Matx31d c =
            rotation * cv::Matx31d(p.x, p.y, p.z) + cv::Matx31d(tvec[0], tvec[1], tvec[2]);
        return cv::Vec3d(c(0, 0), c(1, 0), c(2, 0));
    };
    const auto project = [&](const cv::Vec3d& camera_point) {
        return cv::Point2d(
            camera_matrix(0, 0) * camera_point[0] / camera_point[2] + camera_matrix(0, 2),
            camera_matrix(1, 1) * camera_point[1] / camera_point[2] + camera_matrix(1, 2));
    };
    const auto sample_intensity = [&](double x, double y) -> double {
        const int xi = static_cast<int>(std::lround(x));
        const int yi = static_cast<int>(std::lround(y));
        if (xi < 0 || yi < 0 || xi >= grey.cols || yi >= grey.rows) {
            return -1.0;
        }
        return static_cast<double>(grey.at<std::uint8_t>(yi, xi));
    };

    // Face visibility and frontality under this pose.
    std::array<bool, 6> front{};
    std::array<double, 6> frontality{};
    for (int face = 0; face < 6; ++face) {
        const cv::Point3f n = face_normal(face);
        const cv::Matx31d normal_c = rotation * cv::Matx31d(n.x, n.y, n.z);
        const cv::Vec3d centre_c = to_camera(n);  // face centre = its unit normal, half-edge 1
        const double along = normal_c(0, 0) * centre_c[0] + normal_c(1, 0) * centre_c[1] +
                             normal_c(2, 0) * centre_c[2];
        const double distance = cv::norm(centre_c);
        front[static_cast<std::size_t>(face)] = along < 0.0;
        frontality[static_cast<std::size_t>(face)] = distance > 1e-9 ? -along / distance : 0.0;
    }

    for (const auto& edge : model_edges()) {
        const bool is_lattice = edge.face_b < 0;
        if (is_lattice) {
            if (!front[static_cast<std::size_t>(edge.face_a)] ||
                frontality[static_cast<std::size_t>(edge.face_a)] < params.frontality_min) {
                continue;
            }
        } else if (!front[static_cast<std::size_t>(edge.face_a)] &&
                   !front[static_cast<std::size_t>(edge.face_b)]) {
            continue;  // fully back-facing outer edge
        }

        const cv::Vec3d camera_a = to_camera(edge.a);
        const cv::Vec3d camera_b = to_camera(edge.b);
        if (camera_a[2] <= 0.2 || camera_b[2] <= 0.2) {
            continue;
        }
        const cv::Point2d projected_a = project(camera_a);
        const cv::Point2d projected_b = project(camera_b);
        const cv::Point2d span = projected_b - projected_a;
        const double length_px = cv::norm(span);
        if (length_px < 2.0 * params.sample_spacing_px) {
            continue;
        }
        const cv::Point2d direction = span / length_px;
        const cv::Point2d normal(-direction.y, direction.x);
        const int samples = std::clamp(static_cast<int>(length_px / params.sample_spacing_px), 2,
                                       params.max_samples_per_edge);

        for (int s = 0; s < samples; ++s) {
            // Interior samples only: corners are rounded on a real cube
            // and neighbouring edges contaminate the window near them.
            const double t = 0.15 + 0.7 * (static_cast<double>(s) / (samples - 1));
            const cv::Point3f model_point = edge.a + (edge.b - edge.a) * static_cast<float>(t);
            const cv::Vec3d camera_point = to_camera(model_point);
            if (camera_point[2] <= 0.2) {
                continue;
            }
            const cv::Point2d projected = project(camera_point);
            // Sub-cell window: a sticker cell spans 2/3 model units, so
            // its projected pitch bounds how far the search may look -
            // beyond ~half a cell lies the aliasing basin, which this
            // stage must never be able to reach.
            const double cell_px = camera_matrix(0, 0) * (2.0 / 3.0) / camera_point[2];
            const double window =
                std::clamp(params.search_cell_fraction * cell_px, 2.0, params.search_cap_px);
            ++m.samples;

            // Profile the window once. Lattice gap lines are dark RIDGES
            // (a gradient on each side) - matching the strongest gradient
            // would land on either flank at random, +-1.5 px of bimodal
            // noise straight into the jitter budget - so lattice samples
            // match the intensity MINIMUM (the gap centre) and outer
            // edges match the strongest step. Both get a parabolic
            // sub-pixel polish.
            const int reach = static_cast<int>(window);
            std::vector<double> profile;
            profile.reserve(static_cast<std::size_t>(2 * reach + 1));
            for (int offset = -reach; offset <= reach; ++offset) {
                profile.push_back(sample_intensity(projected.x + offset * normal.x,
                                                   projected.y + offset * normal.y));
            }
            const auto value_at = [&](int index) {
                return (index >= 0 && index < static_cast<int>(profile.size()))
                           ? profile[static_cast<std::size_t>(index)]
                           : -1.0;
            };
            int best_index = -1;
            double best_score = 0.0;
            std::vector<double> scores(profile.size(), -1.0);
            if (is_lattice) {
                // Score = darkness below the window median; peak = gap
                // centre.
                std::vector<double> valid;
                for (const double v : profile) {
                    if (v >= 0.0) {
                        valid.push_back(v);
                    }
                }
                if (valid.size() < 5) {
                    continue;
                }
                std::nth_element(valid.begin(),
                                 valid.begin() + static_cast<std::ptrdiff_t>(valid.size() / 2),
                                 valid.end());
                const double median = valid[valid.size() / 2];
                for (std::size_t i = 0; i < profile.size(); ++i) {
                    if (profile[i] >= 0.0) {
                        scores[i] = median - profile[i];
                    }
                }
            } else {
                for (std::size_t i = 0; i < profile.size(); ++i) {
                    const double ahead = value_at(static_cast<int>(i) + 1);
                    const double behind = value_at(static_cast<int>(i) - 1);
                    if (ahead >= 0.0 && behind >= 0.0) {
                        scores[i] = std::abs(ahead - behind);
                    }
                }
            }
            for (std::size_t i = 0; i < scores.size(); ++i) {
                if (scores[i] > best_score) {
                    best_score = scores[i];
                    best_index = static_cast<int>(i);
                }
            }
            if (best_index < 0 || best_score < params.min_gradient) {
                continue;
            }
            double best_offset = static_cast<double>(best_index - reach);
            // Parabolic sub-pixel refinement over the score peak.
            const double left =
                best_index > 0 ? scores[static_cast<std::size_t>(best_index - 1)] : -1.0;
            const double right = best_index + 1 < static_cast<int>(scores.size())
                                     ? scores[static_cast<std::size_t>(best_index + 1)]
                                     : -1.0;
            if (left >= 0.0 && right >= 0.0) {
                const double denominator = left - 2.0 * best_score + right;
                if (std::abs(denominator) > 1e-9) {
                    const double delta = 0.5 * (left - right) / denominator;
                    if (std::abs(delta) <= 1.0) {
                        best_offset += delta;
                    }
                }
            }
            m.model_points.push_back(model_point);
            m.image_points.push_back(
                cv::Point2f(static_cast<float>(projected.x + best_offset * normal.x),
                            static_cast<float>(projected.y + best_offset * normal.y)));
            m.displacements.push_back(best_offset);
            m.normals.push_back(normal);
        }
    }

    // Robust trim: matches far from the displacement consensus are
    // occluders or missed edges, and least squares would chase them.
    if (m.displacements.size() >= 4) {
        std::vector<double> sorted = m.displacements;
        std::sort(sorted.begin(), sorted.end());
        const double median = sorted[sorted.size() / 2];
        std::vector<double> deviations;
        deviations.reserve(sorted.size());
        for (const double d : m.displacements) {
            deviations.push_back(std::abs(d - median));
        }
        std::sort(deviations.begin(), deviations.end());
        const double mad = std::max(0.75, deviations[deviations.size() / 2]);
        Measurement kept;
        kept.samples = m.samples;
        for (std::size_t i = 0; i < m.displacements.size(); ++i) {
            if (std::abs(m.displacements[i] - median) <= params.outlier_mad_factor * mad) {
                kept.model_points.push_back(m.model_points[i]);
                kept.image_points.push_back(m.image_points[i]);
                kept.displacements.push_back(m.displacements[i]);
                kept.normals.push_back(m.normals[i]);
            }
        }
        return kept;
    }
    return m;
}

}  // namespace

PoseRefineResult refine_pose(const cv::Mat& image, const cv::Vec3d& rvec, const cv::Vec3d& tvec,
                             const cv::Matx33d& camera_matrix, const PoseRefineParams& params) {
    PoseRefineResult result;
    if (image.empty()) {
        result.reject_reason = "no-image";
        return result;
    }
    cv::Mat grey;
    if (image.channels() == 3) {
        cv::cvtColor(image, grey, cv::COLOR_BGR2GRAY);
    } else {
        grey = image;
    }

    // Measure -> solve -> re-measure: each round's matches are made from
    // the previous round's pose, so the tangential correspondence error
    // shrinks round over round. Caps apply to the TOTAL movement.
    cv::Vec3d current_rvec = rvec;
    cv::Vec3d current_tvec = tvec;
    bool solved_any = false;
    for (int iteration = 0; iteration < std::max(1, params.iterations); ++iteration) {
        const Measurement m =
            measure_edges(grey, current_rvec, current_tvec, camera_matrix, params);
        result.samples = m.samples;
        result.matches = static_cast<int>(m.model_points.size());
        if (result.matches < params.min_matches) {
            if (!solved_any) {
                result.reject_reason = "insufficient";
                return result;
            }
            break;  // earlier rounds already improved the pose; keep them
        }
        // A one-directional match set (say, only one face's parallel
        // lattice lines) leaves the solve free to slide along the
        // unmeasured axis.
        bool has_spread = false;
        for (std::size_t i = 1; i < m.normals.size() && !has_spread; ++i) {
            has_spread = std::abs(m.normals[i].dot(m.normals[0])) < 0.9;
        }
        if (!has_spread) {
            if (!solved_any) {
                result.reject_reason = "degenerate";
                return result;
            }
            break;
        }

        double before = 0.0;
        for (const double d : m.displacements) {
            before += std::abs(d);
        }
        before /= static_cast<double>(m.displacements.size());
        if (result.mean_residual_before_px < 0.0) {
            result.mean_residual_before_px = before;  // first round's = the honest "before"
        }

        cv::Vec3d solved_rvec = current_rvec;
        cv::Vec3d solved_tvec = current_tvec;
        try {
            cv::solvePnPRefineLM(m.model_points, m.image_points, camera_matrix, cv::noArray(),
                                 solved_rvec, solved_tvec);
        } catch (const cv::Exception&) {
            if (!solved_any) {
                result.reject_reason = "solve-failed";
                return result;
            }
            break;
        }

        // Caps are against the ORIGINAL pose: a refinement that wants to
        // travel further than this is a re-anchor decision in disguise.
        if (rotation_angle_deg(rvec, solved_rvec) > params.max_update_deg ||
            cv::norm(solved_tvec - tvec) > params.max_update_translation) {
            if (!solved_any) {
                result.reject_reason = "update-cap";
                return result;
            }
            break;
        }

        std::vector<cv::Point2f> reprojected;
        cv::projectPoints(m.model_points, solved_rvec, solved_tvec, camera_matrix, cv::noArray(),
                          reprojected);
        double after = 0.0;
        for (std::size_t i = 0; i < reprojected.size(); ++i) {
            after += cv::norm(reprojected[i] - m.image_points[i]);
        }
        after /= static_cast<double>(reprojected.size());
        if (after > before) {
            if (!solved_any) {
                result.reject_reason = "worse";
                return result;
            }
            break;
        }
        result.mean_residual_after_px = after;
        current_rvec = solved_rvec;
        current_tvec = solved_tvec;
        solved_any = true;
    }

    if (!solved_any) {
        result.reject_reason = "insufficient";
        return result;
    }
    result.update_deg = rotation_angle_deg(rvec, current_rvec);
    result.update_translation = cv::norm(current_tvec - tvec);
    result.applied = true;
    result.rvec = current_rvec;
    result.tvec = current_tvec;
    return result;
}

}  // namespace cube::vision::pipeline
