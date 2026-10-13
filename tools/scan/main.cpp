// cube_scan: replay recorded scans through the direct-scan reader and score
// them. This is the scoreboard of docs/recovery-plan.md - the only gate is a
// real recorded session ending in the exact true cube.
//
//   cube_scan replay <session-dir>   one session: the cube and its solution
//   cube_scan board <root-dir>       every session under root against truth
//
// A session is a directory of rec_NNNNNN.jpg frames. Its truth, if known, is
// truth.txt: six words of nine letters from WYROGB, one per side, each side
// row-major as seen from outside in any orientation and any order.

#include <cube/model/facelet.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/notation.hpp>
#include <cube/solver/scan_assembly.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/color_resolution.hpp>
#include <cube/vision/direct_scan.hpp>
#include <cube/vision/temporal_fusion.hpp>

#include <opencv2/imgcodecs.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
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
    std::size_t sides = 0;
    std::optional<std::size_t> read_at;  // first frame at which the cube was known
    std::optional<Cube> cube;            // the cube at that frame
    std::size_t changes_after = 0;       // later frames whose cube differed
};

[[nodiscard]] Outcome replay(const fs::path& session) {
    Outcome outcome;
    cube::vision::SideScan scan;
    for (const fs::path& frame : frames_of(session)) {
        const cv::Mat bgr = cv::imread(frame.string(), cv::IMREAD_COLOR);
        const auto read = cube::vision::read_side(bgr);
        outcome.reads += read.has_value() ? 1U : 0U;
        scan.add(read);
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
    fmt::print("{}: frames {}  lattice reads {}  sides {}\n", session.filename().string(),
               outcome.frames, outcome.reads, outcome.sides);
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

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    if (args.size() == 2 && args[0] == "replay") {
        return run_replay(fs::path(args[1]));
    }
    if (args.size() == 2 && args[0] == "board") {
        return run_board(fs::path(args[1]));
    }
    fmt::print("usage:\n  cube_scan replay <session-dir>\n  cube_scan board <root-dir>\n");
    return 2;
}
