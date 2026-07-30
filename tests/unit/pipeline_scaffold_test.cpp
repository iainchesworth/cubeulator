// The pipeline scaffolding's enforcement tests (docs/pipeline-plan.md,
// Phase 0 item 1): trace completeness is a FAILING check, not a
// convention; JSONL serialisation is deterministic; and the legacy adapter
// actually drives the legacy detectors through the stage contracts on a
// frame they can detect.

#include <catch2/catch_test_macros.hpp>

#include <cube/model/facelet.hpp>
#include <cube/vision/pipeline/legacy_pipeline.hpp>
#include <cube/vision/pipeline/stage_types.hpp>
#include <cube/vision/pipeline/trace.hpp>

#include <opencv2/imgproc.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using namespace cube::vision;
using cube::model::FacetColor;

// Same synthetic face recipe the detector's own tests use: 60x60 stickers
// with the exact BGR seed values default_palette() converts to Lab, on a
// dark background with 10px gaps — a frame observe_face reliably detects.
constexpr int kMargin = 15;
constexpr int kCell = 60;
constexpr int kGap = 10;
constexpr int kFrameSize = 230;  // 15 + 3*60 + 2*10 + 15

cv::Mat synthetic_face_frame() {
    static const std::array<cv::Scalar, 9> layout{{
        cv::Scalar(255, 255, 255),
        cv::Scalar(0, 213, 255),
        cv::Scalar(0, 0, 196),
        cv::Scalar(0, 106, 255),
        cv::Scalar(72, 155, 0),
        cv::Scalar(173, 70, 0),
        cv::Scalar(255, 255, 255),
        cv::Scalar(0, 213, 255),
        cv::Scalar(0, 0, 196),
    }};
    cv::Mat frame = cv::Mat::zeros(kFrameSize, kFrameSize, CV_8UC3);
    int index = 0;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col, ++index) {
            const int x = kMargin + col * (kCell + kGap);
            const int y = kMargin + row * (kCell + kGap);
            cv::rectangle(frame, cv::Rect(x, y, kCell, kCell), layout[static_cast<std::size_t>(index)], cv::FILLED);
        }
    }
    return frame;
}

pipeline::FramePacket packet_for(const cv::Mat& frame, int seq, std::int64_t t_ms) {
    pipeline::FramePacket packet;
    packet.bgr = frame;
    packet.seq = seq;
    packet.t_ms = t_ms;
    packet.full_size = frame.size();
    return packet;
}

}  // namespace

TEST_CASE("every stage traces even on an empty frame", "[pipeline]") {
    pipeline::LegacyPipeline pipe;
    const cv::Mat gray(240, 320, CV_8UC3, cv::Scalar(64, 64, 64));

    const auto result = pipe.process(packet_for(gray, 0, 1000));

    // Completeness is the contract: exactly the canonical stages, in
    // order, one record each — including the not-run commit stages.
    const auto problem = pipeline::validate_trace(result.trace);
    INFO(problem.value_or(""));
    CHECK(!problem.has_value());

    CHECK_FALSE(result.detection.found);
    CHECK(result.faces.faces.empty());
    CHECK(result.cells.faces.empty());
    CHECK(result.samples.faces.empty());
    CHECK(result.reads.faces.empty());

    // The commit stages exist in the trace and honestly say they did not
    // run (they are app-owned until the routing harness lands).
    REQUIRE(result.trace.stages.size() == pipeline::kStageNames.size());
    CHECK(result.trace.stages[7].stage == "fuse");
    CHECK_FALSE(result.trace.stages[7].ran);
    CHECK(result.trace.stages[8].stage == "assemble");
    CHECK_FALSE(result.trace.stages[8].ran);
}

TEST_CASE("a validate_trace failure names the defect", "[pipeline]") {
    pipeline::FrameTrace trace;
    trace.seq = 7;
    // Missing stages entirely.
    auto problem = pipeline::validate_trace(trace);
    REQUIRE(problem.has_value());
    CHECK(problem->find("frame 7") != std::string::npos);

    // Right count, wrong name.
    for (const auto name : pipeline::kStageNames) {
        pipeline::StageTrace stage;
        stage.stage = std::string(name);
        trace.stages.push_back(std::move(stage));
    }
    trace.stages[2].stage = "trakc";
    problem = pipeline::validate_trace(trace);
    REQUIRE(problem.has_value());
    CHECK(problem->find("trakc") != std::string::npos);
}

TEST_CASE("the legacy adapter detects and classifies a synthetic face", "[pipeline]") {
    pipeline::LegacyPipeline pipe;
    const cv::Mat frame = synthetic_face_frame();

    const auto result = pipe.process(packet_for(frame, 1, 1000));

    REQUIRE(!pipeline::validate_trace(result.trace).has_value());
    CHECK(result.detection.found);
    REQUIRE(result.detection.legacy_face.has_value());

    // One capture-grade face view, index-aligned cells/samples/reads.
    REQUIRE(result.faces.faces.size() == 1);
    CHECK(result.faces.faces[0].capture_grade);
    REQUIRE(result.cells.faces.size() == 1);
    REQUIRE(result.samples.faces.size() == 1);
    REQUIRE(result.reads.faces.size() == 1);

    // The detector sampled real colour and classification produced real
    // names: at least the six clean palette-seed cells must not be
    // unknown (classify_lab may abstain on none of these seeds, but the
    // bound is deliberately loose — this is a plumbing test, not a
    // colour-accuracy gate).
    int named = 0;
    for (const auto& facelet : result.reads.faces[0].facelets) {
        named += facelet.color != FacetColor::unknown ? 1 : 0;
    }
    CHECK(named >= 6);

    // A pose came out of the face path and the tracker consumed it.
    CHECK(result.track.pose_valid);
    CHECK(result.track.pose_source == "face");
    CHECK(result.track.phase != pipeline::TrackPhase::lost);
}

TEST_CASE("traces serialise deterministically and validly", "[pipeline]") {
    const cv::Mat frame = synthetic_face_frame();

    // Two fresh pipelines, same input stream -> byte-identical JSONL.
    // (Timings differ run to run, so determinism is checked on a copy with
    // durations zeroed — everything else must match exactly.)
    const auto run = [&frame] {
        pipeline::LegacyPipeline pipe;
        auto result = pipe.process(packet_for(frame, 3, 500));
        for (auto& stage : result.trace.stages) {
            stage.duration_ms = 0.0;
        }
        return pipeline::to_jsonl(result.trace);
    };
    const std::string first = run();
    const std::string second = run();
    CHECK(first == second);

    // Shape sanity of the JSONL line itself.
    CHECK(first.front() == '{');
    CHECK(first.back() == '}');
    CHECK(first.find("\"seq\":3") != std::string::npos);
    CHECK(first.find("\"stages\":[") != std::string::npos);
    CHECK(first.find('\n') == std::string::npos);
    for (const auto name : pipeline::kStageNames) {
        CHECK(first.find("\"stage\":\"" + std::string(name) + "\"") != std::string::npos);
    }
}

TEST_CASE("trace values escape and format edge cases", "[pipeline]") {
    pipeline::FrameTrace trace;
    trace.seq = 1;
    pipeline::StageTrace stage;
    stage.stage = "acquire";
    stage.ran = true;
    stage.add("quote\"back\\slash", std::string("line\nbreak\ttab"));
    stage.add("nan", std::numeric_limits<double>::quiet_NaN());
    stage.add("int", static_cast<std::int64_t>(-42));
    stage.add("flag", false);
    trace.stages.push_back(std::move(stage));

    const std::string line = pipeline::to_jsonl(trace);
    CHECK(line.find("quote\\\"back\\\\slash") != std::string::npos);
    CHECK(line.find("line\\nbreak\\ttab") != std::string::npos);
    CHECK(line.find("\"nan\":null") != std::string::npos);
    CHECK(line.find("\"int\":-42") != std::string::npos);
    CHECK(line.find("\"flag\":false") != std::string::npos);
    CHECK(line.find('\n') == std::string::npos);
}
