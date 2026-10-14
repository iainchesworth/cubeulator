# Live diagnostics

What the running app is reading and deciding, written to a file you can open
while it is still up: the colour of each side, how the colours were named, how
the cube was put together, what the camera accepted, where the solve stands.
For finding out *why* a scan misnames a colour, a cube will not assemble or a
solve behaves oddly, without restarting the app with an environment variable
set and trying to get back to the same state.

It is **developer-only, off by default, and costs nothing while off**: each
place that could write a record tests one boolean first, and nothing is built,
formatted or written until the channel is on.

## Switching it on and off

It is switched live, with no restart:

| Where | What it does |
| --- | --- |
| **Settings > Developer > Write diagnostics to a file** | Starts and stops the channel. While it is on, every screen shows a strip at its lower left that says so, and where the file is. |
| **Ctrl+Shift+D** (desktop) | The same switch, from any screen. |
| **Settings > Developer > Snapshot now**, **Ctrl+Shift+S** | Writes the whole current scan and solve as one record. See [Snapshot](#snapshot-now). |
| `CUBE_DIAGNOSTICS=1` | Sets the state the app *starts* in (`1`, `true`, `on` or `yes`). After that the switch above is the one in charge: set to `1` and the app still stops writing when you turn it off. |

Nothing is remembered: the app starts with the channel off unless
`CUBE_DIAGNOSTICS` says otherwise, so one cannot be left on by accident.

## Where it writes

A fixed, per-user place, chosen by the platform layer
(`paths::current().diagnostics_dir`, see [Platform isolation](platform-isolation.md)),
so another process - or you, with `tail -f` - can find it without being told:

| Platform | File |
| --- | --- |
| Windows | `%LOCALAPPDATA%\Cubeulator\Diagnostics\diagnostics.jsonl` |
| macOS | `~/Library/Logs/Cubeulator/Diagnostics/diagnostics.jsonl` |
| Linux | `$XDG_STATE_HOME/cubeulator/diagnostics/diagnostics.jsonl` (`~/.local/state/cubeulator/diagnostics/` when `XDG_STATE_HOME` is unset) |
| Browser | The page's own in-memory filesystem, `/home/web_user/.local/state/cubeulator/diagnostics/`. It is gone when the tab is, and no other process can read it. |
| Android, iOS | Not available yet: their paths are not wired (`paths_android.cpp`, `paths_ios.cpp`). The switch is not offered. |

The directory is local to the machine (on Windows, not the roaming profile) and
outside every repository. Settings shows the exact path.

`diagnostics.previous.jsonl` beside it is the run before this one: when the app
first switches the channel on in a process, whatever `diagnostics.jsonl` held is
moved there and replaced. The file is also bounded: past 64 MiB it is moved to
`diagnostics.previous.jsonl` and a new one begun, so a channel left on cannot
fill a disk. Switching it off and on again in the same run keeps appending to
the same file.

### Reading it while the app runs

Each line is flushed as it is written.

```bash
# Linux and macOS
tail -f ~/.local/state/cubeulator/diagnostics/diagnostics.jsonl
```

```powershell
# Windows
Get-Content "$env:LOCALAPPDATA\Cubeulator\Diagnostics\diagnostics.jsonl" -Wait
```

With [`jq`](https://jqlang.github.io/jq/): what each cluster of a scan was
named, every time that changed:

```bash
jq -c 'select(.kind=="scan" and .data.naming) |
       {t: .time, names: [.data.naming.clusters[].name], override: .data.naming.hue_override,
        assembly: .data.assembly.path}' diagnostics.jsonl
```

## The file's format

JSON Lines: one JSON object per line, UTF-8, `\n` between lines. Every line is
a record:

```json
{"seq":12,"time":"2026-10-14T20:11:03.412Z","kind":"scan","data":{ ... }}
```

| Field | Meaning |
| --- | --- |
| `seq` | Records written since the process started, from 0. A gap means a line is missing. |
| `time` | UTC, ISO 8601, milliseconds. |
| `kind` | What `data` holds (below). A reader ignores kinds it does not know. |
| `data` | An object whose members depend on `kind`. |

Colours are CIELAB, `[L, a, b]` (L 0 to 100). Names are fixed words: `white`,
`yellow`, `red`, `orange`, `green`, `blue`, `unknown`; errors are the
`CubeError` enumerator's name (`ambiguous_scan`, `invalid_cube_state`, ...).
A number that is not finite is `null`, so a line is always valid JSON. Cube
states are the 54-character `U R F D L B` strings of
[recording-format.md](recording-format.md#manifestjson) (`state`), so a cube
on screen can be pasted into a manifest.

### Record kinds

**`session`** - the channel's own events. `data.event` is `on`, `off`,
`snapshot` (a snapshot taken with the channel off), `frames_on` or
`frames_off`. `on` and `snapshot` also carry `pid`, `version`, `commit`,
`build_target`, `directory` and `frames`, so a file says who wrote it.

**`scan`** - one evaluation of the scan, written when its outcome *changed*
(a new side captured, a different naming, a different assembly path, a
different verdict) and not for every frame: `unchanged_since_previous` counts
the evaluations that agreed with the one before. `trigger` is `sides_changed`,
`corner_added` or `restart` (a bare record: the scan was started over).

| Member | Meaning |
| --- | --- |
| `groups`, `captured`, `corner_reads` | Groups of reads so far (strays included), those with enough reads to be a side, and corner reads kept. |
| `sides` | The captured sides, at most six, in the order they were handed to colour naming (most reads first). Each: `first_seen` (its place in the order sides were first shown), `reads`, `lab` (nine fused colours, laid out as the side was *first* seen), `roll` (quarter-turns from that layout to how it was last shown), `in_view`. |
| `naming` | How the 54 colours were named - the next table. `null` until six sides are in; `{"error": ...}` if naming failed. |
| `assembly` | How the cube was put together - the table after. `null` when there was no naming. |
| `result` | `{"ok": true, "state", "colours"}` with the cube and what each colour measured in the session, or `{"ok": false, "error"}`. |

`naming`, from `resolve_colors_explained`:

| Member | Meaning |
| --- | --- |
| `clusters` | Six, indexed by the side whose centre seeded the cluster: `seed` and `mean` (Lab), `hue` (`classify_lab`'s reading of the mean: `hue_deg`, `chroma`, `rule`, `name`, `confidence`), `by_distance` (the palette colour the distance matching chose), `name` (the final name). |
| `hue_override` | What the hue names did: `applied` (six distinct hue bands, taken), `collision` (two means in one band, so the distance matching stood), `unknown_cluster` (a mean classified `unknown`; distance matching stood). |
| `hue_changed_names` | Whether taking the hue names changed any cluster's name from `by_distance`. |
| `match_cost`, `iterations` | The palette matching's summed Lab distance, and the refinement rounds the clustering ran. |
| `sides` | Per side, in the order of `sides` above: nine each of `cluster` (the cluster index the sample went to), `name`, `confidence` (0 when a rival cluster was as close). |

`hue.rule` is the rule of `classify_lab` that decided: `dark_neutral` (below the
lightness floor: `unknown`), `neutral_white`, `warm_cast_white` or `hue_band`.

`assembly`, from the assembly step:

| Member | Meaning |
| --- | --- |
| `path` | `single_candidate` (the naming makes exactly one cube), `chosen_by_corners` (several, and corner reads singled one out), `ambiguous` (several, and they could not: `ambiguous_scan`), `direct`, `names_exchanged` (the naming made no cube; trading two names did) or `failed`. |
| `candidates`, `corner_fits`, `corner_reads`, `chosen` | How many cubes the naming makes; when several, how many corner reads fit each and which was chosen. |
| `direct_error` | Why assembling the naming as it stood failed, when it did. |
| `exchange` | The pair traded when `path` is `names_exchanged`: `first_side`, `second_side` (indices into `sides`), `first_name`, `second_name`, `centre_distance` (the closest-looking pair wins). |
| `error` | Why there is no cube. |

**`camera`** - the camera and its photometry lock. `data.event` is
`opened_lock_requested`, `opened_lock_refused`, `lock_reported`,
`lock_timed_out`, `open_failed` or `stopped`; each carries `device`,
`frame_width`, `frame_height`, `awaiting_lock`, `photometry_control_supported`
and `lock`: what the driver accepted, or `null` before it reported.

```json
{"platform_supported":true,"exposure_locked":true,"fully_applied":true,
 "auto_white_balance":{"outcome":"applied","requested":0,"readback":0},
 "exposure":{"outcome":"applied","requested":-6,"readback":-6},
 "brightness":{...},"saturation":{...},
 "white_balance_kelvin":4700,"summary":"Camera locked: ..."}
```

`outcome` is `applied` (read back and confirmed), `rejected` (the driver took
the write and kept its old value), `unsupported` or `not_requested`.

**`held`** - the solve screen's camera, each time what it makes of a frame
*changes* (this is what `CUBE_HELD_LOG` used to write): `verdict` (`nothing`,
`held`, `moved`, `unsure`, `wrong`, `unknown`), `move_index`, `asked` (the move
the solution expects), `shown_hold` and `verdict_hold` (`up`, `front`), `seen`,
`turned_instead`, `stickers_found`, the nine `found` flags and `lab` of the
read, and `as_it_stood` / `after_move` - each the Lab `distance` of the side in
view from the cube as it stood, and after the move, and how many stickers
(`stickers_off`) plainly disagree.

**`solve`** - when a solution lands (`event: "solution"`) and on every step
(`event: "state"`): `method`, `is_solving`, `move_index`, `total_moves`,
`current_move`, `moves` (the whole solution in standard notation), `cube` (the
state now), `started_from`, `solved`, `stage`.

**`snapshot`** - see below.

## Snapshot now

*Settings > Developer > Snapshot now*, or **Ctrl+Shift+S**, writes one record
of kind `snapshot` holding everything the controllers know at that moment, so
a bad result can be captured while it is on screen rather than reconstructed:

| Member | Holds |
| --- | --- |
| `scan` | The scan controller: running, watching, complete, conflicted, ambiguous, the frame size, the device, `lock` (the photometry report), `scan` (a full `scan` record's `data`, from `SideScan::diagnose()`: sides with their read counts and rolls, naming, assembly, result), `scanned_state` and `session_colours` once complete. |
| `solve` | The solve controller, as a `solve` record: method, the cube now, the whole move list, the index. |
| `held` | The camera's hold on the cube: `hold`, `seen`, `sight`, `wrong_side`. |

A snapshot works with the channel *off*: the file is opened for the one record
(and a `session` header with `event: "snapshot"`) and closed again, and nothing
else is switched on.

## Camera frames

The channel writes numbers and names. It **never writes a camera frame** unless
a separate switch is on - frames show whatever the camera sees, which can be a
person - and then the frames are written by the recorder that
[recording-format.md](recording-format.md) describes, unchanged:

- **Settings > Developer > Also keep camera frames** (it only takes effect
  while the channel is on), or `CUBE_DIAGNOSTICS_FRAMES=1` for the state the
  app starts in.
- Frames go to `frames/` beside the diagnostics file, one folder per scan, in
  that format. The scan screen's red recording marker is shown, as it is for
  `CUBE_SCAN_RECORD_DIR`.
- Switching the channel or the frames off puts the recorder's folder back as
  it was: a folder set by `CUBE_SCAN_RECORD_DIR` is untouched by all of this.

## What it costs

- **Off:** a pointer test and a boolean read at each place that writes. No
  allocation, no formatting, no I/O. The scan's evaluation is `scanned()`,
  exactly as without the channel.
- **On:** the scan is evaluated with `diagnose()` - the same evaluation, with
  its working kept - once per change of the captured sides, and the record is
  built only when its outcome differs from the last one written. The colour
  naming and assembly structs hold values those steps already computed: building
  them adds no computation, and `resolve_colors` is `resolve_colors_explained`
  with the rest dropped, not a second implementation.

## Privacy and the repository

The repository is public and a camera frame can show a person. So:

- The file is in a per-user directory outside the repository, by construction.
  It holds Lab values, cluster assignments and cube states, and the name of the
  camera device; no picture.
- Frames are a separate switch, off by default, and are never written by
  "Snapshot now".
- Nothing a diagnostics session produces is to be committed. `.gitignore`
  excludes `diagnostics.jsonl`, `diagnostics.previous.jsonl` and a `frames`
  folder under a `diagnostics` one, in case a developer points `HOME`,
  `XDG_STATE_HOME` or `LOCALAPPDATA` into a checkout.
- Nothing is uploaded: the channel has no network code.

## For contributors

| | |
| --- | --- |
| `cube/diagnostics/json.hpp` | A write-only JSON value, one line, ordered members, valid UTF-8 and finite numbers whatever goes in. |
| `cube/diagnostics/record.hpp` | The `seq`/`time`/`kind`/`data` envelope and the UTC timestamp. |
| `cube/diagnostics/jsonl_file.hpp` | The flushed, bounded, rotating file. |
| `cube/diagnostics/describe.hpp` | `to_json` for the library's result structs: `ColorResolution`, `AssemblyTrace`, `ScanDiagnosis`, the cube. |
| `cube/vision/color_resolution.hpp` | `resolve_colors_explained`, `classify_lab_explained`. |
| `cube/vision/direct_scan.hpp` | `SideScan::diagnose`, `AssemblyTrace`. |
| `qt/controllers/diagnostics.hpp` | The switch and the file's owner: `Diagnostics`, registered in QML as `Diagnostics`. |
| `interfaces/paths.hpp` | `AppPaths::diagnostics_dir`, defined once per platform. |

To add a record: build a `Json` where the thing happens, behind
`diagnostics->enabled()`, and call `write("<kind>", data)`. Do not log from
inside an algorithm; give the algorithm a result struct that holds what it
already computed and describe that (as `ColorResolution` does), so the
algorithm has one implementation and the channel cannot change what it does.
Tests: `tests/unit/diagnostics_test.cpp` (serialisation, file),
`tests/unit/color_resolution_test.cpp` (the explained resolution, including the
hue-collision fallback), `tests/unit/direct_scan_test.cpp` (`diagnose`),
`tests/unit/diagnostics_controller_test.cpp` (switching, records, snapshot).
