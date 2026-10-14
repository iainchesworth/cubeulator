#!/usr/bin/env python3
"""Changelog fragments: one file per change, assembled when a release is cut.

Many pull requests open at once, from several agents and people, each wanting a
line in `## [Unreleased]`. Edited in place, that one block is a conflict for
every pair of them, and the loser has to rebase to add a sentence. So a change
adds a file of its own instead:

    changelog.d/<slug>.<type>.md        <type>: added changed deprecated removed fixed security

holding one item, written the way an item is written in CHANGELOG.md, without
the leading dash:

    **The scan names the side you turned.** When the camera sees a different
    turn from the one asked for, the screen says which side it was.

When a release is prepared (prepare_release.py) the fragments are merged into
`[Unreleased]` under their `###` subsections, the section is cut as the
release, and the fragment files are deleted. Until then `[Unreleased]` is
untouched, so what is in the file stays what a person wrote by hand.

    fragments.py check [--root DIR]      the fragments in DIR/changelog.d are well formed

Standard library only.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changelog  # noqa: E402

DIRECTORY = "changelog.d"

# The order Keep a Changelog lists them in, which is also changelog.SUBSECTION_TITLES.
KINDS = tuple(t.lower() for t in changelog.SUBSECTION_TITLES)

_NAME_RE = re.compile(r"^([A-Za-z0-9][A-Za-z0-9._-]*)\.(" + "|".join(KINDS) + r")\.md$")


@dataclass(frozen=True)
class Fragment:
    name: str  # the file name
    kind: str  # "added", ...
    text: str  # the item, as written in the file

    @property
    def title(self) -> str:
        return self.kind.capitalize()


def problems_in(name: str, text: str) -> list[str]:
    """What is wrong with one fragment, by its file name and its content."""
    found: list[str] = []
    if _NAME_RE.match(name) is None:
        found.append(
            f"{name}: the name must be <slug>.<type>.md, with <type> one of {', '.join(KINDS)}"
        )
    body = changelog.normalise(text).strip("\n")
    if not body:
        found.append(f"{name}: is empty")
        return found
    if not body.startswith("**"):
        found.append(f"{name}: must open with a **bold lead-in** naming the change")
    if body.startswith(("- ", "* ")):
        found.append(f"{name}: write the item without its leading dash; the dash is added")
    if "\n\n" in body:
        found.append(f"{name}: is one item, so one paragraph: it has a blank line in it")
    if re.search(r"^#{1,6} ", body, re.M):
        found.append(f"{name}: has a heading in it; a fragment is a single list item")
    return found


def read(directory: Path) -> tuple[list[Fragment], list[str]]:
    """The fragments in `directory` (README.md and dotfiles excepted) and every problem."""
    fragments: list[Fragment] = []
    problems: list[str] = []
    if not directory.is_dir():
        return fragments, problems
    for path in sorted(directory.iterdir()):
        if not path.is_file() or path.name.startswith(".") or path.name.lower() == "readme.md":
            continue
        text = path.read_text(encoding="utf-8")
        problems.extend(problems_in(path.name, text))
        m = _NAME_RE.match(path.name)
        if m is not None:
            fragments.append(Fragment(path.name, m.group(2), changelog.normalise(text).strip("\n")))
    order = {kind: i for i, kind in enumerate(KINDS)}
    fragments.sort(key=lambda f: (order[f.kind], f.name))
    return fragments, problems


def _item(text: str) -> str:
    """A fragment as a markdown list item: a dash, then continuation lines indented."""
    first, *rest = text.split("\n")
    return "\n".join(["- " + first, *("  " + line.strip() for line in rest)])


def merge(changelog_text: str, fragments: list[Fragment]) -> str:
    """`[Unreleased]` with each fragment added to the end of its subsection."""
    text = changelog.normalise(changelog_text)
    if not fragments:
        return text
    unreleased = changelog.get(text, "Unreleased")
    if unreleased is None:
        raise changelog.ChangelogError("CHANGELOG.md has no '## [Unreleased]' section")
    body = changelog.split_body(unreleased.body)

    subsections = {title: content for title, content in body.subsections}
    for fragment in fragments:
        existing = subsections.get(fragment.title, "").rstrip("\n")
        item = _item(fragment.text)
        subsections[fragment.title] = (existing + "\n" + item) if existing else item

    rank = {title: i for i, title in enumerate(changelog.SUBSECTION_TITLES)}
    ordered = sorted(subsections.items(), key=lambda kv: rank.get(kv[0], len(rank)))
    parts: list[str] = []
    if body.intro:
        parts.append(body.intro)
    parts.extend(f"### {title}\n\n{content}" for title, content in ordered)
    if body.caveat:
        parts.append(body.caveat)
    new_body = "\n\n".join(parts)

    head = text[: unreleased.start]
    tail = text[unreleased.end :]
    merged = f"{head}## [Unreleased]\n\n{new_body}\n"
    if tail:
        merged += "\n" + tail
    return merged


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("command", choices=["check"])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)

    fragments, problems = read(args.root / DIRECTORY)
    for problem in problems:
        print(f"error: {problem}", file=sys.stderr)
    print(f"{len(fragments)} fragment(s) in {DIRECTORY}/", end="")
    print("" if not problems else f", {len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
