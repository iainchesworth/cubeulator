#!/usr/bin/env python3
"""Read, check and cut CHANGELOG.md.

The file follows Keep a Changelog, with the conventions the other projects of
the family use (docs/releasing.md):

    ## [Unreleased]

    ## [0.1.0-beta.1] - 2026-10-14

    One paragraph saying what the release is. It becomes the summary line of the
    release notes.

    ### Added

    - **Bold lead-in naming the change.** Then why, in user-facing words.

    ### Changed
    ### Fixed
    ### Removed
    ### Security

    > **Pre-release.** What is not yet proven. Pre-releases only.

A release is cut by moving `[Unreleased]` down under a dated heading; the notes
of the GitHub Release are then rendered from that section
(render_release_notes.py), so the changelog is written once.

Standard library only.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from datetime import date

# vMAJOR.MINOR.PATCH[-(alpha|beta|rc).N]: the shape docs/releasing.md allows.
VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-(alpha|beta|rc)\.([1-9][0-9]*))?$"
)

SUBSECTION_TITLES = ("Added", "Changed", "Deprecated", "Removed", "Fixed", "Security")

CAVEAT_MARKER = "> **Pre-release.**"

_HEADING_RE = re.compile(r"^## \[([^\]]+)\](?: - ([0-9]{4}-[0-9]{2}-[0-9]{2}))?[ \t]*$")
_LABEL_RANK = {"alpha": 0, "beta": 1, "rc": 2}


class ChangelogError(ValueError):
    """The changelog cannot be read or cut as asked."""


@dataclass(frozen=True)
class Section:
    """One `## [x]` section. `body` has no leading or trailing blank lines."""

    version: str  # "Unreleased" or "0.1.0-beta.1"
    date: str | None
    body: str
    start: int  # offset of the heading line
    end: int  # offset of the next heading, or the end of the text


@dataclass(frozen=True)
class Body:
    """A section's body, taken apart the way the release notes are laid out."""

    intro: str
    subsections: tuple[tuple[str, str], ...]  # (title, content) in file order
    caveat: str | None  # the trailing "> **Pre-release.** ..." block, if any


def normalise(text: str) -> str:
    """LF line endings, so offsets and regexes do not depend on the checkout."""
    return text.replace("\r\n", "\n").replace("\r", "\n")


def is_prerelease(version: str) -> bool:
    return "-" in version


def parse_version(version: str) -> tuple[int, int, int, int, int]:
    """A sort key: a pre-release sorts before its final release."""
    m = VERSION_RE.match(version)
    if m is None:
        raise ChangelogError(
            f"'{version}' is not M.M.P or M.M.P-(alpha|beta|rc).N (docs/releasing.md)"
        )
    major, minor, patch, label, number = m.groups()
    if label is None:
        return (int(major), int(minor), int(patch), 99, 0)
    return (int(major), int(minor), int(patch), _LABEL_RANK[label], int(number))


def _lines_outside_fences(text: str):
    """(offset, line) for every line that is not inside a fenced code block."""
    offset = 0
    fenced = False
    for line in text.split("\n"):
        stripped = line.lstrip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            fenced = not fenced
        elif not fenced:
            yield offset, line
        offset += len(line) + 1


def sections(text: str) -> list[Section]:
    """Every `## [x]` section, in file order."""
    text = normalise(text)
    heads: list[tuple[int, int, str, str | None]] = []  # (start, body_start, version, date)
    for offset, line in _lines_outside_fences(text):
        m = _HEADING_RE.match(line)
        if m is not None:
            heads.append((offset, offset + len(line) + 1, m.group(1), m.group(2)))
    found: list[Section] = []
    for index, (start, body_start, version, when) in enumerate(heads):
        end = heads[index + 1][0] if index + 1 < len(heads) else len(text)
        found.append(Section(version, when, text[body_start:end].strip("\n"), start, end))
    return found


def get(text: str, version: str) -> Section | None:
    for section in sections(text):
        if section.version == version:
            return section
    return None


def split_body(body: str) -> Body:
    """Intro paragraph, `###` subsections and the pre-release caveat."""
    body = normalise(body).strip("\n")

    # The caveat is the blockquote that closes the section: the trailing run of
    # lines starting with ">" , when its first line is the pre-release marker.
    caveat: str | None = None
    lines = body.split("\n")
    first = len(lines)
    while first > 0 and lines[first - 1].startswith(">"):
        first -= 1
    if first < len(lines) and lines[first].startswith(CAVEAT_MARKER):
        caveat = "\n".join(lines[first:])
        body = "\n".join(lines[:first]).strip("\n")

    heads: list[tuple[int, int, str]] = []
    for offset, line in _lines_outside_fences(body):
        if line.startswith("### "):
            heads.append((offset, offset + len(line) + 1, line[4:].strip()))
    if not heads:
        return Body(body.strip("\n"), (), caveat)
    intro = body[: heads[0][0]].strip("\n")
    subs: list[tuple[str, str]] = []
    for index, (_, content_start, title) in enumerate(heads):
        end = heads[index + 1][0] if index + 1 < len(heads) else len(body)
        subs.append((title, body[content_start:end].strip("\n")))
    return Body(intro, tuple(subs), caveat)


def cut(text: str, version: str, on: date) -> str:
    """Move `[Unreleased]` down under `## [version] - date`; leave it empty."""
    parse_version(version)
    text = normalise(text)
    if get(text, version) is not None:
        raise ChangelogError(f"CHANGELOG.md already has a section for {version}")
    unreleased = get(text, "Unreleased")
    if unreleased is None:
        raise ChangelogError("CHANGELOG.md has no '## [Unreleased]' section to cut from")
    if not unreleased.body.strip():
        raise ChangelogError("'## [Unreleased]' is empty: there is nothing to release")
    newer = [s for s in sections(text) if s.version != "Unreleased"]
    if newer and parse_version(newer[0].version) >= parse_version(version):
        raise ChangelogError(
            f"{version} is not newer than the latest released section, {newer[0].version}"
        )
    head = text[: unreleased.start]
    tail = text[unreleased.end :]
    cut_text = f"{head}## [Unreleased]\n\n## [{version}] - {on.isoformat()}\n\n{unreleased.body}\n"
    if tail:
        cut_text += "\n" + tail
    return cut_text


def _bullets(content: str) -> list[str]:
    """Top-level list items of a subsection (continuation lines are indented)."""
    items: list[str] = []
    for _, line in _lines_outside_fences(content):
        if re.match(r"^[-*] ", line):
            items.append(line[2:].lstrip())
    return items


def validate(text: str, version: str | None = None) -> list[str]:
    """Every problem found, as one line each; empty when the file is sound.

    Structure is checked everywhere. Content rules (a summary, bold lead-ins,
    the pre-release caveat) apply to `version`, the section being released:
    older sections were written before them and are not re-litigated.
    """
    problems: list[str] = []
    found = sections(normalise(text))

    if not found or found[0].version != "Unreleased":
        problems.append("the first section must be '## [Unreleased]'")

    seen: set[str] = set()
    previous_key: tuple[int, int, int, int, int] | None = None
    previous_date: str | None = None
    for section in found:
        if section.version == "Unreleased":
            if section.date is not None:
                problems.append("'## [Unreleased]' has a date")
            continue
        label = f"## [{section.version}]"
        try:
            key = parse_version(section.version)
        except ChangelogError as exc:
            problems.append(f"{label}: {exc}")
            continue
        if section.version in seen:
            problems.append(f"{label}: appears twice")
        seen.add(section.version)
        if section.date is None:
            problems.append(f"{label}: has no ' - YYYY-MM-DD' date")
        else:
            try:
                date.fromisoformat(section.date)
            except ValueError:
                problems.append(f"{label}: '{section.date}' is not a date")
        if previous_key is not None and key >= previous_key:
            problems.append(f"{label}: sections must run newest first")
        if previous_date and section.date and section.date > previous_date:
            problems.append(f"{label}: is dated after the section above it")
        previous_key = key
        previous_date = section.date or previous_date
        for title, _ in split_body(section.body).subsections:
            if title not in SUBSECTION_TITLES:
                problems.append(
                    f"{label}: '### {title}' is not one of {', '.join(SUBSECTION_TITLES)}"
                )

    if version is None:
        return problems

    target = get(text, version)
    if target is None:
        problems.append(f"no '## [{version}]' section (cut it from [Unreleased] first)")
        return problems
    label = f"## [{version}]"
    released = [s for s in found if s.version != "Unreleased"]
    if released and released[0].version != version:
        problems.append(f"{label}: is not the newest released section ({released[0].version} is)")
    if target.date is None:
        problems.append(f"{label}: has no date")
    body = split_body(target.body)
    if not body.subsections:
        problems.append(f"{label}: has no '### Added/Changed/...' subsection")
    if not body.intro.strip():
        problems.append(
            f"{label}: has no summary; write one paragraph between the heading and the first '###'"
        )
    for title, content in body.subsections:
        items = _bullets(content)
        if not items:
            problems.append(f"{label}: '### {title}' has no list items")
        for item in items:
            if not item.startswith("**"):
                problems.append(
                    f"{label}: '### {title}': item does not start with a bold lead-in: "
                    f"'{item[:60]}'"
                )
    if is_prerelease(version) and body.caveat is None:
        problems.append(
            f"{label}: a pre-release needs a closing '{CAVEAT_MARKER} ...' blockquote "
            "saying what is not yet proven"
        )
    return problems
