#pragma once

// Reader for the per-clip ground-truth sidecars the labelling tool
// produces (docs/ground-truth-format.md is normative; this header follows
// it, never the other way round). Consumed by the runner's ground-truth
// metrics (docs/pipeline-metrics.md): segment tags define every rate-gate
// denominator, keyframes carry the pixel referents.
//
// Errors are strings on purpose: this is an instrument, and "keyframe
// 000123.json: faces[0] has 8 centres" redirects a labeller where a
// CubeError enum value could not.

#include <cube/model/face.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cube::vision::groundtruth {

// "Sustained" per the spec: >=5 consecutive passes.
inline constexpr int kSustainedFrames = 5;

// Segment tags as a bitmask so a frame's effective tag set (the UNION of
// every covering segment) is one byte.
enum Tag : std::uint8_t {
    kInFrame = 1U << 0U,
    kAbsent = 1U << 1U,
    kOccluded = 1U << 2U,  // implies in_frame (validated)
    kSteady = 1U << 3U,
    kTurning = 1U << 4U,
};
using TagSet = std::uint8_t;

struct Segment {
    int start = 0;  // pass numbers, inclusive both ends
    int end = 0;
    TagSet tags = 0;
};

struct Segments {
    int version = 0;
    std::string clip;
    std::vector<Segment> segments;
    std::vector<int> reentry_events;

    // Union of tags across every segment covering `pass`; 0 = unannotated
    // (the frame appears in no gate denominator).
    [[nodiscard]] TagSet tags_at(int pass) const;
};

struct LabeledCorner {
    int model_id = -1;  // Singmaster order: 0 URF 1 UFL 2 ULB 3 UBR 4 DFR 5 DLF 6 DBL 7 DRB
    cv::Point2f px{};
};

struct LabeledFace {
    cube::model::Face face = cube::model::Face::U;
    std::array<cv::Point2f, 4> quad{};     // tl, tr, br, bl in the spec's face-local grid order
    std::array<cv::Point2f, 9> centres{};  // row-major in the same grid
};

struct Keyframe {
    int version = 0;
    int pass = 0;
    // true = tool-interpolated aid; gate metrics must score only
    // interpolated == false frames (plan ground rule).
    bool interpolated = false;
    std::vector<LabeledCorner> corners;
    std::vector<LabeledFace> faces;
};

struct ClipGroundTruth {
    std::optional<Segments> segments;
    std::map<int, Keyframe> keyframes;  // keyed by pass
};

template <typename T>
using Parsed = std::expected<T, std::string>;

// Parse from JSON text (the file contents). Validation is part of
// parsing: exclusivity rules (in_frame vs absent, steady vs turning,
// occluded implies in_frame), model_id range/uniqueness, mandatory
// quad + exactly-9-centres per face, pass consistency.
[[nodiscard]] Parsed<Segments> parse_segments(const std::string& json_text);
[[nodiscard]] Parsed<Keyframe> parse_keyframe(const std::string& json_text);

// Load a clip's sidecar directory: <recording-dir>/groundtruth/. A clip
// with no groundtruth/ at all yields an empty ClipGroundTruth (not an
// error - unlabelled clips are legal, they just gate nothing); a MALFORMED
// file is an error naming the file.
[[nodiscard]] Parsed<ClipGroundTruth> load_clip(const std::string& recording_dir);

}  // namespace cube::vision::groundtruth
