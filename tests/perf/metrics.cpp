#include "metrics.hpp"

#include "budgets.hpp"
#include "measure.hpp"
#include "scenes.hpp"

#include <cube/solver/cube.hpp>
#include <cube/solver/solver.hpp>
#include <cube/vision/direct_scan.hpp>

// The solver's own hard caps, which are private: the budgets are held under
// them below, so that a cap stays a backstop and never becomes the budget.
#include "bfs_search.hpp"
#include "last_layer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cube::perf {

namespace {

namespace b = budget;

using solver::SolveMethod;

// --- the budgets stay clear of the caps --------------------------------------

constexpr std::array<std::string_view, kBeginnerStages> kStageNames = {
    "cross", "white_corners", "middle_edges", "oll", "pll"};

// A single search may be budgeted up to half its cap and no further: a budget
// that reaches the cap has stopped saying anything, and a search that reaches
// it has stopped being a search (it fails with no_solution instead).
constexpr bool clear_of_caps() {
    for (std::size_t i = 0; i < kBeginnerStages; ++i) {
        const std::uint64_t cap =
            i < 3 ? solver::detail::kMaxVisitedNodes : solver::detail::kMaxLastLayerStates;
        if (b::kBeginnerStages[i].max_search * 2 > cap) {
            return false;
        }
    }
    return b::kBeginnerMaxSearch * 2 <= solver::detail::kMaxVisitedNodes &&
           b::kIntermediate.max_search < solver::detail::kMaxVisitedNodes &&
           b::kBeginnerSolveMaxNodes >= b::kBeginnerStages[0].max_nodes;
}
static_assert(clear_of_caps(),
              "a search budget is above half of the solver's hard cap (kMaxVisitedNodes / "
              "kMaxLastLayerStates): fix the search, do not raise the budget");

// --- a statistic over the corpus ----------------------------------------------

class Track {
public:
    void add(unsigned seed, double value) {
        sum_ += value;
        ++count_;
        if (count_ == 1 || value > max_) {
            max_ = value;
            seed_ = seed;
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

[[nodiscard]] Metric metric(std::string name, double measured, double budget, unsigned seed,
                            std::string unit) {
    return Metric{.name = std::move(name),
                  .measured = measured,
                  .budget = budget,
                  .seed = seed,
                  .unit = std::move(unit),
                  .note = {}};
}

// A solve that produced moves visited at least one node. One that did not has
// a search that is not reporting its work (it does not use
// cube/solver/search_work.hpp), and every node budget would pass on a zero:
// better to say so than to let it.
[[nodiscard]] Metric uncounted(std::string name, unsigned count, const char* unit) {
    Metric m = metric(std::move(name), count, 0.0, kNoSeed, unit);
    m.note =
        "a search that found moves visited no nodes: it is not calling "
        "cube::solver::detail::note_search (cube/solver/search_work.hpp)";
    return m;
}

void add_max_and_mean(std::vector<Metric>& out, const std::string& base, const Track& track,
                      double max_budget, double mean_budget, const char* unit) {
    out.push_back(metric(base + ".max", track.max(), max_budget, track.seed(), unit));
    out.push_back(metric(base + ".mean", track.mean(), mean_budget, kNoSeed, unit));
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
        nodes.add(seed, static_cast<double>(work.work.nodes));
        search.add(seed, static_cast<double>(work.work.largest));
        moves.add(seed, static_cast<double>(work.moves));
    }

    const std::string base(name);
    std::vector<Metric> out;
    add_max_and_mean(out, base + ".solve.nodes", nodes, static_cast<double>(limit.max_nodes),
                     static_cast<double>(limit.mean_nodes), "nodes");
    out.push_back(metric(base + ".search.max", search.max(), static_cast<double>(limit.max_search),
                         search.seed(), "nodes"));
    add_max_and_mean(out, base + ".moves", moves, static_cast<double>(limit.max_moves),
                     limit.mean_moves, "moves");
    out.push_back(metric(base + ".unsolved", unsolved, 0.0, kNoSeed, "solves"));
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
        for (const StageWork& stage : work.stages) {
            silent += stage.moves > 0 && stage.work.nodes == 0 ? 1U : 0U;
        }
        for (std::size_t i = 0; i < kBeginnerStages; ++i) {
            stage_nodes[i].add(seed, static_cast<double>(work.stages[i].work.nodes));
            stage_search[i].add(seed, static_cast<double>(work.stages[i].work.largest));
        }
        total.add(seed, static_cast<double>(work.total.nodes));
        search.add(seed, static_cast<double>(work.total.largest));
        moves.add(seed, static_cast<double>(work.moves));
    }

    std::vector<Metric> out;
    for (std::size_t i = 0; i < kBeginnerStages; ++i) {
        const std::string base = "beginner." + std::string(kStageNames[i]);
        const b::StageBudget& limit = b::kBeginnerStages[i];
        add_max_and_mean(out, base + ".nodes", stage_nodes[i], static_cast<double>(limit.max_nodes),
                         static_cast<double>(limit.mean_nodes), "nodes");
        out.push_back(metric(base + ".search.max", stage_search[i].max(),
                             static_cast<double>(limit.max_search), stage_search[i].seed(),
                             "nodes"));
    }
    add_max_and_mean(out, "beginner.solve.nodes", total,
                     static_cast<double>(b::kBeginnerSolveMaxNodes),
                     static_cast<double>(b::kBeginnerSolveMeanNodes), "nodes");
    out.push_back(metric("beginner.search.max", search.max(),
                         static_cast<double>(b::kBeginnerMaxSearch), search.seed(), "nodes"));
    add_max_and_mean(out, "beginner.moves", moves, static_cast<double>(b::kBeginnerMaxMoves),
                     b::kBeginnerMeanMoves, "moves");
    out.push_back(metric("beginner.unsolved", unsolved, 0.0, kNoSeed, "solves"));
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

    for (std::size_t i = 0; i < kSceneSizes.size(); ++i) {
        const SceneSize size = kSceneSizes[i];
        const std::string at = std::to_string(size.width) + "x" + std::to_string(size.height);
        const cv::Mat frame = side_frame(size);
        const cv::Mat nothing = empty_frame(size);

        std::optional<vision::SideRead> read;
        const BufferWork found = count_buffers([&] { read = vision::read_side(frame); });
        std::optional<vision::SideRead> none;
        const BufferWork empty = count_buffers([&] { none = vision::read_side(nothing); });
        found_bytes[i] = static_cast<double>(found.bytes);

        // A read that found nothing is the cheap path, and would pass for
        // the real one; say so rather than let it.
        out.push_back(
            metric("read." + at + ".missed", read.has_value() ? 0.0 : 1.0, 0.0, kNoSeed, "reads"));
        out.push_back(metric("read." + at + ".empty_frame_read", none.has_value() ? 1.0 : 0.0, 0.0,
                             kNoSeed, "reads"));
        out.push_back(metric("read." + at + ".buffers", static_cast<double>(found.allocations),
                             static_cast<double>(b::kReadBuffers), kNoSeed, "Mat buffers"));
        out.push_back(metric("read." + at + ".bytes", static_cast<double>(found.bytes),
                             static_cast<double>(b::kReadBytes), kNoSeed, "bytes"));
        out.push_back(metric("read." + at + ".empty_frame.buffers",
                             static_cast<double>(empty.allocations),
                             static_cast<double>(b::kEmptyReadBuffers), kNoSeed, "Mat buffers"));
        out.push_back(metric("read." + at + ".empty_frame.bytes", static_cast<double>(empty.bytes),
                             static_cast<double>(b::kEmptyReadBytes), kNoSeed, "bytes"));
    }
    // The contract: a bigger frame is reduced to the working width first, so
    // what the reader allocates does not follow the camera.
    for (std::size_t i = 1; i < kSceneSizes.size(); ++i) {
        out.push_back(metric("read." + std::to_string(kSceneSizes[i].width) + "x" +
                                 std::to_string(kSceneSizes[i].height) + ".bytes_vs_640x360",
                             found_bytes[i] / found_bytes[0], b::kReadBytesGrowth, kNoSeed,
                             "x working-width read"));
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
    out.push_back(metric("scan.sides_found", static_cast<double>(scan.sides().size()), 6.0, kNoSeed,
                         "sides"));
    out.push_back(metric("scan.cube_missed",
                         scanned.has_value() && scanned->cube == cube ? 0.0 : 1.0, 0.0, kNoSeed,
                         "scans"));
    out.push_back(metric("scan.add.buffers", static_cast<double>(added.allocations),
                         static_cast<double>(b::kScanAddBuffers), kNoSeed, "Mat buffers"));
    out.push_back(metric("scan.scanned.buffers", static_cast<double>(finished.allocations),
                         static_cast<double>(b::kScannedBuffers), kNoSeed, "Mat buffers"));
    return out;
}

// --- time ----------------------------------------------------------------------

namespace {

[[nodiscard]] Metric timed(std::string name, const RelativeTime& time, const b::TimeBudget& limit,
                           unsigned seed) {
    char note[160];
    std::snprintf(
        note, sizeof note, "fastest run: task %.2f ms, calibration %.2f ms (medians %.2f and %.2f)",
        time.task_ms, time.calibration_ms, time.task_median_ms, time.calibration_median_ms);
    Metric m = metric(std::move(name), time.ratio, limit.ratio, seed, "x calibration");
    m.note = note;
    return m;
}

}  // namespace

std::vector<Metric> solve_timing_metrics() {
    std::vector<Metric> out;

    // One seed, the same for each method, so that the ratio follows the code
    // and not the scramble. Seed 0 is an average beginner solve (0.9 million
    // nodes against a corpus mean of 1.0).
    constexpr unsigned kSolveSeed = 0;
    const solver::Cube cube = corpus_scramble(kSolveSeed);
    const auto solve = [&cube](SolveMethod method) {
        return [&cube, method]() -> std::uint64_t {
            const auto solution = solver::solve(cube, method);
            return solution.has_value() ? solution->steps.size() : 0U;
        };
    };
    solver::prepare(SolveMethod::Advanced);
    // The beginner solve's searches keep tens of megabytes: the calibration
    // that is like it is the one that does too.
    out.push_back(
        timed("timing.beginner_solve",
              time_against_calibration(solve(SolveMethod::Beginner), b::kSolveRuns, Weight::heavy),
              b::kBeginnerSolveTime, kSolveSeed));
    out.push_back(timed("timing.intermediate_solve",
                        time_against_calibration(solve(SolveMethod::Intermediate), b::kFastRuns),
                        b::kIntermediateSolveTime, kSolveSeed));
    out.push_back(timed("timing.advanced_solve",
                        time_against_calibration(solve(SolveMethod::Advanced), b::kSolveRuns),
                        b::kAdvancedSolveTime, kSolveSeed));
    return out;
}

std::vector<Metric> read_timing_metrics() {
    const SingleThreadedOpenCv one_thread;
    std::vector<Metric> out;
    const cv::Mat frame360 = side_frame(kSceneSizes[0]);
    const cv::Mat frame1080 = side_frame(kSceneSizes[2]);
    const cv::Mat nothing360 = empty_frame(kSceneSizes[0]);
    const auto read = [](const cv::Mat& frame) {
        return
            [&frame]() -> std::uint64_t { return vision::read_side(frame).has_value() ? 1U : 0U; };
    };
    out.push_back(timed("timing.read_side.640x360",
                        time_against_calibration(read(frame360), b::kFastRuns), b::kReadTime360,
                        kNoSeed));
    out.push_back(timed("timing.read_side.1920x1080",
                        time_against_calibration(read(frame1080), b::kFastRuns), b::kReadTime1080,
                        kNoSeed));
    out.push_back(timed("timing.read_side.640x360_empty",
                        time_against_calibration(read(nothing360), b::kFastRuns),
                        b::kEmptyReadTime360, kNoSeed));
    return out;
}

std::vector<Metric> scan_timing_metrics() {
    const SingleThreadedOpenCv one_thread;
    std::vector<Metric> out;

    // A side held for a hundred frames: SideScan::add, which re-fuses what it
    // has each time. A hundred is three seconds of a held cube.
    const std::optional<vision::SideRead> held = vision::read_side(side_frame(kSceneSizes[0]));
    out.push_back(timed("timing.scan_add.100_reads",
                        time_against_calibration(
                            [&held]() -> std::uint64_t {
                                vision::SideScan scan;
                                for (int i = 0; i < 100; ++i) {
                                    scan.add(held);
                                }
                                return scan.sides().size();
                            },
                            b::kFastRuns),
                        b::kScanAddTime, kNoSeed));

    // Finishing a scan: colour resolution, then the 4^6 assembly search.
    vision::SideScan scan;
    for (const std::optional<vision::SideRead>& r : scan_reads(test::scramble(1), 12)) {
        scan.add(r);
    }
    out.push_back(
        timed("timing.scanned",
              time_against_calibration(
                  [&scan]() -> std::uint64_t { return scan.scanned().has_value() ? 1U : 0U; },
                  b::kFastRuns),
              b::kScannedTime, kNoSeed));
    return out;
}

}  // namespace cube::perf
