#pragma once

#include <cstdint>
#include <vector>

#include <cube/error.hpp>
#include <cube/solver/cube.hpp>

namespace cube::solver {

// The stage a SolveStep belongs to. Each SolveMethod uses its own subset, in
// the order listed for it; the enumerator values are identifiers, not a
// solving order, and the original five keep the values they have always had
// (the solve screen indexes its stage copy by them).
//
//   Beginner      Cross, WhiteCorners, MiddleEdges, Oll, Pll
//   Intermediate  Cross, F2l, Oll, Pll
//   Advanced      Reduce, Finish
enum class Stage : std::uint8_t {
    Cross,
    WhiteCorners,
    MiddleEdges,
    Oll,
    Pll,
    // Intermediate: the first two layers' four corner-edge pairs, built and
    // slotted in together.
    F2l,
    // Advanced: the two phases of the two-phase search. Reduce brings the
    // cube into a state that needs only half turns of four sides to finish;
    // Finish does that.
    Reduce,
    Finish,
};

// One stage's worth of moves - kept separate per stage (rather than one
// flat move list) so a caller can label each step for teaching purposes
// (see the solve-visualization plan: the UI shows which stage the user is
// currently on, not just a bare move counter).
struct SolveStep {
    Stage stage = Stage::Cross;
    std::vector<Move> moves;
};

struct Solution {
    std::vector<SolveStep> steps;
};

// Merges every run of adjacent moves on the same face into one move, across
// stage boundaries as well as within a stage: turn counts add mod 4, and a
// sum of 0 removes the pair outright - which can bring the moves either side
// of it together, so this repeats until no two adjacent moves share a face
// (e.g. "F2 | F2" at a stage boundary vanishes, "U | U" reads "U2"). Linear
// in the move count.
//
// The steps keep their stages and their order, and a step may end up with no
// moves at all. A move merged across a stage boundary stays with the earlier
// stage (the one it began in), so it belongs to exactly one step. The net
// effect of the whole sequence on a cube is unchanged, but a merge that
// straddles a boundary changes the cube *at* that boundary: only the end of
// the solution is guaranteed, not that each stage's work is complete where its
// moves end. solve() applies this to everything it returns; it's public so a
// Solution from elsewhere can be tidied the same way.
[[nodiscard]] Solution merge_adjacent_moves(Solution solution);

// How a cube is solved: three tiers, from the one the app teaches to the
// one that is simply as short as it can be.
enum class SolveMethod : std::uint8_t {
    // Layer by layer: cross, white corners, middle edges, 2-look OLL, 2-look
    // PLL. The structure the UI teaches, not a short solve: about 100 moves.
    Beginner,
    // CFOP, the speedcuber's method: cross, first-two-layers pairs, then the
    // last layer's orientation and permutation each in one sequence. About
    // 55 moves, in a shape a learner can go on to practise.
    Intermediate,
    // Kociemba's two-phase search: about 20 moves, with no structure a
    // person could learn from - it is the shortest, not the clearest.
    Advanced,
};

// Solves `scrambled` by `method`. Every method returns moves with no two
// adjacent moves on the same face, even across a stage boundary (see
// merge_adjacent_moves), and every one completes in a bounded, small amount
// of work on every input.
//
// Fails with CubeError::invalid_cube_state if `scrambled` isn't physically
// valid (guards against a malformed read, e.g. from a vision pipeline). For
// Intermediate and Advanced, a cube that is valid but unreachable by turning
// the sides (a twisted corner, a flipped edge, a swapped pair, centres out of
// order) fails with CubeError::unsolvable_state; Beginner reports
// CubeError::no_solution for such a cube, as it always has, but also checks
// up front, so it says so at once rather than after its searches give up. Any
// other failure is a solver bug, not a legitimate dead end.
[[nodiscard]] Result<Solution> solve(const Cube& scrambled,
                                     SolveMethod method = SolveMethod::Beginner);

// Does any one-off setup `method` needs now rather than on its first solve,
// so a person is not kept waiting for it: Advanced builds about 6 MB of
// lookup tables (a fraction of a second). Safe to call from any thread, any
// number of times; the other methods have nothing to prepare.
void prepare(SolveMethod method);

// A pseudo-random scramble, deterministic for a given seed - used both by
// tests and (until a real vision pipeline exists) as the app's stand-in
// scramble source.
[[nodiscard]] Cube random_scramble(unsigned seed, int move_count = 25) noexcept;

}  // namespace cube::solver
