// Fast, corpus-free tests for the stateless simple face pipeline.
//
// The [.simple-pipeline] harness measures this module against real
// recordings, but the corpus does not ship with the repo, so without these
// the module has NO coverage in a normal build - and its first version
// shipped a bug the corpus caught immediately (accepting a single sticker
// as the face quad) that a synthetic face would also have caught, in a
// second, on any machine.
//
// The synthetic face is drawn, not photographed, so it deliberately cannot
// stand in for the corpus on anything photometric. What it does pin down is
// the geometry-and-plumbing contract: a face quad is found, the nine cell
// centres land in the nine cells in row-major order, the abstention rule
// fires when it should, and the store keys faces by centre colour.

#include <catch2/catch_test_macros.hpp>

#include <cube/vision/simple_face_pipeline.hpp>

#include <opencv2/imgproc.hpp>

#include <array>
#include <string>

namespace {

using cube::model::FacetColor;
namespace simple = cube::vision::simple;

// Reference sticker BGRs, saturated enough that neither classifier route is
// being asked to make a marginal call - these tests are about geometry.
cv::Scalar sticker_bgr(FacetColor color) {
    switch (color) {
        case FacetColor::white: return {245, 245, 245};
        case FacetColor::yellow: return {0, 213, 255};
        case FacetColor::red: return {0, 0, 196};
        case FacetColor::orange: return {0, 106, 255};
        case FacetColor::green: return {72, 155, 0};
        case FacetColor::blue: return {173, 70, 0};
        case FacetColor::unknown: break;
    }
    return {0, 0, 0};
}

// A frontal 3x3 face on a mid-grey ground, with dark inter-tile gaps - the
// same structure the LSD route traces on the real footage (it recovers the
// sticker lattice, not the silhouette; see the module header).
cv::Mat draw_face(const std::array<FacetColor, 9>& cells, int origin_x = 185, int origin_y = 105,
                  int face_size = 270) {
    cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(90, 90, 90));
    const int pitch = face_size / 3;
    // The inter-tile gap has to survive the detector's mask dilation, or the
    // nine stickers fuse into one blob and the only contour left is the
    // face outline. A first version of this fixture drew a 5 px gap, the
    // dilation closed it, and the module found THREE candidates on a
    // synthetic face where the real footage yields a dozen - a fixture that
    // exercised a code path the corpus never takes. A gap around an eighth
    // of the pitch matches what the recordings actually show.
    const int gap = std::max(3, pitch / 8);
    cv::rectangle(frame, cv::Rect(origin_x, origin_y, face_size, face_size), cv::Scalar(20, 20, 20),
                  cv::FILLED);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const cv::Rect tile(origin_x + col * pitch + gap, origin_y + row * pitch + gap,
                                pitch - 2 * gap, pitch - 2 * gap);
            cv::rectangle(frame, tile, sticker_bgr(cells[static_cast<std::size_t>(row * 3 + col)]),
                          cv::FILLED);
        }
    }
    // A touch of blur, because a perfectly hard synthetic edge is not what
    // LSD is tuned for and not what a JPEG frame delivers.
    cv::GaussianBlur(frame, frame, cv::Size(3, 3), 0.0);
    return frame;
}

std::string letters(const simple::FaceReading& reading) {
    std::string text;
    for (const auto& cell : reading.cells) {
        switch (cell.color) {
            case FacetColor::white: text += 'W'; break;
            case FacetColor::yellow: text += 'Y'; break;
            case FacetColor::red: text += 'R'; break;
            case FacetColor::orange: text += 'O'; break;
            case FacetColor::green: text += 'G'; break;
            case FacetColor::blue: text += 'B'; break;
            case FacetColor::unknown: text += '?'; break;
        }
    }
    return text;
}

constexpr std::array<FacetColor, 9> kScrambled{FacetColor::red,    FacetColor::green,
                                               FacetColor::blue,   FacetColor::yellow,
                                               FacetColor::orange, FacetColor::white,
                                               FacetColor::green,  FacetColor::blue,
                                               FacetColor::red};

}  // namespace

TEST_CASE("a drawn face yields a quad covering the whole face, not one sticker", "[vision]") {
    const cv::Mat frame = draw_face(kScrambled);
    const simple::PipelineParams params;
    // The evidence pattern the corpus shows: many sticker-scale candidates,
    // which is what the hull route is built to consume.
    const auto candidates = simple::quad_candidates_line_segments(frame, params);
    CHECK(candidates.quads.size() >= 9);

    const auto quad = simple::detect_quad_sticker_hull(frame, params);

    REQUIRE(quad.has_value());
    // The regression this test exists for. A 210x210 face in a 640x480 frame
    // is ~24% of it; a single sticker is ~2%. The first version of the
    // module returned the sticker, on 87.9% of runD's frames, and reported
    // full confidence while doing it.
    CHECK(quad->area_fraction > 0.15F);
    CHECK(quad->area_fraction < 0.32F);
    CHECK(quad->source == simple::QuadSource::sticker_hull);
}

TEST_CASE("the nine cells are read in row-major order across the quad", "[vision]") {
    const cv::Mat frame = draw_face(kScrambled);
    const simple::PipelineParams params;
    const simple::FaceReading reading = simple::read_face(frame, params);

    REQUIRE(reading.quad.has_value());
    CHECK(reading.unknown_count == 0);
    CHECK(letters(reading) == "RGBYOWGBR");
    // Identity is the centre sticker, and nothing else (M2 in the module
    // header): the middle cell of the drawn face is orange.
    CHECK(reading.centre_color() == FacetColor::orange);
}

TEST_CASE("a frame with no face yields no quad and no reading", "[vision]") {
    const cv::Mat empty(480, 640, CV_8UC3, cv::Scalar(90, 90, 90));
    const simple::PipelineParams params;
    const simple::FaceReading reading = simple::read_face(empty, params);

    CHECK_FALSE(reading.quad.has_value());
    CHECK_FALSE(reading.complete());
    CHECK(reading.centre_color() == FacetColor::unknown);
}

TEST_CASE("a cell whose patch is split between two colours abstains", "[vision]") {
    cv::Mat frame = draw_face(kScrambled);
    // Repaint the left half of the centre tile, so its sample disc straddles
    // two colours and neither can claim the majority the gate demands. This
    // is the synthetic form of the corpus's measured failure: a sample that
    // lands across a sticker boundary. It must read UNKNOWN, not whichever
    // half won by a pixel (M3).
    cv::rectangle(frame, cv::Rect(185 + 90, 105 + 90, 45, 90), sticker_bgr(FacetColor::green),
                  cv::FILLED);
    const simple::PipelineParams params;
    const simple::FaceReading reading = simple::read_face(frame, params);

    REQUIRE(reading.quad.has_value());
    CHECK(reading.cells[simple::kCentreCell].color == FacetColor::unknown);
    CHECK(reading.cells[simple::kCentreCell].purity < params.min_purity);
    CHECK_FALSE(reading.complete());
}

TEST_CASE("the face store keys faces by centre colour and counts disagreement", "[vision]") {
    simple::FaceStore store;
    const simple::PipelineParams params;

    const simple::FaceReading first = simple::read_face(draw_face(kScrambled), params);
    REQUIRE(first.complete());
    CHECK(store.offer(first));
    CHECK(store.filled_count() == 1);
    CHECK(store.slot(FacetColor::orange).filled);
    CHECK(store.slot(FacetColor::orange).cells[simple::kCentreCell] == FacetColor::orange);

    // The same face at a different position and scale is the SAME face -
    // identity is the centre colour, not where the quad landed - and it must
    // agree, so it fills no new slot and records no conflict.
    const simple::FaceReading again =
        simple::read_face(draw_face(kScrambled, 250, 150, 213), params);
    REQUIRE(again.complete());
    CHECK_FALSE(store.offer(again));
    CHECK(store.filled_count() == 1);
    CHECK(store.slot(FacetColor::orange).readings == 2);
    CHECK(store.slot(FacetColor::orange).conflicts == 0);

    // A different face with the SAME centre must not overwrite the kept
    // reading, and must be counted as the disagreement it is.
    std::array<FacetColor, 9> altered = kScrambled;
    altered[0] = FacetColor::white;
    const simple::FaceReading conflicting = simple::read_face(draw_face(altered), params);
    REQUIRE(conflicting.complete());
    CHECK_FALSE(store.offer(conflicting));
    CHECK(store.slot(FacetColor::orange).conflicts == 1);
    CHECK(store.slot(FacetColor::orange).cells[0] == FacetColor::red);
}
