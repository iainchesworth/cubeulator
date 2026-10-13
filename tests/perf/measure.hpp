#pragma once

// How the performance tests measure, in one place so that the budget tests,
// the timing tests and the hidden report all read the same numbers.
//
// Two kinds of figure, kept apart on purpose (docs/testing.md, "Performance
// tests"):
//
//   work    counted, not timed: the nodes a search visited, the pixel buffers
//           a read allocated. The same on every machine, compiler and build
//           type, so a budget on it is exact and can sit close to the truth.
//   time    a stopwatch, only ever as a ratio to a calibration workload run
//           in the same process, and never trusted outside an optimised,
//           uninstrumented build (perf_build.hpp).

#include <cube/solver/cube.hpp>
#include <cube/solver/search_work.hpp>
#include <cube/solver/solver.hpp>

#include <opencv2/core.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cube::perf {

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

// The scrambles every budget is measured on. random_scramble() draws through
// std::uniform_int_distribution, whose algorithm is each standard library's
// own, so the same seed is a different cube on MSVC, libstdc++ and libc++ and
// a node budget would be a budget for one of them. std::mt19937's output is
// specified exactly, so this takes it modulo the number of choices: the same
// seed is the same cube everywhere, and so is every count below.
//
// 25 moves, the length the solver tests use.
inline constexpr int kScrambleMoves = 25;
[[nodiscard]] solver::Cube corpus_scramble(unsigned seed, int moves = kScrambleMoves);

// ---------------------------------------------------------------------------
// Solver work
// ---------------------------------------------------------------------------

inline constexpr std::size_t kBeginnerStages = 5;

struct StageWork {
    solver::Stage stage = solver::Stage::Cross;
    solver::SearchWork work;
    std::size_t moves = 0;
};

// One beginner solve, taken stage by stage through the public stage functions:
// the chain solve() runs, with the work of each link read as it ends.
struct BeginnerWork {
    std::array<StageWork, kBeginnerStages> stages;
    // What all five spent, and the length of what solve() would return for
    // them: merged, as the app shows it.
    solver::SearchWork total;
    std::size_t moves = 0;
    bool solved = false;
};

[[nodiscard]] BeginnerWork measure_beginner(const solver::Cube& scrambled);

// One solve by any method.
struct MethodWork {
    solver::SearchWork work;
    std::size_t moves = 0;
    bool solved = false;
};

[[nodiscard]] MethodWork measure_method(const solver::Cube& scrambled, solver::SolveMethod method);

// ---------------------------------------------------------------------------
// Pixel buffers
// ---------------------------------------------------------------------------

// What `action` asked cv::Mat to allocate. OpenCV allocates every Mat's pixel
// buffer through one process-wide allocator; this swaps in one that counts and
// then hands each request to the standard one, so the work is unchanged.
// Temporary buffers inside an OpenCV routine that do not go through a Mat are
// not seen, which is the right scope: they are the library's, and a
// regression in the reader's own use of it is what this is for.
struct BufferWork {
    std::uint64_t allocations = 0;
    std::uint64_t bytes = 0;
};

template <typename Action>
[[nodiscard]] BufferWork count_buffers(Action&& action);

namespace detail {
void begin_counting();
[[nodiscard]] BufferWork end_counting();
}  // namespace detail

template <typename Action>
BufferWork count_buffers(Action&& action) {
    detail::begin_counting();
    action();
    return detail::end_counting();
}

// ---------------------------------------------------------------------------
// Time, relative to a calibration workload
// ---------------------------------------------------------------------------

using Clock = std::chrono::steady_clock;

[[nodiscard]] inline double milliseconds_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// True when the person running the tests has asked for the wall-time cases:
// CUBE_PERF_TIMING is set to something other than "" or "0". They are opt-in
// because a stopwatch on a shared machine is the one check that can be wrong
// for no reason of its own (docs/testing.md, "Performance tests").
[[nodiscard]] bool timing_requested();

// How much a calibration workload touches. A task that stays in cache (reading
// a frame: a megabyte or two) is calibrated against one that does; a task that
// does not (a beginner solve keeps tens of megabytes of search nodes and hash
// entries) against one that does not either. A busy neighbour costs the second
// kind several times what it costs the first (measured: a solve at 2.5 times
// its ratio, with fourteen other solvers running, against a calibration that
// did not move), and a ratio that follows the machine's load is no use.
enum class Weight { light, heavy };

// A fixed amount of integer, hash-table and copy work that touches none of the
// code under test, so a regression in that code cannot also slow it down and
// cancel itself out of the ratio. What it measures is how fast this machine
// is, right now, at the kind of work the solver and the reader do (hash
// insertions, small structs appended and copied, table lookups). Its result is
// returned so that the optimiser cannot drop it.
[[nodiscard]] std::uint64_t calibration_workload(Weight weight = Weight::light);

// The median of `values`; the middle one, or the mean of the two middle ones.
[[nodiscard]] double median(std::vector<double> values);

// What a task cost against the calibration workload.
//
// `ratio` is the task's fastest run over the calibration's fastest run. The
// fastest, not the median: interference only ever adds time, so the fastest
// run is the one the code itself cost and a burst of load that lands on a
// third of the runs cannot move it, where a median of fifteen reads of a 4 ms
// task was seen at 83 ms while another process had every core. A regression
// slows every run, the fastest included, so it still shows. The medians are
// kept for the report, which is where a skew between the two is worth seeing.
struct RelativeTime {
    double task_ms = 0.0;
    double calibration_ms = 0.0;
    double ratio = 0.0;
    double task_median_ms = 0.0;
    double calibration_median_ms = 0.0;
};

// OpenCV on one thread for as long as this lives: its thread pool waits on
// cores that another process may hold, which is a measurement of that process.
class SingleThreadedOpenCv {
public:
    SingleThreadedOpenCv() : previous_(cv::getNumThreads()) { cv::setNumThreads(1); }
    ~SingleThreadedOpenCv() { cv::setNumThreads(previous_); }

    SingleThreadedOpenCv(const SingleThreadedOpenCv&) = delete;
    SingleThreadedOpenCv& operator=(const SingleThreadedOpenCv&) = delete;
    SingleThreadedOpenCv(SingleThreadedOpenCv&&) = delete;
    SingleThreadedOpenCv& operator=(SingleThreadedOpenCv&&) = delete;

private:
    int previous_;
};

// Runs `task` and the calibration workload alternately, `runs` times each
// after one warm-up of each. `task` takes no arguments and returns a number
// that depends on its work (a count, a checksum): it is what keeps an
// optimiser that sees the whole program from deciding the work was not
// needed.
//
// Run it under a SingleThreadedOpenCv where the task uses OpenCV: the
// calibration is one thread, and so should the task be.
template <typename Task>
[[nodiscard]] RelativeTime time_against_calibration(Task&& task, int runs,
                                                    Weight weight = Weight::light);

namespace detail {
// Written, never read: the sink the results of timed work are stored in.
inline volatile std::uint64_t g_sink = 0;
}  // namespace detail

template <typename Task>
RelativeTime time_against_calibration(Task&& task, int runs, Weight weight) {
    std::uint64_t sink = calibration_workload(weight);
    sink += static_cast<std::uint64_t>(task());
    std::vector<double> task_ms;
    std::vector<double> calibration_ms;
    for (int run = 0; run < runs; ++run) {
        auto start = Clock::now();
        sink += calibration_workload(weight);
        calibration_ms.push_back(milliseconds_since(start));
        start = Clock::now();
        sink += static_cast<std::uint64_t>(task());
        task_ms.push_back(milliseconds_since(start));
    }
    detail::g_sink = sink;

    RelativeTime out;
    out.task_ms = *std::ranges::min_element(task_ms);
    out.calibration_ms = *std::ranges::min_element(calibration_ms);
    out.ratio = out.task_ms / out.calibration_ms;
    out.task_median_ms = median(task_ms);
    out.calibration_median_ms = median(calibration_ms);
    return out;
}

}  // namespace cube::perf
