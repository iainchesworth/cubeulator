#pragma once

#include <cube/error.hpp>
#include <cube/model/facelet.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/solver/cube.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/prepared_frame.hpp>
#include <cube/vision/read_set.hpp>
#include <cube/vision/side_grid.hpp>

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
//   frame -> sticker blobs -> N x N lattice -> N^2 Lab samples  (read_side)
//   reads -> grouped into sides by their own content            (SideScan)
//   six sides -> resolve_colors -> assemble                     (SideScan::cube)
//
// A side is identified by what is on it, so nothing needs to know how the
// cube is oriented or which side came before. See docs/recovery-plan.md for
// why the scan is built this way.
//
// Everything is written for a cube of N x N x N, N from 2 to 9, and is a 3x3
// when N is not said: a side is N^2 cells numbered row * N + column (side_grid.hpp),
// thresholds that are about the stickers are stated in sticker spacings, and the
// 3x3 is the case the numbers were measured on. What is validated beyond it, and
// what is not, is in docs/nxn-app-plan.md (gate S0): odd sizes are read and
// assembled end to end; an even size has no middle sticker, so its sides can be
// read here but not yet named or assembled; the corner reader reads any size up to kMaxCornerSize.

// One frame's read of the side facing the camera.
//
// Its vectors are a 3x3's nine zeroes until the reader fills them, as the arrays
// they were.
struct SideRead {
    // The cube's size: the side is size x size stickers.
    int size = 3;
    // Sticker centres in the input frame's pixels, row-major as the camera
    // sees them: index 0 is the sticker nearest the image's top-left.
    std::vector<cv::Point2f> centres = std::vector<cv::Point2f>(9);
    // Float CIELAB (L in 0..100) at each centre, the median of a small patch.
    std::vector<cv::Vec3f> lab = std::vector<cv::Vec3f>(9);
    // Sticker spacing in the input frame's pixels.
    float pitch = 0.0F;
    // How many of the N^2 stickers were found as blobs; the rest of the
    // lattice is placed by the fit through those.
    int stickers_found = 0;
    // Which of them they were, in the same order as centres and lab. A
    // sticker that was not found is usually one with a finger across it; its
    // place is still sampled, and what is sampled may be the finger.
    std::vector<bool> found = std::vector<bool>(9, false);
};

// Finds the N x N sticker lattice of a side held roughly square to the camera
// and samples its colours. Relies on darker plastic between and around
// the stickers; returns nothing for a side seen at an angle, a partial
// lattice that does not span N rows and N columns, or no cube. `size` is the
// cube's: the caller says it (it is chosen, never inferred).
//
// A frame with six or more sticker-sized outlines in it and no side among them
// is looked at a second time, against a narrower block (narrow_contours,
// prepared_frame.hpp): a dark sticker beside a bright one is clipped by the
// first, and a lattice the second finds is kept only if it sits on plastic
// darker than its darkest sticker. A side the first look reads is read as it
// was.
//
// `bgr` is an 8-bit BGR frame of any size; frames wider than the working
// width are reduced first, so the cost beyond that reduction does not grow
// with camera resolution.
[[nodiscard]] std::optional<SideRead> read_side(const cv::Mat& bgr, int size = 3);
// The same read from a frame already prepared (prepared_frame.hpp), for a
// caller that hands the frame to more than one reader. The result is the one
// read_side(bgr) gives for the frame it was prepared from.
[[nodiscard]] std::optional<SideRead> read_side(const PreparedFrame& frame, int size = 3);

// A read counts only if the side has been held still: its centre within this
// fraction of a sticker spacing of where the frame before had it. A side still
// moving is blurred, and its colours are not to be trusted. Blur is judged
// against the sticker, so the distance is in spacings and a small sticker is
// held to a smaller shake.
inline constexpr float kSteadyFraction = 0.12F;
// The point of a read whose motion is judged: the middle sticker of an odd
// size, the middle of the four middle stickers of an even one.
[[nodiscard]] cv::Point2f steady_point(const SideRead& read);
[[nodiscard]] bool held_still(const SideRead& read, const cv::Point2f& previous_centre);

// kSteadyFraction is how far a side may move between two frames a camera's
// nominal interval apart. A side turned at the same speed moves further between
// frames that are further apart - a webcam in lamplight settles at 20 frames a
// second or less, and a frame the page was late for is two - while its blur,
// which is what the gate is for, is the same. SideScan::add is told how long it
// was since the frame before, and the distance allowed grows with it, to at
// most kMaxIntervalScale times the nominal one so that a long stall does not
// let a side that was being turned count.
inline constexpr float kNominalFrameMs = 33.0F;
inline constexpr float kMaxIntervalScale = 4.0F;
[[nodiscard]] float interval_scale(float interval_ms);

// A side of a corner view counts as a read of its side only if it was seen
// whole enough and not too foreshortened. The ten sides the recorded sessions
// show in two-side views (cube_scan corner-colours) sat 3 to 7 Lab units from
// their side but one: a strip with six of nine found and a step under half the
// other, at 12, the same-side distance. Another six-of-nine strip sat at 6, so
// these are a margin and not a measured edge: a sticker the fit placed is what
// a finger looks like, and ten sides are too few to say where the line is.
inline constexpr int kMinCornerFound = 7;
inline constexpr float kMinCornerAspect = 0.45F;

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
    // How far from neutral the light was, read from the cube's own white
    // centre (WhiteReference::cast); 0 when no white could be told. 3 to 5 by
    // daylight, 29 under a warm-white LED.
    float cast = 0.0F;
};

// The same for a cube of any size: what the app holds (decision 1 of
// docs/nxn-app-plan.md). At size 3 it is ScannedCube's cube lifted to an NCube.
struct ScannedCubeN {
    nxn::NCube cube;
    SessionColours colours{};
    float cast = 0.0F;
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
    // When the scan is ambiguous: the colours of the centres of the three sides
    // that meet at the corner whose view would tell the most of the cubes still
    // possible apart (those the corner reads so far fit best), in the order of
    // solver::Color. Showing that corner - two or
    // three sides in one view - is what settles the scan; a view of sides the
    // candidates agree on, however long held, does not. Empty when nothing
    // would, or when the scan is not ambiguous.
    std::optional<std::array<solver::Color, 3>> settling_corner;
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
        // The N^2 fused colours, laid out as the side was FIRST seen - the
        // layout resolve_colors names. SideScan::sides() lists the same
        // turned `roll` quarter-turns, to how the side was last shown.
        std::vector<cv::Vec3f> lab;
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
    // The cube's own white, which the sides were named by: how far from
    // neutral the light was (white->cast) and the gains that took it out.
    // Empty until six sides are captured, and when none was plausibly white.
    std::optional<WhiteReference> white;
    // How the colours were named; empty until six sides are captured, and
    // when naming failed.
    std::optional<ColorResolution> naming;
    // How the cube was assembled; empty when there was no naming.
    std::optional<AssemblyTrace> assembly;
    // What SideScan::scanned() answers (a 3x3 scan only: another size answers
    // invalid_cube_state here and in `result_n` what scanned_n() does).
    Result<ScannedCube> result = std::unexpected(CubeError::face_capture_incomplete);
    // What SideScan::scanned_n() answers, for a scan of any size.
    Result<ScannedCubeN> result_n = std::unexpected(CubeError::face_capture_incomplete);
};

// Six sides that make no cube are not put together again while nothing the
// attempt rested on has changed. Naming the 54 stickers costs about 0.3 ms and
// is done every time; assembling the names costs 55 ms on average where no cube
// can be made (15 pairs of name exchanges of 4,096 roll combinations each, up
// to 104 ms native on the tumbles measured) and 6 ms where there are several.
// The colours of the side in view move a little with every read, so a failure
// that persisted was assembled afresh for every frame that counted.
//
// What assembling rests on, by how the naming came out:
//   - no cube to be made: the names alone. A new name on any sticker is a new
//     assembly; colours that moved without changing a name are not.
//   - several cubes: the names, the corner reads (any added or replaced), and
//     what each of the six colours measured, which the corner reads are
//     fitted in. Those must be within this many Lab units (plain Euclidean) of
//     what they were at the last assembly.
inline constexpr float kRejudgeLabDistance = 1.0F;

// Accumulates reads over one scan and reports the sides seen so far and,
// once six are in, the cube.
//
// Not safe to share between threads, const calls included: scanned(), cube()
// and diagnose() keep the last failed assembly (kRejudgeLabDistance).
class SideScan {
public:
    // Reads a side needs before it counts as captured. One read is a single
    // frame; eight is about a third of a second of holding still.
    static constexpr std::size_t kMinReads = 8;

    // A scan of a cube of `size` x `size` x `size` (a 3x3 unless said).
    explicit SideScan(int size = 3);

    [[nodiscard]] int size() const noexcept { return size_; }

    struct Side {
        // Fused colours, in the orientation the side was last shown in.
        std::vector<cv::Vec3f> lab;
        std::size_t reads = 0;
        // True when the most recent frame's read belongs to this side.
        bool in_view = false;
    };

    // Feed every analysed frame in order, including frames with no read: a
    // read only counts when the lattice has held still since the frame before.
    //
    // `interval_ms` is how long it was since the frame before; zero or less
    // says nobody knows, and the nominal interval is taken (interval_scale).
    void add(const std::optional<SideRead>& read, float interval_ms = 0.0F);
    void reset();

    // Feed the frame's corner read - two or three sides in one view - when
    // it has one, and only for a frame that had no square-on read. The corner
    // reader is a 3x3's: on another size this does nothing.
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
    void add_corner(const std::optional<CornerRead>& read, float interval_ms = 0.0F);
    // Corner reads kept so far; the newest few dozen.
    [[nodiscard]] std::size_t corner_reads() const noexcept { return corners_.size(); }
    // How many times named sides have been put together into a cube since the
    // scan began: the costly half of judging them. scanned(), cube() and
    // diagnose() ask for the same judgement, and an assembly that failed is not
    // made again while what it rested on is as it was (kRejudgeLabDistance):
    // that answer is the last one, and is not counted here.
    [[nodiscard]] std::size_t assemblies() const noexcept { return assemblies_; }

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
    // The same, with what each colour measured: the median over the stickers
    // of that colour, so one odd sticker (a logo on a centre) does not stand
    // for its colour.
    //
    // These two are the 3x3's: they answer invalid_cube_state for another
    // size. cube_n() and scanned_n() are the same for a scan of any size, and
    // what the app reads once it holds an NCube.
    [[nodiscard]] Result<ScannedCube> scanned() const;
    [[nodiscard]] Result<nxn::NCube> cube_n() const;
    [[nodiscard]] Result<ScannedCubeN> scanned_n() const;
    // scanned() with the working kept: the sides it used, how the colours
    // were named, how the cube was assembled. The same evaluation, so
    // `result` is exactly what scanned() returns; it costs what scanned()
    // costs plus copying the answer out, and nothing in the app calls it
    // unless a developer has asked (docs/diagnostics.md).
    [[nodiscard]] ScanDiagnosis diagnose() const;

private:
    struct Group {
        std::vector<cv::Vec3f> fused;
        // Every read of the side, square-on or from a corner view.
        ReadSet reads;
        // The square-on ones among them, which the colours come from once
        // there are enough.
        ReadSet flat_reads;
        int roll = 0;
    };

    // The last assembly that failed, with what it rested on.
    struct FailedAssembly {
        // The name each sticker was given, side by side in the order the sides
        // were named in.
        std::array<std::vector<model::FacetColor>, 6> names{};
        // What each colour measured, and the corner reads there were: what
        // choosing among several cubes rests on.
        SessionColours colours{};
        std::uint64_t corner_revision = 0;
        AssemblyTrace trace;
        CubeError error = CubeError::invalid_cube_state;
    };

    [[nodiscard]] std::vector<std::size_t> captured_by_support() const;
    // Names six sides and puts them together, or answers as the last failed
    // assembly did when nothing it rested on has changed
    // (kRejudgeLabDistance). Only naming, assembly and result are filled.
    [[nodiscard]] ScanDiagnosis judge(const std::array<CapturedFace, 6>& faces) const;
    // Counts one read of N^2 colours towards the side it is, or starts a
    // side with it, and puts that side among those in view. A read from a
    // corner view that could be either of two sides is not counted, and
    // nothing is started. Says whether it was counted.
    bool count_read(const std::vector<cv::Vec3f>& lab, bool square_on);
    // Two groups that have turned out to be one side: the earlier one, the
    // later one, and the quarter-turn that lays the later on the earlier.
    struct AlikePair {
        std::size_t keep = 0;
        std::size_t other = 0;
        int roll = 0;
    };
    // The first such pair, in the order the groups were begun.
    [[nodiscard]] std::optional<AlikePair> first_alike_pair() const;
    // Joins the later group of `pair` into the earlier and renumbers the
    // sides in view.
    void absorb(const AlikePair& pair);
    // Joins groups that have turned out to be one side.
    void merge_alike_groups();
    // Bounds the groups kept, dropping the least-supported stray.
    void drop_weakest_stray();

    int size_ = 3;
    std::vector<Group> groups_;
    std::vector<CornerRead> corners_;
    // Counts every change to corners_ (a read added, an old one dropped), so
    // that "the same corner reads" is one comparison.
    std::uint64_t corner_revision_ = 0;
    // Kept by const calls: what they answer is a function of the scan, and
    // this is only how often it is worked out.
    mutable std::optional<FailedAssembly> failed_;
    mutable std::size_t assemblies_ = 0;
    std::optional<cv::Point2f> previous_centre_;
    // Where each side's centre was in the previous corner frame, for the rule
    // that a side counts only if it has held still.
    std::vector<cv::Point2f> previous_corner_centres_;
    // The sides the frame just added counted towards.
    std::vector<std::size_t> in_view_;
};

}  // namespace cube::vision
