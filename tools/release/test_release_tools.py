#!/usr/bin/env python3
"""The release tools' own tests (CTest case release_tools_selftest).

They hold the changelog's shape, the cut, the notes' layout and the version edit
to what docs/releasing.md says, so that the first thing to find out the tools are
wrong is not a tag.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest
from datetime import date
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import changelog  # noqa: E402
import check_health  # noqa: E402
import check_release  # noqa: E402
import fragments  # noqa: E402
import make_sbom  # noqa: E402
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
        "Cubeulator-0.2.0-beta.1-web.zip",
        "SHA512SUMS",
    ]

    def render(self, **kw):
        base = dict(
            version="0.2.0-beta.1",
            tag="v0.2.0-beta.1",
            repo="owner/repo",
            names=self.NAMES,
            install_template=(
                "## Installing\n\nRun `@WINDOWS@`; `@MACOS@`; `@DEB@`; `@RPM@`; host `@WEB@`.\n"
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
        self.assertIn("host `Cubeulator-0.2.0-beta.1-web.zip`", out)

    def test_the_web_package_has_its_own_row(self):
        out = self.render(attested=True)
        self.assertIn("| Web: any current browser (static files to host) | `Cubeulator-0.2.0-beta.1-web.zip` |", out)
        # a preview made before any package exists still names it
        preview = notes.render(
            GOOD, version="0.2.0-beta.1", tag="v0.2.0-beta.1", repo="owner/repo",
            install_template="host `@WEB@`",
        )
        self.assertIn("host `Cubeulator-0.2.0-beta.1-web.zip`", preview)

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


class Sbom(unittest.TestCase):
    INVENTORY = {
        "components": [
            {"id": "vcpkg/opencv4", "name": "opencv4", "version": "4.12.0#5", "license": "Apache-2.0",
             "distributedUnder": "Apache-2.0", "copyright": "", "homepage": "https://opencv.org/",
             "kind": "vcpkg", "via": []},
            {"id": "qt/qtbase", "name": "Qt Base", "version": "6.12.0",
             "license": "LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-3.0-only",
             "distributedUnder": "GPL-3.0-only", "copyright": "The Qt Company", "homepage": "",
             "kind": "qt", "via": ["linked"]},
            {"id": "design/font", "name": "A font", "version": "", "license": "see the font's own page",
             "distributedUnder": "", "copyright": "", "homepage": "", "kind": "design", "via": []},
        ],
        "licenseTexts": {},
        "unresolved": [],
    }
    NAMES = {
        "windows": "Cubeulator-0.1.0-beta.1-windows-x64.exe",
        "macos": "Cubeulator-0.1.0-beta.1-macos-arm64.dmg",
        "deb": "Cubeulator-0.1.0-beta.1-linux-x64.deb",
        "rpm": "Cubeulator-0.1.0-beta.1-linux-x64.rpm",
        "web": "Cubeulator-0.1.0-beta.1-web.zip",
    }

    def files(self, *keys: str):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        out = []
        for key in keys or self.NAMES:
            path = Path(tmp.name) / self.NAMES[key]
            path.write_bytes(b"bytes of " + key.encode())
            out.append(path)
        return out

    def make(self, files=None, notices=None):
        return make_sbom.build(
            version="0.1.0-beta.1",
            tag="v0.1.0-beta.1",
            repo="owner/repo",
            files=files if files is not None else self.files(),
            notices=notices
            if notices is not None
            else {p: self.INVENTORY for p in ("windows", "macos", "linux", "web")},
            created=make_sbom.datetime(2026, 10, 14, tzinfo=make_sbom.timezone.utc),
        )

    def test_it_is_an_spdx_document_that_holds_together(self):
        doc = self.make()
        self.assertEqual(doc["spdxVersion"], "SPDX-2.3")
        ids = [p["SPDXID"] for p in doc["packages"]]
        self.assertEqual(len(ids), len(set(ids)))
        for rel in doc["relationships"]:
            self.assertIn(rel["spdxElementId"], ids)
            self.assertIn(rel["relatedSpdxElement"], ids)
        for pid in ids:
            self.assertRegex(pid, r"^SPDXRef-[A-Za-z0-9.\-]+$")

    def test_each_file_is_described_and_holds_its_platforms_components(self):
        doc = self.make()
        self.assertEqual(len(doc["documentDescribes"]), 5)
        for file_pkg in (p for p in doc["packages"] if "packageFileName" in p):
            held = [r for r in doc["relationships"] if r["spdxElementId"] == file_pkg["SPDXID"]]
            self.assertEqual(len(held), 3, file_pkg["name"])
            self.assertEqual({r["relationshipType"] for r in held}, {"CONTAINS"})

    def test_files_carry_their_hashes_and_where_they_are_downloaded_from(self):
        import hashlib
        doc = self.make()
        pkg = next(p for p in doc["packages"] if p.get("packageFileName", "").endswith(".exe"))
        sums = {c["algorithm"]: c["checksumValue"] for c in pkg["checksums"]}
        self.assertEqual(sums["SHA512"], hashlib.sha512(b"bytes of windows").hexdigest())
        self.assertEqual(sums["SHA256"], hashlib.sha256(b"bytes of windows").hexdigest())
        self.assertEqual(
            pkg["downloadLocation"],
            "https://github.com/owner/repo/releases/download/v0.1.0-beta.1/" + self.NAMES["windows"],
        )

    def test_components_keep_their_version_and_licence(self):
        doc = self.make()
        cv = next(p for p in doc["packages"] if p["name"] == "opencv4" and p["SPDXID"].startswith("SPDXRef-linux-"))
        self.assertEqual(cv["versionInfo"], "4.12.0#5")
        self.assertEqual(cv["licenseDeclared"], "Apache-2.0")
        qt = next(p for p in doc["packages"] if p["name"] == "Qt Base")
        self.assertEqual(qt["licenseConcluded"], "GPL-3.0-only")

    def test_a_licence_reference_is_defined_and_prose_is_not_pretended_to_be_spdx(self):
        doc = self.make()
        self.assertEqual(
            [e["licenseId"] for e in doc["hasExtractedLicensingInfos"]], ["LicenseRef-Qt-Commercial"]
        )
        font = next(p for p in doc["packages"] if p["name"] == "A font")
        self.assertEqual(font["licenseDeclared"], "NOASSERTION")
        self.assertNotIn("versionInfo", font)

    def test_an_unresolved_inventory_is_refused(self):
        bad = dict(self.INVENTORY, unresolved=["Qt is not the Qt the notices were written for"])
        with self.assertRaises(make_sbom.SbomError):
            self.make(notices={"windows": bad, "macos": bad, "linux": bad, "web": bad})

    def test_a_platform_with_no_inventory_is_refused_rather_than_left_thin(self):
        with self.assertRaises(make_sbom.SbomError):
            self.make(notices={"windows": self.INVENTORY, "linux": self.INVENTORY})

    def test_the_web_zip_is_described_from_the_web_inventory(self):
        doc = self.make()
        zip_pkg = next(p for p in doc["packages"] if p.get("packageFileName", "").endswith("-web.zip"))
        self.assertEqual(zip_pkg["summary"], "Cubeulator 0.1.0-beta.1 for web")
        held = [r for r in doc["relationships"] if r["spdxElementId"] == zip_pkg["SPDXID"]]
        self.assertEqual(len(held), 3)
        # the zip is a package of its own platform and only that
        self.assertTrue(
            all(r["relatedSpdxElement"].startswith("SPDXRef-web-") for r in held),
            [r["relatedSpdxElement"] for r in held],
        )
        with self.assertRaises(make_sbom.SbomError):
            self.make(notices={p: self.INVENTORY for p in ("windows", "macos", "linux")})

    def test_files_that_are_not_packages_are_left_out(self):
        files = self.files("windows")
        extra = files[0].parent / "SHA512SUMS"
        extra.write_text("x")
        doc = self.make(files=files + [extra])
        self.assertEqual(len(doc["documentDescribes"]), 1)


class Fragments(unittest.TestCase):
    def write(self, root: Path, name: str, text: str):
        d = root / "changelog.d"
        d.mkdir(exist_ok=True)
        (d / name).write_text(text, encoding="utf-8", newline="")

    def tree(self, log: str = UNRELEASED):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        (root / "CHANGELOG.md").write_text(log, encoding="utf-8", newline="")
        (root / "CMakeLists.txt").write_text(Prepare.CMAKE, encoding="utf-8", newline="")
        return root

    def test_a_good_fragment_has_no_problems(self):
        self.assertEqual(
            fragments.problems_in("scan-review.added.md", "**A review screen.** Fix a sticker.\n"), []
        )

    def test_each_mistake_is_named(self):
        cases = {
            "wrong type": ("a.tweaked.md", "**A.** b", "one of"),
            "no suffix": ("a.md", "**A.** b", "name must be"),
            "empty": ("a.added.md", "\n", "empty"),
            "no lead-in": ("a.added.md", "It does a thing.", "bold lead-in"),
            "dash": ("a.added.md", "- **A.** b", "leading dash"),
            "two paragraphs": ("a.added.md", "**A.** b\n\nAnd c.", "blank line"),
            "heading": ("a.added.md", "**A.** b\n### Fixed\nc", "heading"),
        }
        for label, (name, text, fragment) in cases.items():
            with self.subTest(label):
                found = fragments.problems_in(name, text)
                self.assertTrue(any(fragment in p for p in found), found)

    def test_they_are_read_in_changelog_order_and_readme_is_not_one(self):
        root = self.tree()
        self.write(root, "README.md", "How to write one.")
        self.write(root, "b.fixed.md", "**B.** x")
        self.write(root, "a.added.md", "**A.** x")
        self.write(root, "c.added.md", "**C.** x")
        found, problems = fragments.read(root / "changelog.d")
        self.assertEqual(problems, [])
        self.assertEqual([f.name for f in found], ["a.added.md", "c.added.md", "b.fixed.md"])

    def test_merging_adds_to_the_subsection_and_keeps_what_was_written(self):
        frag = [fragments.Fragment("n.added.md", "added", "**New.** It does a thing.")]
        out = fragments.merge(UNRELEASED, frag)
        body = changelog.split_body(changelog.get(out, "Unreleased").body)
        added = dict(body.subsections)["Added"]
        self.assertIn("- **Something.** It does a thing.", added)
        self.assertIn("- **New.** It does a thing.", added)
        self.assertLess(added.index("Something"), added.index("New"))
        self.assertEqual(body.intro, "A summary.")
        self.assertEqual(body.caveat, "> **Pre-release.** Unproven.")

    def test_merging_makes_a_missing_subsection_in_the_changelog_order(self):
        frags = [
            fragments.Fragment("s.security.md", "security", "**S.** x"),
            fragments.Fragment("f.fixed.md", "fixed", "**F.** x"),
        ]
        out = fragments.merge(UNRELEASED, frags)
        body = changelog.split_body(changelog.get(out, "Unreleased").body)
        self.assertEqual([t for t, _ in body.subsections], ["Added", "Fixed", "Security"])
        self.assertEqual(body.caveat, "> **Pre-release.** Unproven.")

    def test_a_long_item_keeps_its_lines_in_the_list(self):
        frag = [fragments.Fragment("n.added.md", "added", "**New.** It does\na thing over\nthree lines.")]
        out = fragments.merge("## [Unreleased]\n", frag)
        self.assertIn("- **New.** It does\n  a thing over\n  three lines.", out)

    def test_nothing_to_merge_changes_nothing(self):
        self.assertEqual(fragments.merge(UNRELEASED, []), changelog.normalise(UNRELEASED))

    def test_preparing_a_release_takes_the_fragments_in_and_removes_them(self):
        root = self.tree()
        self.write(root, "a.fixed.md", "**Fixed it.** It was broken.")
        rc = prepare_release.main(["0.3.0-beta.1", "--root", str(root), "--date", "2026-12-01"])
        self.assertEqual(rc, 0)
        log = (root / "CHANGELOG.md").read_text(encoding="utf-8")
        self.assertIn("## [0.3.0-beta.1] - 2026-12-01", log)
        self.assertIn("- **Fixed it.** It was broken.", log)
        self.assertEqual(list((root / "changelog.d").glob("*.fixed.md")), [])

    def test_check_keeps_the_fragments(self):
        root = self.tree()
        self.write(root, "a.fixed.md", "**Fixed it.** It was broken.")
        prepare_release.main(["0.3.0-beta.1", "--root", str(root), "--check"])
        self.assertTrue((root / "changelog.d" / "a.fixed.md").exists())
        self.assertNotIn("Fixed it", (root / "CHANGELOG.md").read_text(encoding="utf-8"))

    def test_a_malformed_fragment_stops_the_cut_and_changes_nothing(self):
        root = self.tree()
        self.write(root, "a.fixed.md", "no lead-in here")
        before = (root / "CHANGELOG.md").read_text(encoding="utf-8")
        rc = prepare_release.main(["0.3.0-beta.1", "--root", str(root)])
        self.assertEqual(rc, 1)
        self.assertEqual((root / "CHANGELOG.md").read_text(encoding="utf-8"), before)

    def test_a_dry_run_reads_them_and_a_release_refuses_to_leave_them(self):
        root = self.tree()
        root_cmake = Prepare.CMAKE.replace("VERSION 0.1.0", "VERSION 0.2.0").replace(
            'set(CUBEULATOR_VERSION_PRERELEASE "" CACHE INTERNAL "pre-release label")',
            'set(CUBEULATOR_VERSION_PRERELEASE "beta.1")',
        )
        (root / "CMakeLists.txt").write_text(root_cmake, encoding="utf-8", newline="")
        self.write(root, "a.fixed.md", "**Fixed it.** It was broken.")
        dry = check_release.check(root, dry_run=True)
        self.assertEqual(dry.problems, [])
        # the same tree, cut by hand with its fragment left behind
        cut = changelog.cut(UNRELEASED, "0.2.0-beta.1", date(2026, 10, 20))
        (root / "CHANGELOG.md").write_text(cut, encoding="utf-8", newline="")
        strict = check_release.check(root)
        self.assertTrue(any("fragment" in p for p in strict.problems), strict.problems)

    def test_the_repository_fragments_are_well_formed(self):
        found, problems = fragments.read(prepare_release.ROOT / fragments.DIRECTORY)
        self.assertEqual(problems, [])


def _bash() -> str | None:
    """A bash that can run the script: on Windows, Git's, not the WSL stub on PATH."""
    git = shutil.which("git")
    if sys.platform == "win32" and git:
        candidate = Path(git).resolve().parent.parent / "bin" / "bash.exe"
        if candidate.exists():
            return str(candidate)
    return shutil.which("bash")


@unittest.skipUnless(_bash() and shutil.which("git"), "needs bash and git")
class FragmentCheckScript(unittest.TestCase):
    SCRIPT = Path(__file__).resolve().parent / "check-fragment.sh"

    def repo(self):
        tmp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        self.git(root, "init", "-q", "-b", "develop")
        self.git(root, "config", "user.email", "t@example.invalid")
        self.git(root, "config", "user.name", "t")
        self.git(root, "config", "commit.gpgsign", "false")
        (root / "README.md").write_text("x\n", encoding="utf-8")
        self.git(root, "add", "-A")
        self.git(root, "commit", "-qm", "base")
        self.git(root, "switch", "-qc", "topic")
        return root

    def git(self, root: Path, *args: str):
        subprocess.run(["git", *args], cwd=root, check=True, capture_output=True)

    def commit(self, root: Path, files: dict[str, str]):
        for name, text in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="")
        self.git(root, "add", "-A")
        self.git(root, "commit", "-qm", "change")

    def run_check(self, root: Path, label: str = "false"):
        proc = subprocess.run(
            [_bash(), self.SCRIPT.as_posix(), "develop", "HEAD", label],
            cwd=root,
            capture_output=True,
            text=True,
        )
        return proc.returncode, proc.stdout + proc.stderr

    def test_a_visible_change_without_a_fragment_is_flagged(self):
        root = self.repo()
        self.commit(root, {"src/app/x.cpp": "int x;\n"})
        rc, out = self.run_check(root)
        self.assertEqual(rc, 1)
        self.assertIn("no changelog fragment", out)

    def test_the_label_opts_out(self):
        root = self.repo()
        self.commit(root, {"src/app/x.cpp": "int x;\n"})
        rc, out = self.run_check(root, "true")
        self.assertEqual(rc, 0, out)

    def test_a_fragment_satisfies_it(self):
        root = self.repo()
        self.commit(root, {"src/app/x.cpp": "int x;\n", "changelog.d/x.fixed.md": "**Fixed.** Why.\n"})
        rc, out = self.run_check(root)
        self.assertEqual(rc, 0, out)

    def test_a_change_nobody_reads_about_needs_none(self):
        root = self.repo()
        self.commit(root, {"tests/unit/x_test.cpp": "int x;\n", "docs/page.md": "x\n"})
        rc, out = self.run_check(root)
        self.assertEqual(rc, 0, out)

    def test_a_bad_fragment_is_flagged_even_when_none_was_needed(self):
        root = self.repo()
        for name, text in {
            "a.tweaked.md": "**A.** b\n",
            "b.added.md": "no lead-in\n",
            "c.added.md": "**C.** b\n\nsecond paragraph\n",
        }.items():
            self.commit(root, {f"changelog.d/{name}": text})
        rc, out = self.run_check(root)
        self.assertEqual(rc, 1)
        self.assertIn("the name must be", out)
        self.assertIn("bold lead-in", out)
        self.assertIn("blank line", out)


class Check(unittest.TestCase):
    CMAKE = Prepare.CMAKE.replace("VERSION 0.1.0", "VERSION 0.2.0").replace(
        'set(CUBEULATOR_VERSION_PRERELEASE "" CACHE INTERNAL "pre-release label")',
        'set(CUBEULATOR_VERSION_PRERELEASE "beta.1")',
    )

    def tree(self, log: str, cmake: str | None = None):
        tmp = tempfile.TemporaryDirectory()
        root = Path(tmp.name)
        (root / "CHANGELOG.md").write_text(log, encoding="utf-8", newline="")
        (root / "CMakeLists.txt").write_text(cmake or self.CMAKE, encoding="utf-8", newline="")
        self.addCleanup(tmp.cleanup)
        return root

    def test_a_prepared_tree_is_ready(self):
        result = check_release.check(self.tree(GOOD), tag="v0.2.0-beta.1")
        self.assertEqual(result.problems, [])
        self.assertEqual((result.version, result.tag, result.prerelease), ("0.2.0-beta.1", "v0.2.0-beta.1", True))

    def test_a_tag_that_names_another_version_is_refused(self):
        for tag in ("v0.2.0", "v0.1.0", "v0.2.0-beta.2"):
            result = check_release.check(self.tree(GOOD), tag=tag)
            self.assertTrue(any("CMakeLists.txt says" in p for p in result.problems), tag)

    def test_a_tree_not_yet_prepared_is_not_ready_but_a_dry_run_can_read_unreleased(self):
        log = UNRELEASED
        strict = check_release.check(self.tree(log))
        self.assertTrue(any("no '## [0.2.0-beta.1]'" in p for p in strict.problems))
        dry = check_release.check(self.tree(log), dry_run=True)
        self.assertEqual(dry.problems, [])
        self.assertTrue(dry.from_unreleased)

    def test_a_dry_run_holds_unreleased_to_the_same_rules(self):
        bad = UNRELEASED.replace("- **Something.** It does a thing.", "- It does a thing.")
        dry = check_release.check(self.tree(bad), dry_run=True)
        self.assertTrue(any("bold lead-in" in p for p in dry.problems))

    def test_the_label_is_required_to_exist(self):
        root = self.tree(GOOD, cmake='project(Cubeulator\n    VERSION 0.2.0\n    LANGUAGES CXX)\n')
        result = check_release.check(root)
        self.assertTrue(any("CUBEULATOR_VERSION_PRERELEASE" in p for p in result.problems))

    def test_github_output_is_written(self):
        root = self.tree(GOOD)
        out = root / "out.txt"
        rc = check_release.main(["--root", str(root), "--tag", "v0.2.0-beta.1", "--github-output", str(out)])
        self.assertEqual(rc, 0)
        self.assertIn("version=0.2.0-beta.1\n", out.read_text(encoding="utf-8"))
        self.assertIn("prerelease=true\n", out.read_text(encoding="utf-8"))


class Health(unittest.TestCase):
    """check_health: is a commit green on the workflows the README has badges for."""

    SHA = "a" * 40
    MERGED = "b" * 40
    FIRST = "c" * 40

    @staticmethod
    def wf_run(workflow_event, status="completed", conclusion="success", created="2026-10-14T10:00:00Z"):
        return {
            "event": workflow_event,
            "status": status,
            "conclusion": conclusion,
            "created_at": created,
            "html_url": f"https://example.invalid/{workflow_event}/{created}",
        }

    def api(self, runs_by_workflow, checks=(), parents=(), merged_runs=None):
        """A fake `gh api` over a commit, its workflow runs and its check runs."""

        def fake(path):
            if path.endswith(f"/commits/{self.SHA}") or path.endswith("/commits/develop"):
                return {
                    "sha": self.SHA,
                    "parents": [{"sha": p} for p in parents],
                    "commit": {"message": "Merge pull request #1\n\nbody"},
                }
            if path.endswith(f"/commits/{self.MERGED}"):
                return {"sha": self.MERGED, "parents": [], "commit": {"message": "Tip of develop"}}
            if "/actions/workflows/" in path:
                workflow = path.split("/actions/workflows/")[1].split("/")[0]
                table = merged_runs if (merged_runs is not None and self.MERGED in path) else runs_by_workflow
                return {"workflow_runs": table.get(workflow, [])}
            if "/check-runs" in path:
                return {"total_count": len(checks), "check_runs": list(checks)}
            raise AssertionError(path)

        return fake

    @staticmethod
    def check(name, status="completed", conclusion="success"):
        return {"name": name, "status": status, "conclusion": conclusion}

    def assess(self, runs, **kw):
        extra = {k: kw.pop(k) for k in ("checks", "parents", "merged_runs") if k in kw}
        return check_health.assess(
            "o/r", "develop", ["ci.yml", "codeql.yml"], self.api(runs, **extra), **kw
        )

    def test_the_workflows_are_the_ones_the_readme_has_badges_for(self):
        readme = (
            "[![CI](https://github.com/o/r/actions/workflows/ci.yml/badge.svg)](x)\n"
            "[![Zizmor](https://github.com/o/r/actions/workflows/zizmor.yml/badge.svg)](x)\n"
            "[![again](https://github.com/o/r/actions/workflows/ci.yml/badge.svg)](x)\n"
            "[![Docs](https://img.shields.io/badge/docs-published-2f7d7b)](x)\n"
        )
        self.assertEqual(check_health.badge_workflows(readme), ["ci.yml", "zizmor.yml"])
        # and the real README names the ones the docs say it does
        real = (Path(__file__).resolve().parents[2] / "README.md").read_text(encoding="utf-8")
        names = check_health.badge_workflows(real)
        self.assertIn("ci.yml", names)
        self.assertIn("codeql.yml", names)
        for name in names:
            workflow = Path(__file__).resolve().parents[2] / ".github" / "workflows" / name
            self.assertTrue(workflow.exists(), f"README has a badge for {name}, which does not exist")

    def test_green_when_every_workflow_succeeded_on_the_commit(self):
        report = self.assess(
            {"ci.yml": [self.wf_run("push")], "codeql.yml": [self.wf_run("push")]},
            checks=[self.check("a"), self.check("b", conclusion="skipped")],
        )
        self.assertTrue(report.ok, report.problems())
        self.assertEqual(report.checks_green, 1)

    def test_a_failed_workflow_is_named(self):
        report = self.assess(
            {"ci.yml": [self.wf_run("push", conclusion="failure")], "codeql.yml": [self.wf_run("push")]}
        )
        self.assertFalse(report.ok)
        self.assertEqual(len(report.problems()), 1)
        self.assertIn("ci.yml is failure", report.problems()[0])

    def test_a_workflow_still_running_is_not_green(self):
        report = self.assess(
            {
                "ci.yml": [self.wf_run("push", status="in_progress", conclusion=None)],
                "codeql.yml": [self.wf_run("push")],
            }
        )
        self.assertFalse(report.ok)
        self.assertIn("has not finished", report.problems()[0])

    def test_a_workflow_that_never_ran_is_not_green(self):
        report = self.assess({"ci.yml": [self.wf_run("push")]})
        self.assertFalse(report.ok)
        self.assertIn("codeql.yml has no run", report.problems()[0])

    def test_a_cancelled_run_says_what_to_do(self):
        report = self.assess(
            {"ci.yml": [self.wf_run("push", conclusion="cancelled")], "codeql.yml": [self.wf_run("push")]}
        )
        self.assertIn("Re-run it", report.problems()[0])

    def test_skipped_proves_nothing(self):
        report = self.assess(
            {"ci.yml": [self.wf_run("push", conclusion="skipped")], "codeql.yml": [self.wf_run("push")]}
        )
        self.assertFalse(report.ok)

    def test_the_newest_push_run_speaks_for_a_workflow(self):
        runs = [
            self.wf_run("push", conclusion="failure", created="2026-10-14T09:00:00Z"),
            self.wf_run("push", created="2026-10-14T11:00:00Z"),
            # a newer pull_request run does not replace what the badge shows
            self.wf_run("pull_request", conclusion="failure", created="2026-10-14T12:00:00Z"),
        ]
        report = self.assess({"ci.yml": runs, "codeql.yml": [self.wf_run("push")]})
        self.assertTrue(report.ok, report.problems())

    def test_a_hotfix_branch_is_judged_on_its_pull_request_runs(self):
        runs = {"ci.yml": [self.wf_run("pull_request")], "codeql.yml": [self.wf_run("pull_request")]}
        self.assertTrue(self.assess(runs).ok)
        # unless the event is pinned
        self.assertFalse(self.assess(runs, event="push").ok)

    def test_a_red_check_run_blocks_a_running_one_is_only_listed(self):
        runs = {"ci.yml": [self.wf_run("push")], "codeql.yml": [self.wf_run("push")]}
        checks = [
            self.check("Build (Windows)", conclusion="failure"),
            self.check("Release readiness", conclusion="failure"),
            self.check("Build (macOS)", status="in_progress", conclusion=None),
        ]
        report = self.assess(runs, checks=checks, ignore=("Release readiness",))
        self.assertEqual(report.problems(), ["check run failed: Build (Windows) (failure)"])
        self.assertEqual(report.checks_running, ["Build (macOS)"])
        # a pull request's own checks that are still going do not make a commit not green
        report = self.assess(runs, checks=[self.check("Build (macOS)", status="queued", conclusion=None)])
        self.assertTrue(report.ok, report.problems())

    def test_a_merge_commit_is_judged_by_what_it_merged(self):
        merged = {"ci.yml": [self.wf_run("push")], "codeql.yml": [self.wf_run("push")]}
        # the merge commit itself has only just been pushed
        fresh = {"ci.yml": [self.wf_run("push", status="queued", conclusion=None)]}
        report = self.assess(
            fresh, parents=[self.FIRST, self.MERGED], merged_runs=merged, through_merge=True
        )
        self.assertTrue(report.ok, report.problems())
        self.assertEqual(report.sha, self.MERGED)
        self.assertIn("merge commit", report.note)
        # without the flag it is the merge commit that is read
        self.assertFalse(
            self.assess(fresh, parents=[self.FIRST, self.MERGED], merged_runs=merged).ok
        )

    def test_a_commit_that_is_not_a_merge_is_read_as_it_is(self):
        runs = {"ci.yml": [self.wf_run("push")], "codeql.yml": [self.wf_run("push")]}
        report = self.assess(runs, parents=[self.FIRST], through_merge=True)
        self.assertEqual(report.sha, self.SHA)
        self.assertTrue(report.ok)

    def test_a_partial_list_of_check_runs_is_not_taken_as_green(self):
        runs = {"ci.yml": [self.wf_run("push")], "codeql.yml": [self.wf_run("push")]}
        api = self.api(runs)

        def paged(path):
            if "/check-runs" in path:
                # one check run on the first page, and nothing where 249 more should be
                return {"total_count": 250, "check_runs": [self.check("a")] if path.endswith("&page=1") else []}
            return api(path)

        report = check_health.assess("o/r", "develop", ["ci.yml", "codeql.yml"], paged)
        self.assertFalse(report.ok)
        self.assertIn("1 of 250", report.problems()[0])

    def test_the_summary_is_markdown_and_says_what_is_wrong(self):
        report = self.assess(
            {"ci.yml": [self.wf_run("push", conclusion="failure")], "codeql.yml": [self.wf_run("push")]}
        )
        text = report.markdown()
        self.assertIn("| `ci.yml` | ❌ |", text)
        self.assertIn("| `codeql.yml` | ✅ |", text)
        self.assertIn("**Not green:**", text)

    def test_scorecard_without_a_result_says_why_the_badge_is_red(self):
        ok, text = check_health.scorecard("o/r", lambda url: (404, ""))
        self.assertFalse(ok)
        self.assertIn("invalid repo path", text)
        body = '{"score": 8.3, "date": "2026-10-20T04:00:00Z", "repo": {"commit": "deadbeef00"}}'
        ok, text = check_health.scorecard("o/r", lambda url: (200, body))
        self.assertTrue(ok)
        self.assertIn("8.3", text)
        with self.assertRaises(check_health.HealthError):
            check_health.scorecard("o/r", lambda url: (500, ""))


@unittest.skipUnless(
    _bash() and all(shutil.which(t) for t in ("zip", "unzip", "python3")),
    "needs bash, zip, unzip and python3",
)
class WebPackage(unittest.TestCase):
    """packaging/web/make-package.sh and check-package.sh, over a made-up build."""

    ROOT = Path(__file__).resolve().parents[2]
    MAKE = ROOT / "packaging" / "web" / "make-package.sh"
    CHECK = ROOT / "packaging" / "web" / "check-package.sh"
    INVENTORY = {"platform": "web", "qtPlatformVerified": True, "components": [{"name": "qt"}], "unresolved": []}

    def build_dir(self, inventory=None, drop=()):
        tmp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        bin_dir = root / "build" / "bin"
        notices = root / "build" / "third_party_notices"
        bin_dir.mkdir(parents=True)
        notices.mkdir(parents=True)
        (bin_dir / "index.html").write_text(
            '<script src="cubeulator-isolation.js"></script><script>load("cube_app.js");load("qtloader.js")</script>',
            encoding="utf-8",
        )
        (bin_dir / "cube_app.js").write_text('var m="cube_app.wasm"; /* PThread */', encoding="utf-8")
        (bin_dir / "qtloader.js").write_text("qtLoad", encoding="utf-8")
        (bin_dir / "cubeulator-isolation.js").write_text("sw", encoding="utf-8")
        (bin_dir / "icon.svg").write_text("<svg/>", encoding="utf-8")
        (bin_dir / "cube_app.wasm").write_bytes(b"\x00asm\x01\x00\x00\x00" + b"\x00" * 1_100_000)
        (notices / "THIRD-PARTY-NOTICES.txt").write_text(
            "Qt 6.12 ... OpenCV ... Noto Sans JP\n" + "\n".join(f"line {i}" for i in range(300)),
            encoding="utf-8",
        )
        (notices / "third-party-notices.json").write_text(
            json.dumps(inventory if inventory is not None else self.INVENTORY), encoding="utf-8"
        )
        for name in drop:
            (bin_dir / name).unlink()
        return root

    def make(self, root: Path):
        out = root / "dist"
        proc = subprocess.run(
            [_bash(), self.MAKE.as_posix(), (root / "build").as_posix(), "0.1.0-beta.1", out.as_posix()],
            capture_output=True,
            text=True,
        )
        return proc, out / "Cubeulator-0.1.0-beta.1-web.zip"

    def check(self, package: Path):
        proc = subprocess.run(
            [_bash(), self.CHECK.as_posix(), package.as_posix()], capture_output=True, text=True
        )
        return proc.returncode, proc.stdout + proc.stderr

    def test_a_good_build_makes_a_package_that_passes(self):
        proc, package = self.make(self.build_dir())
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertTrue(package.exists())
        code, out = self.check(package)
        self.assertEqual(code, 0, out)
        self.assertIn("passes", out)

    def test_an_unresolved_inventory_makes_no_package(self):
        bad = dict(self.INVENTORY, unresolved=["Linked Qt library x"])
        proc, package = self.make(self.build_dir(inventory=bad))
        self.assertNotEqual(proc.returncode, 0)
        self.assertFalse(package.exists())
        self.assertIn("unresolved", proc.stdout + proc.stderr)

    def test_qt_sboms_not_read_for_the_platform_makes_no_package(self):
        proc, package = self.make(self.build_dir(inventory=dict(self.INVENTORY, qtPlatformVerified=False)))
        self.assertNotEqual(proc.returncode, 0)
        self.assertFalse(package.exists())

    def test_a_site_without_its_module_is_not_packaged(self):
        proc, package = self.make(self.build_dir(drop=("cube_app.wasm",)))
        self.assertNotEqual(proc.returncode, 0)
        self.assertFalse(package.exists())

    def test_the_check_names_what_is_wrong(self):
        proc, package = self.make(self.build_dir())
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        tmp = tempfile.TemporaryDirectory(ignore_cleanup_errors=True)
        self.addCleanup(tmp.cleanup)
        work = Path(tmp.name)
        shutil.unpack_archive(str(package), work / "x")
        # a module that is not WebAssembly, and a page that does not load the worker
        (work / "x" / "Cubeulator-0.1.0-beta.1-web" / "cube_app.wasm").write_bytes(b"MZ" + b"\x00" * 1_100_000)
        (work / "x" / "Cubeulator-0.1.0-beta.1-web" / "index.html").write_text("<html/>", encoding="utf-8")
        bad = work / "Cubeulator-0.1.0-beta.1-web.zip"
        shutil.make_archive(str(bad.with_suffix("")), "zip", work / "x")
        code, out = self.check(bad)
        self.assertNotEqual(code, 0)
        self.assertIn("cube_app.wasm is a WebAssembly module", out)
        self.assertIn("index.html loads the isolation worker", out)


if __name__ == "__main__":
    unittest.main()
