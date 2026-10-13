# CI & dependencies

## Workflows

| Workflow | Purpose | Blocking? | In the merge queue? |
| --- | --- | --- | --- |
| `ci.yml` | Build + test matrix (Linux/macOS/Windows, release only), Android build (Qt/QML app, release only), iOS build-only (release only), coverage gate | Yes on `main`; on `develop` as each check goes green | Not yet |
| `codeql.yml` | Static analysis (C++) | Yes on `main`; on `develop` once green | Not yet |
| `branch-name-check.yml` | Enforces gitflow branch naming | Yes | Yes (skipped, a pass) |
| `dependency-review.yml` | Fails on a moderate+ vulnerability newly introduced in a PR | Yes | Yes |
| `osv-scanner.yml` | Scans `vcpkg.json` against the OSV database | No (visible only) | No |
| `zizmor.yml` | Lints the workflow YAML itself | No (visible only) | No |
| `scorecard.yml` | OpenSSF Scorecard supply-chain health score | No (badge/score) | No |
| `docs.yml` | Builds and publishes the mkdocs site to GitHub Pages | N/A (push to `main` only) | No |
| `dependabot-auto-merge.yml` | Enables auto-merge on Dependabot's minor/patch PRs for GitHub Actions and pip | No (acts on Dependabot PRs only) | No |

See [Branch protection and the merge queue](branch-protection.md) for which
checks are required on `develop` and `main`, how the queue is configured, and
the `merge_group` rule a workflow has to follow to be part of it.

### Running some legs by hand

A push, pull request or queue entry always runs every leg of `ci.yml`. A
manual dispatch can name just the ones it needs, which matters because the
whole matrix is hours of hosted runners and the pool is shared:

```sh
gh workflow run ci.yml --ref <branch> -f legs=macos
gh workflow run ci.yml --ref <branch> -f legs=clang-format,linux
```

`legs` is a comma-separated list (no spaces) of `clang-format`,
`platform-isolation`, `clang-tidy`, `linux`, `macos`, `windows`, `coverage`,
`system`, `android`, `ios`, or `all` (the default). An unknown name fails the
run rather than running nothing. `platform-isolation` is the no-`#ifdef` check
([platform-isolation.md](platform-isolation.md)): fast, and independent of the
build legs.
`system` is the Linux job that runs the system tests under Xvfb (see
[testing.md](testing.md#system-tests)); on Windows and macOS the same suite
runs as a non-blocking step of their build legs.

Jobs of a dispatched run are named `dispatch - <leg>`, deliberately not the
names of the required checks: a run that skips legs would otherwise report
"skipped" (which counts as a pass) under a required check's name on the
branch's head commit.

### Self-hosted runners

The org's hosted pool (20 jobs, 5 macOS) is shared with ICL Forge, and under
load even a seconds-long required check queues for half an hour. Five jobs can
therefore run on the organisation's self-hosted fleet instead (the
`iainchesworthlabs/ci-runners` repository builds it; the org's runner groups,
not this repo, decide who may use it):

| Job | Workflow |
| --- | --- |
| `Validate gitflow branch name` | `branch-name-check.yml` |
| `Scan dependency diff` | `dependency-review.yml` |
| `clang-format` | `ci.yml` |
| `platform-isolation` | `ci.yml` |
| `Plan build legs` | `ci.yml` |

They are routed by the `CONTROL_RUNNER_JSON` repository variable, a JSON array
of runner labels. Unset, each job runs on the hosted image it names, exactly as
before.

```sh
gh variable set CONTROL_RUNNER_JSON --body '["self-hosted","Linux","X64","shared"]'   # on
gh variable delete CONTROL_RUNNER_JSON                                                # off
```

- **Deleting the variable is the kill switch.** It is read fresh on every run,
  so a dead fleet can never lock it shut. Required checks queued on a dead
  fleet do not fail on their own; delete the variable and re-run them.
- **Fork pull requests never leave the hosted pool.** The `runs-on:`
  expression pins them to the hosted image whatever the variable says, because
  fork code must not run on a runner inside the owner's network. Any job added
  to this routing must carry the whole expression, pin included.
- **Keep `shared` in the label set.** It selects the general pool (on-prem VMs,
  and AWS spot instances that start only when the on-prem pool is busy) and
  excludes the `big` runners, which another repository's heavy builds depend
  on.
- **Not routed:** the build, coverage, clang-tidy and system-test legs (cold
  vcpkg builds of ONNX Runtime and OpenCV took 87-131 minutes on hosted
  runners in July, against the fleet's 12 GB of RAM and AWS's 90-minute
  limit), and everything macOS, iOS and Android, which the fleet cannot
  build. Moving one is a measured, separate change.
- **Local actions are referenced as `$/.github/actions/...`, not
  `./.github/actions/...`** (the vcpkg cache restore/save steps; zizmor's
  `self-repository` audit asks for it). `$/` resolves to this repository at
  the workflow's own commit, so the action does not depend on the workspace.
  It needs runner **2.336.0 or newer** and is github.com-only. Every job that
  uses it today runs on a hosted image; the fleet reported 2.337.0 on
  2026-10-13. Check the fleet's runner version before routing such a job
  there: GitHub's changelog makes 2.336.0 the minimum.
  actionlint does not parse `$/` yet (as of 1.7.12,
  [rhysd/actionlint#711](https://github.com/rhysd/actionlint/issues/711));
  nothing in this repository runs it.

## Toolchain versions

What CI builds with, and why anything is not simply "the newest". The pins that
are bumped by hand are listed under [Dependabot](#pins-dependabot-cannot-see).

| Component | In use | Newer exists? |
| --- | --- | --- |
| Qt | 6.12.0 (LTS; every fourth minor, 6.8 before it) | No - 6.12.0 is the newest release. The Windows kit is `msvc2022_64`; Qt ships no MSVC 2026 x64 kit, and it works with the VS 2026 toolset. Installing the Windows kit needs an unreleased aqtinstall (the pinned commit in `ci.yml` and `ci/install-qt.ps1`): the last release predates Qt 6.11's per-architecture repository layout. |
| MSVC | Visual Studio 2026, via `windows-2025-vs2026` | No. |
| GCC | 15.2 (`g++-15`, `gcov-15`) | Yes: GCC 16 is released upstream, but ubuntu-26.04's `gcc-16` is a March 2026 snapshot (`16-20260322`), not a release. The `Linux (GCC 15)` check name is also required on `main`. |
| clang-format | 23.1.3, pinned PyPI wheel (`ci/requirements-lint.txt`) | No. |
| clang-tidy and its compiler | Clang 23.x from apt.llvm.org | No, but it is a snapshot of `release/23.x` rebuilt as the branch moves, not a frozen version; there is no clang-tidy 23 wheel on PyPI yet. |
| AppleClang | Xcode 26 on `macos-26` (desktop); `macos-15` (iOS) | iOS stays on `macos-15` for the open vcpkg pkg-config issue [microsoft/vcpkg#50681](https://github.com/microsoft/vcpkg/issues/50681). |
| Android | NDK r28c (28.2.13676358), JDK 21, platform 36, build-tools 36.0.0 | r30 exists. r28c is newer than the r27c Qt 6.12 names and builds its libraries with: Qt recommends matching it but does not rule a newer NDK out, so this is outside Qt's tested configuration on purpose (r27c is the fallback). r28c's libc++ has `std::expected::transform`/`and_then`, `std::format`/`std::print`, `ranges::contains`/`fold_left` and `mdspan`, which r26b lacks; it still lacks float `from_chars`, `views::enumerate` and `flat_map`. |
| CMake | newest 4.x (`lukka/get-cmake`, `cmakeVersion: latest`); policies to 4.4 | No. |
| mkdocs / mkdocs-material | 1.6.x / 9.7.x | mkdocs 2.0 is still only a development release and mkdocs-material has no series newer than 9, so the range caps stay. |
| vcpkg libraries | OpenCV 4.x, ONNX Runtime 1.23, Catch2, fmt, GLFW | Upstream is ahead (OpenCV 5.0, ONNX Runtime 1.31) but vcpkg's registry has no newer major of either, so these cannot move without a port of our own. A newer baseline only brings minors. |
| iOS deployment target | 18.0 (`CMakePresets.json`) | No - 18 is Qt 6.12's minimum iOS (it was 16.3 for Qt 6.8), built with Xcode 16's iOS 18 SDK. iOS CI is build-only and does not build Qt, so this is the project's own floor, not something Qt exercises there. |

## Dependencies

vcpkg (vendored submodule, manifest mode) supplies Catch2, fmt, OpenCV and ONNX
Runtime on every triplet, desktop and mobile alike, via a
chainloaded toolchain on `arm64-ios`/`arm64-android` — see below. Qt is a
separate prebuilt install (`jurplel/install-qt-action` in CI,
`ci/install-qt.*` locally) — kept out of vcpkg for build size and Windows
`MAX_PATH` reasons.

Mobile (arm64-ios/arm64-android) vcpkg builds of opencv4/onnxruntime are
new and not yet proven out in CI (the `android-build`/`ios-build` jobs stay
`continue-on-error: true` until they are) — see
[Dependency strategy](dependency-strategy.md) for the evidence and the
fallback plan if either doesn't work out.

## Dependabot

`.github/dependabot.yml` keeps the pins Dependabot *can* see current. Every
pull request targets `develop`, and a version is only proposed a week after it
is published (`cooldown`), so a compromised release has time to be caught.

| Ecosystem | What it updates | Cadence | Merges itself? |
| --- | --- | --- | --- |
| `github-actions` | The SHA-pinned actions in `.github/workflows` | Weekly | Minor/patch, grouped in one PR. A major is its own PR and waits for a human. |
| `pip` (`/docs`) | `mkdocs`, `mkdocs-material` within the capped ranges | Weekly | Minor/patch |
| `pip` (`/ci`) | `zizmor` (`ci/requirements-zizmor.txt`) | Weekly | Minor/patch |
| `vcpkg` + `gitsubmodule` | `builtin-baseline` in `vcpkg.json` and the `deps/vcpkg` checkout, as one PR | Monthly | No |

"Merges itself" means `dependabot-auto-merge.yml` runs `gh pr merge --auto
--merge` on the PR, which puts it in `develop`'s merge queue once the required
checks pass. It is the same command a maintainer would run: no ruleset is
bypassed, and a PR that fails a required check simply leaves the queue.

Auto-merge is only as safe as the required checks behind it, and today those
are `Validate gitflow branch name` and `Scan dependency diff` only (see
[Required checks](branch-protection.md#required-checks)). That is why the
vcpkg baseline, which rebuilds every dependency, is excluded: nothing required
would catch it breaking the build. Add an ecosystem to `ALLOWED_ECOSYSTEMS` in
`dependabot-auto-merge.yml` once the checks that would catch its breakage are
required on `develop`.

### Pins Dependabot cannot see

These are bumped by hand, and are the first place to look when asked "are we on
the latest?":

- **Qt** — `ci/install-qt.sh`, `ci/install-qt.ps1`, and the `version:` of both
  `jurplel/install-qt-action` steps in `ci.yml` (desktop and Android).
- **Android NDK / SDK / JDK** — the `sdkmanager --install` lines and
  `JAVA_HOME` in the `android-build` job. Qt pins the NDK it was built with;
  follow Qt's *Qt for Android* page, not the newest NDK.
- **Compilers** — `g++-15`/`gcov-15` in `ci.yml` and `codeql.yml`, and the
  Clang packages the `clang-format`/`clang-tidy` jobs install.
- **Runner images** — every `runs-on:` label, and the `matrix.os` values.
- **CMake policy range** — the upper bound in `cmake_minimum_required()`.
- **Libraries built by vcpkg** (OpenCV, ONNX Runtime, ...) — these move
  only with the `builtin-baseline`, and only as far as the vcpkg registry has
  them.
