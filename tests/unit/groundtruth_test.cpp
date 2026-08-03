// Ground-truth sidecar reader (docs/ground-truth-format.md is normative):
// the spec's own tiny example must parse verbatim, the exclusivity rules
// must reject what they claim to reject, and errors must name the defect.

#include <catch2/catch_test_macros.hpp>

#include <cube/vision/groundtruth.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace cube::vision::groundtruth;

namespace {

// The spec's "complete tiny example", verbatim.
const std::string kSpecSegments = R"({
  "version": 1,
  "clip": "example-clip",
  "segments": [
    { "start": 0, "end": 11, "tags": ["in_frame", "steady"] }
  ],
  "reentry_events": []
})";

const std::string kSpecKeyframe = R"({
  "version": 1,
  "pass": 5,
  "interpolated": false,
  "corners": [
    { "model_id": 1, "x": 200.0, "y": 100.0 },
    { "model_id": 0, "x": 380.0, "y": 100.0 },
    { "model_id": 4, "x": 380.0, "y": 280.0 },
    { "model_id": 5, "x": 200.0, "y": 280.0 }
  ],
  "faces": [
    {
      "face": "F",
      "quad": {
        "tl": { "x": 200.0, "y": 100.0 },
        "tr": { "x": 380.0, "y": 100.0 },
        "br": { "x": 380.0, "y": 280.0 },
        "bl": { "x": 200.0, "y": 280.0 }
      },
      "centres": [
        { "x": 230.0, "y": 130.0 }, { "x": 290.0, "y": 130.0 }, { "x": 350.0, "y": 130.0 },
        { "x": 230.0, "y": 190.0 }, { "x": 290.0, "y": 190.0 }, { "x": 350.0, "y": 190.0 },
        { "x": 230.0, "y": 250.0 }, { "x": 290.0, "y": 250.0 }, { "x": 350.0, "y": 250.0 }
      ]
    }
  ]
})";

}  // namespace

TEST_CASE("the spec's tiny example parses verbatim", "[groundtruth]") {
    const auto segments = parse_segments(kSpecSegments);
    REQUIRE(segments.has_value());
    CHECK(segments->clip == "example-clip");
    REQUIRE(segments->segments.size() == 1);
    CHECK(segments->tags_at(0) == (kInFrame | kSteady));
    CHECK(segments->tags_at(11) == (kInFrame | kSteady));
    CHECK(segments->tags_at(12) == 0);  // unannotated

    const auto keyframe = parse_keyframe(kSpecKeyframe);
    REQUIRE(keyframe.has_value());
    CHECK(keyframe->pass == 5);
    CHECK_FALSE(keyframe->interpolated);
    REQUIRE(keyframe->corners.size() == 4);
    CHECK(keyframe->corners[1].model_id == 0);
    CHECK(keyframe->corners[1].px == cv::Point2f(380.0F, 100.0F));
    REQUIRE(keyframe->faces.size() == 1);
    CHECK(keyframe->faces[0].face == cube::model::Face::F);
    CHECK(keyframe->faces[0].quad[2] == cv::Point2f(380.0F, 280.0F));  // br
    CHECK(keyframe->faces[0].centres[4] == cv::Point2f(290.0F, 190.0F));
}

TEST_CASE("overlapping segments union their tags", "[groundtruth]") {
    const auto segments = parse_segments(R"({
      "segments": [
        { "start": 0,  "end": 99, "tags": ["in_frame"] },
        { "start": 40, "end": 60, "tags": ["occluded"] },
        { "start": 10, "end": 20, "tags": ["steady"] }
      ],
      "reentry_events": [61]
    })");
    REQUIRE(segments.has_value());
    CHECK(segments->tags_at(5) == kInFrame);
    CHECK(segments->tags_at(15) == (kInFrame | kSteady));
    CHECK(segments->tags_at(50) == (kInFrame | kOccluded));
    REQUIRE(segments->reentry_events.size() == 1);
    CHECK(segments->reentry_events[0] == 61);
}

TEST_CASE("exclusivity rules reject contradictory annotations", "[groundtruth]") {
    // in_frame + absent on the same frames.
    CHECK_FALSE(parse_segments(R"({"segments":[
        {"start":0,"end":10,"tags":["in_frame"]},
        {"start":5,"end":15,"tags":["absent"]}]})")
                    .has_value());
    // steady + turning.
    CHECK_FALSE(parse_segments(R"({"segments":[
        {"start":0,"end":10,"tags":["steady","turning"]}]})")
                    .has_value());
    // occluded without in_frame.
    CHECK_FALSE(parse_segments(R"({"segments":[
        {"start":0,"end":10,"tags":["occluded"]}]})")
                    .has_value());
    // Unknown tag, reversed range.
    CHECK_FALSE(parse_segments(R"({"segments":[{"start":0,"end":10,"tags":["wobbly"]}]})").has_value());
    CHECK_FALSE(parse_segments(R"({"segments":[{"start":10,"end":0,"tags":["absent"]}]})").has_value());
}

TEST_CASE("keyframe validation names the defect", "[groundtruth]") {
    // 8 centres instead of 9.
    const auto short_centres = parse_keyframe(R"({
      "pass": 1, "corners": [],
      "faces": [{ "face": "U",
        "quad": {"tl":{"x":0,"y":0},"tr":{"x":1,"y":0},"br":{"x":1,"y":1},"bl":{"x":0,"y":1}},
        "centres": [{"x":0,"y":0},{"x":0,"y":0},{"x":0,"y":0},{"x":0,"y":0},
                    {"x":0,"y":0},{"x":0,"y":0},{"x":0,"y":0},{"x":0,"y":0}] }]})");
    REQUIRE_FALSE(short_centres.has_value());
    CHECK(short_centres.error().find("9") != std::string::npos);

    // Duplicate corner id.
    const auto duplicate = parse_keyframe(R"({"pass": 1,
      "corners": [{"model_id":3,"x":0,"y":0},{"model_id":3,"x":1,"y":1}], "faces": []})");
    REQUIRE_FALSE(duplicate.has_value());
    CHECK(duplicate.error().find("duplicate") != std::string::npos);

    // Out-of-range id, bad face letter, missing quad corner.
    CHECK_FALSE(parse_keyframe(R"({"pass":1,"corners":[{"model_id":8,"x":0,"y":0}],"faces":[]})").has_value());
    CHECK_FALSE(parse_keyframe(R"({"pass":1,"corners":[],
      "faces":[{"face":"X","quad":{},"centres":[]}]})")
                    .has_value());
}

TEST_CASE("load_clip reads a sidecar directory and checks filename consistency", "[groundtruth]") {
    const fs::path dir = fs::temp_directory_path() / "cubeulator-tests" / "gt-clip";
    fs::remove_all(dir);
    fs::create_directories(dir / "groundtruth" / "keyframes");
    std::ofstream(dir / "groundtruth" / "segments.json") << kSpecSegments;
    std::ofstream(dir / "groundtruth" / "keyframes" / "000005.json") << kSpecKeyframe;

    const auto clip = load_clip(dir.string());
    REQUIRE(clip.has_value());
    REQUIRE(clip->segments.has_value());
    REQUIRE(clip->keyframes.size() == 1);
    CHECK(clip->keyframes.contains(5));

    // A keyframe whose pass contradicts its filename is an error.
    std::ofstream(dir / "groundtruth" / "keyframes" / "000009.json") << kSpecKeyframe;
    const auto mismatched = load_clip(dir.string());
    REQUIRE_FALSE(mismatched.has_value());
    CHECK(mismatched.error().find("000009") != std::string::npos);

    // No groundtruth/ at all: legal, empty.
    const fs::path bare = fs::temp_directory_path() / "cubeulator-tests" / "gt-bare";
    fs::remove_all(bare);
    fs::create_directories(bare);
    const auto empty = load_clip(bare.string());
    REQUIRE(empty.has_value());
    CHECK(!empty->segments.has_value());
    CHECK(empty->keyframes.empty());
}
