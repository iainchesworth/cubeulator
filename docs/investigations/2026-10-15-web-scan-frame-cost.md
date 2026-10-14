# What does one frame of the scan cost in the browser, and does the verdict freeze the page?

**Asked:** 2026-10-14. **Closed:** 2026-10-15. **By:** an agent working for the
maintainer, from the [tumble investigation](2026-10-14-scan-tumble.md), which
found the verdict "six sides, no cube" to cost "up to 150 ms of native time on
every frame - several times that in WebAssembly, which was not measured". The
browser build's reader runs on the page's main thread, so a frame that long
would be a page that does not draw, take input or feed itself camera frames.

## What was tried

The build was `web-emscripten-diagnostics` (Emscripten 5.0.5, Qt 6.12 for
WebAssembly, Release with the in-page recorder), played in Chromium's headless
shell on SwiftShader by `tests/web`: a session's frames, at the times they were
taken, to the page's camera (`__fakeCamera.image`), and the recorder's trace read
out every fifteen frames, so that each frame's `direct_scan.process_frame` is its
own number and not a mean. Long tasks (`PerformanceObserver`) were watched for
the freezes a person would feel. The same sessions were replayed natively by
`cube_scan cost` (new, `tools/scan`), which times the stages of
`DirectScan::processFrame`'s loop on the decoded frames. The machine is a
32-core Windows desktop that other sessions were building on, which is a fair
stand-in for a busy phone and makes every browser number noisy: each is one of
two or three passes, builds alternating, and the spread is given where it
matters.

Three builds: the tumble branch as it was (**base**), the same with an exact
memory of the last eight namings' assemblies (**memo**, built here, below), and
`develop` with the assembly guard that landed while this was being measured
(PR #146, **guard**).

Six sessions:

| Session | What it is | Its judgements |
| --- | --- | --- |
| drawn tumble, 20 cubes | the patterned cube drawn at 60 degrees a second, 30 frames a second | one, which settles it |
| drawn tumble, 112 cubes | the cube whose six flat sides make the most | one |
| drawn tumble, one edge flipped | `cube_tumble_sim` of a cube with the stickers of one edge traded in place: six sides read, no cube, **the worst case** | every frame the held side moves a colour |
| recorded, patterned cube | the 480 frames of the tumble investigation, 20 cubes, still ambiguous | the naming goes back and forth between "20 cubes" and "none" |
| recorded take, no cube | 805 frames, six sides read, none of the namings makes a cube | every frame |
| recorded take, 12 cubes | 1,158 frames, ambiguous | every frame |

## What it found

**The verdict is not 150 ms.** `cube_scan cost` on an optimised build
(RelWithDebInfo, MSVC, no sanitizers) puts the dearest judgement, six sides that
make no cube, at **47 ms** mean and 77 ms worst (the listing and fifteen
trades of two names, 2.6 to 3 ms each), and in WebAssembly it is **12 ms** on a
quiet desktop, 20 to 25 ms with the machine busy (a listing 1.5 to 3.4 ms, a
trade 0.6 to 1.2 ms), and 6 ms for the recorded take. The build the 150 ms was
read from, the tumble session's, has AddressSanitizer on
(`CUBEULATOR_ENABLE_SANITIZERS=ON` in its cache): a factor of three to five.
A judgement that makes a cube, or several, is one search: 4 to 5 ms in
WebAssembly, 10 ms natively; with a hundred and twelve candidates and eight
corner reads, the votes and the corner to ask for add 2 to 7 ms.

**It was the repetition that was felt, and the guard ended it.** The app judges
the scan whenever a side held in view moves a fused colour, which a camera's
noise does on nearly every read, so a scan that was held and did not make a cube
paid the whole judgement again on each frame. Natively that was **145 of 1,188
frames over 33 ms, 81 of them over 50** (the drawn no-cube tumble, three
passes); in the page, with the machine quiet, a frame's mean went from 5 ms to
8.7, 9.9 and 10.1 ms in three passes and its 90th percentile to 18 to 21 ms.
Nothing there was a freeze (no frame over 33 ms beyond the cold one, below, on a
quiet machine), and on a busier one the 99th percentile was 56 to 74 ms on the
recorded patterned cube - ten frames over 50 ms in 580, and a judgement of
82 ms - which is a run of dropped frames, not a stopped page. A phone is slower
than this desktop, and the same judgement there is a few times the above.

| Assemblies (listings + trades), in the page | base | memo | guard |
| --- | --- | --- | --- |
| drawn, one edge flipped | 52 + 832, 45 + 720, 52 + 832 | 2 + 30 (three passes) | 4 + 64, 3 + 48 |
| recorded patterned cube | 37 + 592, 10 + 0, 49 + 384 | 11 + 105, 5 + 75, 16 + 120 | 8, 13 (ambiguous: listings only) |
| recorded take, no cube | 121 + 1,936 | 3 + 45 | 4 + 32 |
| the frame's 90th percentile (ms), drawn one edge flipped | 18.0, 21.3, 19.8 | 8.1, 8.4, 6.9 | 6.0, 7.9 |
| frames over 33 / over 50 ms, recorded patterned cube | 21 / 10, 19 / 7, 23 / 2 | 3 / 1, 15 / 5, 1 / 1 | 1 / 1, 1 / 1 |
| native, drawn tumble: frames over 33 / over 50 ms (of 1,188) | 145 / 81 | 7 / 4 | 7 / 6 |

The memo (`SideScan` keeping the listing and each trade for the last eight
namings, exact because a search is a function of the naming alone) was built
here before the guard landed, and the two were compared on the same sessions.
The least number of times the sides have to be put together is the number of
different namings the session went through, which `cube_scan cost` now counts:
2 for the drawn tumble, 5 for the recorded patterned cube, 3 for the take. The
guard assembled 2, 6 and 4 times natively - at the floor, give or take the
one re-assembly when an ambiguous scan's colours move a Lab unit - so the
memory of eight would have saved at most one assembly a session, and was not
kept. What the guard leaves is each naming's first assembly: 12 to 25 ms in the
page for six sides that make no cube, a few of them a scan.

**The sessions that complete were never a problem.** The drawn tumbles that
settle judged once, at 5 to 10 ms, and their frames cost 5.0 to 5.5 ms on
average with no frame over 33 ms; the recorded 12-cube take cost the verdict
0.5 ms. The numbers for the builds there are inside each other's spread.

**What else turned up.**

- **The first frame of a scan is 250 to 330 ms in the page** (90 to 140
  natively), once, and it is not the verdict: it is the first
  `cv::cvtColor(float BGR -> Lab)`, in the first `sticker_lab` of the first side
  read (91 ms natively, timed inside; the other eight stickers of the same read
  take 0.2 ms each). OpenCV builds the conversion's tables on first use. It is
  not V8's lazy compilation (`--no-wasm-lazy-compilation` changed nothing) and
  not OpenCV's thread pool. It is the one stall a person would see: the page
  does not draw for a third of a second as the camera's first frame arrives.
- **A held scan costs a little more every second.** `fuse` takes the median of
  every read of a side, so each counted read costs the number of reads so far:
  `vision.direct_scan.add` was 0.07 ms in the first ten seconds of a held cube
  and 0.59 ms in the seventh ten seconds, +0.09 ms every ten seconds, with the
  whole frame going from 4.3 to 4.6 ms. Ten minutes is 5 ms, an hour 30 ms, and
  the reads are kept (about 100 bytes each).
- **`vision.corner_view.fit_corner` is a zone per call**, which is a
  candidate times a kept read, up to 16,384 events for one judgement, and the
  recorder's ring holds 131,072: a long ambiguous scan overwrites its own trace.

## Decision

Done, in the change that carries this page (it adds no behaviour: the guard is
#146's):

- **The work is held in counts, on every machine**: `judge.*` in
  `cube_perf_tests` (`tests/perf`, "the scan's judgement"): a drawn scan judged
  after every read and a hundred times after, of sides that make a cube, several
  and none: 1, 1 and 1 assemblies (budgets 1, 2, 2), and none for a judgement of
  the finished scan.
- **A ceiling on a frame of the scan in the browser**, `perf.spec.mjs`: the
  held no-cube scan (always: no more than sixteen assemblies in the hold and not
  one in the second half; with `CUBEULATOR_WEB_PERF_TIMING=1`: mean inside 33 ms,
  95th percentile inside 66, longest inside 250), and a session named by
  `TUMBLE_SESSION` held to the same, with the cold frame reported apart.
- **`cube_scan cost`** repeats the native half: the stages of a frame, what the
  verdicts came to and how many assembled afresh, and how many namings the
  session went through. The `vision.direct_scan.assemble` zone counts the
  assemblies in a page or a Tracy capture.

Open, and measured above:

- the first-frame stall (a warm-up of the conversion off the main thread);
- the growth of a held scan's cost;
- the first assembly of a naming (a search over the 4^6 turns that checks the
  pairs of faces already turned as it goes, exactly), and the corner votes (the
  candidates that agree on the sides a read shows fitted once);
- the per-call `fit_corner` zone;
- whether a phone feels any of it: the numbers are a desktop's.

## Where the code is

`bugfix/web-scan-frame-cost`. The tools, to repeat it:

```sh
cube_scan cost <session> [runs] [-v]            # native: the stages of a frame, the verdicts, the assemblies
cube_tumble_sim <out> "<layout>" 60 30          # a layout with one edge flipped is six sides and no cube
TUMBLE_SESSION=<out> CUBEULATOR_WEB_PERF_TIMING=1 npx playwright test --project=perf -g tumbled
```

A layout that makes no cube: the patterned cube of the tumble investigation with
the up side's bottom-middle sticker and the front side's top-middle one traded,
`U:YYWYYYWBY D:YWWWWWWYY L:RRRRRRRRR R:OOOOOOOOO F:BWGGGGGGB B:BBGBBBGGB`.
