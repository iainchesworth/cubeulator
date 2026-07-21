#pragma once

// ---------------------------------------------------------------------------
// A PARALLEL, stateless face-reading pipeline, evaluated against the
// production one - NOT (yet) a replacement for it. Nothing in the shipping
// vision stack calls into here; the only consumer is the hidden
// [.simple-pipeline] replay harness.
//
// WHY IT EXISTS. The production pipeline segments stickers, fits a 3x3
// lattice by RANSAC, places cells by homography, and derives face IDENTITY
// from a 24-element orientation-tracking lattice that is deliberately never
// allowed to consult colour. Measured on the nine-clip corpus that stack
// captures all six faces and yet the 54 samples cannot form a legal cube:
// 0 of 720 namings, 0 of 46080 naming x mirror combinations, and a ~9.4M
// candidate partition search all returned zero valid assemblies; built = 0
// of 4096 rotations, i.e. the stickers never form 26 legitimate cubies.
//
// Three measurements from that post-mortem drive every design choice below,
// and are cited again at the code they justify:
//
//   (M1) CENTRES ARE THE RELIABLE SIGNAL. Centre stickers classified with
//        100% self-consistency across readings, on every face of every
//        clip - by far the most reliable thing the system produces. They
//        are stable because a centre cell sits AT the lattice fit's anchor,
//        while peripheral cells are extrapolated and wander.
//   (M2) EXTRAPOLATED CELLS LAND ON THE WRONG STICKER. Reading instability
//        tracks anchors-detected-per-frame: cells the detector never found
//        were extrapolated from a grid fitted to few anchors and landed on
//        neighbouring stickers or inter-tile gaps. 7 of 54 samples sat
//        below L=35 and two at L=9.1/12.9 - shadow, gap, or off-face
//        background, i.e. not stickers at all.
//   (M3) THE WRONG STICKERS WERE NOT THE DOUBTED ONES. A two-swap repair
//        over the least-confident cells failed, because the misreads were
//        confident. Silence is worth more than a nearest guess: an explicit
//        UNKNOWN turns a wrong sticker into a MISSING one, which a
//        census-constrained resolver can treat as a free variable, whereas
//        a confidently wrong sticker poisons the assembly.
//
// The four inversions, each answering one of the failures above:
//
//   1. NO TEMPORAL STATE. Every frame is processed independently. No
//      orientation tracking, no epochs, no gauge, no registration, no
//      cross-frame fusion. This deletes the entire class of failure that
//      capped the production system at 2-3 usable faces, at the price of
//      giving up every benefit tracking was supposed to buy - which,
//      measured, was none.
//   2. FACE IDENTITY IS THE CENTRE STICKER'S COLOUR (M1). A reading is
//      filed under its centre colour. Six distinct centre colours are six
//      distinct faces by construction - a fact of the physical cube, not an
//      inference. This is the exact inversion of the production
//      architecture, which forbids colour from touching identity.
//   3. GEOMETRY FROM THE FACE OUTLINE, NOT FROM SEGMENTED STICKERS (M2).
//      The nine cell centres are read at FIXED FRACTIONS of a recovered
//      face quadrilateral, so no cell is ever extrapolated from a fit to
//      other cells. A cell is either inside a quad the detector actually
//      found, or there is no reading at all.
//   4. ABSTENTION IS A FIRST-CLASS OUTPUT (M3). A cell whose sampled pixels
//      do not agree on one colour reads UNKNOWN, never nearest-guess.
//
// Qt-free and app-free; OpenCV only.
// ---------------------------------------------------------------------------

#include <cube/model/face.hpp>
#include <cube/model/facelet.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace cube::vision::simple {

inline constexpr std::size_t kCellCount = cube::model::facelets_per_face;
inline constexpr std::size_t kCentreCell = cube::model::center_facelet_index;

// Which edge evidence produced a quad. Recorded per reading because the two
// routes are being MEASURED against each other on the corpus, not blended:
// a fraction-of-frames number per source is the whole point of carrying it.
enum class QuadSource : std::uint8_t {
    none,
    // cv::createLineSegmentDetector(LSD_REFINE_STD) segments, rasterised
    // into an edge mask. The hypothesis under test: a white face on a light
    // lattice segments poorly (where production is weakest) but its
    // SILHOUETTE is crisp, so line evidence should survive exactly the
    // frames sticker evidence does not.
    line_segments,
    // Canny edges. The control, and the fallback the reference design also
    // carries - if this wins outright, the LSD route buys nothing here and
    // that is a result worth reporting rather than hiding behind a blend.
    canny,
};

[[nodiscard]] std::string_view to_string(QuadSource source);

// Which colour decision rule named the cells. Both are run and compared on
// the corpus; neither is assumed. See the header comment on
// classify_patch_hsv() in the .cpp for the measured motivation for a
// separate white axis.
enum class ClassifierMode : std::uint8_t {
    // The repo's existing cube::vision::classify_lab - hue-band naming with
    // a chroma floor and a warm-cast white escape, already tuned against
    // this same corpus by the production stack.
    lab_hue_bands,
    // Whites decided on the low-saturation / high-value axis BEFORE any
    // chroma comparison, so white never has to out-chroma a crushed blue.
    hsv_white_axis,
};

[[nodiscard]] std::string_view to_string(ClassifierMode mode);

// A face quadrilateral in image pixels, corners in clockwise order starting
// from the corner nearest the image origin. The starting corner is an
// arbitrary but DETERMINISTIC choice: which physical corner it is cannot be
// known from one frame, and does not need to be - the assembler searches
// all four in-plane rotations per face anyway (scan_assembly.hpp).
struct FaceQuad {
    std::array<cv::Point2f, 4> corners{};
    QuadSource source = QuadSource::none;
    // Fraction of the frame area the quad covers, and the ratio of its
    // longest to shortest side. Reported so a harness can characterise what
    // was accepted rather than trusting that acceptance was correct.
    float area_fraction = 0.0F;
    float side_ratio = 0.0F;
};

// One sampled cell. `purity` is the fraction of the patch's pixels that
// voted for `color` - the abstention statistic, kept even when the cell
// abstained so a harness can see HOW undecided it was.
struct CellReading {
    cube::model::FacetColor color = cube::model::FacetColor::unknown;
    float purity = 0.0F;
    cv::Vec3f lab{};       // patch median, CIE Lab (L 0-100, a/b signed)
    cv::Point2f centre{};  // image-space patch centre, for overlay/debug
    int pixels = 0;
};

// Everything one frame yielded. A reading with quad == nullopt produced
// nothing; a reading whose centre cell abstained has no identity and cannot
// be filed (see FaceStore::offer).
struct FaceReading {
    std::optional<FaceQuad> quad;
    std::array<CellReading, kCellCount> cells{};
    int unknown_count = static_cast<int>(kCellCount);

    [[nodiscard]] cube::model::FacetColor centre_color() const {
        return cells[kCentreCell].color;
    }
    // Usable == a quad was found, the centre named itself, and every cell
    // committed. Deliberately strict: with no temporal state there is no
    // later frame to repair a hole from, so a partial reading is only worth
    // keeping if something downstream can use holes, and nothing does yet.
    [[nodiscard]] bool complete() const {
        return quad.has_value() && unknown_count == 0;
    }
};

struct PipelineParams {
    ClassifierMode classifier = ClassifierMode::lab_hue_bands;

    // Quad acceptance. All four are shape sanity, not tuning knobs against
    // an expected answer: they reject things that cannot be a cube face
    // seen by a hand-held camera, and are set an order of magnitude wide of
    // the corpus's actual framing (the cube covers ~7% of a 640x480 frame
    // in runD) so no clip can be passed by narrowing them.
    float min_area_fraction = 0.01F;
    float max_area_fraction = 0.60F;
    float max_side_ratio = 1.8F;      // opposite sides under perspective
    float min_corner_deg = 50.0F;     // a quad, not a sliver
    float max_corner_deg = 130.0F;

    // Sampling. Cell centres sit at {1/6, 1/2, 5/6} along both quad
    // directions - the exact centres of a 3x3 division - and the patch
    // radius is a fraction of the CELL pitch, so a patch stays well inside
    // its sticker at any scale. 0.28 of a third of the face leaves roughly
    // a fifth of the cell as margin on each side, which is more than the
    // inter-tile gap ever occupies.
    float patch_radius_cells = 0.28F;

    // Abstention (M3). A colour must claim this fraction of the patch's
    // pixels or the cell reads unknown. 0.60 is a clear majority without
    // demanding the near-unanimity a glossy sticker's specular highlight
    // makes impossible.
    float min_purity = 0.60F;
};

// The whole per-frame pipeline: quad, sample, classify, abstain. Pure -
// same frame in, same reading out, no state anywhere.
//
// `bgr` is a colour frame at whatever scale the caller has; nothing here
// depends on resolution beyond the area fraction gate.
[[nodiscard]] FaceReading read_face(const cv::Mat& bgr, const PipelineParams& params);

// The two quad routes, exposed separately so the harness can report a
// per-source recovery rate instead of only the combined one.
[[nodiscard]] std::optional<FaceQuad> detect_quad_line_segments(const cv::Mat& bgr,
                                                               const PipelineParams& params);
[[nodiscard]] std::optional<FaceQuad> detect_quad_canny(const cv::Mat& bgr,
                                                        const PipelineParams& params);

// ---------------------------------------------------------------------------
// The face store: six slots keyed by CENTRE COLOUR (M1).
//
// There is no "which face is this" question to get wrong. A reading whose
// centre reads green IS the green face; if a later reading also reads green
// it is the same face again, and the two must agree. Disagreement is
// therefore directly measurable evidence about the sampler, which is why
// the store counts it rather than silently keeping the newest.
// ---------------------------------------------------------------------------
class FaceStore {
public:
    struct Slot {
        bool filled = false;
        std::array<cube::model::FacetColor, kCellCount> cells{};
        int readings = 0;   // how many complete readings named this centre
        int conflicts = 0;  // how many of them disagreed with the kept one
    };

    // Files a reading under its centre colour. Ignores readings that are
    // not complete() and readings whose centre is unknown - an unidentified
    // face has nowhere to go.
    //
    // FIRST COMPLETE READING WINS. Not majority-vote, not most-recent: a
    // vote across frames is temporal state by another name, and the point
    // of this pipeline is to find out whether ONE frame is enough. If it is
    // not, `conflicts` says so in numbers.
    bool offer(const FaceReading& reading);

    [[nodiscard]] const Slot& slot(cube::model::FacetColor centre) const;
    [[nodiscard]] std::size_t filled_count() const;
    // The six slots in FacetColor order (white, yellow, red, orange, green,
    // blue), shaped for cube::solver::assemble_scanned_faces_with_mapping.
    // nullopt unless all six are filled.
    [[nodiscard]] std::optional<std::array<std::array<cube::model::Facelet, kCellCount>, 6>>
    as_faces() const;

private:
    static constexpr std::size_t kSlotCount = 6;
    [[nodiscard]] static std::size_t slot_index(cube::model::FacetColor centre);
    std::array<Slot, kSlotCount> slots_{};
    Slot empty_{};
};

}  // namespace cube::vision::simple
