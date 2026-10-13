#!/usr/bin/env python3
"""The release tools' own tests (CTest case release_tools_selftest).

They hold the changelog's shape, the cut, the notes' layout and the version edit
to what docs/releasing.md says, so that the first thing to find out the tools are
wrong is not a tag.
"""

from __future__ import annotations

import sys
import tempfile
import textwrap
import unittest
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changelog  # noqa: E402
import prepare_release  # noqa: E402
import render_release_notes as notes  # noqa: E402

GOOD = textwrap.dedent(
    """\
    # Changelog

    Preamble.

    ## [Unreleased]

    ## [0.2.0-beta.1] - 2026-11-02

    Scanning reads a cube on a poor webcam.

    ### Added

    - **A new scan.** Reads the cube straight off the camera. See
      [docs/vision-pipeline.md](docs/vision-pipeline.md).

    ### Fixed

    - **The solver no longer stalls.** It finishes in seconds.

    > **Pre-release.** Not yet proven on every webcam.

    ## [0.1.0] - 2026-10-20

    ### Added

    - **First.** The first release.
    """
)

UNRELEASED = textwrap.dedent(
    """\
    # Changelog

    ## [Unreleased]

    A summary.

    ### Added

    - **Something.** It does a thing.

    > **Pre-release.** Unproven.
    """
)


class Versions(unittest.TestCase):
    def test_a_prerelease_sorts_before_its_release(self):
        order = ["0.1.0-alpha.1", "0.1.0-beta.1", "0.1.0-beta.2", "0.1.0-rc.1", "0.1.0", "0.1.1"]
        keys = [changelog.parse_version(v) for v in order]
        self.assertEqual(keys, sorted(keys))

    def test_the_shape_is_strict(self):
        for bad in ("1.0", "v1.0.0", "1.0.0-beta", "1.0.0-dev.1", "01.0.0", "1.0.0-beta.0"):
            with self.assertRaises(changelog.ChangelogError, msg=bad):
                changelog.parse_version(bad)


class Sections(unittest.TestCase):
    def test_the_sections_are_found_in_order(self):
        found = changelog.sections(GOOD)
        self.assertEqual([s.version for s in found], ["Unreleased", "0.2.0-beta.1", "0.1.0"])
        self.assertEqual(found[1].date, "2026-11-02")

    def test_a_heading_inside_a_code_fence_is_not_a_section(self):
        text = "## [Unreleased]\n\n```\n## [9.9.9] - 2026-01-01\n```\n"
        self.assertEqual([s.version for s in changelog.sections(text)], ["Unreleased"])

    def test_a_body_is_taken_apart(self):
        body = changelog.split_body(changelog.get(GOOD, "0.2.0-beta.1").body)
        self.assertEqual(body.intro, "Scanning reads a cube on a poor webcam.")
        self.assertEqual([t for t, _ in body.subsections], ["Added", "Fixed"])
        self.assertEqual(body.caveat, "> **Pre-release.** Not yet proven on every webcam.")
        self.assertNotIn("Pre-release", body.subsections[-1][1])

    def test_a_blockquote_that_is_not_the_caveat_stays_in_the_text(self):
        body = changelog.split_body("### Added\n\n- **A.** b\n\n> Note: something.")
        self.assertIsNone(body.caveat)
        self.assertIn("> Note", body.subsections[0][1])


class Cut(unittest.TestCase):
    def test_unreleased_moves_down_and_is_left_empty(self):
        out = changelog.cut(UNRELEASED, "0.1.0-beta.1", date(2026, 10, 20))
        found = changelog.sections(out)
        self.assertEqual([s.version for s in found], ["Unreleased", "0.1.0-beta.1"])
        self.assertEqual(found[0].body, "")
        self.assertEqual(found[1].date, "2026-10-20")
        self.assertIn("- **Something.** It does a thing.", found[1].body)
        self.assertEqual(changelog.validate(out, "0.1.0-beta.1"), [])

    def test_older_sections_are_untouched(self):
        text = GOOD.replace("## [Unreleased]\n", "## [Unreleased]\n\n### Added\n\n- **New.** x.\n")
        out = changelog.cut(text, "0.2.0", date(2026, 12, 1))
        self.assertIn("## [0.2.0-beta.1] - 2026-11-02", out)
        self.assertLess(out.index("## [0.2.0] - 2026-12-01"), out.index("## [0.2.0-beta.1]"))

    def test_an_empty_unreleased_is_refused(self):
        with self.assertRaises(changelog.ChangelogError):
            changelog.cut(GOOD, "0.3.0", date(2026, 12, 1))

    def test_a_version_that_exists_or_is_older_is_refused(self):
        text = GOOD.replace("## [Unreleased]\n", "## [Unreleased]\n\n### Added\n\n- **New.** x.\n")
        for version in ("0.2.0-beta.1", "0.1.5", "0.1.0-beta.1"):
            with self.assertRaises(changelog.ChangelogError, msg=version):
                changelog.cut(text, version, date(2026, 12, 1))

    def test_line_endings_do_not_matter(self):
        out = changelog.cut(UNRELEASED.replace("\n", "\r\n"), "0.1.0-beta.1", date(2026, 10, 20))
        self.assertNotIn("\r", out)


class Validate(unittest.TestCase):
    def test_a_sound_file_has_no_problems(self):
        self.assertEqual(changelog.validate(GOOD, "0.2.0-beta.1"), [])

    def test_each_rule_is_caught(self):
        cases = {
            "unreleased first": (GOOD.replace("## [Unreleased]\n\n", ""), None, "[Unreleased]"),
            "undated": (GOOD.replace(" - 2026-11-02", ""), None, "date"),
            "out of order": (
                GOOD.replace("0.2.0-beta.1] - 2026-11-02", "0.0.9] - 2026-11-02"),
                None,
                "newest first",
            ),
            "unknown subsection": (GOOD.replace("### Fixed", "### Tweaks"), None, "Tweaks"),
            "no lead-in": (GOOD.replace("- **The solver no longer stalls.** It", "- It"), "0.2.0-beta.1", "bold lead-in"),
            "no caveat": (GOOD.replace("> **Pre-release.** Not yet proven on every webcam.\n", ""), "0.2.0-beta.1", "Pre-release"),
            "no summary": (GOOD.replace("Scanning reads a cube on a poor webcam.\n\n", ""), "0.2.0-beta.1", "summary"),
            "not the newest": (GOOD, "0.1.0", "newest"),
            "missing": (GOOD, "0.9.0", "no '## [0.9.0]'"),
        }
        for name, (text, version, fragment) in cases.items():
            with self.subTest(name):
                problems = changelog.validate(text, version)
                self.assertTrue(any(fragment in p for p in problems), problems)

    def test_a_final_release_needs_no_caveat(self):
        text = UNRELEASED.replace("\n> **Pre-release.** Unproven.\n", "")
        out = changelog.cut(text, "0.1.0", date(2026, 10, 20))
        self.assertEqual(changelog.validate(out, "0.1.0"), [])


class Render(unittest.TestCase):
    NAMES = [
        "Cubeulator-0.2.0-beta.1-windows-x64.exe",
        "Cubeulator-0.2.0-beta.1-macos-arm64.dmg",
        "Cubeulator-0.2.0-beta.1-linux-x64.deb",
        "Cubeulator-0.2.0-beta.1-linux-x64.rpm",
        "SHA512SUMS",
    ]

    def render(self, **kw):
        base = dict(
            version="0.2.0-beta.1",
            tag="v0.2.0-beta.1",
            repo="owner/repo",
            names=self.NAMES,
            install_template=(
                "## Installing\n\nRun `@WINDOWS@`; `@MACOS@`; `@DEB@`; `@RPM@`.\n"
            ),
        )
        base.update(kw)
        return notes.render(GOOD, **base)

    def test_the_parts_come_in_the_families_order(self):
        out = self.render(signed=True, attested=True, sbom=True)
        order = [
            "## What's Changed since v0.1.0",
            "Scanning reads a cube on a poor webcam.",
            "### Added",
            "### Fixed",
            "## Installing",
            "## Artifacts",
            "**Full Changelog**: https://github.com/owner/repo/compare/v0.1.0...v0.2.0-beta.1",
            "> **Pre-release.** Not yet proven on every webcam.",
        ]
        positions = [out.index(part) for part in order]
        self.assertEqual(positions, sorted(positions))
        self.assertTrue(out.rstrip().endswith("webcam."))

    def test_a_first_release_has_no_since_and_no_compare_link(self):
        text = UNRELEASED
        text = changelog.cut(text, "0.1.0-beta.1", date(2026, 10, 20))
        out = notes.render(
            text, version="0.1.0-beta.1", tag="v0.1.0-beta.1", repo="owner/repo", names=self.NAMES
        )
        self.assertIn("## What's Changed\n", out)
        self.assertNotIn("since", out.split("## Artifacts")[0])
        self.assertNotIn("Full Changelog", out)

    def test_relative_links_point_at_the_tag(self):
        out = self.render()
        self.assertIn(
            "](https://github.com/owner/repo/blob/v0.2.0-beta.1/docs/vision-pipeline.md)", out
        )
        self.assertNotIn("](docs/", out)

    def test_the_table_lists_real_files_and_only_what_was_done(self):
        out = self.render(signed=True, attested=True, sbom=True)
        self.assertIn("| Windows 10 and later, 64-bit | `Cubeulator-0.2.0-beta.1-windows-x64.exe` |", out)
        self.assertIn("build-provenance attestations, an SPDX SBOM and GPG signatures", out)
        self.assertIn("`gh attestation verify <file> --repo owner/repo`", out)
        bare = self.render()
        self.assertIn("Self-contained and SHA-512-summed:", bare)
        self.assertNotIn("gh attestation", bare)
        self.assertNotIn(".asc", bare)

    def test_install_names_come_from_the_files(self):
        out = self.render()
        self.assertIn("`Cubeulator-0.2.0-beta.1-linux-x64.deb`", out)

    def test_an_unready_changelog_is_refused(self):
        bad = GOOD.replace("> **Pre-release.** Not yet proven on every webcam.\n", "")
        with self.assertRaises(notes.NotesError):
            notes.render(bad, version="0.2.0-beta.1", tag="v0.2.0-beta.1", repo="owner/repo")

    def test_a_missing_section_says_what_to_do(self):
        with self.assertRaises(notes.NotesError) as ctx:
            notes.render(GOOD, version="0.9.0", tag="v0.9.0", repo="owner/repo")
        self.assertIn("prepare_release", str(ctx.exception))


class Prepare(unittest.TestCase):
    CMAKE = textwrap.dedent(
        """\
        cmake_minimum_required(VERSION 3.28)
        project(Cubeulator
            VERSION 0.1.0
            LANGUAGES CXX)
        set(CUBEULATOR_VERSION_PRERELEASE "" CACHE INTERNAL "pre-release label")
        """
    )

    def tree(self, log: str):
        tmp = tempfile.TemporaryDirectory()
        root = Path(tmp.name)
        (root / "CHANGELOG.md").write_text(log, encoding="utf-8", newline="")
        (root / "CMakeLists.txt").write_text(self.CMAKE, encoding="utf-8", newline="")
        self.addCleanup(tmp.cleanup)
        return root

    def test_it_cuts_the_changelog_and_sets_the_version(self):
        root = self.tree(UNRELEASED)
        rc = prepare_release.main(["0.3.0-rc.1", "--root", str(root), "--date", "2026-12-01"])
        self.assertEqual(rc, 0)
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(prepare_release.full_version(cmake), "0.3.0-rc.1")
        log = (root / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertIn("## [0.3.0-rc.1] - 2026-12-01", log)

    def test_a_final_release_clears_the_label(self):
        root = self.tree(UNRELEASED)
        prepare_release.main(["0.3.0-rc.1", "--root", str(root), "--date", "2026-12-01"])
        (root / "CHANGELOG.md").write_text(
            (root / "CHANGELOG.md").read_text(encoding="utf-8").replace(
                "## [Unreleased]\n", "## [Unreleased]\n\n### Fixed\n\n- **A.** b.\n", 1
            ),
            encoding="utf-8",
            newline="",
        )
        prepare_release.main(["0.3.0", "--root", str(root), "--date", "2026-12-02"])
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(prepare_release.full_version(cmake), "0.3.0")

    def test_check_changes_nothing(self):
        root = self.tree(UNRELEASED)
        before = (root / "CHANGELOG.md").read_text(encoding="utf-8")
        prepare_release.main(["0.3.0-rc.1", "--root", str(root), "--check"])
        self.assertEqual((root / "CHANGELOG.md").read_text(encoding="utf-8"), before)

    def test_a_refusal_writes_neither_file(self):
        root = self.tree("## [Unreleased]\n")
        rc = prepare_release.main(["0.3.0", "--root", str(root)])
        self.assertEqual(rc, 1)
        cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(prepare_release.full_version(cmake), "0.1.0")

    def test_the_repository_is_in_a_state_the_tools_can_work_with(self):
        cmake = (prepare_release.ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIsNotNone(prepare_release.project_version(cmake))
        self.assertIsNotNone(prepare_release.project_prerelease(cmake))
        log = (prepare_release.ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertEqual(changelog.validate(log), [])


if __name__ == "__main__":
    unittest.main()
