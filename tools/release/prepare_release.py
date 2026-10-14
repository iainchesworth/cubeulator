#!/usr/bin/env python3
"""Prepare a release on develop: cut the changelog and set the version.

    prepare_release.py 0.1.0-beta.1                 # edits CHANGELOG.md, CMakeLists.txt
    prepare_release.py 0.1.0-beta.1 --date 2026-10-20
    prepare_release.py 0.1.0-beta.1 --check         # change nothing; say what is wrong

What it does, and nothing else (docs/releasing.md, "Preparing the release"):

  * merges the fragments in changelog.d/ into `## [Unreleased]` (and deletes
    them), then moves it in CHANGELOG.md under `## [<version>] - <date>`,
    leaving `[Unreleased]` empty;
  * sets `project(Cubeulator VERSION M.M.P ...)` and
    `CUBEULATOR_VERSION_PRERELEASE` in the top-level CMakeLists.txt;
  * then validates the result the way the Release workflow will, and says so if
    the section still needs work (the summary paragraph, bold lead-ins, the
    pre-release caveat).

It writes both files or neither. It never commits, tags or pushes: the result is
a diff for a `release/<version>` branch and a pull request into develop.

Standard library only.
"""

from __future__ import annotations

import argparse
import re
import sys
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changelog  # noqa: E402
import fragments  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]

_PROJECT_VERSION_RE = re.compile(
    r"(project\(Cubeulator\s+VERSION\s+)([0-9]+\.[0-9]+\.[0-9]+)", re.MULTILINE
)
_PRERELEASE_RE = re.compile(
    r'^(set\(CUBEULATOR_VERSION_PRERELEASE\s+")([^"]*)("[^)]*\))', re.MULTILINE
)


def project_version(cmake_text: str) -> str | None:
    m = _PROJECT_VERSION_RE.search(cmake_text)
    return m.group(2) if m else None


def project_prerelease(cmake_text: str) -> str | None:
    m = _PRERELEASE_RE.search(cmake_text)
    return m.group(2) if m else None


def full_version(cmake_text: str) -> str | None:
    """M.M.P, or M.M.P-label.N: what CMakeLists.txt says the version is."""
    core = project_version(cmake_text)
    if core is None:
        return None
    pre = project_prerelease(cmake_text)
    return f"{core}-{pre}" if pre else core


def set_version(cmake_text: str, version: str) -> str:
    changelog.parse_version(version)
    core, _, pre = version.partition("-")
    if _PROJECT_VERSION_RE.search(cmake_text) is None:
        raise changelog.ChangelogError("no project(Cubeulator VERSION x.y.z) in CMakeLists.txt")
    if _PRERELEASE_RE.search(cmake_text) is None:
        raise changelog.ChangelogError(
            'no set(CUBEULATOR_VERSION_PRERELEASE "...") in CMakeLists.txt'
        )
    out = _PROJECT_VERSION_RE.sub(lambda m: m.group(1) + core, cmake_text, count=1)
    return _PRERELEASE_RE.sub(lambda m: m.group(1) + pre + m.group(3), out, count=1)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("version", help="M.M.P or M.M.P-(alpha|beta|rc).N, no leading v")
    parser.add_argument("--date", type=date.fromisoformat, default=date.today())
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--check", action="store_true", help="change nothing")
    args = parser.parse_args(argv)

    changelog_path = args.root / "CHANGELOG.md"
    cmake_path = args.root / "CMakeLists.txt"
    # newline="" keeps each file's own line endings (core.autocrlf checkouts).
    log_raw = changelog_path.read_text(encoding="utf-8", newline="")
    cmake_raw = cmake_path.read_text(encoding="utf-8", newline="")
    log_eol = "\r\n" if "\r\n" in log_raw else "\n"
    cmake_eol = "\r\n" if "\r\n" in cmake_raw else "\n"

    # Changes arrive as files in changelog.d/ (fragments.py); they join
    # [Unreleased] here, and are removed once the release holds them.
    found, frag_problems = fragments.read(args.root / fragments.DIRECTORY)
    if frag_problems:
        for problem in frag_problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1

    try:
        cut_text = changelog.cut(fragments.merge(log_raw, found), args.version, args.date)
        new_cmake = set_version(cmake_raw.replace("\r\n", "\n"), args.version)
    except changelog.ChangelogError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    problems = changelog.validate(cut_text, args.version)
    for problem in problems:
        print(f"needs work: {problem}", file=sys.stderr)

    if args.check:
        print(f"{args.version}: would cut CHANGELOG.md and set the version in CMakeLists.txt")
        return 1 if problems else 0

    changelog_path.write_text(cut_text.replace("\n", log_eol), encoding="utf-8", newline="")
    cmake_path.write_text(new_cmake.replace("\n", cmake_eol), encoding="utf-8", newline="")
    for fragment in found:
        (args.root / fragments.DIRECTORY / fragment.name).unlink()
    merged = f", with {len(found)} fragment(s) from {fragments.DIRECTORY}/" if found else ""
    print(f"CHANGELOG.md: [Unreleased] cut as [{args.version}] - {args.date.isoformat()}{merged}")
    print(f"CMakeLists.txt: version {args.version}")
    if problems:
        print("The section is cut; fix the points above, then re-run with --check.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
