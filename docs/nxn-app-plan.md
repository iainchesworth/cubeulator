# N x N in the app plan

> **Status:** 0.1.0-beta.1 solved a 3x3 and nothing else. On `develop` now the
> app follows a cube of 2 to 5 (more, in the library) from a typed scramble or
> hand-entered colours, draws it, reviews it and keeps it in History
> ([Progress](#progress)); the camera in the app still reads only a 3x3. The
> library reads, names and assembles any odd size from drawn cubes
> ([Gate S0](#gate-s0), offline half passed); no real cube but a 3x3 has been read.
> The rest of the page is the plan as accepted.

Written 2026-10-15. Status: **decisions accepted 2026-10-15; P0 done and P1
built 2026-10-16 to 17** ([Progress](#progress)). This is the basis for 0.2.0-beta.1 and
began once 0.1.0-beta.1 was out ([Release shape](#release-shape)).

This is the app half of the N x N work. The library half is done and described
in [nxn-solver.md](nxn-solver.md); the camera half is described in
[vision-pipeline.md](vision-pipeline.md) and
[recovery-plan.md](recovery-plan.md), and this plan changes both.

## Verdict

The solver is not the work. The library solves 2x2 to 9x9 and has been
replayed against 20 scrambles of every size. What stops a person solving a 5x5
in the app is everything around it: the state and move types the screens are
written in, a 3D view that is a hard-coded 3x3, a history that stops at 400
turns, a hand-entry screen that was never built, and, the one real unknown,
reading a cube of another size through a camera tuned and measured on 3x3s.

So the order is: make the app's state and moves size-generic without changing
a pixel of the 3x3; then let a person solve any size without a camera (pick a
size, get a scramble or enter a cube, follow the solution); then teach the
camera, odd sizes first, because an odd cube keeps the middle sticker the
3x3 reader anchors on, and even sizes last, because they have no middle
sticker at all.

The camera is the only phase that can fail for reasons nobody can read off the
code, so it has a cheap offline gate in front of it ([S0](#gate-s0)) and the
release does not wait for it.

## What was found

Read from `develop` at the merge of pull request 148.

| area | today | where |
|---|---|---|
| Solver | 2..9 solved by reduction; a 3x3 is one core stage. Mean moves over 20 scrambles: 8 / 20 / 134 / 171 / 334 / 415 / 615 / 740 for 2..9 | `src/lib/include/cube/nxn`, [nxn-solver.md](nxn-solver.md) |
| The app's solve path | `cube::solver::Cube`, `solver::Move{face, turns}` (outer turns only, no layer depth), `SteppedSolution::from(const Cube&, const Solution&)`. The solve already runs on a `QtConcurrent` worker | `src/lib/include/cube/solver/stepped_solution.hpp`, `src/app/qt/solve_controller.cpp` |
| Library moves | `LayerMove{face, depth, turns}` and `SliceTurn{axis, layer, turns}`; `NCube` holds `6*N*N` facelets and exposes a `std::mdspan` view | `cube/nxn/geometry.hpp`, `cube/nxn/cube.hpp` |
| 3D view | 26 cubies by construction (the file says so), `cubieSize` 34, a face's stickers read only if `side.length === 9`, layers addressed as -1/0/+1 | `src/app/qt/Cube3DView.qml` |
| Controller state | `stickers` is a map of six 9-colour arrays | `src/app/qt/solve_controller.cpp` |
| History | `kFormatVersion = 1`; a 54-letter cube; at most `kMostTurns = 400` turns; the moves are re-derived by solving the stored cube | `src/app/qt/controllers/solve_history_store.{hpp,cpp}` |
| Reading a side | lattice fit, blob and colour constants, the corner reader (since made N x N to 6x6: [investigation](investigations/2026-10-17-nxn-corner-reader.md)) and the steadiness gate are tuned on 3x3 recordings and hard-code 3 and 9 | `src/lib/src/vision/direct_scan.cpp`, `color_resolution.cpp`, `corner_view.cpp` |
| Assembling a cube | tries all 4^6 = 4096 per-side rolls and asks "is this a solvable 3x3" of each; the answer is the one that survives | `src/lib/src/solver/scan_assembly.cpp` |
| Is this an N x N a real cube? | **No oracle.** `NCube` has `is_solved()` and `is_reduced()`; nothing says a facelet array is a cube that turns can produce. The 3x3 has `Cube::is_solvable` and the assembler leans on it | `cube/nxn/cube.hpp` |
| Review and hand entry | not built (R3 of the recovery plan), for 3x3 either; a 3x3 design exists | [recovery-plan.md](recovery-plan.md), [design/screens/review.md](design/screens/review.md) |
| Wording | 418 strings in nine languages; those that name the 3x3, a "top face", the nine dots or the 54 colours need new text, not a number | [i18n.md](i18n.md) |
| Test helpers | `drawn_cube.hpp` and `cube_view_reading.hpp` draw and read six 3x3 sides | `tests/unit` |

The 3D view is the one that cannot be parameterised. A 9x9 has
9^3 - 7^3 = 386 surface cubies and 486 stickers, and the view builds several
scene nodes per cubie today (a body, an outline and its stickers); drawn the
same way it would be several thousand nodes with a binding each. A 5x5 is 98
cubies and 150 stickers and may well be fine; 7x7 is 218 and 294 and has not
been tried.

## Decisions

Accepted by Iain on 2026-10-15.

1. **One model, end to end.** The app uses `NCube` and `LayerMove` everywhere.
   A 3x3 is `NCube(3)`, which pull request 150 proved equivalent to the 3x3
   solver stage by stage and move by move. `solver::Cube` stays inside the
   solver and vision libraries and meets the app only at adapters. A dual
   3x3/N path would double every screen and every test.
2. **Increments, camera last.** Size picker, demo scramble and hand entry
   first (no camera for N > 3), then scanning odd sizes, then even.
3. **Size range.** The UI launches at 2 to 5, then 6 and 7 once the 3D view has
   proved itself. 8 and 9 stay library-only; they are a future release's
   decision, if they are wanted at all.
4. **3D by lifting the layer.** Static faces drawn as textured quads, with real
   nodes only for the layer that is turning, rather than instancing every
   cubie.
5. **N-generic from the first line.** Hand entry, the oracle and the notation
   are written for any N, not 3x3 first and retrofitted.

## Not in this plan

- 8x8 and 9x9 in the UI (decision 3).
- Shorter solutions. The wing stage is half of every solution above 4x4 and
  "pair first, place later" is the next gain ([nxn-solver.md](nxn-solver.md));
  it continues on its own track and changes no interface here.
- Non-cubic puzzles, and anything online.
- Changing a 3x3 solve. The three methods and their move counts are as
  [solver-methods.md](solver-methods.md) says; at N >= 4 they apply to the
  core only, and the screens say so.

## Design

### State and moves

`SteppedSolution` is written in `Cube` and `Move`. It becomes a stepped plan
over `NCube` and `LayerMove`, built from the library's `Solution`; its 3x3
instance is what the app already shows, and the controller tests pin that.
`solver::Move` survives only inside the 3x3 solvers and at the scan/solve
adapter (`cube3.hpp`).

The stage a person sees comes from the solution, not from a `solver::Stage`
integer: the library's stages (`Parity`, `Centres`, `Edges`, `Core`, `Corners`,
with the core's own stage nested inside `Core`) become a descriptor list the
UI renders (an id, a translatable name, a move range). The 3x3 keeps its six
stage names unchanged by being the `Core` list alone.

### Validity and reachability

Hand entry, a scan's assembly and a history entry loaded from disk all need
the question the 3x3 answers with `is_solvable`: is this a cube a person could
hold? The oracle is `cube::nxn::validate(const NCube&)` returning `ok` or a
reason a person can be told:

- colour counts (each colour N^2 times),
- the pieces: each corner, wing and middle edge is a real piece, none repeated,
  corner twist and chirality,
- centres: the right multiset per orbit, and on an odd size the middle
  stickers are six different colours,
- the parities that tie corners, wings and centres together at this size.

The rules are the solver's, written once, and tested against it as ground
truth: scramble many cubes and require `ok`; mutate each (swap two stickers,
twist a corner, swap two pieces, repaint one) and require that the oracle and a
solve attempt agree. It runs 4096 times per scan assembly, so it is O(stickers)
and allocation-free.

### Notation

`LayerMove` has the depth; nothing prints or parses it. Add the WCA forms:
`R` (outer), `Rw` and `3Rw` (the outer two and three layers), `2R` (the second
layer only) and `2-3R` (a range), with `'` and `2`. Print and parse
round-trip over every move of every size. The translation of letters is none:
the moves are names, as at 3x3.

For display only, consecutive turns of the same axis and direction in adjacent
layers merge into one wide move; the animation turns the block together. What
runs is unchanged and replayed. The move count a person is shown is the
displayed count (a wide move is one), and the history stores the layer turns.

A layer needs words for people who do not read WCA: "the second layer from the
right". The solve screen shows both.

### The 3D view

The 3x3 view stays as it is until the new one beats it, then goes. The new one
draws each of the six faces as one textured quad (an N x N sticker texture, or
a small instanced sticker grid) and builds real cubie nodes only for the layer
that is turning, swapping them in for the duration of the move. A move's
animation is "lift layer k of axis a, turn, drop it and re-texture", which is
the same at every size and costs the same per move. Orbiting, the gestures to
pick up and set down a layer, and the stickers' light response are carried
over; the lights are still capped at four by Qt Quick 3D.

The prototype is the first thing built after the foundations, because decision
3 depends on it: it runs the 7x7 and 9x9 demos and is measured the way the 3x3
view is now, on a phone-class GPU and in the browser, before any screen is
rebuilt on it.

**Result (2026-10-16, [tools/cube3d_prototype](https://github.com/iainchesworthlabs/cubeulator/blob/main/tools/cube3d_prototype/README.md)).**
Built and measured against the same view scaled up the way `Cube3DView.qml` would
scale (one model per cubie and per sticker, a material bound to each). The lift
renderer draws a cube of any size as three scene objects: on the development
machine it costs 0.2 ms of scene-graph work a frame at every size from 3x3 to
9x9, and in a browser (Edge, ANGLE D3D11, the same GPU) 0.4 ms and a steady 60
fps at 9x9, with 0.1 to 0.2 ms to lift a layer and set it down. The naive view is
9.8 ms a frame in the browser at 5x5, 30.6 at 7x7 (31 fps) and 47.8 at 9x9 (20
fps). So the renderer is not what limits the size, and decision 4 stands: sizes 6
and 7 clear the 3D gate, and 8 and 9 would on this evidence. **A phone has not run
it**: the Android APK is built and `android_matrix.py` is the one command that
measures it (needs a device); until it has, decision 3's phone half is an
estimate (about 1 to 2 ms a frame, GPU not included).

### Screens and flow

- **Size.** A choice before scanning or entry. It is chosen, never inferred
  from the camera. Where it sits is a design question for the screens lane.
- **Demo.** `CUBE_SOLVE_DEMO=<seed>` (`main.cpp`) gains a size; a scramble of
  any size opens the solve screen. This is also how the 3D view is checked.
- **Enter a cube.** The review and hand-entry screen the recovery plan always
  needed (R3), built N-generic: a sticker grid per side, paint, the oracle's
  reason shown beside the Solve button. It starts from the 3x3 design in
  [design/screens/review.md](design/screens/review.md) and changes what that
  assumes: nine stickers a side becomes N^2 (49 at 7x7, so stickers shrink and
  paint-by-drag matters), "the centre is fixed" holds only for odd N, and
  "any way up" still holds because the four rolls of a side are the same at
  every size. A 5x5 is 150 stickers; fill and mirror helpers make it humane,
  and for 6x6 and 7x7 the realistic route is the next item.
- **Enter a scramble.** Type or paste a scramble in notation and the app
  applies it to a solved cube. Cheap (the notation exists by then), exact, and
  how a person who has the scramble from a timer or a puzzle site gets a 6x6
  into the app without a camera.
- **Follow the solution.** The same screen, with: per-stage progress
  (Parity, Centres, Edges, then the 3x3 stages), a "skip ahead to the 3x3
  stage" jump for people who can finish a 3x3 themselves, wide-move grouping
  and layer words, and the method picker reading "finish with" because the
  methods apply to the core.
- **A long solution.** 134 to 740 moves is a different experience to 20. The
  stage chunking, a persistent progress bar and a resume point are in scope; a
  "solve it for me" autoplay is not.
- **History.** Version 2: a `size` field and the moves stored, the 400-turn cap
  lifted to the library's worst case with margin, the thumbnail size-aware.
  A version 1 file loads as size 3. The moves are stored because re-deriving
  them by solving would change a stored solution whenever the solver
  improves, and at N > 3 it already would.

### Reading a cube of another size

The 3x3 pipeline reads six flat sides, then optionally corner views, then
assembles them under the 4096 rolls and the solvability check. Scaling it:

- **Constants.** About fifteen thresholds are expressed in 3x3 pixels or
  assume nine blobs (lattice pitch, blob area, fit residual, the steadiness
  gate, the cluster seeds). They become a function of N, and several are
  stated as multiples of the pitch so the unit changes and the logic does not.
- **Resolution.** On the corpus's 640x360 frames a 3x3 sticker is about 22
  pixels across. At the same framing a 5x5 sticker is about 13 and a 7x7 about
  9. That is an estimate from the framing, not a measurement, and the gate
  below measures it.
- **Odd sizes (5, 7)** keep a middle sticker per side. Centre-anchored colour
  classification, white-reference and same-side identity carry over with N^2
  stickers per colour instead of 9.
- **Even sizes (2, 4, 6)** have no middle sticker. The seeds for the six
  colours and the "which side is this" identity both rely on it. The
  replacement seeds clusters from the corners (every corner shows three
  colours that must be mutually distinct and chirally ordered) and infers the
  scheme from them (`scheme_of` exists in the library). It is a harder
  problem, not a port, and 2x2 is the vehicle for it: smallest, no centres, no
  wings.
- **Assembly.** Still four rolls per side, so still 4096; the check becomes
  the oracle. A false accept (an assembly the oracle passes that is not the
  cube in the person's hand) is measured and reported per size, as at 3x3.
- **Corner reader.** Close to a rewrite: it reads three sides at once with a
  3-wide lattice. A flats-only first version is a legitimate result.

#### Gate S0

Before any scan UI. Offline, no screens:

1. `drawn_cube.hpp` and `cube_view_reading.hpp` made N-generic, drawing an N x N
   cube under the noise models the 3x3 tests already use.
2. The side reader run against them, per size, reporting sticker accuracy and
   the point (pixels per sticker) where it falls off.
3. The same on real recordings: this needs physical cubes. At least a 5x5, and
   a 4x4 for the even case; a 2x2 and a 7x7 if available. One session each in
   the lights the 3x3 corpus was taken in.

The gate decides P2 and P3: pass for 5x5 and 7x7 means P2 is built; a size
that does not pass is not offered a camera, and its screen says "enter this
one by hand".

**Result (2026-10-17, items 1 and 2; [the investigation](investigations/2026-10-17-nxn-scan-s0.md)).**
Offline, on drawn cubes in daylight and a warm lamp's light, the reader reads a 5x5 and
a 7x7 as it reads a 3x3 at the same sticker size: 100.00% of stickers where a side is read
at all (99.91% at the edge), every sticker of every complete scan named right, and no wrong
cube put together in 4,240 scans (the 3x3's replay of the real recordings is unchanged, line
for line). The line is a sticker's size, not the cube's: stickers of 13 px (22 px in lamplight)
in a 640-wide frame, which a 5x5 holds at 22% (39%) of the frame's height and a 7x7 at 31%
(54%): **a 5x5 is read at about a 3x3's distance in daylight and closer by a lamp, a 7x7 must
be held about 1.4 to 2.4 times closer** (a wider working frame, which a bigger cube now asks
for, moves it to 25% and 30% where the camera gives 1280). What it cannot say: **item 3 -
real recordings - is not done**, so nothing is known of a real 5x5's gaps, glare, plastic or
hands; the oracle cannot see two centre stickers swapped, so a bigger cube's scan is as right as
its naming (the review screen is the check); and a small sticker makes the steadiness gate
a smaller shake in pixels (at +-4 px a 7x7 counts one frame in nine). **Even sizes (P3) are done
offline as well**: named from their corners and put together by trying where each side goes; a
4x4 and a 6x6 read and assemble as the odd sizes do, and a 2x2's six flat sides make more than
one cube one time in five, which only the review can settle.

## Phases

| phase | scope | exit gate |
|---|---|---|
| **P0 Foundations** (no visible change) | stepped plan over `NCube`/`LayerMove`; the oracle; WCA notation; stage descriptors; history v2 (size, moves, cap) and its v1 migration; `size` in recordings and diagnostics; N-generic test helpers; budgets for `prepare()` and `solve()` and **measured phone and wasm timings**; the 3D prototype | 3x3 behaviour and pictures unchanged (controller tests, QML tests, system walk, golden pictures); oracle agrees with the solver over random and mutated cubes at 2..7; budgets written down |
| **P1 Solve and follow any size** | size picker; demo scramble; enter a cube; enter a scramble; the new 3D view; stage progress and skip-ahead; layer words; wide-move display; method picker rescope; history v2 on screen; new strings in nine languages; guide, screenshots | a 5x5 demo solved end to end on desktop, web and a phone-class device inside the time budget; 3x3 pictures byte-identical |
| **P2 Scan odd sizes (5, 7)** | gate S0, then constants, colour resolution at N^2 per colour, assembly with the oracle, the steadiness gate in pitch units, N^2 dots on the scan screen, flats-only corner handling | at least 99% sticker accuracy on the corpus; false-accept rate measured; no regression on 3x3 |
| **P3 Scan even sizes (2, 4, 6)** | corner-seeded clustering, scheme inference, white reference without a centre, 2x2 first | its own gate; may conclude that the review screen confirms the side colours |
| **P4 Release** | budgets in CI; docs and the 25 guide screenshots; privacy wording ("54 colours" is no longer true); store text; accessibility (layer words, wide moves announced) | the beta checklist in [releasing.md](releasing.md) |

### Progress

Each slice is one pull request to `develop`, library first, with the screens on
top. Updated as slices land.

| slice | phase | state |
|---|---|---|
| Validity oracle `cube::nxn::validate` | P0 | merged (#164) |
| WCA notation: print and parse, round trip over every move of every size | P0 | merged (#165) |
| Stepped plan over `NCube`/`LayerMove`; stage descriptors | P0 | merged (#166) |
| The solve controller holds `NCube` and a stepped plan | P0 | merged (#167) |
| History format v2 | P0 | merged (#168) |
| Budgets and measured timings | P0 | merged (#169); **desktop and wasm measured, a phone not** (see below) |
| 3D layer-lift prototype, measured at 5x5, 7x7, 9x9 | P0 | merged (#170) |
| `size` in recordings and diagnostics | P0 | merged (#171) |
| N-generic test helpers | P0 | merged (#172) |
| The controller says size, layers of a move, the 3x3 stage's start | P1 | merged (#173) |
| The 3D view for any size (`CubeNView`, `cube::nxn::build_mesh`) | P1 | merged (#174) |
| The solve screen for any size: layer words, stage list, skip to the 3x3 | P1 | merged (#175) |
| Home's *Another size of cube*; size 2 to 5; scramble entry; demo size | P1 | merged (#176) |
| Typing a cube in a colour at a time, with the validity oracle | P1 | merged (#177) |
| Review and History for any size; the kept solution carries its stages | P1 | merged (#178) |
| Translation drafts for the new strings in eight languages | P1 | open (#179) |
| Layout checks hold the new screens in the pseudo-locale and the RTL drafts | P1 | open (#180) |
| User guide page and pictures | P1 | not started |
| Assembling six scanned sides of any odd size (`cube::nxn::assemble_scanned_sides`, on the oracle) | P2 | merged (#190) |
| The reader, the scan and the naming for any size; gate S0 offline (`cube_scan_s0`) | S0, P2 | merged (#191); **drawn cubes only** |
| The scan screen and controller for any odd size: `DirectScan.size`, N^2 dots and chips, the review at its size; recorded at its size; `CUBE_SCAN_SIZE`; **not offered on the entry screen** | P2 | merged (#192) |
| Even sizes: colours named from the corners, sides put on the cube by trying (`resolve_colors`, `assemble_scanned_sides`), `DirectScan` takes 2 to 9 | P3 | merged (#193); **drawn cubes only** |
| Real recordings of a 5x5 (`CUBE_SCAN_SIZE=5 CUBE_SCAN_RECORD_DIR=...`) and a 4x4, then offering the camera for 5 (and 4) on the entry screen (with its words) | S0, P2, P3 | not started; needs a 5x5 and a 4x4 in the hand |

**What P1 found.** Wide moves are rare: grouping adjacent layers turned together
saves under 1% of the moves (2,634 to 2,618 at 4x4, 8,297 to 8,264 at 7x7), so a
person following a 5x5 is helped by being told *which layer* ("2R", "layer 2
from the right"), which the screens do, more than by shorter lists. The 3D view
costs three scene objects at any size, 0.2 ms of scene-graph work a frame
natively and 0.4 ms in a browser at 3x3 and at 9x9. Hand entry names a side by
where it is on the cube, not by a centre colour, which an even size does not
have; that is where the plan's "any way up" bent. Sizes 6 and 7 are built and
tested in the library and the view but are not offered in the UI yet (decision 3
waits on a phone measurement). The 3x3's own screens, pictures and tests are
unchanged and pass as they were.

**Open against the P1 gate.** The gate asks for a 5x5 demo solved end to end on
a phone-class device inside the time budget. Desktop and the browser build are
covered by tests (the system walk, the Playwright spec `sizes.spec.mjs`); the
phone timing was not run (it needs a phone and was waived on 2026-10-17), so a
phone's cost for `solve()` at 5x5 is unknown. `SteppedSolution` is not deleted
yet: it is still the reference the 3x3 stepped-plan tests are held against.

P0 and P1 are the milestone. A person can solve and learn any size from 2 to 5
(then 7) from a hand-entered or demo cube; scanning arrives size by size.

Branches follow gitflow (`feature/nxn-app-<topic>`) and each phase lands as
several small pull requests to `develop`; none enables auto-merge unless
asked.

## Release shape

0.2.0-beta.1, in [releasing.md](releasing.md)'s numbering (a minor for a
feature, a counter within a pre-release line), is proposed to be:

- **In:** P0 and P1 for sizes 2 to 5, and 6 and 7 if the 3D prototype clears
  its gate. Enter a cube, enter a scramble, demo, follow, history.
- **In if S0 passes in time:** scanning a 5x5 (and 7x7). Otherwise scanning
  ships in 0.2.0-beta.2 or later, and the beta says so on the scan screen for
  N > 3.
- **Out:** even-size scanning (P3) unless it surprises, 8x8 and 9x9.

Each user-visible pull request carries a `changelog.d` fragment; foundation
work with no visible change is `no-changelog`. 0.1.0-beta.1 itself is handled
by its own session and is not part of this plan; pull request 150 (the 3x3
through the library) is in it.

## Risks

| risk | why | retired by |
|---|---|---|
| **Scanning N >= 4 may not be reliable.** | Smaller stickers at the same framing; four-way symmetry without centres at even sizes; a corner reader that assumes three. | Gate S0 offline before any UI; the release does not depend on it; a size that fails is hand-entered |
| **3D at 7x7 and above is unproven.** | Node count; per-node bindings; the estimate above is not a measurement. | The layer-lift prototype in P0, measured before a screen is rebuilt on it |
| **Phone and wasm timings are unmeasured.** | Tables are per size and built on first use (about 0.1 s at 5x5, 0.5 s at 7x7 on the development machine); a solve is 50 to 300 ms there. A phone may be 3 to 5 times that. | P0 measures it; `prepare(size)` runs while a person scans or enters, off the UI thread; a size over budget is not offered on that platform |
| **700-move solutions.** | Nobody follows 700 moves one at a time without help. | Stage chunking, wide moves, skip-ahead and a resume point in P1; shortening continues on its own track |
| **History re-derives solutions.** | A stored 5x5 would change the day the solver improves. | Moves stored in v2; v1 loads as size 3 and keeps its behaviour |
| **Even sizes have no fixed centres.** | The UI names a side by its centre colour (`stickers[side][4]`). | Naming by position; the scheme is a property of the cube, shown on the review screen |
| **Test helpers are 6x9 throughout.** | `drawn_cube.hpp`, `cube_view_reading.hpp`, the system walks and the picture goldens. | P0 makes them N-generic with N = 3 as the default so every existing test stays itself; this is the largest single cost |
| **Real cubes.** | Gate S0 and the even-size problem both need physical cubes and light. | Iain: a 5x5 and a 4x4 at least, a session each |
| **Lanes.** | The work crosses the scan, kit and screens, solve and translation lanes. | It starts after 0.1.0-beta.1 so none of their beta work is disturbed, and each phase names the lane its pull requests land in |

## Order

```
#150 merged -> 0.1.0-beta.1 released   (not this plan)
        |
        v
P0 foundations --------------+--> 3D prototype gate (sizes 6, 7)
        |                    |
        v                    |
P1 solve and follow ---------+
        |
        +--> S0 offline (can start in P0, needs N-generic helpers)
        |        |
        |        v
        |    P2 scan odd --> P3 scan even (own gate)
        v
P4 release readiness --> 0.2.0-beta.1
```

S0 starts as soon as the N-generic helpers exist in P0; it does not wait for
P1.

## Open decisions

For Iain, none of which block P0:

1. The release cut above, in particular whether 0.2.0-beta.1 waits for
   scanning a 5x5 or ships without it.
2. What a "move" is on screen: wide moves as one (the WCA count, shorter) or
   layer turns (the number the library reports). Proposed: wide moves as one,
   with the layer turns in the detail.
3. Whether an "enter a scramble" path is a first-class screen or an advanced
   option under hand entry.
4. Whether 2x2 is in the first release at all. It needs no reduction, its
   solver is a different algorithm, and it is the hardest to scan; it is
   cheapest as "enter by hand" in 0.2.0-beta.1.
