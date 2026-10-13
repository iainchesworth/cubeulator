# Cutting a release

How a version of Cubeulator becomes three downloadable packages on a GitHub
Release. The workflow is `.github/workflows/release.yml`; what is in the
packages is [Packaging the desktop app](packaging.md).

A release is a deliberate human step (see
[Branch protection](branch-protection.md)). The workflow does everything up to
a **draft** release; making it public is the click that is yours.

## What a release holds

| Platform | Package | Built by |
|---|---|---|
| Windows 10 and later, 64-bit | `Cubeulator-<version>-windows-x64.exe` (NSIS installer) | CPack `NSIS` |
| macOS 12 and later, Apple Silicon | `Cubeulator-<version>-macos-arm64.dmg` | CPack `DragNDrop` |
| Linux, Debian and Ubuntu | `Cubeulator-<version>-linux-x64.deb` | CPack `DEB` |
| Linux, Fedora and openSUSE | `Cubeulator-<version>-linux-x64.rpm` | CPack `RPM` |
| | `SHA256SUMS.txt` | the publish job |

**None of it is code-signed.** The release notes the workflow writes say so
and say what a person will see: SmartScreen on Windows, Gatekeeper on macOS.
Where signing plugs in is the last section of [packaging.md](packaging.md).

## Steps

1. **Bump the version.** `project(Cubeulator VERSION x.y.z ...)` in the
   top-level `CMakeLists.txt` is the one place. The packages, the Windows
   version resource, `Info.plist` and the AppStream release all follow it.
2. **Land it on `main`** the usual way: a `develop` → `main` release PR (or a
   `hotfix/*`).
3. **Tag the commit on `main`** and push the tag:

   ```sh
   git switch main && git pull
   git tag -a v0.1.0 -m "Cubeulator 0.1.0"
   git push origin v0.1.0
   ```

   A tag with a suffix (`v0.2.0-rc.1`) is a pre-release and may be cut from a
   `release/*` branch instead of `main`.
4. **Watch the `Release` workflow.** `Verify the tag` is quick; the three
   `Build` jobs are the long ones (a cold vcpkg cache is hours on Windows and
   macOS; the cache CI keeps makes it much less). If a platform fails, fix it
   and tag again, or re-run the failed jobs.
5. **Review the draft** under Releases: the notes, the four packages and
   `SHA256SUMS.txt`. Edit the notes if you want to.
6. **Publish it.**

## What stops a release

The workflow refuses, with the reason in the log, when:

| | |
|---|---|
| The tag is not `vX.Y.Z` or `vX.Y.Z-suffix` | |
| The tag's version is not `CMakeLists.txt`'s | the packages are named for the project's version, so `v1.2.0` would hold `Cubeulator-1.1.0-*` |
| A final release (no suffix) is not on `main` | |
| A package fails `packaging/check-package.sh` | Qt, the executable, the icon, the identifier, the camera string, the notices: the same checks `ci.yml` makes on every push |
| A platform produces no package, or two | |

## Trying it without releasing

- **Run the workflow by hand** (Actions > Release > Run workflow, or
  `gh workflow run release.yml --ref <branch>`). Without a tag it builds and
  checks all three packages and keeps them as workflow artifacts; nothing is
  created. `workflow_dispatch` only works for a workflow file that is on the
  default branch.
- **A PR that changes `release.yml` or `check-package.sh`** runs only the
  `Verify the tag` job, so GitHub parses the file and a typo shows before a tag
  does. `ci.yml` already builds and checks the packages on that PR.

## If a release has to be undone

A draft can be deleted from the Releases page with no trace. A published
release can be deleted too, but a tag people may have fetched should not be
moved: tag the next patch version instead.

## Design choices, and why

- **A draft, not a published release.** The packages are unsigned and the
  notes are generated; a person should look before the world can download
  them.
- **The workflow can write in one job only.** `publish` has
  `contents: write`; the build jobs that run the whole toolchain have `read`.
- **It reads the vcpkg cache and never writes it**, so nothing a release run
  does can end up in what the other workflows trust.
- **Everything it uses is pinned** (see [CI & dependencies](ci.md)).
