# Solve methods

`cube::solver::solve(cube, method)` has three methods. They differ in how
many moves a person has to make and in how much of the solution a person
could learn from, and they are deliberately three separate engines rather
than one engine with a setting.

| method | what it is | moves (mean) | longest seen | time (desktop) |
|---|---|---|---|---|
| `Beginner` | Layer by layer: cross, white corners, middle edges, 2-look OLL, 2-look PLL | 100.7 | 135 | 1.3 s |
| `Intermediate` | CFOP: cross, first-two-layers pairs, one-sequence OLL, one-sequence PLL | 52.1 | 62 | 3 ms |
| `Advanced` | Kociemba's two-phase search | 20.3 | 22 | 37 ms |

Measured on the same random scrambles (100-move scrambles from
`random_scramble`, 300 to 1500 cubes per row), a clang `-O2` desktop build.
These have not been measured on a phone; expect a small multiple of the
desktop figure. `Beginner` needed a table-driven `Cube::apply` to stay
interactive, the other two were written to be cheap from the start.

`Beginner` is unchanged by the other two and remains the default.

## Where `Beginner`'s hundred moves go

| stage | mean moves |
|---|---|
| cross | 5.7 |
| corners | 16.8 |
| middle edges | 28.0 |
| OLL | 21.2 |
| PLL | 28.9 |

Each stage is already the shortest it can be *given* the stages before it, so
no tuning of its search removes moves; changing the last layer's search to
minimise moves instead of algorithm count saved 0.8 of them. The cost is in
the shape of the method: pieces are solved one at a time, and the last layer
is composed from a single orientation of each of Sune, A-perm and U-perm.
Getting fewer moves means a different method, which is what the other two are.

## `Intermediate`: CFOP

Four stages (`Stage::Cross`, `F2l`, `Oll`, `Pll`):

- **Cross** is the optimum (never more than 8 moves), found by searching from
  both ends at once over only the four cross edges.
- **F2L** builds the four corner-edge pairs one at a time, always taking
  whichever remaining pair is cheapest. Each pair is the shortest sequence
  that finishes it and leaves the cross and the pairs already built exactly
  where they were, turning only the top, the pair's own slot, and the slot a
  piece of the pair is stuck in. That is the "get it out, then put it in" a
  person does, and it averages 23 moves (a person is nearer 30). The same
  both-ends search runs over only the pieces that matter, so a pair takes
  about a millisecond.
- **OLL** and **PLL** are each one table lookup. The tables
  (`src/lib/src/solver/cfop_case_words.inc`) hold, for all 216 orientation
  cases and 288 permutation cases, the shortest sequence over `<U, R, F>`:
  10.1 moves to orient and 13.4 to permute on average, longest 13 and 16.
  `<U, R, F>` is the set people learn last-layer algorithms in, so every
  sequence is one a learner can practise.

The tables are generated, not written by hand, by `tools/cfop_tables`
(`cube_generate_cfop_tables`, about 10 seconds):

```
cube_generate_cfop_tables > src/lib/src/solver/cfop_case_words.inc
```

It meets in the middle: any last-layer algorithm of up to 16 moves is two
halves of at most 8, and the second half completes the first exactly when it
puts the first two layers' pieces where the first half's inverse would. Every
word of up to 8 moves is filed by that, and each is paired with the files
that complete it. Re-run it only if the alphabet or half-length changes;
`tests/unit/solver/cfop_test.cpp` solves all 62,208 last-layer states from the
checked-in file, so a stale or wrong table fails there.

## `Advanced`: two-phase

Two stages (`Stage::Reduce`, `Stage::Finish`):

- **Reduce** turns the cube, with any move, into one that
  `<U, D, R2, L2, F2, B2>` can finish: every corner untwisted, every edge
  flipped the right way round, the four middle-layer edges somewhere in the
  middle layer.
- **Finish** does that, using only those ten moves.

Each phase is an iterative-deepening search over small integer coordinates
(corner twist, edge flip, middle-edge positions; then corner, U/D-edge and
middle-edge permutations), pruned by four tables holding the exact distance
to the goal in a pair of coordinates. About 6 MB of tables are built on first
use in roughly 150 ms; `prepare(SolveMethod::Advanced)` does that ahead of
time, from any thread, so the solve screen can build them while the person is
still scanning.

The first solution takes about 160 thousand nodes at the median and never
more than 5.5 million in 8,000 cubes, and the search then spends a fixed
budget of 5 million more looking for a shorter one. The budget is counted in nodes, not
seconds, so a given cube gets the same solution on every device and the cost
is a known quantity. A 60 million node backstop turns a bug into
`CubeError::no_solution` rather than a hang.

A boundary between the two phases is a real checkpoint (the cube is in the
group the second phase works in), but unlike a CFOP stage it is not something
a person can see: the person is following arrows, not building a structure.

## Why not Korf's optimal solver, or Thistlethwaite's

Korf's IDA* with pattern databases finds the provably shortest solution
(about 18 moves). It needs roughly 80 MB of tables and takes minutes to hours
per cube, which is not a thing a phone does in seconds; and the shortest
solution is only one or two moves better than the two-phase one. Thistlethwaite's
four-group method is the same idea as two-phase with a coarser reduction, and
gives 30 to 40 moves; two-phase is strictly better for the same machinery.

## Invariants the tests hold

- Every method's solution solves the cube, has no two adjacent moves on one
  face (even across a stage boundary), and is reproducible: the same cube
  always gets the same moves.
- `Intermediate` and `Advanced` check reachability up front
  (`Cube::is_solvable`) and report a valid-but-unreachable cube as
  `CubeError::unsolvable_state`.
- The cubie model (`src/lib/src/solver/cubie.hpp`) is derived from
  `Cube::apply` and decoded through `Cube`'s own `locate_corner`/`locate_edge`,
  so it cannot drift from the facelet geometry; `cubie_test.cpp` proves the
  two agree on random cubes for all 18 moves.
- The stage boundaries are exact for `Intermediate` before merging (cross
  solved, first two layers solved, top oriented, solved), and the end of
  `Advanced`'s `Reduce` is exactly in the second phase's group.
