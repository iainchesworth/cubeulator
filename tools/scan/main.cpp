// cube_scan: replay recorded scans through the direct-scan reader and score
// them. This is the scoreboard of docs/recovery-plan.md - the only gate is a
// real recorded session ending in the exact true cube.
//
//   cube_scan replay <session-dir>   one session: the cube and its solution
//   cube_scan board <root-dir>       every session under root against truth
//   cube_scan holds <session-dir>    how the cube was held, frame by frame
//   cube_scan corners <dir> [dist]   frames showing two sides at once, and
//                                    whether they fit the true cube
//   cube_scan corner-colours <dir>   how the sides of those frames compare
//                                    with the sides read square-on
//   cube_scan ambiguity <dir>        the cubes six sides make, and how the corner
//                                    reads of the session vote among them
//   cube_scan funnel <dir> [-v]      how far each frame with no square-on side got
//                                    in the corner reader, and what the scan
//                                    decided, as the app's own loop would
//   cube_scan cost <dir> [runs] [-v] what each frame costs, by stage, in the
//                                    app's own loop, and what the scan's
//                                    verdicts came to (and cost) - the number
//                                    to hold against the camera's 33 ms
//
// A session of a cube that is not a 3x3 says so in its manifest.json ("size", the
// recorder writes it); replay, board, diagnose, funnel and cost then read it as that size
// and score it against truth.txt of N x N letters a side, or, for a cube whose six flat
// sides fit several cubes, against cube_n.txt (6 x N^2 letters in the cube's own facelet
// order, which cube_tumble_sim writes). The other commands are a 3x3's.
//
// A session is a directory of rec_NNNNNN.jpg frames. Its truth, if known, is
// truth.txt: six words of nine letters from WYROGB, one per side, each side
// row-major as seen from outside in any orientation and any order. Six sides
// that make several cubes name none of them, so a session of such a cube says
// which it is in layout.txt instead: the line `cube_scan replay` prints for a
// cube, "U:... D:... L:... R:... F:... B:...", each side as the solver lays it
// out. The frame times come from index.csv when there is one.

#include <cube/model/facelet.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/geometry.hpp>
#include <cube/nxn/scan_assembly.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/hold.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/corner_fit.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/held_cube.hpp>
#include <cube/vision/sticker_colour.hpp>

#include <opencv2/imgcodecs.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
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

// The cube a session's layout.txt names: six sides as `cube_scan replay` prints
// a cube, "U:YYWYYYWWY D:..." in the solver's own layout.
[[nodiscard]] std::optional<Cube> layout_truth_of(const fs::path& session) {
    std::ifstream in(session / "layout.txt");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.empty()) {
        return std::nullopt;
    }
    constexpr std::array<std::pair<Face, char>, 6> kOrder = {{{Face::U, 'U'},
                                                              {Face::D, 'D'},
                                                              {Face::L, 'L'},
                                                              {Face::R, 'R'},
                                                              {Face::F, 'F'},
                                                              {Face::B, 'B'}}};
    std::array<std::array<Color, 9>, 6> sides{};
    for (const auto& [face, name] : kOrder) {
        const std::size_t at = text.find(std::string(1, name) + ":");
        if (at == std::string::npos || at + 2 + 9 > text.size()) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 9; ++i) {
            std::optional<Color> colour;
            switch (text[at + 2 + i]) {
                case 'W':
                    colour = Color::White;
                    break;
                case 'Y':
                    colour = Color::Yellow;
                    break;
                case 'R':
                    colour = Color::Red;
                    break;
                case 'O':
                    colour = Color::Orange;
                    break;
                case 'G':
                    colour = Color::Green;
                    break;
                case 'B':
                    colour = Color::Blue;
                    break;
                default:
                    break;
            }
            if (!colour.has_value()) {
                return std::nullopt;
            }
            sides[static_cast<std::size_t>(face)][i] = *colour;
        }
    }
    const auto cube = Cube::from_facelets(sides);
    return cube.has_value() ? std::optional<Cube>(*cube) : std::nullopt;
}

[[nodiscard]] std::optional<Cube> truth_of(const fs::path& session) {
    if (const auto named = layout_truth_of(session); named.has_value()) {
        return named;
    }
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

// The cube's size, as the recorder's manifest.json says it: 3 when it does not.
[[nodiscard]] int session_size(const fs::path& session) {
    std::ifstream in(session / "manifest.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::size_t at = text.find("\"size\"");
    if (at == std::string::npos) {
        return 3;
    }
    const std::size_t colon = text.find(':', at);
    if (colon == std::string::npos) {
        return 3;
    }
    const int size = std::atoi(text.c_str() + colon + 1);
    return size >= 2 && size <= 9 ? size : 3;
}

// The cube a session's truth.txt names when it is not a 3x3: six words of N^2 letters,
// each side as seen from outside in any turn, put together as the scan puts sides
// together, so that what the scan makes and what this makes are the same cube.
[[nodiscard]] std::optional<cube::nxn::NCube> truth_n_of(const fs::path& session, int size) {
    // A session may name the cube itself (cube_n.txt: 6 x N^2 letters in the cube's own
    // facelet order), which a cube whose six flat sides fit several cubes needs; the sides
    // alone do not say which.
    if (std::ifstream whole(session / "cube_n.txt"); whole) {
        std::string letters;
        whole >> letters;
        std::vector<Color> stickers;
        for (const char letter : letters) {
            const auto colour = colour_of(letter);
            if (!colour.has_value()) {
                return std::nullopt;
            }
            stickers.push_back(*colour == FacetColor::white    ? Color::White
                               : *colour == FacetColor::yellow ? Color::Yellow
                               : *colour == FacetColor::red    ? Color::Red
                               : *colour == FacetColor::orange ? Color::Orange
                               : *colour == FacetColor::green  ? Color::Green
                                                               : Color::Blue);
        }
        auto cube = cube::nxn::NCube::from_facelets(size, stickers);
        return cube.has_value() ? std::optional<cube::nxn::NCube>(*cube) : std::nullopt;
    }
    std::ifstream in(session / "truth.txt");
    const auto cells = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    cube::nxn::ScannedSides sides;
    for (auto& side : sides) {
        std::string word;
        if (!(in >> word) || word.size() != cells) {
            return std::nullopt;
        }
        for (const char letter : word) {
            const auto colour = colour_of(letter);
            if (!colour.has_value()) {
                return std::nullopt;
            }
            switch (*colour) {
                case cube::model::FacetColor::white:
                    side.push_back(Color::White);
                    break;
                case cube::model::FacetColor::yellow:
                    side.push_back(Color::Yellow);
                    break;
                case cube::model::FacetColor::red:
                    side.push_back(Color::Red);
                    break;
                case cube::model::FacetColor::orange:
                    side.push_back(Color::Orange);
                    break;
                case cube::model::FacetColor::green:
                    side.push_back(Color::Green);
                    break;
                case cube::model::FacetColor::blue:
                    side.push_back(Color::Blue);
                    break;
                case cube::model::FacetColor::unknown:
                    return std::nullopt;
            }
        }
    }
    auto cube = cube::nxn::assemble_scanned_sides(size, sides);
    return cube.has_value() ? std::optional<cube::nxn::NCube>(*cube) : std::nullopt;
}

// Whether `made` is `truth` or the same cube held another way up: the scan puts a side where
// its centres say it goes, and an even size has none to say.
[[nodiscard]] bool same_cube_n(const cube::nxn::NCube& made, const cube::nxn::NCube& truth) {
    if (made.size() != truth.size()) {
        return false;
    }
    const cube::nxn::Geometry& geometry = cube::nxn::Geometry::of(truth.size());
    for (std::size_t rotation = 0; rotation < geometry.rotations().size(); ++rotation) {
        cube::nxn::NCube held = truth;
        held.apply(geometry.rotation_permutation(rotation));
        if (held == made) {
            return true;
        }
    }
    return false;
}

// How long each frame of a session came after the one before, in milliseconds,
// from the index the recorder wrote: zero where there is none (a session
// without an index.csv, the first frame, a frame whose row was overwritten),
// which SideScan takes as the nominal interval. A replay that ignored it would
// hold a side still at 30 frames a second that the app, at 20, did not.
[[nodiscard]] std::vector<float> frame_intervals(const fs::path& session,
                                                 const std::vector<fs::path>& frames) {
    std::vector<float> intervals(frames.size(), 0.0F);
    std::ifstream in(session / "index.csv");
    if (!in) {
        return intervals;
    }
    // The file for pass n is rec_%06d of n % 3600 (docs/recording-format.md).
    constexpr long kRing = 3600;
    std::map<long, long> time_of_file;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        const std::size_t first = line.find(',');
        const std::size_t second = line.find(',', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            continue;
        }
        const long pass = std::strtol(line.substr(0, first).c_str(), nullptr, 10);
        const long t_ms =
            std::strtol(line.substr(first + 1, second - first - 1).c_str(), nullptr, 10);
        time_of_file[pass % kRing] = t_ms;
    }
    std::optional<long> previous;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const std::string name = frames[i].filename().string();  // rec_000123.jpg
        const long number = std::strtol(name.substr(4, 6).c_str(), nullptr, 10);
        const auto found = time_of_file.find(number);
        if (found == time_of_file.end()) {
            previous.reset();
            continue;
        }
        if (previous.has_value()) {
            intervals[i] = static_cast<float>(found->second - *previous);
        }
        previous = found->second;
    }
    return intervals;
}

[[nodiscard]] std::string_view stage_name(cube::vision::CornerStage stage) {
    switch (stage) {
        case cube::vision::CornerStage::too_few_blobs:
            return "fewer than 12 blobs";
        case cube::vision::CornerStage::too_many_blobs:
            return "more than 48 blobs";
        case cube::vision::CornerStage::no_pair_of_lattices:
            return "fewer than two lattices";
        case cube::vision::CornerStage::no_shared_edge:
            return "no shared edge";
        case cube::vision::CornerStage::off_the_frame:
            return "off the frame";
        case cube::vision::CornerStage::read:
            return "read";
    }
    return "";
}

[[nodiscard]] bool same_sides(const std::vector<cube::vision::SideScan::Side>& a,
                              const std::vector<cube::vision::SideScan::Side>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].in_view != b[i].in_view || a[i].lab != b[i].lab) {
            return false;
        }
    }
    return true;
}

// Which gate of SideScan::add_corner a side of a corner read met.
enum class SideGate : std::uint8_t { counted, not_whole, strip, moving };

[[nodiscard]] SideGate gate_of(const cube::vision::ObliqueSide& side, int size,
                               const std::vector<cv::Point2f>& previous_centres, float interval) {
    const auto n = static_cast<std::size_t>(size);
    const auto least_found = static_cast<std::ptrdiff_t>(std::max<std::size_t>(
        3, (static_cast<std::size_t>(cube::vision::kMinCornerFound) * n * n) / 9));
    if (std::count(side.found.begin(), side.found.end(), true) < least_found) {
        return SideGate::not_whole;
    }
    const auto step = [&](std::size_t to) {
        const cv::Point2f half =
            (side.centres.at(to) - side.centres.at(0)) / static_cast<float>(size - 1);
        return std::hypot(half.x, half.y);
    };
    const float along_columns = step(n - 1);
    const float along_rows = step(n * (n - 1));
    const float longer = std::max(along_columns, along_rows);
    if (longer <= 0.0F ||
        std::min(along_columns, along_rows) < cube::vision::kMinCornerAspect * longer) {
        return SideGate::strip;
    }
    const float reach =
        cube::vision::kSteadyFraction * cube::vision::interval_scale(interval) * side.pitch;
    const bool steady = std::any_of(
        previous_centres.begin(), previous_centres.end(), [&](const cv::Point2f& before) {
            const cv::Point2f gap = side.centres.at((n / 2 * n) + (n / 2)) - before;
            return std::hypot(gap.x, gap.y) < reach;
        });
    return steady ? SideGate::counted : SideGate::moving;
}

// What the scan did with a session, as the app would have done it, and where
// each frame in which no side was square to the camera stopped in the corner
// reader. The loop is DirectScan::processFrame's: both readers from one
// preparation, the corner reader only for a frame with no square-on side, the
// scan judged when its sides changed or, being ambiguous, when a corner was
// added, and the first cube ending it. It is the instrument for a tumble: a
// cube turned slowly about one axis and then another shows two sides at once
// between every pair of square-on views, and this says how many of those
// frames became reads, and which gate the others met.
int run_funnel(const fs::path& session, bool every_frame) {
    using cube::vision::CornerStage;
    const int size = session_size(session);
    const std::vector<fs::path> frames = frames_of(session);
    const std::vector<float> intervals = frame_intervals(session, frames);

    cube::vision::SideScan scan(size);
    std::vector<cube::vision::SideScan::Side> previous_sides;
    bool ambiguous = false;

    std::size_t square_on = 0;
    std::size_t corner_frames = 0;
    std::map<CornerStage, std::size_t> stages;
    std::size_t two_sided = 0;
    std::size_t three_sided = 0;
    std::map<SideGate, std::size_t> gates;
    std::size_t frames_that_counted = 0;
    std::vector<cv::Point2f> previous_centres;
    std::optional<std::size_t> six_at;
    std::optional<std::size_t> first_ambiguous;
    std::optional<std::size_t> cube_at;
    std::optional<Cube> known;
    std::string verdict = "no sixth side was ever captured";
    std::size_t evaluations = 0;

    for (std::size_t i = 0; i < frames.size(); ++i) {
        const cv::Mat bgr = cv::imread(frames[i].string(), cv::IMREAD_COLOR);
        const auto prepared = cube::vision::prepare_frame(bgr, size);
        const auto read =
            prepared.has_value() ? cube::vision::read_side(*prepared, size) : std::nullopt;
        scan.add(read, intervals[i]);

        std::string what = "square-on";
        std::optional<cube::vision::CornerRead> corner;
        if (read.has_value()) {
            ++square_on;
            previous_centres.clear();
        } else {
            ++corner_frames;
            cube::vision::CornerExplained explained;
            if (prepared.has_value()) {
                explained = cube::vision::read_corner_explained(*prepared, size);
            }
            ++stages[explained.stage];
            what = std::string(stage_name(explained.stage));
            what += fmt::format(" ({} blobs, {} windows)", explained.blobs, explained.windows);
            if (explained.read.has_value()) {
                corner = std::move(explained.read);
                (corner->sides.size() == 2 ? two_sided : three_sided) += 1;
                std::vector<cv::Point2f> centres;
                for (const cube::vision::ObliqueSide& side : corner->sides) {
                    const SideGate gate = gate_of(side, size, previous_centres, intervals[i]);
                    ++gates[gate];
                    centres.push_back(side.centres.at(
                        (static_cast<std::size_t>(size) / 2 * static_cast<std::size_t>(size)) +
                        (static_cast<std::size_t>(size) / 2)));
                    what += fmt::format(" [{}]", gate == SideGate::counted     ? "counted"
                                                 : gate == SideGate::not_whole ? "too few found"
                                                 : gate == SideGate::strip     ? "strip"
                                                                               : "moving");
                }
                previous_centres = std::move(centres);
            } else {
                previous_centres.clear();
            }
        }
        scan.add_corner(corner, intervals[i]);
        frames_that_counted += corner.has_value() && scan.reading() ? 1U : 0U;

        // The controller's own trigger for judging the scan.
        auto sides = scan.sides();
        if (every_frame) {
            std::size_t captured = 0;
            for (const auto& side : sides) {
                captured += side.reads >= cube::vision::SideScan::kMinReads ? 1U : 0U;
            }
            fmt::print("{:5} {:>10.0f} ms  sides {}  {}\n", i, intervals[i], captured, what);
        }
        if (same_sides(sides, previous_sides) && !(ambiguous && corner.has_value())) {
            continue;
        }
        previous_sides = std::move(sides);
        if (cube_at.has_value()) {
            continue;  // the app has stopped; only the funnel is still counted
        }
        ++evaluations;
        const auto result = scan.scanned();
        if (result.has_value()) {
            cube_at = i;
            known = result->cube;
            verdict = "a cube";
            if (!six_at.has_value()) {
                six_at = i;
            }
        } else {
            ambiguous = result.error() == cube::CubeError::ambiguous_scan;
            verdict = std::string(to_string(result.error()));
            if (result.error() != cube::CubeError::face_capture_incomplete && !six_at.has_value()) {
                six_at = i;
            }
            if (ambiguous && !first_ambiguous.has_value()) {
                first_ambiguous = i;
            }
        }
    }

    fmt::print("{}: {} frames; {} had a side square to the camera, {} went to the corner reader\n",
               session.filename().string(), frames.size(), square_on, corner_frames);
    for (const CornerStage stage :
         {CornerStage::too_few_blobs, CornerStage::too_many_blobs, CornerStage::no_pair_of_lattices,
          CornerStage::no_shared_edge, CornerStage::off_the_frame, CornerStage::read}) {
        fmt::print("    {:28} {:5}\n", stage_name(stage), stages[stage]);
    }
    fmt::print("  corner reads: {} two-sided, {} three-sided; the scan keeps {} distinct views\n",
               two_sided, three_sided, scan.corner_reads());
    fmt::print(
        "  sides of those reads: {} counted; not counted: {} with fewer than {} stickers found, "
        "{} too foreshortened, {} moving\n",
        gates[SideGate::counted], gates[SideGate::not_whole], cube::vision::kMinCornerFound,
        gates[SideGate::strip], gates[SideGate::moving]);
    fmt::print("  frames that counted a corner side: {}\n", frames_that_counted);
    fmt::print(
        "  judged {} times; the six sides were first judged at frame {}, ambiguous from {}\n",
        evaluations, six_at.has_value() ? std::to_string(*six_at) : "-",
        first_ambiguous.has_value() ? std::to_string(*first_ambiguous) : "-");
    if (cube_at.has_value()) {
        fmt::print("  the cube was known at frame {}\n", *cube_at);
        if (const auto truth = truth_of(session); truth.has_value() && known.has_value()) {
            fmt::print("  truth: {}\n", *truth == *known ? "MATCH" : "WRONG");
        }
    } else {
        fmt::print("  no cube: {}\n", verdict);
    }
    return cube_at.has_value() ? 0 : 1;
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
    int size = 3;                                         // of a cube that is not a 3x3: the size,
    std::optional<cube::nxn::NCube> cube_n;               // and the cube instead of `cube`
    std::optional<cube::CubeError> failure;               // why the last frame had none
    // The corner the scan would ask to be shown, when it is ambiguous.
    std::optional<std::array<Color, 3>> settling_corner;
    std::size_t changes_after = 0;  // later frames whose cube differed
};

[[nodiscard]] Outcome replay(const fs::path& session) {
    Outcome outcome;
    outcome.size = session_size(session);
    const int size = outcome.size;
    cube::vision::SideScan scan(size);
    const std::vector<fs::path> frames = frames_of(session);
    const std::vector<float> intervals = frame_intervals(session, frames);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const cv::Mat bgr = cv::imread(frames[i].string(), cv::IMREAD_COLOR);
        // As the app reads a frame: prepared once, the second reader only when
        // the first found no side.
        const auto prepared = cube::vision::prepare_frame(bgr, size);
        const auto read =
            prepared.has_value() ? cube::vision::read_side(*prepared, size) : std::nullopt;
        outcome.reads += read.has_value() ? 1U : 0U;
        scan.add(read, intervals[i]);
        if (!read.has_value() && size <= cube::vision::kMaxCornerSize) {
            scan.add_corner(
                prepared.has_value() ? cube::vision::read_corner(*prepared, size) : std::nullopt,
                intervals[i]);
        }
        if (size != 3) {
            if (const auto cube = scan.cube_n(); cube.has_value()) {
                if (!outcome.cube_n.has_value()) {
                    outcome.cube_n = *cube;
                    outcome.read_at = outcome.frames;
                } else if (!(*cube == *outcome.cube_n)) {
                    ++outcome.changes_after;
                }
            }
        } else if (const auto cube = scan.cube(); cube.has_value()) {
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
    if (const auto whole = scan.scanned_n(); whole.has_value()) {
        outcome.colours = whole->colours;
    } else {
        outcome.failure = whole.error();
        if (const auto diagnosis = scan.diagnose(); diagnosis.assembly.has_value()) {
            outcome.settling_corner = diagnosis.assembly->settling_corner;
        }
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
    const std::size_t fitting = cube::solver::assemble_all_scanned_faces(*named, 64).size();
    fmt::print("  assembly: {}\n",
               fitting == 0 ? "no cube fits these sides"
                            : fmt::format("{} cube{} fit{} these sides{}", fitting,
                                          fitting == 1 ? "" : "s", fitting == 1 ? "s" : "",
                                          fitting > 1 ? " (cube_scan ambiguity)" : ""));
}

// What the scan made of a session, side by side, at the session's size: how each side was
// named (the letters, row by row, as the side was first seen), what the centres and the
// clusters measured, and why the cube was or was not put together.
// The size the app would find a session's cube to be (DirectScan::detectedSize): for each
// frame, the largest size 2 to 5 at which a side is read, counted, so that what the
// reader says of a real cube at each size can be seen. A frame that reads at no size says
// nothing.
int run_detect_size(const fs::path& session) {
    const std::vector<fs::path> frames = frames_of(session);
    std::map<int, std::size_t> largest;
    std::size_t none = 0;
    for (const fs::path& path : frames) {
        const cv::Mat bgr = cv::imread(path.string(), cv::IMREAD_COLOR);
        int found = 0;
        for (int size = 2; size <= 5; ++size) {
            const auto prepared = cube::vision::prepare_frame(bgr, size);
            if (prepared.has_value() && cube::vision::read_side(*prepared, size).has_value()) {
                found = size;
            }
        }
        if (found == 0) {
            ++none;
        } else {
            ++largest[found];
        }
    }
    fmt::print("{}: {} frames, session size {}; read at no size {}", session.filename().string(),
               frames.size(), session_size(session), none);
    for (const auto& [size, count] : largest) {
        fmt::print("; largest {}: {}", size, count);
    }
    fmt::print("\n");
    return 0;
}

int run_diagnose(const fs::path& session) {
    const int size = session_size(session);
    cube::vision::SideScan scan(size);
    const std::vector<fs::path> frames = frames_of(session);
    const std::vector<float> intervals = frame_intervals(session, frames);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const cv::Mat bgr = cv::imread(frames[i].string(), cv::IMREAD_COLOR);
        const auto prepared = cube::vision::prepare_frame(bgr, size);
        const auto read =
            prepared.has_value() ? cube::vision::read_side(*prepared, size) : std::nullopt;
        scan.add(read, intervals[i]);
        if (!read.has_value() && size <= cube::vision::kMaxCornerSize) {
            scan.add_corner(
                prepared.has_value() ? cube::vision::read_corner(*prepared, size) : std::nullopt,
                intervals[i]);
        }
    }
    const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
    fmt::print("{}: a {}x{}x{}, {} frames, {} groups, {} captured\n", session.filename().string(),
               size, size, size, frames.size(), diagnosis.groups, diagnosis.captured);
    for (const auto& side : diagnosis.sides) {
        const cv::Vec3f& centre = side.lab.at(side.lab.size() / 2);
        fmt::print("  side seen {}th: {} reads; middle cell Lab {:5.1f} {:6.1f} {:6.1f}\n",
                   side.first_seen, side.reads, centre[0], centre[1], centre[2]);
    }
    // How alike each pair of captured sides is, under the turn that lays them best: the number
    // of cells further apart than the scan counts as the same colour (10 Lab), the mean and the
    // largest plain Lab gap, and the mean of colour_distance (which counts lightness and
    // chroma for a quarter). Two reads of one side lit differently show here as close pairs.
    for (std::size_t a = 0; a < diagnosis.sides.size(); ++a) {
        for (std::size_t b = a + 1; b < diagnosis.sides.size(); ++b) {
            const auto& left = diagnosis.sides.at(a).lab;
            const auto& right = diagnosis.sides.at(b).lab;
            const auto cells = left.size();
            std::size_t best_differing = cells + 1;
            double best_mean = 0.0;
            double best_max = 0.0;
            double best_shaded = 0.0;
            for (int roll = 0; roll < 4; ++roll) {
                std::size_t differing = 0;
                double total = 0.0;
                double most = 0.0;
                double shaded = 0.0;
                for (std::size_t i = 0; i < cells; ++i) {
                    const cv::Vec3f& other = right.at(cube::vision::rolled_cell(size, roll, i));
                    const cv::Vec3f gap = left.at(i) - other;
                    const double plain = std::sqrt(static_cast<double>(gap.dot(gap)));
                    differing += plain > 10.0 ? 1U : 0U;
                    total += plain;
                    most = std::max(most, plain);
                    shaded += static_cast<double>(cube::vision::colour_distance(left.at(i), other));
                }
                if (differing < best_differing) {
                    best_differing = differing;
                    best_mean = total / static_cast<double>(cells);
                    best_max = most;
                    best_shaded = shaded / static_cast<double>(cells);
                }
            }
            if (best_differing * 2 <= cells) {
                fmt::print(
                    "  sides {} and {}: {} of {} cells differ; mean gap {:.1f}, largest {:.1f}; "
                    "shaded mean {:.1f}\n                    ",
                    a, b, best_differing, cells, best_mean, best_max, best_shaded);
            }
        }
    }
    if (diagnosis.white.has_value()) {
        fmt::print("  white centre found: side {}, cast {:.1f}\n", diagnosis.white->side,
                   diagnosis.white->cast);
    }
    if (!diagnosis.naming.has_value()) {
        fmt::print("  colours not named: {}\n",
                   diagnosis.result_n.has_value()
                       ? "?"
                       : std::string(to_string(diagnosis.result_n.error())));
        return 1;
    }
    const auto n = static_cast<std::size_t>(size);
    for (std::size_t side = 0; side < diagnosis.naming->faces.size(); ++side) {
        fmt::print("  side {} named:\n", side);
        for (std::size_t row = 0; row < n; ++row) {
            std::string letters;
            std::string sure;
            for (std::size_t col = 0; col < n; ++col) {
                const auto& facelet = diagnosis.naming->faces.at(side).at((row * n) + col);
                letters += letter_of(facelet.color);
                sure += fmt::format(" {:.2f}", static_cast<double>(facelet.confidence));
            }
            fmt::print("    {}  {}\n", letters, sure);
        }
    }
    for (std::size_t c = 0; c < diagnosis.naming->clusters.size(); ++c) {
        const auto& cluster = diagnosis.naming->clusters.at(c);
        fmt::print("  cluster {} {}: mean Lab {:5.1f} {:6.1f} {:6.1f}  hue {:.0f} chroma {:.0f}\n",
                   c, letter_of(cluster.name), cluster.mean[0], cluster.mean[1], cluster.mean[2],
                   static_cast<double>(cluster.hue.hue_degrees),
                   static_cast<double>(cluster.hue.chroma));
    }
    if (diagnosis.assembly.has_value()) {
        std::string fits;
        for (const std::size_t fit : diagnosis.assembly->corner_fits) {
            fits += fmt::format(" {}", fit);
        }
        fmt::print("  assembly: {} candidate(s); {} corner reads kept; reads fitting each:{}\n",
                   diagnosis.assembly->candidates, diagnosis.assembly->corner_reads, fits);
    }
    fmt::print("  result: {}\n", diagnosis.result_n.has_value()
                                     ? "a cube"
                                     : std::string(to_string(diagnosis.result_n.error())));
    return diagnosis.result_n.has_value() ? 0 : 1;
}

int run_replay(const fs::path& session) {
    const Outcome outcome = replay(session);
    fmt::print("{}: frames {}  lattice reads {}  corner reads {}  sides {}\n",
               session.filename().string(), outcome.frames, outcome.reads, outcome.corner_reads,
               outcome.sides);
    if (outcome.size != 3) {
        // A cube of another size: its state and whether it is the truth. There is
        // no 3x3 solution to print.
        fmt::print("  a {0}x{0}x{0}\n", outcome.size);
        if (!outcome.cube_n.has_value()) {
            fmt::print("  no cube: {} of 6 sides captured{}\n", outcome.sides,
                       outcome.failure.has_value()
                           ? fmt::format(" ({})", to_string(*outcome.failure))
                           : std::string());
            return 1;
        }
        fmt::print("  cube known at frame {} ({} later frames disagreed)\n", *outcome.read_at,
                   outcome.changes_after);
        std::string state;
        for (const Color colour : outcome.cube_n->facelets()) {
            state += letter_of(colour);
        }
        fmt::print("  {}\n", state);
        if (const auto truth = truth_n_of(session, outcome.size); truth.has_value()) {
            fmt::print("  truth: {}\n", same_cube_n(*outcome.cube_n, *truth) ? "MATCH" : "WRONG");
        }
        return 0;
    }
    if (!outcome.cube.has_value()) {
        fmt::print("  no cube: {} of 6 sides captured{}\n", outcome.sides,
                   outcome.failure.has_value() ? fmt::format(" ({})", to_string(*outcome.failure))
                                               : std::string());
        explain(outcome);
        if (outcome.settling_corner.has_value()) {
            std::string colours;
            for (const Color colour : *outcome.settling_corner) {
                colours += colours.empty() ? "" : ", ";
                colours += letter_of(colour);
            }
            fmt::print("  it would ask for the corner where the sides with centres {} meet\n",
                       colours);
        }
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
        std::string result = "incomplete";
        bool has_truth = false;
        if (outcome.size != 3) {
            const auto truth = truth_n_of(session, outcome.size);
            has_truth = truth.has_value();
            if (outcome.cube_n.has_value()) {
                result = !truth.has_value()                     ? "read (no truth)"
                         : same_cube_n(*outcome.cube_n, *truth) ? "EXACT"
                                                                : "WRONG";
            }
        } else {
            const auto truth = truth_of(session);
            has_truth = truth.has_value();
            if (outcome.cube.has_value()) {
                result = !truth.has_value()        ? "read (no truth)"
                         : *truth == *outcome.cube ? "EXACT"
                                                   : "WRONG";
            }
        }
        with_truth += has_truth ? 1U : 0U;
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
int run_corners(const fs::path& session, float max_distance) {
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
            cube::vision::fit_corner(*read, *truth, *outcome.colours, max_distance);
        std::size_t rejected = 0;
        for (const Cube& other : neighbours) {
            rejected += cube::vision::fit_corner(*read, other, *outcome.colours, max_distance).fits
                            ? 0U
                            : 1U;
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

// How the sides of a corner view compare with the sides the square-on reads of
// the same session captured. Whether a corner read can be counted towards a
// side depends on its colours landing near that side's, so this prints, for
// every side of every corner read, which captured side it is nearest under the
// best quarter-turn and how near - beside how many of its nine stickers were
// found and how foreshortened it is.
int run_corner_colours(const fs::path& session) {
    // kRoll[k][i] is the cell of a read that lands at cell i once the read is
    // turned k quarter-turns; the scan's own table, which is file-local.
    constexpr std::array<std::array<std::size_t, 9>, 4> kRoll = {{
        {0, 1, 2, 3, 4, 5, 6, 7, 8},
        {2, 5, 8, 1, 4, 7, 0, 3, 6},
        {8, 7, 6, 5, 4, 3, 2, 1, 0},
        {6, 3, 0, 7, 4, 1, 8, 5, 2},
    }};

    // Two reads are one side under this (direct_scan.cpp keeps it to itself).
    constexpr float kSameSideDistance = 12.0F;

    const Outcome outcome = replay(session);
    fmt::print("{}: {} frames, {} square-on reads, {} sides captured\n",
               session.filename().string(), outcome.frames, outcome.reads, outcome.sides);
    if (outcome.captured.empty()) {
        return 1;
    }

    std::size_t corner_frames = 0;
    std::array<std::size_t, 4> by_sides{};
    std::vector<float> distances;
    std::vector<float> margins;
    std::size_t unmatched = 0;
    for (const fs::path& frame : frames_of(session)) {
        const cv::Mat bgr = cv::imread(frame.string(), cv::IMREAD_COLOR);
        if (cube::vision::read_side(bgr).has_value()) {
            continue;
        }
        const auto read = cube::vision::read_corner(bgr);
        if (!read.has_value()) {
            continue;
        }
        ++corner_frames;
        ++by_sides.at(std::min<std::size_t>(read->sides.size(), 3));
        for (std::size_t s = 0; s < read->sides.size(); ++s) {
            const cube::vision::ObliqueSide& side = read->sides.at(s);
            std::size_t found = 0;
            for (const bool f : side.found) {
                found += f ? 1U : 0U;
            }
            const cv::Point2f column = side.centres.at(1) - side.centres.at(0);
            const cv::Point2f row = side.centres.at(3) - side.centres.at(0);
            const float c = std::hypot(column.x, column.y);
            const float r = std::hypot(row.x, row.y);
            const float shear = std::abs(column.x * row.y - column.y * row.x) / (c * r);

            float best = std::numeric_limits<float>::max();
            float second = std::numeric_limits<float>::max();
            std::size_t best_side = 0;
            for (std::size_t g = 0; g < outcome.captured.size(); ++g) {
                float nearest = std::numeric_limits<float>::max();
                for (const auto& roll : kRoll) {
                    float total = 0.0F;
                    for (std::size_t i = 0; i < 9; ++i) {
                        total += cube::vision::colour_distance(side.lab.at(roll.at(i)),
                                                               outcome.captured.at(g).lab.at(i));
                    }
                    nearest = std::min(nearest, total / 9.0F);
                }
                if (nearest < best) {
                    second = best;
                    best = nearest;
                    best_side = g;
                } else if (nearest < second) {
                    second = nearest;
                }
            }
            distances.push_back(best);
            if (second < std::numeric_limits<float>::max()) {
                margins.push_back(second - best);
            }
            unmatched += best >= kSameSideDistance ? 1U : 0U;
            fmt::print(
                "  {} side {}/{}: found {}/9  steps {:.0f}x{:.0f}px  sin(shear) {:.2f}  nearest "
                "side {}  distance {:.1f}  next {:.1f}\n",
                frame.filename().string(), s + 1, read->sides.size(), found, static_cast<double>(c),
                static_cast<double>(r), static_cast<double>(shear), best_side,
                static_cast<double>(best),
                static_cast<double>(second < std::numeric_limits<float>::max() ? second : -1.0F));
        }
    }
    fmt::print(
        "{} corner-read frames: {} with two sides, {} with three; {} sides read, {} further than "
        "the same-side distance ({:.0f}) from every captured side\n",
        corner_frames, by_sides.at(2), by_sides.at(3), distances.size(), unmatched,
        static_cast<double>(kSameSideDistance));
    if (!distances.empty()) {
        fmt::print("  distance to the nearest side: median {:.1f}  90% {:.1f}  max {:.1f}\n",
                   static_cast<double>(percentile(distances, 0.5)),
                   static_cast<double>(percentile(distances, 0.9)),
                   static_cast<double>(percentile(distances, 1.0)));
    }
    if (!margins.empty()) {
        fmt::print("  clear of the next side: min {:.1f}  10% {:.1f}  median {:.1f}\n",
                   static_cast<double>(percentile(margins, 0.0)),
                   static_cast<double>(percentile(margins, 0.1)),
                   static_cast<double>(percentile(margins, 0.5)));
    }
    return 0;
}

[[nodiscard]] std::optional<Color> solver_colour_of(FacetColor colour) {
    switch (colour) {
        case FacetColor::white:
            return Color::White;
        case FacetColor::yellow:
            return Color::Yellow;
        case FacetColor::red:
            return Color::Red;
        case FacetColor::orange:
            return Color::Orange;
        case FacetColor::green:
            return Color::Green;
        case FacetColor::blue:
            return Color::Blue;
        case FacetColor::unknown:
            break;
    }
    return std::nullopt;
}

// Why six captured sides that make more than one cube were not settled: the
// cubes they make, and how every corner read in the session votes among them
// at several distances (kCornerFitDistance is 8). A read that fits some and
// not others is what settles an ambiguous scan; one that fits none, at any
// distance, was blurred or is a side this cube does not have.
int run_ambiguity(const fs::path& session) {
    const Outcome outcome = replay(session);
    fmt::print("{}: {} frames, {} square-on reads, {} sides captured\n",
               session.filename().string(), outcome.frames, outcome.reads, outcome.sides);
    if (outcome.captured.size() < 6) {
        return 1;
    }
    std::array<cube::vision::CapturedFace, 6> faces{};
    for (std::size_t i = 0; i < 6; ++i) {
        faces[i].lab = outcome.captured[i].lab;
    }
    const auto named = cube::vision::resolve_colors(faces);
    if (!named.has_value()) {
        fmt::print("  colour naming failed: {}\n", to_string(named.error()));
        return 1;
    }
    const std::vector<Cube> candidates = cube::solver::assemble_all_scanned_faces(*named, 64);
    fmt::print("  {} cube{} fit the six sides\n", candidates.size(),
               candidates.size() == 1 ? "" : "s");
    if (candidates.size() < 2) {
        return 0;
    }

    // What each colour measured: the per-channel median of the stickers the
    // naming gave that colour, as the scan itself takes it.
    cube::vision::SessionColours colours{};
    for (std::size_t colour = 0; colour < 6; ++colour) {
        std::array<std::vector<float>, 3> values;
        for (std::size_t side = 0; side < 6; ++side) {
            for (std::size_t cell = 0; cell < 9; ++cell) {
                const std::optional<Color> own = solver_colour_of((*named)[side][cell].color);
                if (own.has_value() && static_cast<std::size_t>(*own) == colour) {
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        values[channel].push_back(faces[side].lab[cell](static_cast<int>(channel)));
                    }
                }
            }
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            colours[colour](static_cast<int>(channel)) = percentile(values[channel], 0.5);
        }
    }

    std::vector<cube::vision::CornerRead> reads;
    for (const fs::path& frame : frames_of(session)) {
        const cv::Mat bgr = cv::imread(frame.string(), cv::IMREAD_COLOR);
        if (cube::vision::read_side(bgr).has_value()) {
            continue;
        }
        if (auto read = cube::vision::read_corner(bgr); read.has_value()) {
            reads.push_back(std::move(*read));
        }
    }
    fmt::print("  {} corner reads\n", reads.size());

    constexpr std::array<float, 5> kDistances = {8.0F, 10.0F, 12.0F, 14.0F, 18.0F};
    fmt::print("  reads fitting each cube, by the distance a side may sit at:\n");
    fmt::print("    {:>3}", "");
    for (const float distance : kDistances) {
        fmt::print("  <={:>4.0f}", static_cast<double>(distance));
    }
    fmt::print("   layout\n");
    for (std::size_t c = 0; c < candidates.size(); ++c) {
        fmt::print("    #{:<2}", c);
        for (const float distance : kDistances) {
            std::size_t fits = 0;
            for (const auto& read : reads) {
                fits +=
                    cube::vision::fit_corner(read, candidates[c], colours, distance).fits ? 1U : 0U;
            }
            fmt::print("  {:>6}", fits);
        }
        fmt::print("   {}\n", layout_of(candidates[c]));
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

[[nodiscard]] std::string_view path_name(cube::vision::AssemblyPath path) {
    switch (path) {
        case cube::vision::AssemblyPath::single_candidate:
            return "one cube";
        case cube::vision::AssemblyPath::chosen_by_corners:
            return "chosen by corners";
        case cube::vision::AssemblyPath::ambiguous:
            return "ambiguous";
        case cube::vision::AssemblyPath::direct:
            return "direct";
        case cube::vision::AssemblyPath::names_exchanged:
            return "names exchanged";
        case cube::vision::AssemblyPath::failed:
            return "no cube";
    }
    return "";
}

// What a frame costs, stage by stage, with the loop of DirectScan::processFrame:
// one preparation, the square-on read, the corner read only for a frame with no
// square-on side, the scan fed both, and the scan judged when its sides changed
// or, being ambiguous, when a corner was added. The wall time of each stage is
// taken with the frame's JPEG already decoded (the page's camera hands the app
// pixels, not a file), so what is summed is what the application does between
// two frames of the camera, and the number to hold against its 33 ms.
//
// A page's main thread is that thread, in WebAssembly: a frame that takes
// longer than the camera's interval is a frame the page does not draw, and one
// that takes a tenth of a second is a page that does not answer. The browser
// numbers of the same sessions are tests/web's (docs/profiling.md).
int run_cost(const fs::path& session, std::size_t runs, bool every_frame) {
    using Clock = std::chrono::steady_clock;
    const auto since = [](Clock::time_point from) {
        return std::chrono::duration<float, std::milli>(Clock::now() - from).count();
    };

    const int size = session_size(session);
    const std::vector<fs::path> paths = frames_of(session);
    const std::vector<float> intervals = frame_intervals(session, paths);
    std::vector<cv::Mat> pictures;
    pictures.reserve(paths.size());
    for (const fs::path& path : paths) {
        pictures.push_back(cv::imread(path.string(), cv::IMREAD_COLOR));
    }

    // As the app does while the camera opens (DirectScan::openCamera): the first
    // float conversion to Lab builds tables that take 90 ms natively, and 250 to
    // 330 in a browser, and would be the first frame's.
    const Clock::time_point warming = Clock::now();
    cube::vision::warm_up_colour_conversion();
    const float warm_ms = since(warming);

    struct Stage {
        std::string name;
        std::vector<float> ms;
    };
    std::array<Stage, 6> stages = {{{"prepare_frame", {}},
                                    {"read_side", {}},
                                    {"read_corner", {}},
                                    {"scan add + add_corner + sides", {}},
                                    {"verdict (diagnose)", {}},
                                    {"the frame, all of it", {}}}};
    struct Verdict {
        std::string kind;
        std::vector<float> ms;
        // The search over the 4^6 turns of the named sides, timed apart from
        // the rest of the judgement it is part of.
        std::vector<float> assemble_all_ms;
        // How many of the judgements put the sides together afresh
        // (SideScan::assemblies()): the rest were answered as the last one was.
        std::size_t assemblies = 0;
    };
    std::map<std::string, Verdict> verdicts;
    // The namings the judgements were made on, as 54 bytes: how many there were
    // is the least number of times the sides had to be put together, and how
    // many times the naming changed is what a scan that keeps only the last
    // assembly has to.
    std::set<std::array<std::uint8_t, 54>> namings;
    std::size_t naming_changes = 0;
    std::size_t judged = 0;
    std::size_t completed = 0;

    for (std::size_t run = 0; run < runs; ++run) {
        cube::vision::SideScan scan(size);
        std::vector<cube::vision::SideScan::Side> previous_sides;
        bool ambiguous = false;
        bool done = false;
        std::optional<std::array<std::uint8_t, 54>> last_naming;
        for (std::size_t i = 0; i < pictures.size() && !done; ++i) {
            const Clock::time_point began = Clock::now();
            Clock::time_point mark = began;
            const auto prepared = cube::vision::prepare_frame(pictures[i], size);
            const float t_prepare = since(mark);
            mark = Clock::now();
            const auto read =
                prepared.has_value() ? cube::vision::read_side(*prepared, size) : std::nullopt;
            const float t_side = since(mark);
            mark = Clock::now();
            scan.add(read, intervals[i]);
            float t_scan = since(mark);
            float t_corner = 0.0F;
            std::optional<cube::vision::CornerRead> corner;
            if (!read.has_value()) {
                mark = Clock::now();
                corner = prepared.has_value() ? cube::vision::read_corner(*prepared, size)
                                              : std::nullopt;
                t_corner = since(mark);
                mark = Clock::now();
                scan.add_corner(corner, intervals[i]);
                t_scan += since(mark);
            }
            mark = Clock::now();
            auto sides = scan.sides();
            t_scan += since(mark);

            float t_verdict = 0.0F;
            if (!(same_sides(sides, previous_sides) && !(ambiguous && corner.has_value()))) {
                previous_sides = std::move(sides);
                mark = Clock::now();
                const std::size_t assembled_before = scan.assemblies();
                const cube::vision::ScanDiagnosis diagnosis = scan.diagnose();
                t_verdict = since(mark);
                ++judged;
                ambiguous = !diagnosis.result_n.has_value() &&
                            diagnosis.result_n.error() == cube::CubeError::ambiguous_scan;
                std::string kind = "fewer than six sides";
                if (diagnosis.assembly.has_value()) {
                    kind = fmt::format("{}, {} candidate(s), {} corner read(s)",
                                       path_name(diagnosis.assembly->path),
                                       diagnosis.assembly->candidates,
                                       diagnosis.assembly->corner_reads);
                }
                Verdict& entry = verdicts[kind];
                entry.kind = kind;
                entry.ms.push_back(t_verdict);
                entry.assemblies += scan.assemblies() - assembled_before;
                if (diagnosis.naming.has_value() && size == 3) {
                    std::array<std::uint8_t, 54> naming{};
                    for (std::size_t side = 0; side < 6; ++side) {
                        for (std::size_t cell = 0; cell < 9; ++cell) {
                            naming.at((side * 9) + cell) = static_cast<std::uint8_t>(
                                diagnosis.naming->faces.at(side).at(cell).color);
                        }
                    }
                    namings.insert(naming);
                    naming_changes += last_naming.has_value() && *last_naming != naming ? 1U : 0U;
                    last_naming = naming;
                    mark = Clock::now();
                    const auto all =
                        cube::solver::assemble_all_scanned_faces(diagnosis.naming->faces, 257);
                    entry.assemble_all_ms.push_back(since(mark));
                    (void)all;
                }
                done = diagnosis.result_n.has_value();
                completed += done ? 1U : 0U;
            }
            const float total = since(began);
            stages[0].ms.push_back(t_prepare);
            stages[1].ms.push_back(t_side);
            if (!read.has_value()) {
                stages[2].ms.push_back(t_corner);
            }
            stages[3].ms.push_back(t_scan);
            if (t_verdict > 0.0F) {
                stages[4].ms.push_back(t_verdict);
            }
            stages[5].ms.push_back(total);
            if (every_frame && run == 0) {
                fmt::print("{:5} {:7.2f} ms  verdict {:7.2f}\n", i, total, t_verdict);
            }
        }
    }

    const auto over = [](const std::vector<float>& values, float limit) {
        return static_cast<std::size_t>(
            std::count_if(values.begin(), values.end(), [&](float v) { return v > limit; }));
    };
    const auto mean = [](const std::vector<float>& values) {
        return values.empty() ? 0.0F
                              : std::accumulate(values.begin(), values.end(), 0.0F) /
                                    static_cast<float>(values.size());
    };
    fmt::print("{}: {} frames x {} runs; the scan judged {} times, {} ended in a cube\n",
               session.filename().string(), pictures.size(), runs, judged, completed);
    fmt::print("  (the colour conversion was warmed up first, as the app does: {:.1f} ms)\n",
               static_cast<double>(warm_ms));
    fmt::print("  {:32} {:>6} {:>8} {:>8} {:>8} {:>8} {:>8} {:>5} {:>5} {:>5}\n", "stage (ms)", "n",
               "mean", "p50", "p90", "p99", "max", ">33", ">50", ">100");
    for (const Stage& stage : stages) {
        fmt::print("  {:32} {:>6} {:8.2f} {:8.2f} {:8.2f} {:8.2f} {:8.2f} {:>5} {:>5} {:>5}\n",
                   stage.name, stage.ms.size(), static_cast<double>(mean(stage.ms)),
                   static_cast<double>(percentile(stage.ms, 0.5)),
                   static_cast<double>(percentile(stage.ms, 0.9)),
                   static_cast<double>(percentile(stage.ms, 0.99)),
                   static_cast<double>(percentile(stage.ms, 1.0)), over(stage.ms, 33.0F),
                   over(stage.ms, 50.0F), over(stage.ms, 100.0F));
    }
    fmt::print(
        "  the sides were named {} different ways, and the naming changed {} times, over "
        "{} runs:\n"
        "  the fewest times they had to be put together, and what a scan that keeps only "
        "the\n  last assembly has to\n",
        namings.size(), naming_changes, runs);
    fmt::print("  verdicts by what they came to:\n");
    for (const auto& [kind, entry] : verdicts) {
        fmt::print("    {:56} {:>5} x   mean {:7.2f}  p90 {:7.2f}  max {:7.2f} ms", kind,
                   entry.ms.size(), static_cast<double>(mean(entry.ms)),
                   static_cast<double>(percentile(entry.ms, 0.9)),
                   static_cast<double>(percentile(entry.ms, 1.0)));
        if (!entry.assemble_all_ms.empty()) {
            fmt::print("   (assemble_all alone: mean {:.2f}; {} assembled afresh)",
                       static_cast<double>(mean(entry.assemble_all_ms)), entry.assemblies);
        }
        fmt::print("\n");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "replay") {
        return run_replay(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "detect-size") {
        return run_detect_size(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "diagnose") {
        return run_diagnose(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "board") {
        return run_board(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "holds") {
        return run_holds(fs::path(args[1]));
    }
    if ((args.size() == 2 || args.size() == 3) && args[0] == "corners") {
        // How far a side may sit from the cube's: the app's own unless asked.
        const float max_distance =
            args.size() == 3 ? std::stof(std::string(args[2])) : cube::vision::kCornerFitDistance;
        return run_corners(fs::path(args[1]), max_distance);
    }
    if (args.size() == 2 && args[0] == "ambiguity") {
        return run_ambiguity(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "corner-colours") {
        return run_corner_colours(fs::path(args[1]));
    }
    if ((args.size() == 2 || args.size() == 3) && args[0] == "funnel") {
        return run_funnel(fs::path(args[1]), args.size() == 3 && args[2] == "-v");
    }
    if ((args.size() == 2 || args.size() == 3 || args.size() == 4) && args[0] == "cost") {
        std::size_t runs = 3;
        bool every_frame = false;
        for (std::size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "-v") {
                every_frame = true;
            } else {
                runs = static_cast<std::size_t>(std::max(1, std::stoi(std::string(args[i]))));
            }
        }
        return run_cost(fs::path(args[1]), runs, every_frame);
    }
    fmt::print(
        "usage:\n  cube_scan replay <session-dir>\n  cube_scan diagnose <session-dir>\n  cube_scan "
        "board <root-dir>\n"
        "  cube_scan holds <session-dir>\n  cube_scan corners <session-dir> [max-distance]\n"
        "  cube_scan corner-colours <session-dir>\n  cube_scan ambiguity <session-dir>\n"
        "  cube_scan funnel <session-dir> [-v]\n"
        "  cube_scan cost <session-dir> [runs] [-v]\n");
    return 2;
}
