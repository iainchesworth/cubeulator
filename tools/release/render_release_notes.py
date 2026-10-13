#!/usr/bin/env python3
"""Render the body of a GitHub Release from CHANGELOG.md.

The changelog is the one place the notes are written (changelog.py describes the
section). This lays that section out the way every release of the family reads,
in this order:

    ## What's Changed since v<previous>      (the first release: "## What's Changed")

    <summary: the paragraph under the changelog heading>

    ### Added / Changed / Fixed / ...        (the changelog's own subsections)

    ## Installing                            (per platform, first-run notes)

    ## Artifacts                             (the "Platform | Files" table)

    **Full Changelog**: .../compare/v<previous>...v<this>     (not for the first)

    > **Pre-release.** ...                   (pre-releases only)

Relative links in the changelog (docs/...) are rewritten to the tagged tree, so
they work on the Releases page. What the Artifacts table says was done (signed,
attested, SBOM) is what the caller says was done, never assumed.

    render_release_notes.py --version 0.1.0-beta.1 --repo owner/name \\
        --artifacts release-artifacts --signed --attested --sbom --output notes.md

Standard library only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changelog  # noqa: E402

HERE = Path(__file__).resolve().parent

# (suffix, row label), in the order the table lists them. The file names are
# CPack's (cmake/Packaging.cmake); only the suffix is relied on here.
PLATFORMS: tuple[tuple[str, str], ...] = (
    (".exe", "Windows 10 and later, 64-bit"),
    (".dmg", "macOS 14.4 and later, Apple Silicon"),
    (".deb", "Linux, Debian and Ubuntu"),
    (".rpm", "Linux, Fedora and openSUSE"),
)

_RELATIVE_LINK_RE = re.compile(r"\]\((?!https?://|mailto:|#|/)([^)\s]+)\)")


class NotesError(RuntimeError):
    """The notes cannot be rendered; the message says what to fix."""


def expected_names(version: str) -> list[str]:
    """The package names CPack produces, for a preview with no artifacts yet."""
    return [
        f"Cubeulator-{version}-windows-x64.exe",
        f"Cubeulator-{version}-macos-arm64.dmg",
        f"Cubeulator-{version}-linux-x64.deb",
        f"Cubeulator-{version}-linux-x64.rpm",
    ]


def absolutise_links(text: str, repo: str, tag: str) -> str:
    """docs/x.md -> the same file on GitHub at the tag."""
    base = f"https://github.com/{repo}/blob/{tag}/"
    return _RELATIVE_LINK_RE.sub(lambda m: f"]({base}{m.group(1)})", text)


def _files_for(names: list[str], suffix: str) -> list[str]:
    return sorted(n for n in names if n.lower().endswith(suffix))


def install_section(template: str, names: list[str], version: str) -> str:
    """The template with the real package names in it."""
    fallback = dict(zip((s for s, _ in PLATFORMS), expected_names(version)))
    filled = template
    for token, suffix in (("@WINDOWS@", ".exe"), ("@MACOS@", ".dmg"), ("@DEB@", ".deb"), ("@RPM@", ".rpm")):
        files = _files_for(names, suffix)
        filled = filled.replace(token, files[0] if files else fallback[suffix])
    return filled.strip("\n")


def artifacts_section(
    names: list[str], *, repo: str, tag: str, signed: bool, attested: bool, sbom: bool
) -> str:
    proofs: list[str] = []
    if attested:
        proofs.append("build-provenance attestations")
    if sbom:
        proofs.append("an SPDX SBOM")
    if signed:
        proofs.append("GPG signatures")
    if proofs:
        intro = "Self-contained, SHA-512-summed, with " + ", ".join(proofs[:-1])
        intro += (" and " if len(proofs) > 1 else "") + proofs[-1] + ":"
    else:
        intro = "Self-contained and SHA-512-summed:"

    rows = ["| Platform | Files |", "|---|---|"]
    for suffix, label in PLATFORMS:
        files = _files_for(names, suffix)
        if files:
            rows.append(f"| {label} | " + " · ".join(f"`{f}`" for f in files) + " |")

    checks: list[str] = []
    if attested:
        checks.append(f"`gh attestation verify <file> --repo {repo}`")
    checks.append("`SHA512SUMS`" + ("(`.asc`) + per-file `.asc`" if signed else ""))
    rows.append("| Verification | " + "; ".join(checks) + " |")

    readme = f"https://github.com/{repo}/blob/{tag}/README.md#install"
    return "\n".join(
        [
            "## Artifacts",
            "",
            intro,
            "",
            *rows,
            "",
            f"Full install instructions: [README]({readme}).",
        ]
    )


def previous_tag_from(text: str, version: str) -> str | None:
    """The release below `version` in the changelog, as a tag; None if it is the first."""
    released = [s for s in changelog.sections(text) if s.version != "Unreleased"]
    for index, section in enumerate(released):
        if section.version == version:
            if index + 1 < len(released):
                return "v" + released[index + 1].version
            return None
    return None


def render(
    changelog_text: str,
    *,
    version: str,
    tag: str,
    repo: str,
    previous_tag: str | None = None,
    names: list[str] | None = None,
    signed: bool = False,
    attested: bool = False,
    sbom: bool = False,
    install_template: str = "",
) -> str:
    section = changelog.get(changelog_text, version)
    if section is None:
        raise NotesError(
            f"no CHANGELOG.md section '## [{version}]': cut it from [Unreleased] before "
            "tagging (tools/release/prepare_release.py, docs/releasing.md)"
        )
    problems = changelog.validate(changelog_text, version)
    if problems:
        raise NotesError("CHANGELOG.md is not ready to release:\n  - " + "\n  - ".join(problems))
    body = changelog.split_body(section.body)

    if previous_tag is None:
        previous_tag = previous_tag_from(changelog_text, version)
    names = names if names is not None else expected_names(version)

    parts: list[str] = []
    parts.append(f"## What's Changed since {previous_tag}" if previous_tag else "## What's Changed")
    if body.intro:
        parts.append(body.intro)
    for title, content in body.subsections:
        parts.append(f"### {title}\n\n{content}")
    if install_template.strip():
        parts.append(install_section(install_template, names, version))
    parts.append(
        artifacts_section(names, repo=repo, tag=tag, signed=signed, attested=attested, sbom=sbom)
    )
    if previous_tag:
        parts.append(
            f"**Full Changelog**: https://github.com/{repo}/compare/{previous_tag}...{tag}"
        )
    if body.caveat:
        parts.append(body.caveat)

    return absolutise_links("\n\n".join(parts), repo, tag) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--changelog", type=Path, default=Path("CHANGELOG.md"))
    parser.add_argument("--version", required=True, help="bare version, e.g. 0.1.0-beta.1")
    parser.add_argument("--tag", help="full tag; default v<version>")
    parser.add_argument("--repo", required=True, help="owner/repo")
    parser.add_argument(
        "--previous-tag",
        help="the tag the compare link starts from; default the section below this one "
        "in the changelog, 'none' for a first release",
    )
    parser.add_argument("--artifacts", type=Path, help="directory holding the release files")
    parser.add_argument("--signed", action="store_true", help="GPG signatures are attached")
    parser.add_argument("--attested", action="store_true", help="provenance attestations exist")
    parser.add_argument("--sbom", action="store_true", help="an SPDX SBOM is attached")
    parser.add_argument("--install-notes", type=Path, default=HERE / "install-notes.md")
    parser.add_argument("--output", type=Path, help="write here instead of stdout")
    args = parser.parse_args(argv)

    tag = args.tag or f"v{args.version}"
    previous = None if args.previous_tag in (None, "none") else args.previous_tag
    if args.previous_tag == "none":
        previous = ""  # explicit: a first release, whatever the changelog holds below

    names: list[str] | None = None
    if args.artifacts is not None:
        names = sorted(p.name for p in args.artifacts.iterdir() if p.is_file())
        if not any(n.lower().endswith(s) for n in names for s, _ in PLATFORMS):
            print(f"error: no package found in {args.artifacts}", file=sys.stderr)
            return 1

    text = changelog.normalise(args.changelog.read_text(encoding="utf-8"))
    template = args.install_notes.read_text(encoding="utf-8") if args.install_notes.exists() else ""
    try:
        notes = render(
            text,
            version=args.version,
            tag=tag,
            repo=args.repo,
            previous_tag=previous,
            names=names,
            signed=args.signed,
            attested=args.attested,
            sbom=args.sbom,
            install_template=template,
        )
    except (NotesError, changelog.ChangelogError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.output is not None:
        args.output.write_text(notes, encoding="utf-8", newline="\n")
    else:
        sys.stdout.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
        sys.stdout.write(notes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
