#pragma once

#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/prepared_frame.hpp>

#include <opencv2/core.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cube::vision {

// Direct scanning: read whichever side of the cube is held square to the
// camera, with no pose tracker and no learned model in the way.
//
//   frame -> sticker blobs -> 3x3 lattice -> nine Lab samples   (read_side)
//   reads -> grouped into sides by their own content            (SideScan)
//   six sides -> resolve_colors -> assemble_scanned_faces       (SideScan::cube)
//
// A side is identified by what is on it, so nothing needs to know how the
// cube is oriented or which side came before. See docs/recovery-plan.md for
// why the scan is built this way.

// One frame's read of the side facing the camera.
struct SideRead {
    // Sticker centres in the input frame's pixels, row-major as the camera
    // sees them: index 0 is the sticker nearest the image's top-left.
    std::array<cv::Point2f, 9> centres{};
    // Float CIELAB (L in 0..100) at each centre, the median of a small patch.
    std::array<cv::Vec3f, 9> lab{};
    // Sticker spacing in the input frame's pixels.
    float pitch = 0.0F;
    // How many of the nine stickers were found as blobs; the rest of the
    // lattice is placed by the fit through those.
    int stickers_found = 0;
    // Which of the nine they were, in the same order as centres and lab. A
    // sticker that was not found is usually one with a finger across it; its
    // place is still sampled, and what is sampled may be the finger.
    std::array<bool, 9> found{};
};

// Finds the 3x3 sticker lattice of a side held roughly square to the camera
// and samples its nine colours. Relies on darker plastic between and around
// the stickers; returns nothing for a side seen at an angle, a partial
// lattice that does not span three rows and three columns, or no cube.
//
// `bgr` is an 8-bit BGR frame of any size; frames wider than the working
// width are reduced first, so the cost beyond that reduction does not grow
// with camera resolution.
[[nodiscard]] std::optional<SideRead> read_side(const cv::Mat& bgr);
// The same read from a frame already prepared (prepared_frame.hpp), for a
// caller that hands the frame to more than one reader. The result is the one
// read_side(bgr) gives for the frame it was prepared from.
[[nodiscard]] std::optional<SideRead> read_side(const PreparedFrame& frame);

// A read counts only if the side has been held still: its centre sticker
// within this fraction of a sticker spacing of where the frame before had it.
// A side still moving is blurred, and its colours are not to be trusted.
inline constexpr float kSteadyFraction = 0.12F;
[[nodiscard]] bool held_still(const SideRead& read, const cv::Point2f& previous_centre);

// Display colour for a fused Lab sample (8-bit BGR), for showing the user
// what was read before the colours have been named.
[[nodiscard]] cv::Vec3b lab_to_bgr(const cv::Vec3f& lab);

// How far apart two sticker colours are (float CIELAB in, Lab units out),
// with lightness and chroma counted for a quarter of what hue is: shading
// moves the first two and leaves hue alone, so a sticker in shadow stays
// close to its own colour.
[[nodiscard]] float colour_distance(const cv::Vec3f& a, const cv::Vec3f& b);

// What each of the six sticker colours measured during one scan, indexed by
// solver::Color. Anything read later in the same session is compared with
// these and not with any idea of what "red" ought to measure: the camera and
// the light decide that, and they differ from one session to the next.
using SessionColours = std::array<cv::Vec3f, 6>;

// A finished scan: the cube, and the colours it was read in.
struct ScannedCube {
    solver::Cube cube;
    SessionColours colours{};
};

// Which way the named sides were put together into a cube (or not).
enum class AssemblyPath : std::uint8_t {
    // The naming makes exactly one cube.
    single_candidate,
    // It makes several, and the corner reads fit one of them better than any
    // other.
    chosen_by_corners,
    // It makes several, and the corner reads so far cannot say which:
    // CubeError::ambiguous_scan.
    ambiguous,
    // No candidate was listed, but assembling directly made a cube.
    direct,
    // The naming makes no cube, and trading the names of two centres does.
    names_exchanged,
    // Nothing assembled, traded or not.
    failed,
};

// The pair of names exchanged to make a cube, when one was.
struct NameExchange {
    // The two sides, as indices into the six handed to resolve_colors (see
    // ScanDiagnosis::sides).
    std::size_t first_side = 0;
    std::size_t second_side = 0;
    // The names those two centres carried before the trade.
    cube::model::FacetColor first_name = cube::model::FacetColor::unknown;
    cube::model::FacetColor second_name = cube::model::FacetColor::unknown;
    // colour_distance between the two centres: the closest-looking pair wins.
    float centre_distance = 0.0F;
};

// How the cube was put together from the six named sides, or why it was not:
// the working of the assembly step, read-only, for whoever is finding out why
// a scan came out as it did (docs/diagnostics.md).
struct AssemblyTrace {
    AssemblyPath path = AssemblyPath::failed;
    // How many cubes the naming makes as it stands.
    std::size_t candidates = 0;
    // When there were several: how many of the corner reads fit each.
    std::vector<std::size_t> corner_fits;
    std::size_t corner_reads = 0;
    // The candidate the corner reads chose.
    std::optional<std::size_t> chosen;
    // Why assembling the naming as it stands failed, when it was tried.
    std::optional<CubeError> direct_error;
    std::optional<NameExchange> exchange;
    // Why there is no cube.
    std::optional<CubeError> error;
};

// What one evaluation of a scan did, end to end, as plain data. The same
// evaluation SideScan::scanned() makes, with its working kept.
struct ScanDiagnosis {
    // One captured side.
    struct Side {
        // Its place in the order sides were first shown: the order
        // SideScan::sides() lists them in.
        std::size_t first_seen = 0;
        std::size_t reads = 0;
        // The nine fused colours, laid out as the side was FIRST seen - the
        // layout resolve_colors names. SideScan::sides() lists the same nine
        // turned `roll` quarter-turns, to how the side was last shown.
        std::array<cv::Vec3f, 9> lab{};
        int roll = 0;
        // The most recent frame's read belongs to this side.
        bool in_view = false;
    };

    // Reads grouped so far, strays included, and how many of those have
    // enough reads to count as a side.
    std::size_t groups = 0;
    std::size_t captured = 0;
    std::size_t corner_reads = 0;
    // The captured sides, at most six, in the order resolve_colors takes them
    // (most reads first) - which is the order `naming` and `assembly` use.
    std::vector<Side> sides;
    // How the colours were named; empty until six sides are captured, and
    // when naming failed.
    std::optional<ColorResolution> naming;
    // How the cube was assembled; empty when there was no naming.
    std::optional<AssemblyTrace> assembly;
    // What SideScan::scanned() answers.
    Result<ScannedCube> result = std::unexpected(CubeError::face_capture_incomplete);
};

// Accumulates reads over one scan and reports the sides seen so far and,
// once six are in, the cube.
class SideScan {
public:
    // Reads a side needs before it counts as captured. One read is a single
    // frame; eight is about a third of a second of holding still.
    static constexpr std::size_t kMinReads = 8;

    struct Side {
        // Fused colours, in the orientation the side was last shown in.
        std::array<cv::Vec3f, 9> lab{};
        std::size_t reads = 0;
        // True when the most recent frame's read belongs to this side.
        bool in_view = false;
    };

    // Feed every analysed frame in order, including frames with no read: a
    // read only counts when the lattice has held still since the frame before.
    void add(const std::optional<SideRead>& read);
    void reset();

    // Feed the frame's corner read - two or three sides in one view - when
    // it has one, and only for a frame that had no square-on read.
    //
    // Two things come of it. Square-on reads cannot say which way up each
    // side is relative to its neighbours; a corner read can, and it is what
    // settles the cubes that six flat sides put together two ways (R5 in
    // docs/recovery-plan.md). And a cube held on a corner shows up to three
    // sides at once, so each side of the read that was seen whole enough
    // counts as a read of that side, as a square-on read does, subject to the
    // same rule that it has held still: three sides are captured in the time
    // it takes one. A side seen at an angle is lit a little differently from
    // one seen square-on, so once a side has kMinReads square-on reads of its
    // own its colours come from those alone.
    void add_corner(const std::optional<CornerRead>& read);
    // Corner reads kept so far; the newest few dozen.
    [[nodiscard]] std::size_t corner_reads() const noexcept { return corners_.size(); }

    // True when the frame just added counted as a read of some side, whether
    // or not that side has enough reads to be captured yet.
    [[nodiscard]] bool reading() const noexcept { return !in_view_.empty(); }
    // How many reads the side in the frame just added has, captured or not;
    // zero when that frame was not a read. With several sides in view (a
    // corner), the one with the fewest: the side that is still holding the
    // capture up. Against kMinReads this is how far the sides being held are
    // towards counting.
    [[nodiscard]] std::size_t reads_in_view() const noexcept;

    // The captured sides, in the order they were first seen. At most six: if
    // more groups have been captured (a stray lattice, a side re-captured
    // under changed light) these are the six with the most reads.
    [[nodiscard]] std::vector<Side> sides() const;

    // The cube, once six captured sides make exactly one physically valid
    // cube. face_capture_incomplete until six sides are in;
    // no_valid_cube_detected or invalid_cube_state while they do not yet
    // make one (a misread side, or the same side captured twice), and
    // ambiguous_scan when they make more than one and the corner reads so
    // far do not say which.
    [[nodiscard]] Result<solver::Cube> cube() const;
    // The same, with what each colour measured: the median over the nine
    // stickers of that colour, so one odd sticker (a logo on a centre) does
    // not stand for its colour.
    [[nodiscard]] Result<ScannedCube> scanned() const;
    // scanned() with the working kept: the sides it used, how the colours
    // were named, how the cube was assembled. The same evaluation, so
    // `result` is exactly what scanned() returns; it costs what scanned()
    // costs plus copying the answer out, and nothing in the app calls it
    // unless a developer has asked (docs/diagnostics.md).
    [[nodiscard]] ScanDiagnosis diagnose() const;

private:
    struct Group {
        std::array<cv::Vec3f, 9> fused{};
        // Every read of the side, square-on or from a corner view.
        std::vector<std::array<cv::Vec3f, 9>> reads;
        // The square-on ones among them, which the colours come from once
        // there are enough.
        std::vector<std::array<cv::Vec3f, 9>> flat_reads;
        int roll = 0;
    };

    [[nodiscard]] std::vector<std::size_t> captured_by_support() const;
    // Counts one read of nine colours towards the nearest side, or starts a
    // side with it, and says which. A read from a corner view that is nearly
    // as close to a second side is not counted, and nothing is said.
    std::optional<std::size_t> count_read(const std::array<cv::Vec3f, 9>& lab, bool square_on);

    std::vector<Group> groups_;
    std::vector<CornerRead> corners_;
    std::optional<cv::Point2f> previous_centre_;
    // Where each side's centre was in the previous corner frame, for the rule
    // that a side counts only if it has held still.
    std::vector<cv::Point2f> previous_corner_centres_;
    // The sides the frame just added counted towards.
    std::vector<std::size_t> in_view_;
};

}  // namespace cube::vision
