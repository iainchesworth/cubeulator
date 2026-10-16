#include "metrics.hpp"

#include "budgets.hpp"
#include "measure.hpp"
#include "scenes.hpp"

#include <cube/nxn/commutator.hpp>
#include <cube/nxn/cube.hpp>
#include <cube/nxn/cube3.hpp>
#include <cube/nxn/strategy.hpp>
#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/corner_view.hpp>
#include <cube/vision/direct_scan.hpp>

// The solver's own hard caps, which are private: the budgets are held under
// them below, so that a cap stays a backstop and never becomes the budget.
#include "bfs_search.hpp"
#include "last_layer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cube::perf {

namespace {

namespace b = budget;

using solver::SolveMethod;

[[nodiscard]] constexpr double number(std::uint64_t value) {
    return static_cast<double>(value);
}

// --- the budgets stay clear of the caps --------------------------------------

constexpr std::array<std::string_view, kBeginnerStages> kStageNames = {
    "cross", "white_corners", "middle_edges", "oll", "pll",
};

// A single search may be budgeted up to half its cap and no further: a budget
// that reaches the cap has stopped saying anything, and a search that reaches
// it has stopped being a search (it fails with no_solution instead).
constexpr bool clear_of_caps() {
    std::size_t stage = 0;
    for (const b::StageBudget& limit : b::kBeginnerStages) {
        const std::uint64_t cap =
            stage++ < 3 ? solver::detail::kMaxVisitedNodes : solver::detail::kMaxLastLayerStates;
        if (limit.max_search * 2 > cap) {
            return false;
        }
    }
    return b::kBeginnerMaxSearch * 2 <= solver::detail::kMaxVisitedNodes &&
           b::kIntermediate.max_search < solver::detail::kMaxVisitedNodes &&
           b::kBeginnerSolveMaxNodes >= b::kBeginnerStages.front().max_nodes;
}
static_assert(clear_of_caps(),
              "a search budget is above half of the solver's hard cap (kMaxVisitedNodes / "
              "kMaxLastLayerStates): fix the search, do not raise the budget");

// --- a statistic over the corpus ----------------------------------------------

struct Observation {
    unsigned seed;
    double value;
};

class Track {
public:
    void add(const Observation& observation) {
        sum_ += observation.value;
        ++count_;
        if (count_ == 1 || observation.value > max_) {
            max_ = observation.value;
            seed_ = observation.seed;
        }
    }
    [[nodiscard]] double max() const noexcept { return max_; }
    [[nodiscard]] double mean() const noexcept {
        return count_ == 0 ? 0.0 : sum_ / static_cast<double>(count_);
    }
    [[nodiscard]] unsigned seed() const noexcept { return seed_; }

private:
    double sum_ = 0.0;
    double max_ = 0.0;
    std::size_t count_ = 0;
    unsigned seed_ = kNoSeed;
};

// What a budget is, for the pair of metrics (the worst corpus scramble and the
// mean) every counted quantity gets.
struct Limits {
    double max;
    double mean;
};

void add_max_and_mean(std::vector<Metric>& out, const std::string& base, const Track& track,
                      const Limits& limits, const char* unit) {
    out.push_back({
        .name = base + ".max",
        .measured = track.max(),
        .budget = limits.max,
        .seed = track.seed(),
        .unit = unit,
    });
    out.push_back(
        {.name = base + ".mean", .measured = track.mean(), .budget = limits.mean, .unit = unit});
}

// A count that must be zero: solves that did not finish, searches that did
// not report.
[[nodiscard]] Metric must_be_zero(std::string name, unsigned count, const char* unit) {
    return {.name = std::move(name), .measured = static_cast<double>(count), .unit = unit};
}

// A solve that produced moves visited at least one node. One that did not has
// a search that is not reporting its work (it does not use
// cube/solver/search_work.hpp), and every node budget would pass on a zero:
// better to say so than to let it.
[[nodiscard]] Metric uncounted(std::string name, unsigned count, const char* unit) {
    Metric m = must_be_zero(std::move(name), count, unit);
    m.note =
        "a search that found moves visited no nodes: it is not calling "
        "cube::solver::detail::note_search (cube/solver/search_work.hpp)";
    return m;
}

// A read that must (or must not) have found something, as a zero when it did
// what it should.
[[nodiscard]] Metric expected_read(std::string name, bool found, bool should_find) {
    return {.name = std::move(name), .measured = found == should_find ? 0.0 : 1.0, .unit = "reads"};
}

[[nodiscard]] std::string size_name(const SceneSize& size) {
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

// The buffers and bytes of one read against their two budgets.
void add_buffers(std::vector<Metric>& out, const std::string& base, const BufferWork& work,
                 const Limits& limits) {
    out.push_back({
        .name = base + ".buffers",
        .measured = number(work.allocations),
        .budget = limits.max,
        .unit = "Mat buffers",
    });
    out.push_back({
        .name = base + ".bytes",
        .measured = number(work.bytes),
        .budget = limits.mean,
        .unit = "bytes",
    });
}

// --- the three methods ---------------------------------------------------------

[[nodiscard]] std::vector<Metric> method_metrics(std::string_view name, SolveMethod method,
                                                 const b::MethodBudget& limit) {
    solver::prepare(method);
    Track nodes;
    Track search;
    Track moves;
    unsigned unsolved = 0;
    unsigned silent = 0;
    for (unsigned seed = 0; seed < b::kSolverSeeds; ++seed) {
        const MethodWork work = measure_method(corpus_scramble(seed), method);
        unsolved += work.solved ? 0U : 1U;
        silent += work.moves > 0 && work.work.nodes == 0 ? 1U : 0U;
        nodes.add({.seed = seed, .value = number(work.work.nodes)});
        search.add({.seed = seed, .value = number(work.work.largest)});
        moves.add({.seed = seed, .value = static_cast<double>(work.moves)});
    }

    const std::string base(name);
    std::vector<Metric> out;
    add_max_and_mean(out, base + ".solve.nodes", nodes,
                     {.max = number(limit.max_nodes), .mean = number(limit.mean_nodes)}, "nodes");
    out.push_back({
        .name = base + ".search.max",
        .measured = search.max(),
        .budget = number(limit.max_search),
        .seed = search.seed(),
        .unit = "nodes",
    });
    add_max_and_mean(out, base + ".moves", moves,
                     {.max = number(limit.max_moves), .mean = limit.mean_moves}, "moves");
    out.push_back(must_be_zero(base + ".unsolved", unsolved, "solves"));
    out.push_back(uncounted(base + ".uncounted_solves", silent, "solves"));
    return out;
}

}  // namespace

std::vector<Metric> beginner_metrics() {
    std::array<Track, kBeginnerStages> stage_nodes;
    std::array<Track, kBeginnerStages> stage_search;
    Track total;
    Track search;
    Track moves;
    unsigned unsolved = 0;
    unsigned silent = 0;
    for (unsigned seed = 0; seed < b::kSolverSeeds; ++seed) {
        const BeginnerWork work = measure_beginner(corpus_scramble(seed));
        unsolved += work.solved ? 0U : 1U;
        std::size_t index = 0;
        for (const StageWork& stage : work.stages) {
            silent += stage.moves > 0 && stage.work.nodes == 0 ? 1U : 0U;
            stage_nodes.at(index).add({.seed = seed, .value = number(stage.work.nodes)});
            stage_search.at(index).add({.seed = seed, .value = number(stage.work.largest)});
            ++index;
        }
        total.add({.seed = seed, .value = number(work.total.nodes)});
        search.add({.seed = seed, .value = number(work.total.largest)});
        moves.add({.seed = seed, .value = static_cast<double>(work.moves)});
    }

    std::vector<Metric> out;
    std::size_t index = 0;
    for (const b::StageBudget& limit : b::kBeginnerStages) {
        const std::string base = "beginner." + std::string(kStageNames.at(index));
        add_max_and_mean(out, base + ".nodes", stage_nodes.at(index),
                         {.max = number(limit.max_nodes), .mean = number(limit.mean_nodes)},
                         "nodes");
        out.push_back({
            .name = base + ".search.max",
            .measured = stage_search.at(index).max(),
            .budget = number(limit.max_search),
            .seed = stage_search.at(index).seed(),
            .unit = "nodes",
        });
        ++index;
    }
    add_max_and_mean(
        out, "beginner.solve.nodes", total,
        {.max = number(b::kBeginnerSolveMaxNodes), .mean = number(b::kBeginnerSolveMeanNodes)},
        "nodes");
    out.push_back({
        .name = "beginner.search.max",
        .measured = search.max(),
        .budget = number(b::kBeginnerMaxSearch),
        .seed = search.seed(),
        .unit = "nodes",
    });
    add_max_and_mean(out, "beginner.moves", moves,
                     {.max = number(b::kBeginnerMaxMoves), .mean = b::kBeginnerMeanMoves}, "moves");
    out.push_back(must_be_zero("beginner.unsolved", unsolved, "solves"));
    out.push_back(uncounted("beginner.uncounted_stages", silent, "stages"));
    return out;
}

std::vector<Metric> intermediate_metrics() {
    return method_metrics("intermediate", SolveMethod::Intermediate, b::kIntermediate);
}

std::vector<Metric> advanced_metrics() {
    return method_metrics("advanced", SolveMethod::Advanced, b::kAdvanced);
}

// --- the reader ----------------------------------------------------------------

std::vector<Metric> scan_metrics() {
    std::vector<Metric> out;
    std::array<double, kSceneSizes.size()> found_bytes{};

    std::size_t index = 0;
    for (const SceneSize& size : kSceneSizes) {
        const std::string base = "read." + size_name(size);
        const cv::Mat frame = side_frame(size);
        const cv::Mat nothing = empty_frame(size);

        std::optional<vision::SideRead> read;
        const BufferWork found = count_buffers([&] { read = vision::read_side(frame); });
        std::optional<vision::SideRead> none;
        const BufferWork empty = count_buffers([&] { none = vision::read_side(nothing); });
        found_bytes.at(index++) = number(found.bytes);

        // A read that found nothing is the cheap path, and would pass for the
        // real one; say so rather than let it.
        out.push_back(expected_read(base + ".missed", read.has_value(), true));
        out.push_back(expected_read(base + ".empty_frame_read", none.has_value(), false));
        add_buffers(out, base, found,
                    {.max = number(b::kReadBuffers), .mean = number(b::kReadBytes)});
        add_buffers(out, base + ".empty_frame", empty,
                    {.max = number(b::kEmptyReadBuffers), .mean = number(b::kEmptyReadBytes)});
    }
    // The contract: a bigger frame is reduced to the working width first, so
    // what the reader allocates does not follow the camera.
    for (std::size_t i = 1; i < kSceneSizes.size(); ++i) {
        out.push_back({
            .name = "read." + size_name(kSceneSizes.at(i)) + ".bytes_vs_640x360",
            .measured = found_bytes.at(i) / found_bytes.front(),
            .budget = b::kReadBytesGrowth,
            .unit = "x working-width read",
        });
    }

    // The corner reader, which runs on every frame beside read_side: two sides
    // meeting at an edge, at the working width and at 1080p, and a room.
    std::array<double, 2> corner_bytes{};
    const solver::Cube corner_cube = test::scramble(1);
    constexpr std::array<std::size_t, 2> kCornerSizes = {0, 2};
    index = 0;
    for (const std::size_t which : kCornerSizes) {
        const SceneSize& size = kSceneSizes.at(which);
        const std::string base = "corner." + size_name(size);
        const cv::Mat frame = corner_frame(corner_cube, size);
        std::optional<vision::CornerRead> read;
        const BufferWork found = count_buffers([&] { read = vision::read_corner(frame); });
        corner_bytes.at(index++) = number(found.bytes);
        out.push_back(
            expected_read(base + ".missed", read.has_value() && read->sides.size() >= 2, true));
        add_buffers(out, base, found,
                    {.max = number(b::kCornerBuffers), .mean = number(b::kCornerBytes)});
    }
    out.push_back({
        .name = "corner.1920x1080.bytes_vs_640x360",
        .measured = corner_bytes.back() / corner_bytes.front(),
        .budget = b::kReadBytesGrowth,
        .unit = "x working-width read",
    });
    {
        const cv::Mat nothing = empty_frame(kSceneSizes.front());
        std::optional<vision::CornerRead> read;
        const BufferWork empty = count_buffers([&] { read = vision::read_corner(nothing); });
        out.push_back(expected_read("corner.empty_frame_read", read.has_value(), false));
        add_buffers(out, "corner.empty_frame", empty,
                    {.max = number(b::kEmptyCornerBuffers), .mean = number(b::kEmptyCornerBytes)});
    }

    // The placement of the blobs through the fit, which read_side makes on
    // every frame with six or more blobs and no side held square - a cube
    // turned between two sides is most of them, every quarter-turn - and which
    // allocates a matrix or two for each distinct lattice it weighs. One frame
    // where it finds nothing (a side just past the gate, with the same nine
    // blobs to weigh) and one where it finds the side (at the edge of it).
    {
        const SceneSize& size = kSceneSizes.front();
        const cv::Mat past = gate_edge_frame(corner_cube, size, 32.0F);
        std::optional<vision::SideRead> none;
        const BufferWork missed = count_buffers([&] { none = vision::read_side(past); });
        out.push_back(expected_read("read.640x360.past_gate_read", none.has_value(), false));
        add_buffers(
            out, "read.640x360.past_gate", missed,
            {.max = number(b::kThroughFitMissBuffers), .mean = number(b::kThroughFitMissBytes)});

        const cv::Mat edge = gate_edge_frame(corner_cube, size);
        std::optional<vision::SideRead> found;
        const BufferWork read = count_buffers([&] { found = vision::read_side(edge); });
        out.push_back(expected_read("read.640x360.gate_edge_missed", found.has_value(), true));
        add_buffers(
            out, "read.640x360.gate_edge", read,
            {.max = number(b::kThroughFitReadBuffers), .mean = number(b::kThroughFitReadBytes)});
    }

    // Feeding a scan and finishing it work on Lab triples, not pictures.
    const solver::Cube cube = test::scramble(1);
    vision::SideScan scan;
    const std::vector<std::optional<vision::SideRead>> reads = scan_reads(cube, 12);
    const BufferWork added = count_buffers([&] {
        for (const std::optional<vision::SideRead>& read : reads) {
            scan.add(read);
        }
    });
    Result<vision::ScannedCube> scanned = std::unexpected(CubeError::face_capture_incomplete);
    const BufferWork finished = count_buffers([&] { scanned = scan.scanned(); });
    out.push_back({
        .name = "scan.sides_found",
        .measured = static_cast<double>(scan.sides().size()),
        .budget = 6.0,
        .unit = "sides",
    });
    out.push_back({
        .name = "scan.cube_missed",
        .measured = scanned.has_value() && scanned->cube == cube ? 0.0 : 1.0,
        .unit = "scans",
    });
    out.push_back({
        .name = "scan.add.buffers",
        .measured = number(added.allocations),
        .budget = number(b::kScanAddBuffers),
        .unit = "Mat buffers",
    });
    out.push_back({
        .name = "scan.scanned.buffers",
        .measured = number(finished.allocations),
        .budget = number(b::kScannedBuffers),
        .unit = "Mat buffers",
    });
    return out;
}

// --- the verdict ---------------------------------------------------------------

namespace {

// What a scan's judgements cost over a whole scan of one cube, counted in the
// times the named sides were put together into a cube (SideScan::assemblies()):
// the costly half of a judgement, which a scan whose sides make no cube answers
// again without, while what the attempt rested on is as it was.
struct Judgements {
    // Judgements made with six sides in; a cube ends the scan, as it does in
    // the app.
    std::size_t made = 0;
    // How many of them put the sides together afresh, and how many of the
    // hundred judgements of the finished scan did.
    std::size_t assembled = 0;
    std::size_t again = 0;
    // What the last one came to.
    std::optional<vision::AssemblyPath> path;
    // The faces looked at by all the assemblies, in search nodes
    // (cube/solver/search_work.hpp): what each assembly costs, which the count
    // of assemblies does not say.
    std::uint64_t nodes = 0;
};

[[nodiscard]] Judgements judge_a_scan(const solver::Cube& cube) {
    Judgements out;
    vision::SideScan scan;
    solver::reset_search_work();
    // The controller judges whenever a side's fused colours moved, which a read
    // of a side held in view does; judging after every read is that, bounded.
    // Says whether the scan made a cube.
    const auto judge = [&scan, &out](bool finished) {
        const std::size_t before = scan.assemblies();
        const vision::ScanDiagnosis diagnosis = scan.diagnose();
        if (!diagnosis.assembly.has_value()) {
            return false;  // fewer than six sides yet
        }
        const std::size_t assembled = scan.assemblies() - before;
        ++out.made;
        out.assembled += assembled;
        out.again += finished ? assembled : 0U;
        out.path = diagnosis.assembly->path;
        return diagnosis.result.has_value();
    };
    for (const std::optional<vision::SideRead>& read : scan_reads(cube, 12)) {
        scan.add(read);
        if (judge(false)) {
            out.nodes = solver::search_work().nodes;
            return out;
        }
    }
    // And the cube held in view a few seconds more, which is what it is.
    for (int i = 0; i < 100; ++i) {
        judge(true);
    }
    out.nodes = solver::search_work().nodes;
    return out;
}

void add_judgement(std::vector<Metric>& out, const std::string& base, const Judgements& work,
                   std::uint64_t assembled_budget, std::uint64_t nodes_budget) {
    out.push_back({
        .name = base + ".assembly.nodes",
        .measured = number(work.nodes),
        .budget = number(nodes_budget),
        .unit = "nodes",
        .note = "faces looked at by every assembly of the scan: what skipping combinations "
                "that are no cube saves, held",
    });
    out.push_back({
        .name = base + ".assembled",
        .measured = number(work.assembled),
        .budget = number(assembled_budget),
        .unit = "assemblies",
        .note = std::format("of {} judgements with six sides in", work.made),
    });
    out.push_back({
        .name = base + ".judged_again.assembled",
        .measured = number(work.again),
        .budget = 0.0,
        .unit = "assemblies",
        .note = "judging a scan again, nothing it rests on moved, must not put the sides "
                "together again",
    });
}

}  // namespace

namespace {

// The N x N corpus: 60 + 10 N random layer turns, from seeds 1 and up.
[[nodiscard]] nxn::NCube nxn_scramble(int size, unsigned seed) {
    return nxn::random_scramble({.size = size, .seed = seed, .moves = 60 + (10 * size)});
}

// The 3-cycles a size's library holds, over every orbit of centres and wings.
[[nodiscard]] std::uint64_t cycles_of(int size) {
    if (size < 4) {
        return 0;
    }
    const nxn::Geometry& geometry = nxn::Geometry::of(size);
    const nxn::CycleLibrary& library = nxn::CycleLibrary::of(size);
    std::uint64_t cycles = 0;
    for (std::size_t k = 0; k < geometry.centre_orbits().size(); ++k) {
        cycles += library.centres(k).size();
    }
    for (std::size_t k = 0; k < geometry.wing_orbits().size(); ++k) {
        cycles += library.wings(k).size();
    }
    return cycles;
}

}  // namespace

std::vector<Metric> nxn_metrics() {
    std::vector<Metric> out;
    unsigned unsolved = 0;
    for (const b::SizeBudget& limit : b::kNxnSizes) {
        Track moves;
        for (unsigned seed = 1; seed <= b::kNxnSeeds; ++seed) {
            const auto solution = nxn::solve(nxn_scramble(limit.size, seed));
            if (!solution.has_value()) {
                ++unsolved;
                continue;
            }
            moves.add({.seed = seed, .value = static_cast<double>(solution->move_count())});
        }
        add_max_and_mean(out, std::format("nxn.{0}x{0}.moves", limit.size), moves,
                         {.max = number(limit.max_moves), .mean = limit.mean_moves}, "moves");
        out.push_back({
            .name = std::format("nxn.{0}x{0}.library.cycles", limit.size),
            .measured = number(cycles_of(limit.size)),
            .budget = number(limit.cycles),
            .unit = "3-cycles",
        });
    }
    out.push_back(must_be_zero("nxn.unsolved", unsolved, "solves"));
    return out;
}

std::vector<Metric> judgement_metrics() {
    std::vector<Metric> out;

    // Sides that make a cube end the scan at the first judgement that says so:
    // one assembly.
    const Judgements settled = judge_a_scan(test::scramble(1));
    out.push_back(expected_read("judge.settled.path",
                                settled.path == vision::AssemblyPath::single_candidate, true));
    add_judgement(out, "judge.settled", settled, b::kSettledAssemblies, b::kSettledNodes);

    const Judgements several = judge_a_scan(cube_of_several_ways());
    out.push_back(expected_read("judge.several_ways.path",
                                several.path == vision::AssemblyPath::ambiguous, true));
    add_judgement(out, "judge.several_ways", several, b::kSeveralWaysAssemblies,
                  b::kSeveralWaysNodes);

    // Six sides that make no cube: the dearest, where the listing finds none and
    // each of the fifteen pairs of centres is asked whether trading their names
    // would.
    const Judgements none = judge_a_scan(
        with_edge_flipped(test::scramble(1), solver::Color::White, solver::Color::Green));
    out.push_back(expected_read("judge.no_cube.path",
                                none.path == vision::AssemblyPath::failed ||
                                    none.path == vision::AssemblyPath::names_exchanged,
                                true));
    add_judgement(out, "judge.no_cube", none, b::kNoCubeAssemblies, b::kNoCubeNodes);
    return out;
}

// --- time ----------------------------------------------------------------------

namespace {

[[nodiscard]] Metric timed(std::string name, const RelativeTime& time, const b::TimeBudget& limit,
                           unsigned seed) {
    return {
        .name = std::move(name),
        .measured = time.ratio,
        .budget = limit.ratio,
        .seed = seed,
        .unit = "x calibration",
        .note = std::format(
            "fastest run: task {:.2f} ms, calibration {:.2f} ms (medians {:.2f} and {:.2f})",
            time.task_ms, time.calibration_ms, time.task_median_ms, time.calibration_median_ms),
    };
}

// A task that solves `cube` by `method` and returns something that depends on
// the solution, so that it cannot be dropped.
[[nodiscard]] auto solving(const solver::Cube& cube, SolveMethod method) {
    return [&cube, method] -> std::uint64_t {
        const auto solution = solver::solve(cube, method);
        return solution.has_value() ? solution->steps.size() : 0U;
    };
}

[[nodiscard]] auto reading_side(const cv::Mat& frame) {
    return [&frame] -> std::uint64_t { return vision::read_side(frame).has_value() ? 1U : 0U; };
}

[[nodiscard]] auto reading_corner(const cv::Mat& frame) {
    return [&frame] -> std::uint64_t { return vision::read_corner(frame).has_value() ? 1U : 0U; };
}

}  // namespace

std::vector<Metric> solve_timing_metrics() {
    std::vector<Metric> out;

    // One seed, the same for each method, so that the ratio follows the code
    // and not the scramble. Seed 0 is an average beginner solve (0.9 million
    // nodes against a corpus mean of 0.9).
    constexpr unsigned kSolveSeed = 0;
    const solver::Cube cube = corpus_scramble(kSolveSeed);
    solver::prepare(SolveMethod::Advanced);
    // The beginner solve's searches keep tens of megabytes: the calibration
    // that is like it is the one that does too.
    out.push_back(timed("timing.beginner_solve",
                        time_against_calibration(solving(cube, SolveMethod::Beginner),
                                                 b::kSolveRuns, Weight::heavy),
                        b::kBeginnerSolveTime, kSolveSeed));
    out.push_back(
        timed("timing.intermediate_solve",
              time_against_calibration(solving(cube, SolveMethod::Intermediate), b::kFastRuns),
              b::kIntermediateSolveTime, kSolveSeed));
    out.push_back(
        timed("timing.advanced_solve",
              time_against_calibration(solving(cube, SolveMethod::Advanced), b::kSolveRuns),
              b::kAdvancedSolveTime, kSolveSeed));
    return out;
}

std::vector<Metric> nxn_timing_metrics() {
    std::vector<Metric> out;
    // One seed, the same for each size; the size's setup is built first, the
    // way the app builds it while a person scans or enters a cube.
    constexpr unsigned kSeed = 1;
    const std::array<std::pair<int, b::TimeBudget>, 3> sizes = {
        {
            {5, b::kNxnSolveTime5},
            {7, b::kNxnSolveTime7},
            {9, b::kNxnSolveTime9},
        },
    };
    solver::prepare(SolveMethod::Advanced);
    for (const auto& [size, limit] : sizes) {
        nxn::prepare(size, SolveMethod::Advanced);
        const nxn::NCube cube = nxn_scramble(size, kSeed);
        const auto solving_it = [&cube] -> std::uint64_t {
            const auto solution = nxn::solve(cube);
            return solution.has_value() ? solution->move_count() : 0U;
        };
        out.push_back(timed(std::format("timing.nxn_solve.{0}x{0}", size),
                            time_against_calibration(solving_it, b::kSolveRuns), limit, kSeed));
    }
    return out;
}

std::vector<Metric> read_timing_metrics() {
    const SingleThreadedOpenCv one_thread;
    std::vector<Metric> out;
    const cv::Mat frame360 = side_frame(kSceneSizes.front());
    const cv::Mat frame1080 = side_frame(kSceneSizes.back());
    const cv::Mat nothing360 = empty_frame(kSceneSizes.front());
    out.push_back(timed("timing.read_side.640x360",
                        time_against_calibration(reading_side(frame360), b::kFastRuns),
                        b::kReadTime360, kNoSeed));
    out.push_back(timed("timing.read_side.1920x1080",
                        time_against_calibration(reading_side(frame1080), b::kFastRuns),
                        b::kReadTime1080, kNoSeed));
    out.push_back(timed("timing.read_side.640x360_empty",
                        time_against_calibration(reading_side(nothing360), b::kFastRuns),
                        b::kEmptyReadTime360, kNoSeed));

    const cv::Mat corner360 = corner_frame(test::scramble(1), kSceneSizes.front());
    const cv::Mat corner1080 = corner_frame(test::scramble(1), kSceneSizes.back());
    out.push_back(timed("timing.read_corner.640x360",
                        time_against_calibration(reading_corner(corner360), b::kFastRuns),
                        b::kCornerReadTime360, kNoSeed));
    out.push_back(timed("timing.read_corner.1920x1080",
                        time_against_calibration(reading_corner(corner1080), b::kFastRuns),
                        b::kCornerReadTime1080, kNoSeed));
    out.push_back(timed("timing.read_corner.640x360_empty",
                        time_against_calibration(reading_corner(nothing360), b::kFastRuns),
                        b::kEmptyCornerReadTime360, kNoSeed));
    return out;
}

std::vector<Metric> scan_timing_metrics() {
    const SingleThreadedOpenCv one_thread;
    std::vector<Metric> out;

    // A side held for a hundred frames: SideScan::add, which re-fuses what it
    // has each time. A hundred is three seconds of a held cube.
    const std::optional<vision::SideRead> held = vision::read_side(side_frame(kSceneSizes.front()));
    const auto feeding = [&held] -> std::uint64_t {
        vision::SideScan scan;
        for (int i = 0; i < 100; ++i) {
            scan.add(held);
        }
        return scan.sides().size();
    };
    out.push_back(timed("timing.scan_add.100_reads",
                        time_against_calibration(feeding, b::kFastRuns), b::kScanAddTime, kNoSeed));

    // Finishing a scan: colour resolution, then the 4^6 assembly search.
    vision::SideScan scan;
    for (const std::optional<vision::SideRead>& read : scan_reads(test::scramble(1), 12)) {
        scan.add(read);
    }
    const auto finishing = [&scan] -> std::uint64_t {
        return scan.scanned().has_value() ? 1U : 0U;
    };
    out.push_back(timed("timing.scanned", time_against_calibration(finishing, b::kFastRuns),
                        b::kScannedTime, kNoSeed));
    return out;
}

}  // namespace cube::perf
