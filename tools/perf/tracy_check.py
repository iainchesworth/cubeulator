#!/usr/bin/env python3
"""Hold the per-zone cost of a profiling build to a checked-in baseline.

The optional, local-or-nightly half of the performance tier (docs/testing.md,
"Performance tests"). It builds nothing and CI does not run it: it takes a
profiling build of cube_perf_tests (windows-msvc-profiling / linux-gcc-profiling
/ macos-clang-profiling, i.e. -DCUBEULATOR_ENABLE_TRACY=ON), runs the perf
cases under tracy-capture, exports the zones with tracy-csvexport and compares
them with tools/perf/tracy_baseline.json.

What is compared, per zone:

  count   how many times the zone was entered. The workload is fixed, so this is
          exact; a different count means the instrumentation or the work moved,
          and the baseline is stale (or the change did more than it should).
  ratio   the zone's fastest call divided by the fastest call of the
          `perf.calibration` zone, which the binary records around its
          calibration workload. The machine's speed cancels, so a baseline
          taken on one machine is a baseline for another, within the
          tolerance. The fastest, not the mean: interference only adds time,
          so the fastest call is what the code cost, and a short zone's mean
          over a dozen calls on a busy machine is not stable to a factor of
          three. A zone slower than baseline * tolerance is a failure; one
          faster than baseline / tolerance is a note (the baseline can be
          tightened with --update).

Usage:

    cmake --preset windows-msvc-profiling
    cmake --build --preset windows-msvc-profiling --target cube_perf_tests
    python tools/perf/tracy_check.py --preset windows-msvc-profiling
    python tools/perf/tracy_check.py --preset windows-msvc-profiling --update

Exit status: 0 = within the baseline, 1 = a zone failed, 2 = could not run.
"""

from __future__ import annotations

import argparse
import csv
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_BASELINE = REPO / "tools" / "perf" / "tracy_baseline.json"
CALIBRATION_ZONE = "perf.calibration"
# A zone whose fastest call is under this fraction of the calibration's (about
# ten microseconds) is a timer's granularity and a cached fast path, not a
# cost: its count is still held, its time is not. solver.bfs_search is one -
# its fastest call is the search whose start already meets its goal.
MIN_TIMED_RATIO = 1e-3
# Zones the baseline covers: the library's layers. The app's (capture, UI,
# startup) need a window or a camera and are read from a capture of the app.
ZONE_PREFIXES = ("solver.", "vision.", "perf.")
# The cases the capture runs: the whole perf tier, wall-time cases included
# (they are what record the calibration zone).
DEFAULT_CASES = "[perf]"
SCHEMA = 1


# ---------------------------------------------------------------------------
# Parsing and comparing: no process, no file, so the self-test can drive them.
# ---------------------------------------------------------------------------


def parse_zones_csv(text: str) -> dict[str, dict[str, float]]:
    """Per-zone totals from `tracy-csvexport` output, merged by zone name.

    A zone in a template has one source location per instantiation, and
    csvexport reports each as its own row under the same name; the baseline is
    per name, so they are summed.
    """
    zones: dict[str, dict[str, float]] = {}
    for row in csv.DictReader(io.StringIO(text)):
        name = row["name"]
        entry = zones.setdefault(name, {"count": 0.0, "total_ns": 0.0, "min_ns": float("inf")})
        entry["count"] += float(row["counts"])
        entry["total_ns"] += float(row["total_ns"])
        entry["min_ns"] = min(entry["min_ns"], float(row["min_ns"]))
    return zones


def ratios_to_calibration(zones: dict[str, dict[str, float]]) -> dict[str, dict[str, float]]:
    """{zone: {count, ratio}} for the zones the baseline covers.

    Raises ValueError when there is no calibration zone to divide by.
    """
    calibration = zones.get(CALIBRATION_ZONE)
    if calibration is None or calibration["count"] == 0 or calibration["min_ns"] <= 0:
        raise ValueError(
            f"no '{CALIBRATION_ZONE}' zone in the capture: the wall-time cases did not run "
            "(they need CUBE_PERF_TIMING=1 and an optimised build)"
        )
    out: dict[str, dict[str, float]] = {}
    for name, entry in zones.items():
        if not name.startswith(ZONE_PREFIXES) or entry["count"] == 0:
            continue
        out[name] = {"count": entry["count"], "ratio": entry["min_ns"] / calibration["min_ns"]}
    return out


def compare(
    baseline: dict[str, dict[str, float]],
    current: dict[str, dict[str, float]],
    tolerance: float,
) -> tuple[list[str], list[str], list[str]]:
    """(failures, notes, table lines)."""
    failures: list[str] = []
    notes: list[str] = []
    table: list[str] = []
    header = f"{'zone':<40} {'count':>9} {'baseline':>9} {'ratio':>12} {'baseline':>12} {'x':>7}"
    table.append(header)

    for name in sorted(baseline):
        base = baseline[name]
        now = current.get(name)
        if now is None:
            failures.append(f"{name}: in the baseline but not in the capture (renamed or removed?)")
            table.append(f"{name:<40} {'-':>9} {int(base['count']):>9} {'-':>12} {base['ratio']:>12.4g}")
            continue
        factor = now["ratio"] / base["ratio"] if base["ratio"] > 0 else float("inf")
        verdict = ""
        timed = base["ratio"] >= MIN_TIMED_RATIO
        if int(now["count"]) != int(base["count"]):
            failures.append(
                f"{name}: entered {int(now['count'])} times, baseline {int(base['count'])} "
                "(the work or the instrumentation changed; re-baseline with --update if intended)"
            )
            verdict = "COUNT"
        elif not timed:
            verdict = "count only"
        elif factor > tolerance:
            failures.append(
                f"{name}: {factor:.1f}x its baseline ratio ({now['ratio']:.4g} against "
                f"{base['ratio']:.4g}; tolerance {tolerance:g}x)"
            )
            verdict = "SLOWER"
        elif factor < 1.0 / tolerance:
            notes.append(
                f"{name}: {1.0 / factor:.1f}x faster than its baseline; --update would tighten it"
            )
            verdict = "faster"
        table.append(
            f"{name:<40} {int(now['count']):>9} {int(base['count']):>9} "
            f"{now['ratio']:>12.4g} {base['ratio']:>12.4g} {factor:>7.2f}  {verdict}"
        )
    for name in sorted(set(current) - set(baseline)):
        notes.append(f"{name}: not in the baseline yet; --update adds it")
    return failures, notes, table


def baseline_document(current: dict[str, dict[str, float]], tolerance: float) -> dict:
    return {
        "schema": SCHEMA,
        "about": (
            "Per-zone cost of cube_perf_tests under Tracy: the zone's fastest call, as a "
            f"multiple of the {CALIBRATION_ZONE} zone's fastest call. Written by tools/perf/tracy_check.py --update; "
            "see docs/testing.md, 'Tracy: per-zone, local and nightly'."
        ),
        "workload": f"cube_perf_tests {DEFAULT_CASES} with CUBE_PERF_TIMING=1",
        "tolerance": tolerance,
        "zones": {
            name: {"count": int(entry["count"]), "ratio": float(f"{entry['ratio']:.4g}")}
            for name, entry in sorted(current.items())
        },
    }


# ---------------------------------------------------------------------------
# Running the capture
# ---------------------------------------------------------------------------


def find_tools(build_dir: Path, tools_dir: Path | None) -> tuple[Path, Path]:
    suffix = ".exe" if os.name == "nt" else ""
    candidates: list[Path] = []
    if tools_dir is not None:
        candidates.append(tools_dir)
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        text = cache.read_text(encoding="utf-8", errors="replace")
        installed = re.search(r"^VCPKG_INSTALLED_DIR:[A-Z]+=(.+)$", text, re.M)
        triplet = re.search(r"^VCPKG_TARGET_TRIPLET:[A-Z]+=(.+)$", text, re.M)
        if installed and triplet:
            candidates.append(Path(installed.group(1)) / triplet.group(1) / "tools" / "tracy")
    for triplet_dir in sorted((build_dir / "vcpkg_installed").glob("*")):
        candidates.append(triplet_dir / "tools" / "tracy")
    for directory in candidates:
        capture = directory / f"tracy-capture{suffix}"
        export = directory / f"tracy-csvexport{suffix}"
        if capture.is_file() and export.is_file():
            return capture, export
    searched = ", ".join(str(c) for c in candidates) or "nowhere"
    raise FileNotFoundError(
        "tracy-capture / tracy-csvexport not found (searched: " + searched + "); "
        "pass --tools-dir, or configure with -DCUBEULATOR_ENABLE_TRACY=ON so that vcpkg "
        "builds them"
    )


class UncleanCapture(RuntimeError):
    """The capture attached, but Tracy lost what some zones are called."""


def capture(
    exe: Path,
    capture_tool: Path,
    export_tool: Path,
    cases: str,
    keep: Path | None,
    settle_ms: int,
    drain_ms: int,
    port: int,
) -> str:
    """Runs `exe cases` under tracy-capture once; returns csvexport's output.

    Raises UncleanCapture when the export has zones with no name, which is
    Tracy's client not having answered the capture's queries before the
    capture ended (see tests/perf/profiler_listener.cpp). It happens on a busy
    machine, and a rerun is the answer.
    """
    with tempfile.TemporaryDirectory(prefix="cube-tracy-") as scratch:
        trace = Path(keep) if keep is not None else Path(scratch) / "perf.tracy"

        # The capture first: it retries until the program's client appears.
        # A port of our own (the client takes it from TRACY_PORT) because the
        # default, 8086, is every Tracy user on this machine's, and a capture
        # that attaches to someone else's program, or a program that is
        # attached to by someone else's capture, is a capture of nothing.
        grabber = subprocess.Popen(
            [str(capture_tool), "-o", str(trace), "-a", "127.0.0.1", "-p", str(port), "-f"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        env = dict(
            os.environ,
            CUBE_PERF_TIMING="1",
            TRACY_PORT=str(port),
            CUBE_PERF_PROFILER_SETTLE_MS=str(settle_ms),
            CUBE_PERF_PROFILER_DRAIN_MS=str(drain_ms),
        )
        program = subprocess.Popen([str(exe), cases], env=env, cwd=exe.parent)
        status = program.wait()
        try:
            grabber.wait(timeout=120)
        except subprocess.TimeoutExpired:
            grabber.kill()
            raise RuntimeError("tracy-capture did not finish after the program exited") from None
        if status != 0:
            raise RuntimeError(f"{exe.name} exited with status {status}")

        exported = subprocess.run(
            [str(export_tool), str(trace)], capture_output=True, text=True, check=False
        )
        if exported.returncode != 0:
            raise RuntimeError(f"tracy-csvexport failed: {exported.stderr.strip()}")
        nameless = [
            row["counts"] for row in csv.DictReader(io.StringIO(exported.stdout)) if row["name"] == "???"
        ]
        if nameless:
            raise UncleanCapture(
                f"{len(nameless)} zone(s) in the capture have no name (Tracy lost their strings)"
            )
        return exported.stdout


# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--preset", help="configure preset whose build tree to use (build/<preset>)")
    parser.add_argument("--build-dir", type=Path, help="build tree, if not build/<preset>")
    parser.add_argument("--exe", type=Path, help="cube_perf_tests, if not <build>/bin/cube_perf_tests")
    parser.add_argument("--tools-dir", type=Path, help="directory holding tracy-capture and csvexport")
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--cases", default=DEFAULT_CASES, help="Catch2 test spec to run")
    parser.add_argument("--tolerance", type=float, help="slower-than-baseline factor (default: the baseline's, 3)")
    parser.add_argument("--update", action="store_true", help="write the baseline from this capture")
    parser.add_argument("--keep-capture", type=Path, help="keep the .tracy file here")
    parser.add_argument(
        "--settle-ms", type=int, default=4000, help="let the capture attach this long first"
    )
    parser.add_argument(
        "--drain-ms", type=int, default=8000, help="let the client answer the capture this long after"
    )
    parser.add_argument("--port", type=int, help="Tracy port for this run (default: one of our own)")
    parser.add_argument("--attempts", type=int, default=4, help="captures to try before giving up")
    parser.add_argument("--from-csv", type=Path, help="compare a csvexport file; run nothing")
    args = parser.parse_args(argv)

    try:
        if args.from_csv is not None:
            csv_text = args.from_csv.read_text(encoding="utf-8")
        else:
            if args.build_dir is not None:
                build_dir = args.build_dir
            elif args.preset:
                build_dir = REPO / "build" / args.preset
            else:
                parser.error("one of --preset, --build-dir or --from-csv is needed")
            exe = args.exe or build_dir / "bin" / ("cube_perf_tests.exe" if os.name == "nt" else "cube_perf_tests")
            if not exe.is_file():
                raise FileNotFoundError(f"{exe} not built (cmake --build ... --target cube_perf_tests)")
            capture_tool, export_tool = find_tools(build_dir, args.tools_dir)
            started = time.monotonic()
            csv_text = ""
            for attempt in range(1, args.attempts + 1):
                port = args.port or 20000 + (os.getpid() + attempt * 7919) % 30000
                try:
                    csv_text = capture(
                        exe,
                        capture_tool,
                        export_tool,
                        args.cases,
                        args.keep_capture,
                        args.settle_ms,
                        args.drain_ms,
                        port,
                    )
                    break
                except UncleanCapture as error:
                    print(f"attempt {attempt}/{args.attempts}: {error}; trying again")
            else:
                raise RuntimeError(
                    f"{args.attempts} captures in a row lost zone names; the machine is too busy "
                    "for Tracy to answer in time (try --drain-ms 20000)"
                )
            print(f"captured in {time.monotonic() - started:.0f} s")
        current = ratios_to_calibration(parse_zones_csv(csv_text))
    except (OSError, RuntimeError, ValueError) as error:
        print(f"tracy_check: {error}", file=sys.stderr)
        return 2

    existing: dict = {}
    if args.baseline.is_file():
        existing = json.loads(args.baseline.read_text(encoding="utf-8"))
    tolerance = args.tolerance or float(existing.get("tolerance", 3.0))

    if args.update:
        document = baseline_document(current, tolerance)
        args.baseline.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {args.baseline} ({len(document['zones'])} zones, tolerance {tolerance:g}x)")
        return 0

    if not existing:
        print(f"tracy_check: no baseline at {args.baseline}; run with --update", file=sys.stderr)
        return 2
    failures, notes, table = compare(existing["zones"], current, tolerance)
    print("\n".join(table))
    for note in notes:
        print(f"note: {note}")
    for failure in failures:
        print(f"FAIL: {failure}")
    print(f"\n{len(failures)} failing zone(s), {len(notes)} note(s); tolerance {tolerance:g}x")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
