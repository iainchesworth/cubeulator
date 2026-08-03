// Smoke test for the m3t vcpkg overlay port (docs/m3t-port.md).
//
// Exercises the installed library end to end without any UI: loads a body
// from an OBJ file, brings up M3T's hidden-window GLFW/GLEW context and
// renders a silhouette offscreen. Exit code 0 plus "M3T_PORT_SMOKE_OK" on
// stdout means the port's headers, static library, exported CMake config
// and transitive dependencies (Eigen, GLEW, glfw, OpenGL, OpenCV) all work.
//
// Usage: m3t_port_smoke <mesh.obj>

#include <m3t/body.h>
#include <m3t/common.h>
#include <m3t/renderer_geometry.h>
#include <m3t/silhouette_renderer.h>

#include <Eigen/Geometry>
#include <iostream>
#include <memory>
#include <opencv2/core.hpp>

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: m3t_port_smoke <mesh.obj>" << std::endl;
    return 2;
  }

  auto body_ptr{std::make_shared<m3t::Body>(
      "body", std::filesystem::path{argv[1]}, 1.0f, true, true,
      m3t::Transform3fA::Identity())};
  if (!body_ptr->SetUp()) {
    std::cerr << "FAIL: body setup" << std::endl;
    return 1;
  }

  auto rg_ptr{std::make_shared<m3t::RendererGeometry>("rg")};
  rg_ptr->AddBody(body_ptr);
  if (!rg_ptr->SetUp()) {
    std::cerr << "FAIL: renderer geometry (GLFW/GLEW) setup" << std::endl;
    return 1;
  }

  body_ptr->set_body2world_pose(
      m3t::Transform3fA{Eigen::Translation3f{0.0f, 0.0f, 0.25f}});
  const m3t::Intrinsics intrinsics{600.0f, 600.0f, 320.0f, 240.0f, 640, 480};
  auto renderer_ptr{std::make_shared<m3t::FullSilhouetteRenderer>(
      "sil", rg_ptr, m3t::Transform3fA::Identity(), intrinsics)};
  if (!renderer_ptr->SetUp() || !renderer_ptr->StartRendering() ||
      !renderer_ptr->FetchSilhouetteImage()) {
    std::cerr << "FAIL: offscreen silhouette render" << std::endl;
    return 1;
  }

  const int nonzero{cv::countNonZero(renderer_ptr->silhouette_image())};
  std::cout << "silhouette nonzero px = " << nonzero << std::endl;
  if (nonzero <= 0) {
    std::cerr << "FAIL: silhouette empty" << std::endl;
    return 1;
  }
  std::cout << "M3T_PORT_SMOKE_OK" << std::endl;
  return 0;
}
