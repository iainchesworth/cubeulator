// Smoke checks for the vendored M3T tracker (third_party/m3t - see its
// README.md for provenance and the patch list).
//
// WHY THIS EXISTS
// ---------------
// The Phase 2 feasibility spike (docs/pipeline-plan.md) established four
// properties of M3T on this exact stack - MSVC + dynamic vcpkg libraries,
// GLFW hidden-window offscreen rendering, no visible UI - that the pipeline
// depends on: it builds, it renders headless, it tracks, and it is
// bit-identical across runs (the offline runner is the acceptance surface,
// so determinism is a contract, not a nicety). These tests port the spike's
// two probes (spike_headless.cpp, spike_track.cpp) into the repo so any
// future M3T update, vcpkg bump, or toolchain change can re-verify all four
// properties on demand.
//
// The heavy cases are doubly fenced, following the camera-probe pattern: the
// leading dot in "[.m3t-smoke]" hides them from a default Catch2 run, and
// they SKIP unless CUBE_M3T_SMOKE is set. They create a GL context (hidden
// window) and burn ~1-2 minutes generating the region model, so they must
// never run in an ordinary `ctest` sweep.
//
// Run them with:
//   set CUBE_M3T_SMOKE=1 && cube_m3t_tests "[.m3t-smoke]"
//
// The one visible test is deliberately trivial: it proves the vendored
// library links and its types construct, and it costs nothing, so the fast
// suite exercises the link line on every run.

#include <catch2/catch_test_macros.hpp>

#include <m3t/body.h>
#include <m3t/camera.h>
#include <m3t/common.h>
#include <m3t/link.h>
#include <m3t/optimizer.h>
#include <m3t/region_modality.h>
#include <m3t/region_model.h>
#include <m3t/renderer_geometry.h>
#include <m3t/silhouette_renderer.h>
#include <m3t/tracker.h>

#include <Eigen/Geometry>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Shared scene constants, identical to the spike's: a 57 mm cube (the real
// puzzle's size) about 0.25 m in front of a 640x480 camera with f = 600 px.
// ---------------------------------------------------------------------------
constexpr float kEdge = 0.057F;
constexpr int kW = 640;
constexpr int kH = 480;
constexpr float kFu = 600.0F;
constexpr float kFv = 600.0F;
constexpr float kPpu = 320.0F;
constexpr float kPpv = 240.0F;
constexpr int kNFrames = 100;

// Writes the cube geometry M3T loads as its CAD model: 8 vertices, 12
// triangles, metres, CCW winding viewed from outside (culling is enabled).
std::filesystem::path write_cube_obj(const std::filesystem::path& dir) {
    const std::filesystem::path path = dir / "cube_57mm.obj";
    std::ofstream out(path);
    const float h = kEdge / 2.0F;
    for (int i = 0; i < 8; ++i) {
        const float x = ((i & 1) != 0) ? h : -h;
        const float y = ((i & 2) != 0) ? h : -h;
        const float z = ((i & 4) != 0) ? h : -h;
        out << "v " << x << ' ' << y << ' ' << z << '\n';
    }
    // 1-based indices for vertices (-h,-h,-h),(h,-h,-h),(h,h,-h),(-h,h,-h),
    // (-h,-h,h),(h,-h,h),(h,h,h),(-h,h,h) in the classic ordering.
    out << "f 5 6 7\nf 5 7 8\n"    // +z
        << "f 2 1 4\nf 2 4 3\n"    // -z
        << "f 6 2 3\nf 6 3 7\n"    // +x
        << "f 1 5 8\nf 1 8 4\n"    // -x
        << "f 8 7 3\nf 8 3 4\n"    // +y
        << "f 1 2 6\nf 1 6 5\n";   // -y
    REQUIRE(out.good());
    return path;
}

// The vertex ordering write_cube_obj() emits, reused by the synthetic frame
// renderer so the drawn cube and the CAD model agree exactly.
std::array<Eigen::Vector3f, 8> cube_corners() {
    const float h = kEdge / 2.0F;
    return {{{-h, -h, -h},
             {h, -h, -h},
             {h, h, -h},
             {-h, h, -h},
             {-h, -h, h},
             {h, -h, h},
             {h, h, h},
             {-h, h, h}}};
}

std::filesystem::path make_scratch_dir() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "cube_m3t_smoke";
    std::filesystem::create_directories(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Synthetic sequence (ported from spike_track.cpp): ground-truth pose per
// frame is a slow tumble plus translation drift; frames are drawn with
// OpenCV as a coloured cube on a mildly textured grey background.
// ---------------------------------------------------------------------------
m3t::Transform3fA gt_pose(int i) {
    const float t = static_cast<float>(i);
    const float angle = 0.012F * t;  // rad, ~0.7 deg/frame
    const Eigen::Vector3f axis = Eigen::Vector3f{0.3F, 1.0F, 0.2F}.normalized();
    const Eigen::Vector3f trans{0.02F * std::sin(0.05F * t),
                                0.015F * std::sin(0.037F * t),
                                0.25F + 0.012F * std::sin(0.023F * t)};
    return m3t::Transform3fA{
        Eigen::Translation3f{trans} *
        Eigen::AngleAxisf{0.5F, Eigen::Vector3f{1, 1, 0}.normalized()} *
        Eigen::AngleAxisf{angle, axis}};
}

cv::Mat render_frame(const m3t::Transform3fA& body2world) {
    cv::Mat img(kH, kW, CV_8UC3, cv::Scalar(105, 115, 108));  // grey-green bg
    // Subtle background texture so the tracker's colour histograms aren't
    // learning against a perfectly uniform backdrop.
    cv::rectangle(img, {0, 0}, {kW, kH / 3}, cv::Scalar(120, 125, 118), -1);

    const auto c = cube_corners();
    struct Face {
        int idx[4];
        cv::Scalar color;  // BGR
        Eigen::Vector3f normal;
    };
    const Face faces[6] = {
        {{4, 5, 6, 7}, {60, 60, 230}, {0, 0, 1}},    // +z red
        {{1, 0, 3, 2}, {50, 140, 245}, {0, 0, -1}},  // -z orange
        {{5, 1, 2, 6}, {240, 240, 240}, {1, 0, 0}},  // +x white
        {{0, 4, 7, 3}, {60, 200, 250}, {-1, 0, 0}},  // -x yellow
        {{7, 6, 2, 3}, {90, 170, 40}, {0, 1, 0}},    // +y green
        {{0, 1, 5, 4}, {200, 100, 40}, {0, -1, 0}},  // -y blue
    };

    struct DrawFace {
        float depth = 0.0F;
        std::vector<cv::Point> pts;
        cv::Scalar color;
    };
    std::vector<DrawFace> to_draw;
    for (const auto& f : faces) {
        // Backface cull in the camera frame.
        const Eigen::Vector3f n_cam = body2world.rotation() * f.normal;
        const Eigen::Vector3f p_cam = body2world * c[static_cast<std::size_t>(f.idx[0])];
        if (n_cam.dot(p_cam) >= 0.0F) {
            continue;
        }
        DrawFace df;
        df.color = f.color;
        float zsum = 0.0F;
        bool ok = true;
        for (int k = 0; k < 4; ++k) {
            const Eigen::Vector3f p = body2world * c[static_cast<std::size_t>(f.idx[k])];
            if (p.z() <= 0.01F) {
                ok = false;
                break;
            }
            zsum += p.z();
            df.pts.emplace_back(static_cast<int>(std::lround(kFu * p.x() / p.z() + kPpu)),
                                static_cast<int>(std::lround(kFv * p.y() / p.z() + kPpv)));
        }
        if (!ok) {
            continue;
        }
        df.depth = zsum / 4.0F;
        to_draw.push_back(std::move(df));
    }
    // Painter's algorithm: far faces first.
    std::sort(to_draw.begin(), to_draw.end(),
              [](const DrawFace& a, const DrawFace& b) { return a.depth > b.depth; });
    for (const auto& df : to_draw) {
        cv::fillConvexPoly(img, df.pts, df.color, cv::LINE_8);
    }
    return img;
}

// In-memory colour camera: frames are handed in from a vector - no device,
// no disk, exactly how the offline runner will feed the tracker.
class InMemColorCamera : public m3t::ColorCamera {
public:
    explicit InMemColorCamera(const std::string& name) : ColorCamera{name} {
        intrinsics_ = m3t::Intrinsics{kFu, kFv, kPpu, kPpv, kW, kH};
    }
    bool SetUp() override {
        set_up_ = true;
        return true;
    }
    void set_frame(const cv::Mat& frame) { pending_ = frame; }
    bool UpdateImage(bool /*synchronized*/) override {
        image_ = pending_;
        SaveImageIfDesired();
        return true;
    }

private:
    cv::Mat pending_;
};

struct TrackRun {
    std::vector<float> pose_words;  // 16 floats (4x4 matrix) per frame
    double mean_terr_mm = 0.0;
    double mean_rerr_deg = 0.0;
    double max_terr_mm = 0.0;
    double max_rerr_deg = 0.0;
    double last_terr_mm = 0.0;
};

// One complete tracking run over the pre-rendered frames: fresh body,
// camera, modality, optimizer and tracker every time; only the region-model
// cache (model_dir) persists between runs. Mirrors RunTrackerProcess's
// stepping order via the advanced-use API, as the spike did.
TrackRun run_track(const std::filesystem::path& obj_path,
                   const std::filesystem::path& model_dir,
                   const std::vector<cv::Mat>& frames) {
    auto body_ptr{std::make_shared<m3t::Body>(
        "cube", obj_path, 1.0F, true, true, m3t::Transform3fA::Identity())};
    auto camera_ptr{std::make_shared<InMemColorCamera>("camera")};
    // Spike-reduced generation parameters (n_divides 4 -> 3, image_size
    // 2000 -> 1000): a one-time offline cost, cached to .bin in model_dir.
    // use_random_seed stays false - determinism is the point.
    auto region_model_ptr{std::make_shared<m3t::RegionModel>(
        "region_model", body_ptr, model_dir / "cube_region_model.bin", 0.8F, 3,
        200, 0.05F, 0.002F, false, 1000)};
    auto region_modality_ptr{std::make_shared<m3t::RegionModality>(
        "region_modality", body_ptr, camera_ptr, region_model_ptr)};
    auto link_ptr{std::make_shared<m3t::Link>("link", body_ptr)};
    link_ptr->AddModality(region_modality_ptr);
    auto optimizer_ptr{std::make_shared<m3t::Optimizer>("optimizer", link_ptr)};
    auto tracker_ptr{std::make_shared<m3t::Tracker>("tracker")};
    tracker_ptr->AddOptimizer(optimizer_ptr);

    // Init pose: frame 0's ground truth, deliberately perturbed (a few mm and
    // ~3 deg) the way a PnP initialisation would be.
    const m3t::Transform3fA init_pose{
        gt_pose(0) * Eigen::Translation3f{0.004F, -0.003F, 0.006F} *
        Eigen::AngleAxisf{0.05F, Eigen::Vector3f{0, 1, 0}}};
    body_ptr->set_body2world_pose(init_pose);

    REQUIRE(tracker_ptr->SetUp());
    tracker_ptr->StartTracking();

    TrackRun run;
    run.pose_words.reserve(static_cast<std::size_t>(kNFrames) * 16U);
    double sum_terr = 0.0;
    double sum_rerr = 0.0;
    for (int i = 0; i < kNFrames; ++i) {
        camera_ptr->set_frame(frames[static_cast<std::size_t>(i)]);
        REQUIRE(tracker_ptr->UpdateCameras(i));
        REQUIRE(tracker_ptr->ExecuteStartingStep(i));
        REQUIRE(tracker_ptr->ExecuteTrackingStep(i));

        const m3t::Transform3fA pose = body_ptr->body2world_pose();
        const Eigen::Matrix4f m = pose.matrix();
        run.pose_words.insert(run.pose_words.end(), m.data(), m.data() + 16);

        const m3t::Transform3fA gt = gt_pose(i);
        const double terr =
            static_cast<double>((pose.translation() - gt.translation()).norm()) * 1000.0;
        const Eigen::AngleAxisf diff{gt.rotation().transpose() * pose.rotation()};
        const double rerr = std::abs(static_cast<double>(diff.angle())) *
                            180.0 / std::numbers::pi;
        run.max_terr_mm = std::max(run.max_terr_mm, terr);
        run.max_rerr_deg = std::max(run.max_rerr_deg, rerr);
        sum_terr += terr;
        sum_rerr += rerr;
        run.last_terr_mm = terr;
    }
    run.mean_terr_mm = sum_terr / kNFrames;
    run.mean_rerr_deg = sum_rerr / kNFrames;
    return run;
}

}  // namespace

// ---------------------------------------------------------------------------
// Always-on: the vendored library links and its basic types construct. No GL
// context, no SetUp() - this exists so every fast-suite run exercises the
// cube::m3t link line (and the DLL load on Windows) at zero cost.
// ---------------------------------------------------------------------------
TEST_CASE("vendored m3t links and constructs", "[m3t]") {
    const m3t::Body body{"cube", "nonexistent.obj", 1.0F, true, true,
                         m3t::Transform3fA::Identity()};
    CHECK(body.name() == "cube");
    CHECK_FALSE(body.set_up());

    const m3t::Intrinsics intr{kFu, kFv, kPpu, kPpv, kW, kH};
    CHECK(intr.width == kW);

    const m3t::Tracker tracker{"tracker"};
    CHECK(tracker.name() == "tracker");
}

// ---------------------------------------------------------------------------
// Spike Q2, as a regression check: M3T's GLFW/GLEW renderer works headless
// (hidden window, console process), and the silhouette it produces is the
// cube we asked for, where we asked for it.
// ---------------------------------------------------------------------------
TEST_CASE("m3t offscreen silhouette render is non-empty and geometrically sane",
          "[.m3t-smoke]") {
    if (std::getenv("CUBE_M3T_SMOKE") == nullptr) {
        SKIP("set CUBE_M3T_SMOKE=1 to run the M3T offscreen-render smoke check");
    }

    const std::filesystem::path scratch = make_scratch_dir();
    const std::filesystem::path obj_path = write_cube_obj(scratch);

    auto body_ptr{std::make_shared<m3t::Body>(
        "cube", obj_path, 1.0F, true, true, m3t::Transform3fA::Identity())};
    REQUIRE(body_ptr->SetUp());

    // glfwInit + hidden window + GLEW all happen inside SetUp(); on a machine
    // where that cannot work, this REQUIRE is the failure point.
    auto rg_ptr{std::make_shared<m3t::RendererGeometry>("rg")};
    rg_ptr->AddBody(body_ptr);
    REQUIRE(rg_ptr->SetUp());

    // Cube 25 cm in front of the camera, tilted so three faces are visible.
    const m3t::Transform3fA body2world{
        Eigen::Translation3f{0.0F, 0.0F, 0.25F} *
        Eigen::AngleAxisf{0.6F, Eigen::Vector3f{1, 1, 0}.normalized()}};
    body_ptr->set_body2world_pose(body2world);

    const m3t::Intrinsics intr{kFu, kFv, kPpu, kPpv, kW, kH};
    auto renderer_ptr{std::make_shared<m3t::FullSilhouetteRenderer>(
        "sil", rg_ptr, m3t::Transform3fA::Identity(), intr)};
    REQUIRE(renderer_ptr->SetUp());
    REQUIRE(renderer_ptr->StartRendering());
    REQUIRE(renderer_ptr->FetchSilhouetteImage());

    const cv::Mat sil = renderer_ptr->silhouette_image();
    REQUIRE_FALSE(sil.empty());
    REQUIRE(sil.rows == kH);
    REQUIRE(sil.cols == kW);

    // Non-empty, and the right amount of non-empty: a 57 mm cube at 0.25 m
    // with f = 600 projects a ~137 px face edge and a ~237 px space diagonal,
    // so the silhouette must cover tens of thousands of pixels but nowhere
    // near the frame. (Spike measurement on this scene: 30676 px.)
    const int nonzero = cv::countNonZero(sil);
    std::cout << "[m3t-smoke] silhouette nonzero px = " << nonzero << " of "
              << sil.total() << '\n';
    REQUIRE(nonzero > 15000);
    REQUIRE(nonzero < 60000);

    // Geometric sanity: the blob sits where the pose puts it - bounding box
    // centred near the principal point (the cube is on the optical axis),
    // sized between one face edge and the projected diagonal, and fully
    // inside the frame (not clipped). Spike measurement: 193x193 bbox centred
    // at (311, 249).
    std::vector<cv::Point> nz;
    cv::findNonZero(sil, nz);
    const cv::Rect bbox = cv::boundingRect(nz);
    std::cout << "[m3t-smoke] silhouette bbox = " << bbox << '\n';
    CHECK(bbox.width >= 130);
    CHECK(bbox.width <= 250);
    CHECK(bbox.height >= 130);
    CHECK(bbox.height <= 250);
    const double cx = bbox.x + bbox.width / 2.0;
    const double cy = bbox.y + bbox.height / 2.0;
    CHECK(std::abs(cx - kPpu) < 30.0);
    CHECK(std::abs(cy - kPpv) < 30.0);
    CHECK(bbox.x > 0);
    CHECK(bbox.y > 0);
    CHECK(bbox.x + bbox.width < kW - 1);
    CHECK(bbox.y + bbox.height < kH - 1);
}

// ---------------------------------------------------------------------------
// Spike Q3, as a regression check: a 100-frame synthetic track stays locked,
// and two complete runs produce bit-identical pose streams. Run A generates
// the region model and caches it; run B loads the cache - the spike showed
// both paths give identical poses, so this also guards the cache round-trip.
// ---------------------------------------------------------------------------
TEST_CASE("m3t 100-frame synthetic track is bit-identical across runs",
          "[.m3t-smoke]") {
    if (std::getenv("CUBE_M3T_SMOKE") == nullptr) {
        SKIP("set CUBE_M3T_SMOKE=1 to run the M3T tracking smoke check");
    }

    const std::filesystem::path scratch = make_scratch_dir();
    const std::filesystem::path obj_path = write_cube_obj(scratch);
    // Force regeneration each test execution so a stale cache from an older
    // geometry or parameter set can never fake a pass; run B then exercises
    // the load-from-cache path against run A's freshly generated model.
    std::filesystem::remove(scratch / "cube_region_model.bin");

    std::vector<cv::Mat> frames;
    frames.reserve(kNFrames);
    for (int i = 0; i < kNFrames; ++i) {
        frames.push_back(render_frame(gt_pose(i)));
    }

    std::cout << "[m3t-smoke] run A (generates region model - takes a while)\n";
    const TrackRun run_a = run_track(obj_path, scratch, frames);
    std::cout << "[m3t-smoke] run B (loads cached region model)\n";
    const TrackRun run_b = run_track(obj_path, scratch, frames);

    std::cout << "[m3t-smoke] run A: mean terr " << run_a.mean_terr_mm
              << " mm, mean rerr " << run_a.mean_rerr_deg << " deg, max terr "
              << run_a.max_terr_mm << " mm, max rerr " << run_a.max_rerr_deg
              << " deg, last terr " << run_a.last_terr_mm << " mm\n";

    // Tracking quality: loose bounds around the spike's measurements (mean
    // 1.96 mm / 1.24 deg, max 13.5 mm / 12.4 deg) - tight enough to fail on
    // divergence, loose enough not to gate on fractions the pipeline phases
    // will gate properly later.
    CHECK(run_a.mean_terr_mm < 8.0);
    CHECK(run_a.mean_rerr_deg < 5.0);
    CHECK(run_a.max_terr_mm < 30.0);
    CHECK(run_a.last_terr_mm < 10.0);

    // Determinism: bit-identical pose matrices across the two runs. memcmp,
    // not float ==, so -0.0/0.0 or NaN can never alias a real difference.
    REQUIRE(run_a.pose_words.size() ==
            static_cast<std::size_t>(kNFrames) * 16U);
    REQUIRE(run_a.pose_words.size() == run_b.pose_words.size());
    const bool bit_identical =
        std::memcmp(run_a.pose_words.data(), run_b.pose_words.data(),
                    run_a.pose_words.size() * sizeof(float)) == 0;
    std::cout << "[m3t-smoke] pose streams bit-identical: "
              << (bit_identical ? "yes" : "NO") << '\n';
    REQUIRE(bit_identical);
}
