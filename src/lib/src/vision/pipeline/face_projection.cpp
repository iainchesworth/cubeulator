#include <cube/vision/pipeline/face_projection.hpp>

#include <opencv2/calib3d.hpp>

#include <algorithm>
#include <cmath>

namespace cube::vision::pipeline {

std::vector<ProjectedFace> derive_faces(const cv::Vec3d& rvec, const cv::Vec3d& tvec,
                                        const cv::Matx33d& camera_matrix, double min_frontality) {
    std::vector<ProjectedFace> out;
    cv::Matx33d rotation;
    cv::Rodrigues(rvec, rotation);

    const auto to_camera = [&](const cv::Point3f& p) {
        const cv::Matx31d c = rotation * cv::Matx31d(p.x, p.y, p.z) + cv::Matx31d(tvec[0], tvec[1], tvec[2]);
        return cv::Vec3d(c(0, 0), c(1, 0), c(2, 0));
    };
    const auto project = [&](const cv::Point3f& p) {
        const cv::Vec3d c = to_camera(p);
        return cv::Point2f(static_cast<float>(camera_matrix(0, 0) * c[0] / c[2] + camera_matrix(0, 2)),
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
        const cv::Matx31d normal_c = rotation * cv::Matx31d(normal.x, normal.y, normal.z);
        const cv::Vec3d centre_c = to_camera(normal);  // face centre = its unit normal (half-edge 1)
        const double distance = cv::norm(centre_c);
        if (distance <= 1e-9 || centre_c[2] <= 0.0) {
            continue;
        }
        const double frontality =
            -(normal_c(0, 0) * centre_c[0] + normal_c(1, 0) * centre_c[1] + normal_c(2, 0) * centre_c[2]) / distance;
        if (frontality < min_frontality) {
            continue;
        }

        ProjectedFace projected;
        projected.face = face;
        projected.frontality = frontality;
        const int axis_u = (face_axis + 1) % 3;
        const int axis_v = (face_axis + 2) % 3;
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
            }
        }
        out.push_back(projected);
    }

    std::sort(out.begin(), out.end(),
              [](const ProjectedFace& a, const ProjectedFace& b) { return a.frontality > b.frontality; });
    return out;
}

}  // namespace cube::vision::pipeline
