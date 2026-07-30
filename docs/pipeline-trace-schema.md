# Pipeline trace schema (JSON Lines)

The per-frame observability format of the tracking-first pipeline
([pipeline-plan.md](pipeline-plan.md), "Trace everything, enforced").
Produced by `cube::vision::pipeline::to_jsonl` (`trace.cpp`), consumed by
the offline runner's metrics and by anything that wants to ask "what did
stage X see on frame N" without a debugger.

Stability contract: **keys are schema.** Renaming or removing a key is a
breaking change to every analysis script downstream — treat it like
renaming a CSV column in `frame_recording.hpp`'s format (same precedent:
one header owns the truth, `pipeline_scaffold_test.cpp` pins it).

## Shape

One JSON object per line, one line per frame:

```json
{"seq": 42, "t_ms": 71350, "stages": [ <stage>, ... ]}
```

- `seq` — the pass number (the recording's `index.csv` key).
- `t_ms` — the frame's steady-clock mailbox timestamp; only differences
  are meaningful.
- `stages` — **exactly** the nine canonical stages, in pipeline order:
  `acquire, detect, track, faces, cells, sample, classify, fuse,
  assemble`. `validate_trace()` enforces presence and order; the runner
  fails a replay whose frames are incomplete.

Each stage record:

```json
{"stage": "detect", "ran": true, "ms": 12.4, "fields": { ... }}
```

- `ran=false` means the stage did not execute this frame — with a
  `fields.reason` saying why (e.g. the commit stages are app-owned until
  the routing harness lands). A missing record is a bug; a `ran=false`
  record is a fact.
- `ms` — wall-clock stage duration. `null` only if non-finite (bug).
- `fields` — flat, insertion-ordered key→value payload; values are JSON
  bool/number/string. Omitted entirely when a stage has no payload.

Numbers are emitted via `std::to_chars` (shortest round-trip, no locale),
so identical replays are **byte-identical** once `ms` is excluded — that
is the determinism check's definition of identical.

## Per-stage fields (legacy adapter)

The legacy adapter (`legacy_pipeline.cpp`) emits the following today.
Phase 1+ stages will extend this list; they must not repurpose existing
keys.

| Stage | Key | Meaning |
| --- | --- | --- |
| acquire | `w,h,full_w,full_h` | analysis / original frame sizes |
| | `fx` | focal length in use (approximation until calibration) |
| | `intrinsics_calibrated` | false until the Phase 2 calibration tool |
| | `motion_score` | StabilityTracker score for this frame |
| detect | `found` | any observation fired |
| | `confidence` | best observation's geometry confidence |
| | `face_found, face_pose_only, face_gated` | frontal path outcome (`gated` = cells off-frame) |
| | `corner_found, corner_gated, corner_hinted` | corner path outcome |
| track | `pose_src` | `corner/face/face-pose-only/edge/none` |
| | `pose_valid, from_seed, reproj_fraction` | accepted solve facts |
| | `edge_ran, edge_found` | edge fallback (runs inside this stage — see legacy_pipeline.hpp honesty notes) |
| | `state, gate, epoch` | tracker verdict (`TrackState`/`TrackGate` spellings) |
| | `front, certain_front` | front body face (URFDLB or `-`), commit-grade identity |
| | `theta_deg, margin_deg, sigma_deg, belief_margin` | tracker diagnostics |
| | `would_idle_skip, cool_hue_pixels` | idle-gate shadow verdict |
| faces | `count, capture_grade` | visible face views, colour-eligible subset |
| cells | `faces_with_cells` | views with real per-cell geometry |
| sample | `sampled_faces, sampled_by` | views whose Lab was adapted (`legacy-detector`) |
| classify | `classified_faces, unknown_cells` | provisional classify_lab outcome |
| fuse | `reason` | not run under the adapter (app-owned commit path) |
| assemble | `reason` | not run per frame (scan-completion stage) |

## Invariants worth scripting against

- A frame with `detect.found=false` has `faces.count=0` and empty
  cells/sample/classify payloads.
- `faces.count == cells.faces_with_cells + zero-filled views` — cells,
  samples and reads are **index-aligned** with the face list; edge views
  and pose-only views carry no colour by contract.
- `track.pose_valid=true` implies `pose_src != "none"`; the converse
  holds too.
- Stage records never disappear: a quiet frame is nine records with
  small payloads, not fewer records.
