# Changelog fragments

A change that a person using Cubeulator would notice adds **one file here**,
instead of editing `[Unreleased]` in `CHANGELOG.md`. When a release is prepared
the fragments are merged into the release's section and deleted.

Why: with several pull requests open at once (from people and from agents), one
shared block in `CHANGELOG.md` is a merge conflict for every pair of them. Files
of their own do not conflict.

## The file

```text
changelog.d/<slug>.<type>.md
```

- `<slug>`: letters, digits, `.`, `_` and `-` (starting with a letter or
  digit) that name the change and are not another change's, such as the branch
  name without its prefix (`scan-review`, `camera-follows-solve`).
- `<type>`: one of `added`, `changed`, `deprecated`, `removed`, `fixed`,
  `security`. They are the `###` subsections of the release's section.

The content is **one list item, one paragraph**, written as an item is in
`CHANGELOG.md`, without the leading dash:

```markdown
**The scan names the side you turned.** When the camera sees a different turn
from the one asked for, the screen says which side it was, so a wrong move is
corrected rather than repeated.
```

- Open with a **bold lead-in** that names the change. It is what a reader
  scans.
- Say what changed for the person using it, in their words, not which file
  moved. Link a page under `docs/` with a relative link (`docs/page.md`); the
  release notes rewrite it to the tagged tree.
- One change, one fragment. A pull request may add several.

## When one is needed

A pull request that changes `src/`, `packaging/`, `cmake/`, `third_party/`,
`vcpkg.json`, the top-level `CMakeLists.txt` or the string inventory
(`docs/design/strings.json`) should add one. If the change is not one a reader of
the changelog would want to hear about (a refactor, a test, a build detail),
label the pull request **`no-changelog`**.

The `Changelog fragment` check says which; it is advisory (it is not one of the
required checks), so it never blocks a merge, but it marks the pull request. It
looks only at pull requests into `develop`, and skips those from `release/*`,
`dependabot/*` and `support/back-merge-*` branches.

## At release time

`python tools/release/prepare_release.py <version>` merges every fragment into
`[Unreleased]`, under its type, cuts the section, and deletes the files. A
release with fragments left over fails `check_release.py`, because its notes
would miss them. Run `python tools/release/fragments.py check` to see whether the
ones here are well formed.
