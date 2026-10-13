#!/usr/bin/env python3
"""Is this tree, and this tag, a release the Release workflow may build?

    check_release.py                          # what version is this tree, and is its changelog ready?
    check_release.py --tag v0.1.0-beta.1      # ... and does the tag name it?
    check_release.py --dry-run                # a section still in [Unreleased] is enough
    check_release.py --github-output "$GITHUB_OUTPUT"

The version is the one CMakeLists.txt declares: `project(VERSION M.M.P)` and
`CUBEULATOR_VERSION_PRERELEASE` (tools/release/prepare_release.py sets both).
The checks, each with the reason it exists:

  * the tag is `v` + that version, exactly, label included: the packages are
    named for it, so a tag that disagrees would publish v0.1.0 holding
    Cubeulator-0.1.0-beta.1-* (or the reverse);
  * CHANGELOG.md has a dated `## [version]` section that is the newest one, with
    a summary, bold lead-ins and (pre-release) the closing caveat: the release
    notes are rendered from it and nothing else.

A dry run has no section yet when it is run before the release is prepared, so
there `[Unreleased]` is checked as if it had been cut: the same rules, and the
notes that come out are the ones the release would carry.

Standard library only. Exit 0: ready. Exit 1: the problems are on stderr.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changelog  # noqa: E402
import prepare_release  # noqa: E402


@dataclass
class Result:
    version: str | None = None
    tag: str | None = None
    prerelease: bool = False
    from_unreleased: bool = False
    problems: list[str] = field(default_factory=list)


def check(root: Path, tag: str | None = None, dry_run: bool = False) -> Result:
    result = Result()
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    version = prepare_release.full_version(cmake)
    if version is None:
        result.problems.append("cannot read project(Cubeulator VERSION x.y.z) from CMakeLists.txt")
        return result
    if prepare_release.project_prerelease(cmake) is None:
        result.problems.append('CMakeLists.txt has no set(CUBEULATOR_VERSION_PRERELEASE "...")')
        return result
    try:
        changelog.parse_version(version)
    except changelog.ChangelogError as exc:
        result.problems.append(f"CMakeLists.txt: {exc}")
        return result

    result.version = version
    result.tag = f"v{version}"
    result.prerelease = changelog.is_prerelease(version)

    if tag is not None and tag != result.tag:
        result.problems.append(
            f"the tag is {tag} but CMakeLists.txt says {version} (tag {result.tag}). "
            "Run tools/release/prepare_release.py for the version you mean, then tag the "
            "commit that has it."
        )

    text = changelog.normalise((root / "CHANGELOG.md").read_text(encoding="utf-8"))
    if dry_run and changelog.get(text, version) is None:
        try:
            text = changelog.cut(text, version, date.today())
            result.from_unreleased = True
        except changelog.ChangelogError as exc:
            result.problems.append(f"CHANGELOG.md: {exc}")
            return result
    result.problems.extend(f"CHANGELOG.md: {p}" for p in changelog.validate(text, version))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--root", type=Path, default=prepare_release.ROOT)
    parser.add_argument("--tag", help="the tag being released, e.g. v0.1.0-beta.1")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--github-output", type=Path, help="append version/tag/prerelease here")
    args = parser.parse_args(argv)

    result = check(args.root, args.tag, args.dry_run)
    for problem in result.problems:
        print(f"error: {problem}", file=sys.stderr)
    if result.version is not None:
        print(f"version     {result.version}")
        print(f"tag         {result.tag}")
        print(f"pre-release {str(result.prerelease).lower()}")
        if result.from_unreleased:
            print("changelog   [Unreleased] (not cut yet; a dry run reads it as if it were)")
    if args.github_output is not None and result.version is not None:
        with args.github_output.open("a", encoding="utf-8", newline="\n") as out:
            out.write(f"version={result.version}\n")
            out.write(f"tag={result.tag}\n")
            out.write(f"prerelease={str(result.prerelease).lower()}\n")
            out.write(f"from_unreleased={str(result.from_unreleased).lower()}\n")
    return 1 if result.problems else 0


if __name__ == "__main__":
    sys.exit(main())
