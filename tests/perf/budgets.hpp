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
// The N x N solver, counted in moves and cycles
// ---------------------------------------------------------------------------

// A cube of any size from 2 is solved by reduction (docs/nxn-solver.md), and
// what that costs is its length: every move of a solution is a table lookup, so
// the moves made are the work done, and the cycle library a size builds on first
// use (the 3-cycles its searches choose from) is what that setup costs and
// holds. Both are counted, not timed, so they are exact on every machine and
// held a quarter above what they measure. The wall time of the same work, on a
// desktop, in WebAssembly and on a phone, is docs/performance.md, "Cubes of any
// size".
//
// The corpus is nxn_scramble(size, 1) .. nxn_scramble(size, kNxnSeeds): 60 + 10
// N random layer turns. Every size the library solves, the 2x2 through the 9x9.
inline constexpr unsigned kNxnSeeds = 6;

struct SizeBudget {
    int size;
    // The most moves in one solve, and the corpus mean.
    std::uint64_t max_moves;
    double mean_moves;
    // The 3-cycles the size's library holds (0 below 4x4, which needs none).
    std::uint64_t cycles;
};
// Measured (worst solve / mean over the six): 2x2 9 / 8.5, 3x3 21 / 19.7, 4x4
// 145 / 129.8, 5x5 187 / 172.0, 6x6 347 / 335.3, 7x7 434 / 412.8, 8x8 643 / 616.5,
// 9x9 759 / 737.0. A library holds 4,048 cycles for each orbit of centres and of
// wings the size has - every 3-cycle of the 24 pieces of an orbit - so that is
// a structural number, held exactly: 4x4 two orbits (one of each), 5x5 three,
// 6x6 six, 7x7 eight, 8x8 twelve, 9x9 fifteen.
inline constexpr std::array<SizeBudget, 8> kNxnSizes = {{
    {.size = 2, .max_moves = 12, .mean_moves = 11.0, .cycles = 0},
    {.size = 3, .max_moves = 27, .mean_moves = 25.0, .cycles = 0},
    {.size = 4, .max_moves = 182, .mean_moves = 163.0, .cycles = 8'096},
    {.size = 5, .max_moves = 235, .mean_moves = 215.0, .cycles = 12'144},
    {.size = 6, .max_moves = 435, .mean_moves = 420.0, .cycles = 24'288},
    {.size = 7, .max_moves = 545, .mean_moves = 516.0, .cycles = 32'384},
    {.size = 8, .max_moves = 805, .mean_moves = 771.0, .cycles = 48'576},
    {.size = 9, .max_moves = 950, .mean_moves = 922.0, .cycles = 60'720},
}};

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
// The placement through the fit that read_side makes of the blobs when no side
// is held square (a lattice placed through the one the blobs already on a
// square grid make, fit_lattice_through_fit in direct_scan.cpp), on the same
// terms: a side just past the gate that it finds nothing in (nine blobs for it
// to weigh, so the frame also goes to the second look, where it is made again),
// and a side at the edge of the gate that it finds. It allocates a matrix or
// two for each distinct lattice it weighs. Measured: 205 buffers, 2.32 MB for
// the side past the gate (the two looks without it, 175), and 119, 1.97 MB for
// the side it finds (113, 1.99 MB for a side held square; 177 without it,
// where the second look reads nothing).
inline constexpr std::uint64_t kThroughFitMissBuffers = 260;
inline constexpr std::uint64_t kThroughFitMissBytes = 2'900'000;
inline constexpr std::uint64_t kThroughFitReadBuffers = 150;
inline constexpr std::uint64_t kThroughFitReadBytes = 2'460'000;
// SideScan::add() works on nine Lab triples and a few vectors, not pictures:
// no pixel buffer at all (measured 0). SideScan::scanned() converts a handful
// of single colours through cv::cvtColor on the way to naming them (measured
// 12), and a Mat per sticker or per frame would be well past this.
inline constexpr std::uint64_t kScanAddBuffers = 0;
inline constexpr std::uint64_t kScannedBuffers = 16;

// ---------------------------------------------------------------------------
// The scan's judgement, counted in assemblies
// ---------------------------------------------------------------------------

// Putting six named sides together into a cube tries every quarter-turn of each
// (4^6 = 4,096 combinations: cube::solver::assemble_*), and where no cube can
// be made it tries each of fifteen pairs of centres whose names might be traded
// as well: ten to twenty-five milliseconds in WebAssembly, on the page's main
// thread, and the scan is judged again whenever a read of a held side moves a
// fused colour, which is nearly every frame. A scan whose sides make no cube
// does not put them together again while what the attempt rested on is as it
// was (SideScan::assemblies(), kRejudgeLabDistance); what is counted here is how
// many times it did, over a scan of drawn sides judged after every read and a
// hundred times more once finished, so that it is exact on every machine and
// does not move with the compiler.
//
// The drawn sides hold one naming and one set of colours from the first
// judgement to the last: one assembly for each. A scan whose sides make a cube
// is over at the first judgement that says so (one), and the budgets allow a
// second assembly for the room a platform's rounding might need - not one for
// every judgement, which is 105.
inline constexpr std::uint64_t kSettledAssemblies = 1;
inline constexpr std::uint64_t kSeveralWaysAssemblies = 2;
inline constexpr std::uint64_t kNoCubeAssemblies = 2;
// And what those assemblies cost, in the search's own nodes (each look at the
// faces turned so far; search_work.hpp). The search turns the faces from the
// sixth to the first and gives a combination up as soon as an edge or a corner
// is impossible, and a face that looks the same turned is tried once. Trying all
// 4,096 combinations is 5,460 nodes an assembly (4 + 16 + ... + 4,096), and
// sides that make no cube cost 482 of them on average, over the sixteen
// searches of the one assembly (measured 388, 776 and 7,720).
inline constexpr std::uint64_t kSettledNodes = 490;
inline constexpr std::uint64_t kSeveralWaysNodes = 970;
inline constexpr std::uint64_t kNoCubeNodes = 9'700;

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

// An N x N solve, with the size's one-off setup (cycles, centre search) already
// built, against the light calibration: 10.4 (5x5, a solve of about 45 ms), 15.6
// (7x7, about 65 ms) and 39.8 (9x9, about 170 ms). The setup is not timed: it can
// be made once per process, and docs/performance.md has it.
inline constexpr TimeBudget kNxnSolveTime5 = {40.0};
inline constexpr TimeBudget kNxnSolveTime7 = {60.0};
inline constexpr TimeBudget kNxnSolveTime9 = {150.0};

// How many timed runs each takes, after one that is thrown away.
inline constexpr int kSolveRuns = 5;
inline constexpr int kFastRuns = 15;

}  // namespace cube::perf::budget
