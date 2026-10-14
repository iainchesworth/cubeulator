#pragma once

// Every threshold of the performance tier, in one file.
//
// How to read a number here, and how to move it, is in docs/testing.md
// ("Performance tests"). The short version: each work budget sits a quarter
// above what the corpus measures today, so that a change that really does cost
// more is a failure with a name on it, and a change that costs a little more
// (a different move order, say) is a conscious edit of a number in this file,
// not a surprise. `cube_perf_tests "[.perf-report]"` prints what each metric
// measures now beside its budget, and how much room is left.
//
// A work budget is exact on every machine and compiler, so it may be tight. A
// time budget is a ratio to the calibration workload and is deliberately
// loose: it is there for the order-of-magnitude slowdown a node count cannot
// see (a copy where there was a reference, an allocation per node), and it
// must never fail because another process was busy.
//
// Set 2026-10-14 on the corpus below, from develop at 8437841.

#include <array>
#include <cstddef>
#include <cstdint>

namespace cube::perf::budget {

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

// The solver budgets are measured on corpus_scramble(0) .. (kSolverSeeds - 1).
// It was twelve while a beginner solve was most of a second and the tier had to
// stay inside a minute; with the piece search (src/lib/src/solver/
// piece_search.hpp) the whole tier is seconds, and 28 includes the heaviest
// middle-edge and corner searches of the seeds looked at, which is what a
// ceiling has to be set on.
inline constexpr unsigned kSolverSeeds = 28;

// ---------------------------------------------------------------------------
// The solver, counted in search nodes
// ---------------------------------------------------------------------------

// One stage of a solve: the most nodes its searches visited in any one solve
// (`max`), the corpus mean, and the most any single search of the stage
// visited.
struct StageBudget {
    std::uint64_t max_nodes;
    std::uint64_t mean_nodes;
    std::uint64_t max_search;
};

// Beginner (layer by layer), the method the app starts on. Cross, corners and
// middle edges are breadth-first searches over a reduced signature; their
// ceiling in the solver is kMaxVisitedNodes (2,000,000) per search, and OLL
// and PLL are searches over whole algorithms, ceiling kMaxLastLayerStates
// (100,000). metrics.cpp holds every search budget to at most half of its
// cap, so that the cap stays a backstop and is never near.
//
// Measured (max / mean over the corpus, largest single search):
//   cross          188,657 /  70,310 / 188,657
//   white corners  347,925 / 115,409 / 194,409
//   middle edges 1,270,904 / 712,247 / 599,850   (four slots, eject + insert)
//   oll                105 /      61 /      90
//   pll                 72 /      41 /      44
inline constexpr std::array<StageBudget, 5> kBeginnerStages = {{
    /* cross         */ {240'000, 85'000, 240'000},
    /* white corners */ {440'000, 140'000, 245'000},
    /* middle edges  */ {1'600'000, 900'000, 750'000},
    /* oll           */ {130, 80, 120},
    /* pll           */ {90, 55, 60},
}};

// A whole solve, all five stages (measured 1,587,822 / 898,067), and the
// largest single search of any stage (measured 599,850).
inline constexpr std::uint64_t kBeginnerSolveMaxNodes = 2'000'000;
inline constexpr std::uint64_t kBeginnerSolveMeanNodes = 1'130'000;
inline constexpr std::uint64_t kBeginnerMaxSearch = 750'000;
// Moves in the solution the app shows, after adjacent moves are merged
// (measured 132 / 101.4).
inline constexpr std::uint64_t kBeginnerMaxMoves = 165;
inline constexpr double kBeginnerMeanMoves = 130.0;

// What one solve costs a method with no stage structure of its own to budget:
// the most nodes in a solve and its mean, the largest single search, and the
// solution's length.
struct MethodBudget {
    std::uint64_t max_nodes;
    std::uint64_t mean_nodes;
    std::uint64_t max_search;
    std::uint64_t max_moves;
    double mean_moves;
};

// Intermediate (CFOP): the cross and the four pairs are each a search from
// both ends; the last layer is a table lookup and visits no nodes at all.
// Measured 32,453 / 17,023, largest search 15,090, moves 59 / 51.4.
inline constexpr MethodBudget kIntermediate = {
    .max_nodes = 41'000,
    .mean_nodes = 21'500,
    .max_search = 19'000,
    .max_moves = 74,
    .mean_moves = 65.0,
};

// Advanced (two-phase): a node is a move tried in either phase. After the
// first solution the search keeps looking for a shorter one for up to five
// million more nodes (kImprovementNodes in two_phase.cpp), so a solve costs
// about that and is cheaper only when the search is exhausted first - which is
// why the mean sits just above five million. Measured 7,431,634 / 5,298,998
// (one search a solve), moves 22 / 19.5.
inline constexpr MethodBudget kAdvanced = {
    .max_nodes = 9'300'000,
    .mean_nodes = 7'000'000,
    .max_search = 9'300'000,
    .max_moves = 26,
    .mean_moves = 25.0,
};

// ---------------------------------------------------------------------------
// The reader, counted in cv::Mat pixel buffers
// ---------------------------------------------------------------------------

// One read_side() of a frame with the side in it, at the working width and at
// the two larger sizes a camera gives. The reader reduces a wide frame to its
// working width before it does anything else, so its buffers do not depend on
// the camera: that is the contract in direct_scan.hpp, and it is what these
// hold. Measured: 101 to 115 buffers, 1.92 MB (2.8 working-width frames) at
// every size; a frame with no cube in it 36 to 54 buffers, 1.85 MB - except
// the 1080p room, 113 buffers and 2.32 MB. That one has seven sticker-sized
// outlines in it and no side, which is what earns a second look (outlines
// taken against a narrower block, read_side in direct_scan.cpp): the
// threshold, the contours and the shape filter again, and a second lattice
// search through what they found.
inline constexpr std::uint64_t kReadBuffers = 145;
inline constexpr std::uint64_t kReadBytes = 2'400'000;
inline constexpr std::uint64_t kEmptyReadBuffers = 140;
inline constexpr std::uint64_t kEmptyReadBytes = 2'900'000;
// Bytes a read of the larger sizes may allocate, as a multiple of what the
// working-width read did: they pay for the one reduction, and nothing else.
// Measured 1.00.
inline constexpr double kReadBytesGrowth = 1.10;
// One read_corner() - two sides meeting at an edge, which the live scan reads
// on every frame beside read_side() - on the same terms. Measured: 966 to
// 1,022 buffers and 2.0 MB with the corner in view, at either size (it is about
// a thousand small buffers, which a profile of the scan should look at before
// anything is added to them), and 32 buffers, 1.85 MB for a room.
inline constexpr std::uint64_t kCornerBuffers = 1300;
inline constexpr std::uint64_t kCornerBytes = 2'520'000;
inline constexpr std::uint64_t kEmptyCornerBuffers = 45;
inline constexpr std::uint64_t kEmptyCornerBytes = 2'320'000;
// SideScan::add() works on nine Lab triples and a few vectors, not pictures:
// no pixel buffer at all (measured 0). SideScan::scanned() converts a handful
// of single colours through cv::cvtColor on the way to naming them (measured
// 12), and a Mat per sticker or per frame would be well past this.
inline constexpr std::uint64_t kScanAddBuffers = 0;
inline constexpr std::uint64_t kScannedBuffers = 16;

// ---------------------------------------------------------------------------
// Time, as a multiple of the calibration workload (perf/measure.hpp)
// ---------------------------------------------------------------------------

// Each budget is a ratio of the task's fastest run to the calibration
// workload's fastest run, measured in the same process and run alternately
// with it, so that the machine's speed and a stretch of load cancel out. The
// comment above each is what that ratio measured on a 32-core Windows desktop
// in RelWithDebInfo while other builds, and then fourteen other processes
// solving, were running on it; the budget is a good deal more than that. What is left over
// for the noise that does not cancel (frequency steps, a neighbour's cache, a
// compiler that is not the one the number was taken with) is the room between
// the two, and a copy or an allocation per node is a factor of several, not a
// few percent. These are not 10% gates and must not be tightened into being
// one.
struct TimeBudget {
    double ratio;
};

// 0.65 to 0.82 (seed 0, 0.9 million nodes, about 80 ms), against the heavy
// calibration, whose working set is a search's. The facelet search the piece
// search replaced read 8.7 to 12.6 here, so a return to it, or to anything
// like its cost per node, is over this budget with room to spare.
inline constexpr TimeBudget kBeginnerSolveTime = {6.0};
// 0.3 to 0.5 (a solve is 4 ms).
inline constexpr TimeBudget kIntermediateSolveTime = {5.0};
// 5.7 to 7.4 (a solve is 60 to 100 ms; the tables are already built).
inline constexpr TimeBudget kAdvancedSolveTime = {30.0};
// 0.14 to 0.23, 0.67 to 0.82 (the 1080p read is the reduction, one thread) and
// 0.15 to 0.17.
inline constexpr TimeBudget kReadTime360 = {2.0};
inline constexpr TimeBudget kReadTime1080 = {4.0};
inline constexpr TimeBudget kEmptyReadTime360 = {2.0};
// 0.08 to 0.10 each: a hundred reads fed to SideScan::add, and one scanned().
inline constexpr TimeBudget kScanAddTime = {1.0};
inline constexpr TimeBudget kScannedTime = {1.0};

// 0.32 to 0.38, 0.89 to 1.12 and 0.17 to 0.18.
inline constexpr TimeBudget kCornerReadTime360 = {3.0};
inline constexpr TimeBudget kCornerReadTime1080 = {8.0};
inline constexpr TimeBudget kEmptyCornerReadTime360 = {2.0};

// How many timed runs each takes, after one that is thrown away.
inline constexpr int kSolveRuns = 5;
inline constexpr int kFastRuns = 15;

}  // namespace cube::perf::budget
