// cube_scan: replay recorded scans through the direct-scan reader and score
// them. This is the scoreboard of docs/recovery-plan.md - the only gate is a
// real recorded session ending in the exact true cube.
//
//   cube_scan replay <session-dir>   one session: the cube and its solution
//   cube_scan board <root-dir>       every session under root against truth
//   cube_scan holds <session-dir>    how the cube was held, frame by frame
//   cube_scan corners <session-dir>  frames showing two sides at once, and
//                                    whether they fit the true cube
//
// A session is a directory of rec_NNNNNN.jpg frames. Its truth, if known, is
// truth.txt: six words of nine letters from WYROGB, one per side, each side
// row-major as seen from outside in any orientation and any order.

#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/held_cube.hpp>

#include <opencv2/imgcodecs.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using cube::model::Facelet;
using cube::model::FacetColor;
using cube::solver::Color;
using cube::solver::Cube;
using cube::solver::Face;

[[nodiscard]] std::vector<fs::path> frames_of(const fs::path& session) {
    std::vector<fs::path> frames;
    for (const auto& entry : fs::directory_iterator(session)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with("rec_") && name.ends_with(".jpg")) {
            frames.push_back(entry.path());
        }
    }
    std::sort(frames.begin(), frames.end());
    return frames;
}

[[nodiscard]] std::optional<FacetColor> colour_of(char letter) {
    switch (letter) {
        case 'W':
            return FacetColor::white;
        case 'Y':
            return FacetColor::yellow;
        case 'R':
            return FacetColor::red;
        case 'O':
            return FacetColor::orange;
        case 'G':
            return FacetColor::green;
        case 'B':
            return FacetColor::blue;
        default:
            return std::nullopt;
    }
}

[[nodiscard]] char letter_of(FacetColor colour) {
    switch (colour) {
        case FacetColor::white:
            return 'W';
        case FacetColor::yellow:
            return 'Y';
        case FacetColor::red:
            return 'R';
        case FacetColor::orange:
            return 'O';
        case FacetColor::green:
            return 'G';
        case FacetColor::blue:
            return 'B';
        case FacetColor::unknown:
            break;
    }
    return '?';
}

[[nodiscard]] char letter_of(Color colour) {
    switch (colour) {
        case Color::White:
            return 'W';
        case Color::Yellow:
            return 'Y';
        case Color::Red:
            return 'R';
        case Color::Orange:
            return 'O';
        case Color::Green:
            return 'G';
        case Color::Blue:
            return 'B';
    }
    return '?';
}

[[nodiscard]] std::optional<Cube> truth_of(const fs::path& session) {
    std::ifstream in(session / "truth.txt");
    std::array<std::array<Facelet, 9>, 6> sides{};
    for (auto& side : sides) {
        std::string word;
        if (!(in >> word) || word.size() != 9) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 9; ++i) {
            const auto colour = colour_of(word[i]);
            if (!colour.has_value()) {
                return std::nullopt;
            }
            side[i] = Facelet{*colour, 1.0F};
        }
    }
    const auto cube = cube::solver::assemble_scanned_faces(sides);
    return cube.has_value() ? std::optional<Cube>(*cube) : std::nullopt;
}

struct Outcome {
    std::vector<cube::vision::SideScan::Side> captured;
    std::size_t frames = 0;
    std::size_t reads = 0;
    std::size_t corner_reads = 0;
    std::size_t sides = 0;
    std::optional<cube::vision::SessionColours> colours;  // what each colour measured
    std::optional<std::size_t> read_at;                   // first frame at which the cube was known
    std::optional<Cube> cube;                             // the cube at that frame
    std::size_t changes_after = 0;                        // later frames whose cube differed
};

[[nodiscard]] Outcome replay(const fs::path& session) {
    Outcome outcome;
    cube::vision::SideScan scan;
    for (const fs::path& frame : frames_of(session)) {
        const cv::Mat bgr = cv::imread(frame.string(), cv::IMREAD_COLOR);
        const auto read = cube::vision::read_side(bgr);
        outcome.reads += read.has_value() ? 1U : 0U;
        scan.add(read);
        if (!read.has_value()) {
            scan.add_corner(cube::vision::read_corner(bgr));
        }
        if (const auto cube = scan.cube(); cube.has_value()) {
            if (!outcome.cube.has_value()) {
                outcome.cube = *cube;
                outcome.read_at = outcome.frames;
            } else if (!(*cube == *outcome.cube)) {
                ++outcome.changes_after;
            }
        }
        ++outcome.frames;
    }
    outcome.captured = scan.sides();
    outcome.sides = outcome.captured.size();
    outcome.corner_reads = scan.corner_reads();
    if (const auto whole = scan.scanned(); whole.has_value()) {
        outcome.colours = whole->colours;
    }
    return outcome;
}

[[nodiscard]] std::string layout_of(const Cube& cube) {
    std::string out;
    constexpr std::array<std::pair<Face, char>, 6> faces = {{{Face::U, 'U'},
                                                             {Face::D, 'D'},
                                                             {Face::L, 'L'},
                                                             {Face::R, 'R'},
                                                             {Face::F, 'F'},
                                                             {Face::B, 'B'}}};
    for (const auto& [face, name] : faces) {
        out += fmt::format("{}:", name);
        for (const Color colour : cube.face(face)) {
            out += letter_of(colour);
        }
        out += ' ';
    }
    return out;
}

[[nodiscard]] std::string_view stage_name(cube::solver::Stage stage) {
    switch (stage) {
        case cube::solver::Stage::Cross:
            return "white cross";
        case cube::solver::Stage::WhiteCorners:
            return "white corners";
        case cube::solver::Stage::MiddleEdges:
            return "middle edges";
        case cube::solver::Stage::Oll:
            return "yellow face";
        case cube::solver::Stage::Pll:
            return "last layer";
        case cube::solver::Stage::F2l:
            return "first two layers";
        case cube::solver::Stage::Reduce:
            return "reduce";
        case cube::solver::Stage::Finish:
            return "finish";
    }
    return "";
}

// Why six captured sides did not make a cube: what each centre measured and
// what the colour naming made of the side.
void explain(const Outcome& outcome) {
    if (outcome.captured.size() < 6) {
        return;
    }
    std::array<cube::vision::CapturedFace, 6> faces{};
    for (std::size_t i = 0; i < 6; ++i) {
        faces[i].lab = outcome.captured[i].lab;
    }
    const auto named = cube::vision::resolve_colors(faces);
    for (std::size_t i = 0; i < 6; ++i) {
        std::string letters(9, ' ');
        if (named.has_value()) {
            for (std::size_t cell = 0; cell < 9; ++cell) {
                letters[cell] = letter_of((*named)[i][cell].color);
            }
        }
        const cv::Vec3f& centre = faces[i].lab[4];
        fmt::print("    side {} named {}  centre Lab {:5.1f} {:6.1f} {:6.1f}  reads {}\n", i,
                   letters, centre[0], centre[1], centre[2], outcome.captured[i].reads);
    }
    if (!named.has_value()) {
        fmt::print("  colour naming failed: {}\n", to_string(named.error()));
        return;
    }
    const auto cube = cube::solver::assemble_scanned_faces(*named);
    fmt::print("  assembly: {}\n", cube.has_value() ? "ok" : to_string(cube.error()));
}

int run_replay(const fs::path& session) {
    const Outcome outcome = replay(session);
    fmt::print("{}: frames {}  lattice reads {}  corner reads {}  sides {}\n",
               session.filename().string(), outcome.frames, outcome.reads, outcome.corner_reads,
               outcome.sides);
    if (!outcome.cube.has_value()) {
        fmt::print("  no cube: {} of 6 sides captured\n", outcome.sides);
        explain(outcome);
        return 1;
    }
    fmt::print("  cube known at frame {} ({} later frames disagreed)\n", *outcome.read_at,
               outcome.changes_after);
    fmt::print("  {}\n", layout_of(*outcome.cube));
    if (const auto truth = truth_of(session); truth.has_value()) {
        fmt::print("  truth: {}\n", *truth == *outcome.cube ? "MATCH" : "WRONG");
    }
    const auto solution = cube::solver::solve(*outcome.cube);
    if (!solution.has_value()) {
        fmt::print("  solver failed: {}\n", to_string(solution.error()));
        return 1;
    }
    std::size_t total = 0;
    fmt::print("  solution, holding yellow up and green towards you:\n");
    for (const auto& step : solution->steps) {
        total += step.moves.size();
        fmt::print("    {:14} {}\n", stage_name(step.stage), cube::solver::to_string(step.moves));
    }
    fmt::print("  {} moves\n", total);
    return 0;
}

int run_board(const fs::path& root) {
    std::vector<fs::path> sessions;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        // A session's fullres/ copy of its frames is not a session of its own.
        if (entry.is_directory() && entry.path().filename() != "fullres" &&
            !frames_of(entry.path()).empty()) {
            sessions.push_back(entry.path());
        }
    }
    std::sort(sessions.begin(), sessions.end());

    std::size_t exact = 0;
    std::size_t wrong = 0;
    std::size_t with_truth = 0;
    fmt::print("{:44} {:>6} {:>6} {:>5} {:>8}  {}\n", "session", "frames", "reads", "sides",
               "read-at", "result");
    for (const fs::path& session : sessions) {
        const Outcome outcome = replay(session);
        const auto truth = truth_of(session);
        std::string result = "incomplete";
        if (outcome.cube.has_value()) {
            result = !truth.has_value()        ? "read (no truth)"
                     : *truth == *outcome.cube ? "EXACT"
                                               : "WRONG";
        }
        with_truth += truth.has_value() ? 1U : 0U;
        exact += result == "EXACT" ? 1U : 0U;
        wrong += result == "WRONG" ? 1U : 0U;
        fmt::print("{:44} {:>6} {:>6} {:>5} {:>8}  {}{}\n",
                   fs::relative(session, root).generic_string(), outcome.frames, outcome.reads,
                   outcome.sides,
                   outcome.read_at.has_value() ? std::to_string(*outcome.read_at) : "-", result,
                   outcome.changes_after > 0
                       ? fmt::format("  ({} later frames disagreed)", outcome.changes_after)
                       : std::string{});
    }
    fmt::print("\nexact {} of {} sessions with a known cube; wrong {}\n", exact, with_truth, wrong);
    return wrong == 0 ? 0 : 1;
}

[[nodiscard]] char letter_of(Face face) {
    switch (face) {
        case Face::U:
            return 'U';
        case Face::D:
            return 'D';
        case Face::L:
            return 'L';
        case Face::R:
            return 'R';
        case Face::F:
            return 'F';
        case Face::B:
            return 'B';
    }
    return '?';
}

[[nodiscard]] float percentile(std::vector<float> values, double fraction) {
    if (values.empty()) {
        return 0.0F;
    }
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    return values[index];
}

// Scans the session for its cube, then goes back over every frame asking
// which way the cube was being held in it (cube/vision/held_cube.hpp). This
// is where that matcher's two distances were measured: how close a read sits
// to the hold it shows, and how far off the nearest hold that would look
// different is.
// Frames that show two or three sides at once, scored against the true cube:
// does the read fit it, and how many of the eighteen cubes one turn away does
// it rule out? A read that rules out none says nothing about those turns
// (they changed stickers it did not show); one that fits the true cube and
// rules out most is what settles an ambiguous scan.
int run_corners(const fs::path& session) {
    const auto truth = truth_of(session);
    if (!truth.has_value()) {
        fmt::print("{}: no truth.txt, or it does not make a cube\n", session.string());
        return 2;
    }
    const Outcome outcome = replay(session);
    if (!outcome.colours.has_value()) {
        fmt::print("{}: the scan did not complete, so the session's colours are not known\n",
                   session.filename().string());
        return 1;
    }
    std::vector<Cube> neighbours;
    for (const Face face : cube::solver::kAllFaces) {
        for (const int turns : {1, 2, 3}) {
            neighbours.push_back(truth->apply(cube::solver::Move{face, turns}));
        }
    }

    std::size_t seen = 0;
    std::size_t fitting = 0;
    std::size_t blurred = 0;
    std::size_t informative = 0;
    std::size_t rejected_total = 0;
    for (const fs::path& frame : frames_of(session)) {
        const cv::Mat bgr = cv::imread(frame.string(), cv::IMREAD_COLOR);
        if (cube::vision::read_side(bgr).has_value()) {
            continue;
        }
        const auto read = cube::vision::read_corner(bgr);
        if (!read.has_value()) {
            continue;
        }
        ++seen;
        const cube::vision::CornerFit fit =
            cube::vision::fit_corner(*read, *truth, *outcome.colours);
        std::size_t rejected = 0;
        for (const Cube& other : neighbours) {
            rejected += cube::vision::fit_corner(*read, other, *outcome.colours).fits ? 0U : 1U;
        }
        fmt::print(
            "  {}: {} sides, {} edges  true cube {} (distance {:.1f})  "
            "one-turn cubes ruled out {}/{}\n",
            frame.filename().string(), read->sides.size(), read->edges.size(),
            fit.fits ? "FITS" : "no fit", static_cast<double>(fit.distance), rejected,
            neighbours.size());
        if (fit.fits) {
            ++fitting;
            if (rejected > 0) {
                ++informative;
                rejected_total += rejected;
            }
        } else {
            ++blurred;
        }
    }
    fmt::print(
        "{}: {} corner reads, {} fit the true cube, {} do not (blur, a finger, a "
        "misread side)\n",
        session.filename().string(), seen, fitting, blurred);
    if (informative > 0) {
        fmt::print(
            "  of those that fit, {} ruled out at least one cube one turn away "
            "(on average {:.1f} of {})\n",
            informative, static_cast<double>(rejected_total) / static_cast<double>(informative),
            neighbours.size());
    }
    return 0;
}

int run_holds(const fs::path& session) {
    const std::vector<fs::path> frames = frames_of(session);
    cube::vision::SideScan scan;
    std::vector<std::optional<cube::vision::SideRead>> reads;
    for (const fs::path& frame : frames) {
        reads.push_back(cube::vision::read_side(cv::imread(frame.string(), cv::IMREAD_COLOR)));
        scan.add(reads.back());
    }
    const auto scanned = scan.scanned();
    if (!scanned.has_value()) {
        fmt::print("{}: no cube, so nothing to match against\n", session.filename().string());
        return 1;
    }
    fmt::print("{}: {}\n", session.filename().string(), layout_of(scanned->cube));

    // One sticker of the wrong colour moves a side's distance by a ninth of
    // the gap between the two colours; the narrowest gap sets the scale.
    constexpr std::array<Color, 6> kColours = {Color::White, Color::Yellow, Color::Green,
                                               Color::Blue,  Color::Orange, Color::Red};
    float narrowest = std::numeric_limits<float>::max();
    std::string narrowest_pair;
    for (std::size_t a = 0; a < kColours.size(); ++a) {
        for (std::size_t b = a + 1; b < kColours.size(); ++b) {
            const float gap = cube::vision::colour_distance(
                scanned->colours[static_cast<std::size_t>(kColours[a])],
                scanned->colours[static_cast<std::size_t>(kColours[b])]);
            if (gap < narrowest) {
                narrowest = gap;
                narrowest_pair =
                    fmt::format("{}/{}", letter_of(kColours[a]), letter_of(kColours[b]));
            }
        }
    }
    fmt::print("  closest two colours: {} at {:.1f}, so one wrong sticker is worth {:.2f}\n",
               narrowest_pair, narrowest, narrowest / 9.0F);

    std::vector<float> fit;  // best distance, steady reads
    std::vector<float>
        clear_by;  // how much nearer the best is than the next different-looking hold
    std::size_t steady = 0;
    std::size_t matched = 0;
    std::size_t undecided = 0;
    std::optional<cv::Point2f> previous;
    std::string run;
    std::size_t run_start = 0;
    const auto close_run = [&](std::size_t end) {
        if (!run.empty()) {
            fmt::print("  frames {:4}-{:4}  {}\n", run_start, end, run);
        }
    };
    for (std::size_t i = 0; i < reads.size(); ++i) {
        std::string now;
        if (reads[i].has_value()) {
            const cv::Point2f centre = reads[i]->centres[4];
            const cv::Point2f moved = previous.has_value() ? centre - *previous : cv::Point2f{};
            const bool still =
                previous.has_value() && std::hypot(moved.x, moved.y) < 0.12F * reads[i]->pitch;
            previous = centre;
            if (still) {
                ++steady;
                const auto fits =
                    cube::vision::fit_holds(*reads[i], scanned->cube, scanned->colours);
                const auto best_view = cube::solver::camera_view(scanned->cube, fits[0].hold);
                fit.push_back(fits[0].distance);
                for (const cube::vision::HoldFit& other : fits) {
                    if (cube::solver::camera_view(scanned->cube, other.hold) != best_view) {
                        clear_by.push_back(other.distance - fits[0].distance);
                        break;
                    }
                }
                const cube::vision::HoldReading reading =
                    cube::vision::read_hold(*reads[i], scanned->cube, scanned->colours);
                if (reading.holds.empty()) {
                    now = "read fits no hold";
                } else {
                    ++matched;
                    undecided += reading.holds.size() > 1 ? 1U : 0U;
                    const cube::solver::Hold hold = reading.holds.front();
                    now = fmt::format(
                        "camera sees {}, {} up{}",
                        letter_of(cube::solver::side_at(hold, cube::solver::Place::Back)),
                        letter_of(hold.up),
                        reading.holds.size() > 1
                            ? fmt::format(" (one of {} that look the same)", reading.holds.size())
                            : std::string{});
                }
            }
        } else {
            previous.reset();
        }
        if (now != run) {
            close_run(i == 0 ? 0 : i - 1);
            run = now;
            run_start = i;
        }
    }
    close_run(reads.size() - 1);

    fmt::print("  steady reads {}  matched {}  of those undecided between look-alikes {}\n", steady,
               matched, undecided);
    fmt::print(
        "  distance to the hold shown:        median {:.2f}  90% {:.2f}  99% {:.2f}  max "
        "{:.2f}\n",
        percentile(fit, 0.5), percentile(fit, 0.9), percentile(fit, 0.99), percentile(fit, 1.0));
    fmt::print(
        "  clear of the next different hold:  min {:.2f}  1% {:.2f}  10% {:.2f}  median "
        "{:.2f}\n",
        percentile(clear_by, 0.0), percentile(clear_by, 0.01), percentile(clear_by, 0.1),
        percentile(clear_by, 0.5));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "replay") {
        return run_replay(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "board") {
        return run_board(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "holds") {
        return run_holds(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "corners") {
        return run_corners(fs::path(args[1]));
    }
    fmt::print(
        "usage:\n  cube_scan replay <session-dir>\n  cube_scan board <root-dir>\n"
        "  cube_scan holds <session-dir>\n  cube_scan corners <session-dir>\n");
    return 2;
}
