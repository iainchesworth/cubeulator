# Cutting a release

How a version of Cubeulator becomes three downloadable packages on a GitHub
Release, with the notes, the checksums and the signatures that go with them.
The workflow is `.github/workflows/release.yml`; the tools are in
`tools/release/`; what is in the packages is
[Packaging the desktop app](packaging.md).

A release is a deliberate human step (see [Branch protection](branch-protection.md)).
The workflow does everything up to a **draft** release; making it public is the
click that is yours.

## The shape of a release

```text
feature/* bugfix/* ──PR──▶ merge queue ──▶ develop
                                              │
        release/<version> ──PR──▶ develop     │   1. cut the changelog, set the version
                                              ▼
                     develop ──release PR (merge commit)──▶ main      2. the release decision
                                                             │
                                          tag v<version> ────┘        3. the Release workflow
                                                             ▼
                                        draft GitHub Release ──click──▶ published   4.
                                                             │
                                         main ──back-merge PR──▶ develop            5.
```

- **Releases go from `develop` into `main`.** The release PR is merged with a
  *merge commit* (never squash or rebase, or the two branches diverge and the
  next release PR re-lists every commit).
- **The changelog is cut on `develop`**, in the PR that sets the version, so
  that what `main` receives is already the release.
- **The tag is the build.** A tag on `main` (or a dispatch, below) runs the
  Release workflow, which builds, checks, signs and attests the packages, renders
  the notes from `CHANGELOG.md` and leaves a draft release.
- **`main` is back-merged into `develop`** afterwards, so that `develop` contains
  the release's merge commit and the next release PR lists only what is new.

## What a release holds

| File | What it is |
|---|---|
| `Cubeulator-<version>-windows-x64.exe` | Windows 10 and later, 64-bit: the NSIS installer |
| `Cubeulator-<version>-macos-arm64.dmg` | macOS 14.4 and later, Apple Silicon |
| `Cubeulator-<version>-linux-x64.deb` | Debian and Ubuntu |
| `Cubeulator-<version>-linux-x64.rpm` | Fedora and openSUSE |
| `SHA512SUMS` | one line per package |
| `SHA512SUMS.asc`, `<package>.asc` | detached GPG signatures, from the project key |
| `cubeulator-signing-key.asc` | the public key, so that a download can be verified offline |
| `Cubeulator-<version>.spdx.json` | an SPDX SBOM of the release files |

Every package also carries a **build-provenance attestation** (Sigstore, from
the workflow's own identity) and an SBOM attestation, held by GitHub, not as
files. [Verifying a download](#verifying-a-download) says how to check each.

**The packages are not OS code-signed** (no Authenticode certificate, no Apple
Developer ID). The notes say so and say what a person will see: SmartScreen on
Windows, Gatekeeper on macOS. Where that plugs in is the last section of
[packaging.md](packaging.md). The GPG signature and the attestation prove where
a file came from; they do not stop SmartScreen from asking.

## Versions

`M.M.P`, or `M.M.P-label.N` for a pre-release, where the label is `alpha`,
`beta` or `rc`: `0.1.0-beta.1`, `0.1.0-rc.1`, `0.1.0`. The tag is `v` and that.

While the project is `0.x`, a release with new features moves the minor
(`0.1.0` → `0.2.0`), a release of fixes moves the patch, and a pre-release
counter moves within a line (`0.2.0-beta.1` → `0.2.0-beta.2`). `1.0.0` is for
the first release the project is willing to call stable.

The version lives in **two lines of the top-level `CMakeLists.txt`**:

```cmake
project(Cubeulator VERSION 0.1.0 ...)
set(CUBEULATOR_VERSION_PRERELEASE "beta.1")     # "" for a final release
```

`tools/release/prepare_release.py` sets both. Everything else follows:

| | |
|---|---|
| Package file names | `Cubeulator-0.1.0-beta.1-windows-x64.exe`: a beta is never called by the final's name |
| `cube::version_string`, About, `--version` | `0.1.0-beta.1` |
| Windows installer, version resource | `0.1.0-beta.1` (the resource's numeric fields are `0,1,0,0`) |
| `.deb`, AppStream | `0.1.0~beta.1` (`~` sorts before `0.1.0`, so `apt upgrade` takes a beta to the final) |
| `.rpm` | version `0.1.0`, release `0.beta.1` (`1` for a final) |
| macOS `Info.plist` | `0.1.0` (macOS wants three numbers) |

The Release workflow refuses a tag that does not name the pair exactly.

## The changelog

`CHANGELOG.md` is [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), for
people following what has shipped. It is written once, as work lands, and the
GitHub Release is rendered from it; nothing is retyped at release time.

```markdown
## [Unreleased]

## [0.1.0-beta.1] - 2026-10-20

One paragraph saying what this release is. It is the summary line of the notes.

### Added

- **Bold lead-in naming the change.** Then what it does and why, in the words of
  the person using it. See [docs/page.md](docs/page.md).

### Changed
### Fixed
### Removed
### Security

> **Pre-release.** What is not yet proven. Pre-releases only.
```

- Newest section first; one `## [Unreleased]` at the top.
- Subsections are `Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`,
  `Security`, nothing else.
- Every item opens with a **bold lead-in**: it is what a person scans. Items say
  what changed for the user, not which file moved.
- Links to the docs are relative (`docs/x.md`); the notes rewrite them to the
  tagged tree.
- A pre-release closes with the `> **Pre-release.**` blockquote. What goes in it
  is a judgement: the single biggest thing that is not yet proven.

`tools/release/changelog.py` holds these rules; `check_release.py` applies them
to the section being released and fails with the line that is wrong.

### What the release page says

`tools/release/render_release_notes.py` lays the section out the way every
release of the project family reads:

1. `## What's Changed since v<previous>` (the first release: `## What's Changed`)
2. the summary paragraph
3. the section's `###` subsections, as written
4. `## Installing`: what each platform does on first run
   (`tools/release/install-notes.md`)
5. `## Artifacts`: the *Platform | Files* table, the verification row, and a link
   to the README's install section
6. `**Full Changelog**: …/compare/v<previous>...v<this>` (not the first release)
7. the `> **Pre-release.**` caveat

The Artifacts sentence ("with build-provenance attestations, an SPDX SBOM and GPG
signatures") lists only what the run did. Preview it any time:

```sh
python tools/release/render_release_notes.py --version 0.1.0-beta.1 \
    --repo iainchesworthlabs/cubeulator --from-unreleased --signed --attested --sbom
```

## Preparing the release

1. **Be ready.** CI is green on `develop` for the commit you mean to release, and
   `python tools/release/check_release.py --dry-run` is clean (it reads
   `[Unreleased]` as if it were cut and applies the same rules).
2. **Rehearse.** Run the pipeline as a [dry run](#the-dry-run) from `develop`
   and read what it produced. Do this before the *first* release, and again after
   any change to the packaging or to this workflow.
3. **Prepare the release on a branch.**

   ```sh
   git switch develop && git pull
   git switch -c release/0.1.0-beta.1
   python tools/release/prepare_release.py 0.1.0-beta.1
   ```

   That moves `[Unreleased]` under `## [0.1.0-beta.1] - <today>` and sets the
   version pair. It says what the section still needs (the summary, the bold
   lead-ins, the caveat); write those. `--check` changes nothing and only says
   what is wrong. Open a PR into `develop`; it goes through the merge queue like
   any other.
4. **Open the release PR**: `develop` → `main`, merge commit. The `Release
   readiness` check puts the rendered notes in its summary: read them there.
   `main`'s ruleset is in [Branch protection](branch-protection.md).
5. **Tag the merge commit on `main`:**

   ```sh
   git switch main && git pull
   git tag -a v0.1.0-beta.1 -m "Cubeulator 0.1.0-beta.1"
   git push origin v0.1.0-beta.1
   ```

   or run the workflow by hand from `main` with `dry_run` unticked: it makes the
   tag itself, at the end, once everything has passed, so a failed run leaves
   no tag behind.
6. **Watch the `Release` workflow.** `Verify the release` is quick; the three
   `Build` jobs are the long ones (a cold vcpkg cache is hours on Windows and
   macOS; the cache CI keeps makes it much less). If a platform fails, fix it
   and tag again, or re-run the failed jobs.
7. **Review the draft** under Releases: the notes, the four packages, the
   checksums and signatures. Edit the notes if you want to.
8. **Publish it.**
9. **Back-merge**: a PR from a `support/back-merge-<version>` branch cut from
   `main`, into `develop`, so `develop` has the release.

## The dry run

Actions → **Release** → *Run workflow*, branch `develop` (or the release
branch), `dry_run` ticked (it is ticked by default). It is the whole release
minus the two things that cannot be undone:

| | dry run | release |
|---|---|---|
| version, changelog and tag checks | yes (`[Unreleased]` stands in for a section not cut yet) | yes |
| build, package, `check-package.sh`, three platforms | yes | yes |
| `SHA512SUMS`, GPG signatures | yes, with a key made for the run | yes, the project key |
| the provisioned key | proven to import and sign, in a keyring that is thrown away | used |
| SBOM, provenance and SBOM attestations, `gh attestation verify` | yes | yes |
| notes rendered from the changelog | yes | yes |
| the tag, the release | **no** | yes |

What it leaves is the `release-<version>` artifact of the run: the exact files
and `notes.md` that a release would attach, and the notes are also in the run's
summary. Download it, install the package on a machine, read the notes. The dry
run's signatures verify against the `cubeulator-signing-key.asc` in the same
artifact, and against nothing else.

It can be run from any branch that has the workflow file, without a tag, and so
before the first release PR has merged (checked on 2026-10-14, from a scratch
branch):

```sh
gh workflow run release.yml --ref <branch> -f dry_run=true
```

Dry-run attestations are real attestations of files that were never released;
they sit in the repository's attestations list under that run and are harmless.

A PR that changes `release.yml`, `check-package.sh` or `tools/release/` runs the
tools' self-test and lets GitHub parse the file; `ci.yml` already builds and
checks the packages on every push.

## The signing key

The release is signed with the project's GPG key, held as two repository
secrets (the same names the other projects of the family use):

| Secret | Value |
|---|---|
| `REPO_GPG_PRIVATE_KEY` | the armored secret key |
| `REPO_GPG_PASSPHRASE` | its passphrase (omit for a key without one) |

A **real release without the key fails**, before anything is published, unless
the dispatch ticks `unsigned`: a missing or mistyped secret cannot become an
unsigned release unnoticed. A dry run reports the key as missing as a warning,
and, when it is present, proves it imports and signs.

To make one:

```sh
gpg --quick-generate-key "Cubeulator <address>" rsa4096 sign 5y
gpg --armor --export-secret-keys "Cubeulator" > cubeulator-signing-private.asc
gh secret set REPO_GPG_PRIVATE_KEY < cubeulator-signing-private.asc
gh secret set REPO_GPG_PASSPHRASE
# then delete cubeulator-signing-private.asc; keep a copy of the key somewhere safe
gpg --fingerprint "Cubeulator"     # publish this in SECURITY.md
```

or reuse the key of another project of the family, so that one fingerprint
vouches for all of them. That is a choice between convenience for people
verifying and the blast radius of one leaked secret; either works with the
workflow.

Keys expire: put the expiry in a calendar. The dry run is the check that the key
is still good before a release needs it.

## Verifying a download

Each file has two independent proofs of origin. [SECURITY.md](SECURITY.md#verifying-build-authenticity)
says how to check them. In short:

```sh
# the checksums, then the signature on them
gpg --import cubeulator-signing-key.asc
gpg --verify SHA512SUMS.asc SHA512SUMS && sha512sum -c SHA512SUMS

# which repository, workflow and commit built a file
gh attestation verify Cubeulator-0.1.0-beta.1-linux-x64.deb --repo iainchesworthlabs/cubeulator
```

## What stops a release

The workflow refuses, with the reason in the log, when:

| | |
|---|---|
| The tag is not `vX.Y.Z` or `vX.Y.Z-(alpha\|beta\|rc).N` | |
| The tag is not `v` + the version `CMakeLists.txt` declares, label included | the packages are named for it, so `v0.1.0` would hold `Cubeulator-0.1.0-beta.1-*` |
| `CHANGELOG.md` has no dated `## [version]` section, or it is not the newest | the notes are rendered from it and nothing else |
| The section has no summary, an item without a bold lead-in, or (pre-release) no closing caveat | the same rules `check_release.py` states |
| A final release is not on `main` | it is tagged on `main`, after the release PR; a pre-release may be cut from a `release/*` branch to be tried first |
| A dispatched release names a tag that exists | |
| A package fails `packaging/check-package.sh` | Qt, the executable, the icon, the identifier, the camera string, the notices: the same checks `ci.yml` makes on every push |
| A platform produces no package, or two | |
| The signing key is not provisioned (a real release) | unless `unsigned` is ticked |
| A package has no attestation that verifies | |

## If a release has to be undone

A draft can be deleted from the Releases page with no trace. A published
release can be deleted too, but a tag people may have fetched should not be
moved: tag the next patch (or the next pre-release counter) instead. If the
release page was wrong and the files were right, edit the notes in place.

## Design choices, and why

- **A draft, not a published release.** The notes are rendered and the packages
  are unsigned by the OS; a person should look before the world can download
  them.
- **The notes come from the changelog**, so that they are written once, where
  the change is made, and reviewed in a PR. A release with no matching section
  fails outright rather than falling back to a list of commits.
- **The same machinery signs a rehearsal and a release.** A dry run that skipped
  the signing and attestation steps would make the release the first time they
  run.
- **The signing job and the publishing job are different jobs.** `sign` holds
  the key and the OIDC identity and can write nothing to the repository;
  `publish` can write releases and holds neither.
- **The build reads the vcpkg cache and never writes it**, so nothing a release
  run does can end up in what the other workflows trust.
- **Everything it uses is pinned** (see [CI & dependencies](ci.md)).
