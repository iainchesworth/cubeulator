# Solving a cube of any size

This is the design of `cube::nxn`, the library that solves a cube of any size
from 2x2x2 up (tested to 9x9x9) and reuses the 3x3 solvers for the last
stage. It is a spike: a library with tests and a benchmark, with no scanning,
no UI and no notation for wide moves yet. Everything below is checked by
`tests/unit/nxn`; the numbers are from `cube_nxn_bench`.

## The idea in one paragraph

A turn of any layer of any cube is one rule: rotate every cubie in that layer,
taking its stickers with it. That rule is applied once per size to build a
permutation table per layer turn (`Geometry`), so a turn costs a table lookup
at every size. A big cube is solved by *reduction*: put the centres right,
pair the edges, and what is left is a 3x3 that the existing solvers finish.
Centres and edges are put right with 3-cycles that are **found by search**,
not written down: short commutators are enumerated, each folded into the one
rearrangement it makes, and the ones that move three pieces and nothing else
are kept (`CycleLibrary`). There are no per-size algorithm tables, and no
parity algorithms.

## Layers

| Layer | Header | What it is |
|---|---|---|
| Permutations | `permutation.hpp` | Gather-convention permutations of facelets: `then`, `inverse`, `support`, `sign` |
| Geometry | `geometry.hpp` | Per size: facelet positions, the table of every layer turn, the 24 whole-cube rotations, sticker orbits, piece roles |
| State | `cube.hpp` | `NCube`: 6 N^2 colours, turned by table; reduced check; `to_cube3()` |
| Concepts and views | `state.hpp` | `FaceletState`, range algorithms, the zero-copy virtual 3x3 |
| Commutator engine | `commutator.hpp` | `CycleLibrary` of pure 3-cycles; `arrange()` sorts by them |
| Strategy | `strategy.hpp` | Stages as values; `default_strategy(size)`; target policies; parity handler |
| Result | `solution.hpp` | `Step`, `Solution`, merging of turns |

## The model

`NCube` is one flat array of `6 * N * N` colours, numbered
`face * N^2 + row * N + col` (Face's enumerator order, row-major), which for
N = 3 is `Cube`'s own numbering. `view()` reads it as six N x N matrices,
`view()[face, row, col]`.

A layer turn is `SliceTurn{axis, layer, turns}`: which axis, how far along it
(0 at the negative end), how many clockwise quarter turns seen from the
positive end. The same layer reached from the other end is the same turn, so
there is one spelling. `LayerMove{face, depth, turns}` is the notation form
(`R`, `R'`, `2R` is `{R, 1, 1}`), and a 3x3 move is just a `LayerMove` with
depth 0 - which is why every 3x3 algorithm runs unchanged on a cube of any
size (`run_algorithm`, `FaceTurnable`).

Coordinates are used only to build the tables, in doubled and centred
integers so even sizes need no half units. They are never used to solve.

### What anchors it

The geometry is derived, not copied from `cube.cpp`, so it is checked against
it: for N = 3 every outer face turn of every count, on scrambled cubes, is
exactly `Cube::apply`. For every size from 2 to 9: every turn has order 4;
turns of different layers of one axis commute; a layer is the same layer from
either end; turning every layer of an axis is a rotation of the whole cube;
and a turn spelled for a rotated cube is the original turn carried through
the rotation.

### Pieces

Sticker positions fall into orbits under the 24 rotations of the whole cube,
and a layer turn only moves a sticker inside its orbit. That is all a
reduction needs to know about "kinds" of piece:

| Size | Centre orbits | Wing orbits | Middle edges |
|---|---|---|---|
| 4 | 1 | 1 | no |
| 5 | 2 | 1 | yes |
| 6 | 4 | 2 | no |
| 7 | 6 | 2 | yes |

Centre orbits are chiral (rotations only), which is why the count grows with
`(N-2)^2 / 4`. A wing has a *primary* sticker (the lower-numbered orbit) and a
*secondary*, so a wing can only sit in a slot one way round; what looks like a
flipped wing is a different wing in that slot. A wing's identity is therefore
the ordered colour pair, which is distinct for all 24 wings of an orbit.

## The commutator engine

A commutator `c z c' z'` where `c` is one turn or a conjugate `x y x'` of one
moves only the pieces where the two overlap. `CycleLibrary` enumerates them
all over every layer turn of the size, folds each into its facelet
permutation in a single pass (stopping once more than six facelets are seen to
move), and keeps those whose moved set is exactly three centre stickers of
one orbit, or exactly the six stickers of three wings of one orbit - a *pure*
3-cycle.

Measured, for every orbit of every size from 4 to 9:

* 3952 of the 4048 possible 3-cycles of 24 pieces are found directly, with
  words of 8 to 10 moves, once each is also conjugated by every single turn
  (do a turn, the cycle, the turn back, which aims it at three other pieces);
* the remaining 96 are each one more conjugation away, found by a lookup per
  turn rather than by conjugating everything again. The library is then
  **complete**: all 4048, for centres and for wings, words of 8 to 22 moves,
  mean 9.
* Every ordered (from, to) pair of cells is reachable directly.

This is the "procedural A and B from the layer depth" idea, with the search
doing the work of choosing them: nothing in it knows what a 4x4 or a 7x7 is.

`arrange()` sorts the cells of one orbit to a target by repeatedly applying
the cycle that fixes the most cells per move. With a complete library and an
even permutation it cannot stall: some cycle always fixes at least one cell
and breaks none (a wrong cell, its unique source, and any third wrong cell; or
the one cycle that finishes a last 3-cycle).

## Parity

A 3-cycle is an even permutation, so it cannot make an odd permutation of the
wings of an orbit. That is the entire content of "parity" here. The cure is
not an algorithm: a single quarter turn of an inner layer moves four wings of
one orbit, which is an odd permutation of that orbit, and after it the centres
and wings are arranged by 3-cycles as before. `make_wings_even` does this,
before the centres are solved, for each wing orbit that needs it.

The permutation's sign is computed from the labels, so nothing is "detected":
the stage knows how many odd turns it needs. Sizes 5 and 7 use it too - there
is no parity *problem* there, just the same arithmetic, and it comes out the
same way as for the even sizes. The one real difference between the two is
what the targets are (below).

What does depend on the corners is whether the reduced cube is a *solvable*
3x3. On an even size nothing fixes which edges end up where, so a target is
chosen: home, or - when the corners are an odd permutation away - two edges
trade places, in whichever of four orientations makes `Cube::is_solvable()`
true. That is the whole of "PLL parity". An odd size's middle edges fix the
targets, and the cube is solvable or it is not.

## Strategy

```
size 2       corners                                  one search (bidirectional)
size 3       core                                     no reduction
even >= 4    plan, parity, centres, edges, core       FreeCentres
odd  >= 5    plan, parity, centres, edges, core       FixedCentres
```

A `Strategy` is an ordered list of named stages; a stage is any copyable
callable on `Context` that returns `Result<void>` (`StageLike`). Selecting by
size is `default_strategy(size)`, so skipping "to the right stage" is the shape
of the list and not a branch in a solver. A 3x3's strategy is the single stage
`core`, and it produces exactly the 3x3 solver's solution (tested for all
three methods).

`TargetPolicy` (a concept) is where odd and even differ:

* `FixedCentres` (odd): the face colours come from the middle cubies of the
  faces and each edge's display from its middle cubie; the centres and wings
  are solved to match, and nothing is free.
* `FreeCentres` (even): the face colours are read from the corners (opposite
  colours are those that never share a corner; the down-back-left corner fixes
  which face gets which), and the edge displays are home or a trade.

`ParityHandler` is the injection point for the parity turn. The default
(`inner_layer_parity`) searches for the first inner layer, perpendicular to a
chosen axis, that moves exactly the wings of the orbit. A caller can supply
another rule, and the solve still solves the cube (tested with three), or
supply one that fixes nothing, in which case the sorter cannot reach the target
and the solve fails rather than returning moves that do not work.

The core method is the caller's: `solve(cube, SolveMethod::Beginner)` reduces
the same way and finishes with the beginner layer-by-layer, `Intermediate`
with CFOP, `Advanced` with the two-phase search. The reduction does not depend
on it.

## One state concept, a virtual 3x3

`FaceletState` says what a puzzle state is: an edge length (`puzzle_size`) and
a colour at every (face, row, col). `solver::Cube` and `NCube` both are, and
the range algorithms in `state.hpp` (`facelets_of`, `has_uniform_faces`,
`colour_counts`) work on either.

`virtual_3x3(cube)` is a view that maps the 6 x 3 x 3 index space of a 3x3
onto the corners, edge cubies and face centres of the N x N storage: row or
column 0 and 2 are the first and last, 1 is the middle one. It copies
nothing, and the mapping is the only place the size appears.

Both views are `std::mdspan` in everything but name (the second is an mdspan
with a custom layout). They are written out as two small classes with the
same `[face, row, col]` subscript and `extent()` because libstdc++ 15, which is
the project's g++, has no `<mdspan>` (libstdc++ 16 has); replacing them with
the standard type later changes nothing a caller writes. `NCube::to_cube3()` reads through it (renaming colours to the
canonical scheme) to build the `Cube` the 3x3 solvers take. For a reduced
cube this *is* the cube's 3x3; before reduction it is a sample of stickers.

## Results

Seeded scrambles of 60 + 10 N random layer turns, Advanced core, MSVC
RelWithDebInfo on the development machine (20 scrambles per size):

| N | Centres | Wings | Parity | Core | Total (mean) | Max |
|---|---|---|---|---|---|---|
| 2 | - | - | - | 8.3 | 8.3 | 10 |
| 3 | - | - | - | 20.1 | 20.1 | 22 |
| 4 | 61.5 | 84.7 | 0.3 | 17.2 | 164 | 182 |
| 5 | 119.5 | 86.2 | 0.4 | 20.4 | 226 | 243 |
| 6 | 240.9 | 171.6 | 1.3 | 17.4 | 431 | 465 |
| 7 | 364.9 | 169.9 | 0.9 | 20.6 | 556 | 586 |
| 8 | 551.6 | 259.6 | 1.1 | 17.1 | 830 | 886 |
| 9 | 736.3 | 257.2 | 1.4 | 20.2 | 1015 | 1048 |

Every solve is verified by replaying its moves on the scramble. The 3x3 core
alone, over 1000 scrambles: beginner 99 mean (59-142), intermediate 52
(36-61), advanced 20.3 (15-23).

The cycle library is built once per size, on first use: about 30 ms at 4x4,
230 ms at 7x7 and 640 ms at 9x9 on the development machine; a solve after
that is a few tens of milliseconds. Tables are per size and per process.

## What this is not, yet

* **Short.** A solution is a few hundred moves at 5x5. The only thing done to
  shorten them is that, of the cycles that fix the most cells per move, the
  one whose setup moves merge with what was just emitted is preferred (about 5%
  fewer moves than without). The larger gains are structural and each is a
  replacement for one stage: building a face's centres with block moves before
  falling back to 3-cycles, and pairing edges with the insert-and-undo method.
* **A mobile cost budget.** The library build is the one-off cost; it has not
  been measured on a phone, and it grows faster than N (N = 9 is over half a
  second here). Symmetry would cut it (build one cycle per rotation class and
  rotate), or it could be built for the sizes asked for only, which it is.
* **Scanning, wide-move notation, a UI.** None exist for sizes above 3.
* **Platforms other than the one it was built on** - see the testing note
  in the pull request.
