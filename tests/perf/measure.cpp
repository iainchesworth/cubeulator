#include "measure.hpp"

#include <cube/profiling.hpp>
#include <cube/solver/corners.hpp>
#include <cube/solver/cross.hpp>
#include <cube/solver/middle_edges.hpp>
#include <cube/solver/oll.hpp>
#include <cube/solver/pll.hpp>

#include <opencv2/core.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <random>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace cube::perf {

using solver::Cube;
using solver::Face;
using solver::Move;
using solver::SearchWork;
using solver::Stage;

Cube corpus_scramble(unsigned seed) {
    std::mt19937 rng{seed};
    Cube cube = Cube::solved();
    for (int i = 0; i < kScrambleMoves; ++i) {
        const Face face = solver::kAllFaces.at(rng() % solver::kAllFaces.size());
        const int turns = static_cast<int>(rng() % 3U) + 1;
        cube = cube.apply(Move{.face = face, .turns = turns});
    }
    return cube;
}

namespace {

using StageFunction = Result<std::vector<Move>> (*)(const Cube&);

struct BeginnerStage {
    Stage stage;
    StageFunction run;
};

constexpr auto kBeginnerChain = std::to_array<BeginnerStage>({
    {.stage = Stage::Cross, .run = &solver::solve_cross},
    {.stage = Stage::WhiteCorners, .run = &solver::solve_corners},
    {.stage = Stage::MiddleEdges, .run = &solver::solve_middle_edges},
    {.stage = Stage::Oll, .run = &solver::solve_oll},
    {.stage = Stage::Pll, .run = &solver::solve_pll},
});
static_assert(kBeginnerChain.size() == kBeginnerStages);

[[nodiscard]] std::size_t length_of(const solver::Solution& solution) {
    std::size_t moves = 0;
    for (const solver::SolveStep& step : solution.steps) {
        moves += step.moves.size();
    }
    return moves;
}

}  // namespace

BeginnerWork measure_beginner(const Cube& scrambled) {
    BeginnerWork out;
    solver::Solution solution;
    Cube cube = scrambled;
    bool complete = true;
    std::size_t index = 0;
    for (const BeginnerStage& link : kBeginnerChain) {
        solver::reset_search_work();
        auto moves = link.run(cube);
        StageWork& stage = out.stages.at(index++);
        stage.stage = link.stage;
        stage.work = solver::search_work();
        out.total.searches += stage.work.searches;
        out.total.nodes += stage.work.nodes;
        out.total.largest = std::max(out.total.largest, stage.work.largest);
        if (!moves.has_value()) {
            complete = false;
            break;
        }
        stage.moves = moves->size();
        for (const Move& move : *moves) {
            cube = cube.apply(move);
        }
        solution.steps.push_back(
            solver::SolveStep{.stage = link.stage, .moves = std::move(*moves)});
    }
    out.solved = complete && cube.is_solved();
    // solve() merges adjacent moves across stage boundaries; so does this.
    out.moves = length_of(solver::merge_adjacent_moves(std::move(solution)));
    return out;
}

MethodWork measure_method(const Cube& scrambled, solver::SolveMethod method) {
    MethodWork out;
    solver::reset_search_work();
    const auto solution = solver::solve(scrambled, method);
    out.work = solver::search_work();
    if (solution.has_value()) {
        out.moves = length_of(*solution);
        out.solved = true;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pixel buffers
// ---------------------------------------------------------------------------

namespace {

// Hands every request to OpenCV's own allocator and keeps count. The returned
// UMatData is the standard allocator's own, so it is released by it too.
class CountingAllocator final : public cv::MatAllocator {
public:
    explicit CountingAllocator(cv::MatAllocator* inner) : inner_(inner) {}

    cv::UMatData* allocate(int dims, const int* sizes, int type, void* data, size_t* step,
                           cv::AccessFlag flags, cv::UMatUsageFlags usage) const override {
        cv::UMatData* result = inner_->allocate(dims, sizes, type, data, step, flags, usage);
        // A Mat over a caller's own memory (data != nullptr) allocates none.
        if (data == nullptr && result != nullptr) {
            allocations_.fetch_add(1, std::memory_order_relaxed);
            bytes_.fetch_add(result->size, std::memory_order_relaxed);
        }
        return result;
    }

    bool allocate(cv::UMatData* data, cv::AccessFlag flags,
                  cv::UMatUsageFlags usage) const override {
        return inner_->allocate(data, flags, usage);
    }

    void deallocate(cv::UMatData* data) const override { inner_->deallocate(data); }

    [[nodiscard]] BufferWork total() const {
        return BufferWork{
            .allocations = allocations_.load(std::memory_order_relaxed),
            .bytes = bytes_.load(std::memory_order_relaxed),
        };
    }

private:
    cv::MatAllocator* inner_;
    mutable std::atomic<std::uint64_t> allocations_{0};
    mutable std::atomic<std::uint64_t> bytes_{0};
};

// Counting is one measurement at a time: the allocator is process-wide, so the
// one in use and the one it replaced are too.
struct Counting {
    std::unique_ptr<CountingAllocator> counter;
    cv::MatAllocator* previous = nullptr;
};

Counting& counting() {
    static Counting state;
    return state;
}

}  // namespace

namespace detail {

void begin_counting() {
    Counting& state = counting();
    state.previous = cv::Mat::getDefaultAllocator();
    state.counter = std::make_unique<CountingAllocator>(state.previous);
    cv::Mat::setDefaultAllocator(state.counter.get());
}

BufferWork end_counting() {
    Counting& state = counting();
    cv::Mat::setDefaultAllocator(state.previous);
    const BufferWork work = state.counter->total();
    state.counter.reset();
    state.previous = nullptr;
    return work;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

bool timing_requested() {
    const char* value = std::getenv("CUBE_PERF_TIMING");
    if (value == nullptr) {
        return false;
    }
    const std::string_view text = value;
    return !text.empty() && text != "0";
}

std::uint64_t calibration_workload(Weight weight) {
    // A zone, so that a Tracy capture of this binary (tools/perf/tracy_check.py)
    // has the calibration beside every other zone to divide by.
    CUBE_ZONE_SCOPED_N("perf.calibration");
    // A xorshift stream feeds a hash set (inserts of mostly new keys, the
    // BFS's visited-set shape) and an arena of 56-byte records appended and
    // read back (its node shape). Light is about ten milliseconds and a few
    // megabytes, which a cache holds; heavy is a tenth of a second and tens of
    // megabytes, which is a search's working set and which it does not.
    const std::size_t steps = weight == Weight::light ? 40'000 : 400'000;
    struct Record {
        std::array<std::uint8_t, 56> bytes;
    };

    std::uint64_t x = 0x9E3779B97F4A7C15ULL;
    std::uint64_t sum = 0;
    std::unordered_set<std::uint64_t> seen;
    std::vector<Record> arena;
    for (std::size_t i = 0; i < steps; ++i) {
        x ^= x << 13U;
        x ^= x >> 7U;
        x ^= x << 17U;
        const std::uint64_t key = x & 0xFFFFFFFULL;
        if (seen.insert(key).second) {
            Record record{};
            record.bytes.fill(static_cast<std::uint8_t>(key));
            arena.push_back(record);
        }
        const Record& back = arena.at((x >> 20U) % arena.size());
        sum += back.bytes.at((x >> 40U) % back.bytes.size());
    }
    return sum + seen.size() + arena.size();
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::ranges::sort(values);
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values.at(middle);
    }
    return (values.at(middle - 1) + values.at(middle)) / 2.0;
}

}  // namespace cube::perf
