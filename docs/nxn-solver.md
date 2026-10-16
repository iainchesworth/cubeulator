# Solving a cube of any size

This is the design of `cube::nxn`, the library that solves a cube of any size
from 2x2x2 up (tested to 9x9x9) and reuses the 3x3 solvers for the last
stage. It is a library with tests and a benchmark, with no scanning and no UI
for sizes above 3 yet. Everything below is checked by
`tests/unit/nxn`; the numbers are from `cube_nxn_bench`.

## The idea in one paragraph

A turn of any layer of any cube is one rule: rotate every cubie in that layer,
taking its stickers with it. That rule is applied once per size to build a
permutation table per layer turn (`Geometry`), so a turn costs a table lookup
at every size. A big cube is solved by *reduction*: put the centres right,
pair the edges, and what is left is a 3x3 that the existing solvers finish.
Wings are put right with 3-cycles that are **found by search**, not written
down: short commutators are enumerated, each folded into the one rearrangement
it makes, and the ones that move three pieces and nothing else are kept
(`CycleLibrary`). Centres are built by a greedy search over a wider set of short
words that may disturb the wings and corners - which are not solved yet - and
scored by how many centre stickers they put right per move. There are no
per-size algorithm tables, and no parity algorithms.

## Layers

| Layer | Header | What it is |
|---|---|---|
| Permutations | `permutation.hpp` | Gather-convention permutations of facelets: `then`, `inverse`, `support`, `sign` |
| Geometry | `geometry.hpp` | Per size: facelet positions, the table of every layer turn, the 24 whole-cube rotations, sticker orbits, piece roles |
| State | `cube.hpp` | `NCube`: 6 N^2 colours, turned by table; reduced check; `to_cube3()` |
| Concepts and views | `state.hpp` | `FaceletState`, range algorithms, the zero-copy virtual 3x3 |
| Commutator engine | `commutator.hpp` | `CycleLibrary` of pure 3-cycles; `arrange()` sorts by them (the edges stage) |
| Centre search | `src/nxn/centres.cpp` | Greedy search over short words for the centres stage |
| Strategy | `strategy.hpp` | Stages as values; `default_strategy(size)`; target policies; parity handler |
| Notation | `notation.hpp` | WCA names for a layer turn: print, parse, and gathering turns into wide moves for display |
| Stepping | `stepped_plan.hpp`, `stage.hpp` | `SteppedPlan`: a solution to follow one move at a time, with the cube at every position; `StageId`: the stage a person is told about |
| Result | `solution.hpp` | `Step`, `Solution`, merging of turns |
| Validity | `validate.hpp` | `validate()`: is this a cube a person could hold, and if not, why |

## The model

`NCube` is one flat array of `6 * N * N` colours, numbered
`face * N^2 + row * N + col` (Face's enumerator order, row-major), which for
N = 3 is `Cube`'s own numbering. `view()` reads it as six N x N matrices with
`std::mdspan`, `view()[face, row, col]`.

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
the cycle that fixes the most cells per move, preferring (among equals) the one
whose setup merges with what was just emitted. With a complete library and an
even permutation it cannot stall: some cycle always fixes at least one cell
and breaks none (a wrong cell, its unique source, and any third wrong cell; or
the one cycle that finishes a last 3-cycle). It is what builds the wings, which
have to land in exact slots and must leave everything else alone.

## Centres

Centres have more freedom than wings: while they are built, the wings and
corners are unsolved, so a word may do whatever it likes to them as long as
two things hold. It must not move a face's middle cubie (an odd size reads its
colour scheme from those). And it must be even on every wing orbit, so that the
parity decision made before the centres are built is still true afterwards.
Outer turns and commutators of two turns are always even on every orbit (the
sign is a homomorphism, and a commutator is `s(x) s(z) s(x) s(z) = 1`), and a
conjugate of an even word is even, so the candidates are:

* every outer-layer turn;
* every commutator `x z x' z'` of two turns of different axes (inner layers
  included), 4 moves, each also done after one setup turn and undone after;
* the pure 3-cycles of the commutator engine, which guarantee progress.

Each step picks a few wrong stickers, and for each looks only at candidates
that put the right colour on it (an index from "sticker that changes" to
"actions that change it", with the action's effect kept only for centre
stickers), and takes the one that puts the most stickers right per move.
Because the pure cycles are among the candidates, some candidate always gains;
the search cannot stall.

What this costs the stages after it: outer turns move corners, which can change
whether an edge target is a solvable 3x3, so the edges' displays are planned
*after* the centres (`plan_edges`), not before. The parity decision does not
need them: every solvable target has the same sign.

Measured against 3-cycles alone, this is 52% fewer centre moves at 4x4 (61 to
29), 45% fewer at 5x5 and 37% fewer at 9x9. A wider family (six- and eight-move
commutators) and conjugation by two turns were tried and are slightly worse and
much slower: the greedy score is per move, and the bigger words mislead it.

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

## Is this a cube?

`validate(size, facelets)` (`validate.hpp`) answers the question the 3x3 answers
with `Cube::is_solvable()`, for a cube of any size and from the stickers alone:
could turns of layers have produced this? Hand entry, a scan's assembly and a
history entry read back from disk all hold a facelet array and need to know, and
if the answer is no, what to tell the person (`Verdict::fault`, with the kind of
piece and one of its stickers to point at).

The rules are the solver's, said once:

* **The scheme is the cube's own.** An odd size names its six faces by the
  middle stickers (six different colours); an even size by its corners
  (`scheme_of`, the same function the solver's `FreeCentres` policy uses). Every
  colour is renamed to the canonical scheme before the pieces are read, so a
  cube coloured or held any way is judged as the cube it is. A cube cannot be
  told apart from its mirror image by colours alone, and need not be: a cube
  with blue and green swapped is a real cube in another scheme.
* **Corners**: each of the eight real corners once, and the twists add up to a
  multiple of three. A corner is read clockwise from the sticker on the up-down
  axis, so a corner that is the *mirror image* of a real one is not a piece.
* **Wings**: each ring's 24 once. A wing is the ordered pair of colours on its
  primary and secondary stickers; there is nothing else to a wing, so there is
  nothing to add up.
* **Middle edges** (odd sizes): each of the twelve once, and the flips add up to
  an even number, where flipped means the colour that ranks higher on the
  up-down, front-back, left-right axes is not on the sticker that ranks higher.
* **Centres**: every ring of centre stickers holds each colour equally often
  (four times, for the rings of 24). Centres have no identity, so nothing else is
  asked of them.
* **Parity**, on an odd size only: the corners and the middle edges are permuted
  with the same parity, the 3x3 rule. An even size has no middle edge to hold
  the corners to, a quarter turn of an inner layer is odd on one wing ring alone,
  and a centre ring's parity cannot be seen (two of the same colour can swap), so
  no parity is asked at all.

That is the whole of "reachable": any cube satisfying it is solved by the
solvers above, and any cube they cannot solve fails it. The tests hold the two
equal rather than argue it:

* over scrambles of every size 2 to 9 (and every single turn of every layer),
  every cube is valid, in any colouring and orientation;
* over cubes damaged in ten ways by hand (stickers swapped anywhere or within a
  ring, corners twisted or swapped, an edge flipped, wings and middle edges
  traded, centres moved within and across rings), the verdict equals "the solver
  solved it", at every size, with both answers common;
* at size 2 the oracle is also held to an enumeration that shares nothing with
  the solver: a breadth-first search over facelets reaches all 3,674,160
  positions (fixing a corner), every one is accepted, and 60,000 damaged,
  shuffled, relabelled and rotated cubes are accepted exactly when they are in
  that set under some naming of the colours;
* at size 3 it agrees with `Cube::is_solvable()` on every cube whose pieces are
  real.

**It is stricter than `Cube::is_solvable()`** in the one way a 3x3 can be: a
corner that is the mirror image of a real one (two of its stickers swapped)
passes `is_physically_valid()` and `is_solvable()`, which do not look at the
chirality of a corner, and the solver then refuses it. About 0.5% of the damaged
3x3s in the test are of this kind. The scan assembly leans on `is_solvable()`
today; moving it to the oracle (phase P2 of [nxn-app-plan.md](nxn-app-plan.md))
can only remove false accepts.

It reads each sticker a constant number of times (no allocation after the size's
tables are built, which `Geometry::of(size)` does once), so it is cheap enough
to ask of the 4096 candidates of a scan's assembly.

A cube written down as text, in a recording's manifest or the diagnostics
channel, is a **state string**: `6 * size^2` letters of `U R F D L B`, side by
side in that order and row by row, each naming the side whose centre colour the
sticker shows ([recording-format.md](recording-format.md#manifestjson)). The
manifest checks one with `validate` (a 3x3's still with the solver's own
`is_solvable()`), and an odd size's middle stickers are fixed by position like a
3x3's centres, so the string says which way the cube is held.

## Strategy

```
size 2       corners                                  one search (bidirectional)
size 3       core                                     no reduction
even >= 4    plan, parity, centres, plan_edges,       FreeCentres
             edges, core
odd  >= 5    plan, parity, centres, plan_edges,       FixedCentres
             edges, core
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

A policy answers three questions: the faces' colours, a *reference* for what
an edge shows when everything is home (what the parity decision needs), and the
edge *displays* the wings are finally built to - the one that makes the reduced
cube a solvable 3x3, asked after the centres.

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

## Notation

`notation.hpp` writes and reads the names a person uses for a turn of layers, at
any size:

| Text | Means |
|---|---|
| `R` `R'` `R2` | the outer layer of R, clockwise, counter-clockwise, a half turn (`R2'` reads as `R2`) |
| `Rw` `3Rw` | the outer two (three) layers of R together; `r` is the older spelling of `Rw` |
| `2R` `4L` | the second layer from R only; the fourth from L (counted from the face named) |
| `2-3R` | layers 2 to 3 from R together |
| `x` `y` `z` | the whole cube turned as R, U, F turn |

`LayerMove` is one layer; a name can be several turned together, so notation is
a `WideMove` (`face`, `first`, `last`, `turns`: a block of adjacent layers),
and `layers_of()` is the layer turns the cube is moved by. `parse(text, size)`
reads whitespace-separated moves and says where it stopped (`ParseFault`: the
offset and length of the token, and whether it is not a move or names a layer
the cube does not have). `to_string()` prints a block as one token; a block
that is every layer of an axis is a rotation and prints as `x`/`y`/`z` named for
the positive face, and parsing any other printed block gives back exactly that
block. The tests print and read back every block of every size from 2 to 9.

`group(moves, size)` is for display. In a run of turns about one axis (which
commute) it gathers adjacent layers through the same angle into one block,
spelled from the nearer end: `R 2R` on a 7x7 is `Rw`, `6R 7R` is `Lw'`. It
cancels nothing and adds nothing, and keeps blocks in the order their first turn
came in, so a run with nothing to gather is returned as it was. The number of
blocks is the move count a person is shown (a wide move is one); what is stored
and replayed is the layer turns, and a person who follows the grouped moves
arrives where the originals arrive (checked over random runs at every size and
over the library's own solutions). Not yet: the middle-slice letters `M` `E` `S`,
which mean different layers at different sizes, and the words ("the second layer
from the right") the screens will pair with the letters.

## Following a solution

`SteppedPlan` (`stepped_plan.hpp`) is what a screen steps through: a solution
flattened into moves, `next()` and `previous()`, the cube cached at every
position (`current_cube()`, `cube_at(i)`), and one span of moves per stage. It is
what the app's `SteppedSolution` is for a 3x3, written for a cube of any size:
the state is an `NCube`, a move is a `WideMove`, and a stage is a `StageId`.
Nothing in it knows about Qt or the screens.

* **What a move is** is the caller's choice (`Display`). `Layers` is one move per
  turn of one layer, the library's solution as it is. `Wide` gathers adjacent
  layers turned the same way into one move (`group()`, above), so the count a
  person is shown is the wide-move count. At 3x3 the two are identical. The turns
  of single layers stay what is stored and replayed (`layer_turns()`,
  `layers_at(i)`), and a block never reaches across two stages. **Measured, the
  difference is small**: over 20 scrambles of each size, gathering turned 2,634
  layer turns into 2,618 moves at 4x4, 3,500 into 3,480 at 5x5, 6,628 into 6,601 at
  6x6 and 8,297 into 8,264 at 7x7, under 1%: the solver rarely turns two adjacent
  layers one after the other. The app shows wide moves for a cube of 4 or more,
  but a person following a 5x5 meets mostly single layers, and the way to say
  which one ("2R", "the second layer from the right") matters more than the
  grouping.
* **Where a person is** is `current_index()`, set by `next()`, `previous()` or
  `seek(i)` (any position, clamped), and `core_start()` is the move at which the
  3x3 stages begin (0 for a 3x3, `move_count()` for a 2x2): the place "skip ahead
  to the 3x3 stage" goes to, with the cube there reduced.
* **A stage** (`StageId`) flattens the two layers the library has, the
  reduction's (`Stage`) and the 3x3 solver's inside the core (`solver::Stage`),
  into the one list a screen wants. The 3x3 solver's stages keep the numbers
  `solver::Stage` gives them (the solve screen words them by number, and a 3x3
  must read as it always did); the reduction's follow: parity 8, centres 9,
  edges 10, corners 11 (a 2x2). A 3x3's solution is the core list alone. A stage
  has no words in the library: `key(id)` is a stable name to translate by, and
  `is_core(id)` says whether the stage is the 3x3 solver's, which is how a
  screen knows where "skip ahead to the 3x3 stage" lands.
* **`solve_held`** (`cube3.hpp`) is the app's way to solve a cube of any size:
  the library's `solve()` above 3, and at 3 the as-held reading described below.

`tests/unit/nxn/stepped_plan_test.cpp` holds a plan of a 3x3 equal to the
`SteppedSolution` the app has always used, for all three methods: the same
moves, the same stage spans, the same cube at every position, stepped both ways.

## One state concept, a virtual 3x3

`FaceletState` says what a puzzle state is: an edge length (`puzzle_size`) and
a colour at every (face, row, col). `solver::Cube` and `NCube` both are, and
the range algorithms in `state.hpp` (`facelets_of`, `has_uniform_faces`,
`colour_counts`) work on either.

`virtual_3x3(cube)` is an `std::mdspan` with a custom layout that maps the
6 x 3 x 3 index space of a 3x3 onto the corners, edge cubies and face centres
of the N x N storage: row or column 0 and 2 are the first and last, 1 is the
middle one. It copies nothing, and the layout is the only place the size
appears. `NCube::to_cube3()` reads through it (renaming colours to the
canonical scheme) to build the `Cube` the 3x3 solvers take. For a reduced
cube this *is* the cube's 3x3; before reduction it is a sample of stickers.

## The app's 3x3

The app solves through this library at size 3: `cube::nxn::solve_3x3()` and
`cube::nxn::prepare()` (`cube/nxn/cube3.hpp`) are the two calls the solve
controller and the startup path make, in place of `cube::solver::solve()` and
`cube::solver::prepare()`. They take and return the 3x3 types the app is
written in (`Cube`, `Solution`), so nothing above them - the stepped solution,
the history, the review - changed.

What they promise is the 3x3 solver's contract exactly: the same stages in the
same order with the same moves, or the same error. Three details make that
true rather than approximately true:

* **The cube is read as held** (`CoreView::AsHeld`, `NCube::to_cube3_as_held`).
  The orientation-free reading every other size needs renames colours so that
  each centre shows its face's colour, which would turn "a scan assembled into
  the wrong slots" into "a cube held another way" and solve it. The app has
  always refused that cube, and still does, with the same error per method.
* **Nothing is merged twice.** The 3x3 solver merges adjacent turns of a face
  in its own stages' terms; the library's merge also looks past turns of other
  layers of an axis (R L R' is L). A solution made only of the 3x3 solver's
  steps is returned as it was made.
* **The solve is replayed**, as at every size, so a solution that does not
  solve the cube it was made for is an error and not a result.

`tests/unit/nxn/cube3_test.cpp` holds all of that equal against the 3x3 solver,
per method: a corpus of scrambles, the solved cube and every single turn of it,
a cube of real pieces that no turns produce, and a cube with its centres out of
order - and, for that last one, that the library's own orientation-free entry
point does solve it, which is the difference the wrapper keeps out of the app.

## Results

Seeded scrambles of 60 + 10 N random layer turns, Advanced core, MSVC
RelWithDebInfo on the development machine (20 scrambles per size):

| N | Centres | Wings | Parity | Core | Total (mean) | Max | Before the centre search |
|---|---|---|---|---|---|---|---|
| 2 | - | - | - | 8.3 | 8.3 | 10 | 8.3 |
| 3 | - | - | - | 20.1 | 20.1 | 22 | 20.1 |
| 4 | 29.2 | 85.8 | 0.6 | 18.1 | 134 | 145 | 164 |
| 5 | 65.7 | 85.3 | 0.3 | 20.1 | 171 | 187 | 226 |
| 6 | 142.7 | 172.0 | 1.2 | 17.9 | 334 | 347 | 431 |
| 7 | 224.1 | 169.6 | 1.1 | 20.3 | 415 | 436 | 556 |
| 8 | 337.2 | 258.9 | 1.0 | 17.9 | 615 | 643 | 830 |
| 9 | 463.7 | 255.1 | 1.4 | 20.1 | 740 | 766 | 1015 |

Every solve is verified by replaying its moves on the scramble. The 3x3 core
alone, over 1000 scrambles: beginner 99 mean (59-142), intermediate 52
(36-61), advanced 20.3 (15-23).

The cycle library and the centre search's action set are built once per size,
on first use: together about 0.1 s at 5x5, 0.5 s at 7x7 and 1.5 s at 9x9 on
the development machine; a solve after that takes 50 to 300 ms. Tables are per
size and per process.

## What this is not, yet

* **Shortest possible.** The wings are now about half of every solution above
  4x4. Three things were tried for them and none is in: richer word families
  (no gain: with the centres, corners and middle edges all held still, the best
  word per move is the pure cycle already used); letting wing words scramble the
  corners (27% fewer wing moves at 4x4, but half the time it leaves an odd
  corner permutation that needs two edges swapped, and it gains nothing at odd
  sizes, whose middle edges fix every target); and "pair the wings first, place
  the pairs afterwards" for even sizes, which is the way humans do it and is
  where the next gain is - it is a different objective (a pair of wings in one
  edge, anywhere) with a parity fix, not a different set of words.
* **A mobile cost budget.** The one-off builds are the cost; they have not been
  measured on a phone and they grow faster than N. Symmetry would cut them
  (build one cycle per rotation class and rotate), and they are built for the
  sizes asked for only.
* **Scanning and a UI.** None exist for sizes above 3.
* **A compiler other than g++-16 snapshot, MSVC and Clang 23** has not been
  built with; the platform legs (Android, macOS, web) need `<mdspan>`, which
  their libc++ has.
