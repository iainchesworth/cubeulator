# CI & dependencies

## Workflows

| Workflow | Purpose | Blocking? | In the merge queue? |
| --- | --- | --- | --- |
| `ci.yml` | Build + test matrix (Linux/macOS/Windows, release only), Android build (Qt/QML app, release only), Web build (the same app as WebAssembly, release only), iOS build-only (release only), coverage gate | Yes on `main`; on `develop` as each check goes green | Not yet |
| `release.yml` | Builds the three desktop packages on a version tag, signs and attests them, and attaches them to a **draft** GitHub Release with notes rendered from `CHANGELOG.md` ([Cutting a release](releasing.md)). Run by hand it is a dry run. A PR that changes it runs only the release tools' self-test | N/A (tags and manual runs) | No |
| `release-check.yml` | On a PR into `main`: is this a release the Release workflow will accept, and what would its notes say (in the job summary) | No (advisory) | No |
| `codeql.yml` | Static analysis (C++) | Yes on `main`; on `develop` once green | Not yet |
| `branch-name-check.yml` | Enforces gitflow branch naming | Yes | Yes (skipped, a pass) |
| `dependency-review.yml` | Fails on a moderate+ vulnerability newly introduced in a PR | Yes | Yes |
| `osv-scanner.yml` | Scans `vcpkg.json` against the OSV database | No (visible only) | No |
| `zizmor.yml` | Lints the workflow YAML itself | No (visible only) | No |
| `scorecard.yml` | OpenSSF Scorecard supply-chain health score | No (badge/score) | No |
| `docs.yml` | Builds the mkdocs site and the web app (`.github/actions/web-build`, the same one `ci.yml`'s Web leg uses), and publishes both to GitHub Pages - the app under `/app/` | N/A (push to `main` only) | No |
| `dependabot-auto-merge.yml` | Enables auto-merge on Dependabot's minor/patch PRs for GitHub Actions and pip | No (acts on Dependabot PRs only) | No |
| `cache-hygiene.yml` | Prunes the Actions cache quota: closed PRs' and deleted branches' entries, superseded vcpkg and Qt generations ([below](#the-vcpkg-binary-cache)) | No (acts on the cache only) | No |

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
`system`, `android`, `ios`, `web`, `web-test`, `perf`, `web-perf`, or `all` (the
default, which includes neither `perf` nor `web-perf`). An unknown name fails
the run rather than running nothing. `platform-isolation` is the no-`#ifdef` check
([platform-isolation.md](platform-isolation.md)): fast, and independent of the
build legs.
`system` is the Linux job that runs the system tests under Xvfb (see
[testing.md](testing.md#system-tests)); on Windows and macOS the same suite
runs as a non-blocking step of their build legs.
`web-test` is the browser's tests ([testing.md](testing.md#browser-tests)): it
builds the `web-emscripten-diagnostics` app and the `web-emscripten-tests`
binaries from one vcpkg tree, runs the unit, integration and performance cases
as WebAssembly under Node with `ctest`, and then drives the app in Chromium with
Playwright (`tests/web`). It uploads the Playwright report and traces. Like
`web`, it is not a required check until it has run green on `develop`.
`web-perf` is its wall-time half: the same diagnostics build, the `perf`
Playwright project on one worker with `CUBEULATOR_WEB_PERF_TIMING=1`, and the
journeys' summaries, Chrome traces and CSVs uploaded as
`cubeulator-web-perf-reports`. It runs only when named, for the reason `perf`
does.
`perf` is the wall-time half of the performance tier: it builds only
`cube_perf_tests` on the Linux Release preset (no Qt, no app) and runs
`ctest --preset linux-gcc-perf`. It runs only when it is named - `all` leaves it
out - because a stopwatch on a shared runner is the one check that can fail
for no reason of its own; the work budgets are exact and already run in every
build leg's plain `ctest` ([testing.md](testing.md#performance-tests)).

Jobs of a dispatched run are named `dispatch - <leg>`, deliberately not the
names of the required checks: a run that skips legs would otherwise report
"skipped" (which counts as a pass) under a required check's name on the
branch's head commit.

### Self-hosted runners

The org's hosted pool (20 jobs, 5 macOS) is shared with ICL Forge, and under
load even a seconds-long required check queues for half an hour. Six jobs can
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
| `Prune Actions caches` | `cache-hygiene.yml` |

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
  limit; ONNX Runtime has since been removed, which has not been
  re-measured), and everything macOS, iOS and Android, which the fleet cannot
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

## The vcpkg binary cache

A cold vcpkg install is the longest thing CI does: ONNX Runtime alone is 39
minutes on the Windows leg, OpenCV 14 on Linux, and a whole macOS or iOS
configure is over an hour. With the cache restored it is seconds (96 of 98
Linux packages came back in 19 s; a macOS configure took 43 s instead of 67
minutes). Every job that runs vcpkg uses the same two composite actions,
[`.github/actions/vcpkg-cache/restore` and `/save`](https://github.com/iainchesworthlabs/cubeulator/tree/main/.github/actions/vcpkg-cache),
which wrap vcpkg's `files` binary-cache provider (its `x-gha` provider was
removed) around `actions/cache`.

| Step | What it does |
| --- | --- |
| restore | Points `VCPKG_BINARY_SOURCES` at `$RUNNER_TEMP/vcpkg-cache` and fills it from the **newest** entry whose key starts `vcpkg-binary-cache-<triplet>-`. |
| *(build)* | vcpkg restores every archive whose ABI hash matches and builds only the rest. |
| save | Runs with `if: always()`, so a red or cancelled leg keeps what it already built. Stores the directory under `vcpkg-binary-cache-<triplet>-<hash of its file listing>` - **only if** an archive was added since the restore **and** that exact key is not already readable from this ref. |

The job summary of every leg carries one line saying which of those happened,
and the log has `vcpkg cache: restored N archive(s) from <key>` and
`vcpkg cache: N package(s); restored X, now Y; save=...`.

### One cache per triplet, not per build type

Keying by the CMake preset (Debug, Release, coverage) was tried and removed: it
stored near-identical copies of the same archives. vcpkg names an archive by an
ABI hash that already covers what actually changes the binaries - the port and
its dependencies' versions, the triplet, the compiler - and every archive holds
both the debug and the release variant of the library (`Building x64-windows-dbg`
and `-rel` happen in one pass). The project's own `CMAKE_BUILD_TYPE` and the
compiler it is built with are not in that hash: `fmt:x64-linux` has ABI
`51a55f9e...` in the Coverage (g++-15), Linux (g++-15) and clang-tidy
(clang++-21) jobs of one run. So the triplet is the whole cache identity:

| Triplet | Legs that share it |
| --- | --- |
| `x64-linux` | Linux (GCC 15), Coverage, System tests, clang-tidy, CodeQL |
| `x64-windows` | Windows (MSVC) |
| `arm64-osx` | macOS (AppleClang) |
| `arm64-ios` | iOS (also holds the `arm64-osx` host tools it builds) |
| `arm64-android` | Android |
| `wasm32-emscripten-threads` | Web (`ci.yml`'s Web leg, and `docs.yml`'s `web` job); an overlay triplet, `cmake/triplets/`, that is vcpkg's `wasm32-emscripten` plus `-pthread` |

Keying finer than that would not make a restore any more correct; it would only
multiply the 10 GB quota's consumption.

### Where an entry can be read from

An Actions cache entry is readable from **its own ref, the base branch of a pull
request, and the default branch (`main`)** - nothing else:

| Run | Reads | Writes to |
| --- | --- | --- |
| Pull request | `refs/pull/N/merge`, then `develop`, then `main` | `refs/pull/N/merge` |
| Push to `develop` | `develop`, then `main` | `develop` |
| Manual dispatch on a feature branch | that branch, then `main` - **not `develop`** | that branch |

Consequences worth knowing:

- `develop` is where the cache every pull request starts from is made, so a
  push to `develop` must run to the end. `ci.yml` and `codeql.yml` therefore do
  not cancel an in-progress run on `develop` or `main` (pull requests and
  dispatches still supersede themselves).
- The first dispatch on a feature branch is cold, because it cannot see
  `develop`'s entries; the second restores the first's. For a leg that has to
  be fast, open the pull request instead.
- A pull request whose dependency set equals `develop`'s stores nothing (the
  save step's exact-key lookup finds `develop`'s entry). Before that check, a PR
  that started from an older generation rebuilt the same set and stored its own
  1.6 GB copy of it: on 2026-10-13 four closed PRs held 6.5 GB of the 10 GB
  between them, three of them byte-for-byte the key `develop` already had.
- A pull request that *changes* the dependency set builds it cold, stores it in
  its own scope, and `develop` builds it cold again after the merge: the scopes
  cannot be bridged.

### Keeping inside the quota

The quota is 10 GB per repository. Past it GitHub evicts least-recently-used
entries without regard to what they cost: a one-hour macOS cache was gone within
two hours of being saved. [`cache-hygiene.yml`](https://github.com/iainchesworthlabs/cubeulator/blob/main/.github/workflows/cache-hygiene.yml)
runs `ci/prune-actions-cache.sh` when a pull request closes and on every push to
`develop` or `main`, and removes

1. entries whose ref no longer exists (closed PR, deleted branch or tag, merge
   queue branch) - nothing can read them again;
2. superseded generations: per ref, the newest vcpkg entry of each triplet and
   the newest Qt install of each OS and target, unless something read the older
   one in the last 20 minutes.

Content-addressed keys mean every change stores a *full* new copy, and vcpkg
never forgets an archive, so a family grows by the stale archives of every
dependency bump it has been through. There is no automatic reset (a wrong one
would throw an hour of work away); when `Largest families kept` in a hygiene
run's summary shows a triplet far past what its install plan needs, reset it:

```sh
gh workflow run cache-hygiene.yml -f purge=vcpkg-binary-cache-x64-windows- -f dry_run=false
```

The next run on that triplet builds from cold and stores a clean entry.
`./ci/prune-actions-cache.sh --dry-run` shows the same report locally.

### Not done, and why

- **Release-only triplets** (`VCPKG_BUILD_TYPE=release`) would halve both the
  archives and the build time, but the Coverage and clang-tidy legs build Debug
  and the Windows Debug runtime does not mix with release libraries. The one
  exception is `wasm32-emscripten-threads`, which is release-only: there is no
  debug C runtime to mix, and the web app links release libraries in every
  configuration.
- **A per-package remote store** (vcpkg's `nuget` provider on GitHub Packages,
  or S3) uploads each archive the moment it is built - so a cancel loses
  seconds, not a 39-minute build - has no quota, and is readable from every
  ref. It is the structural fix for the scoping and eviction above; it costs
  `packages: write` on the build jobs, `mono` on the Linux and macOS images, and
  a decision about sharing one feed with the other repositories. Take it if the
  measures above are not enough.
- **Default branch `develop`** would make every `develop` entry readable from
  dispatches on feature branches. It is a repository setting with wider effects
  (Dependabot's target, Scorecard, the CodeQL default-branch scan) and is the
  owner's call.

### The same shape in the other repositories

The contract any repository should follow: one directory of archives per
triplet; restore the newest entry of the triplet; save under a key derived from
the directory's contents, from an `always()` step, and only when something was
added and the key is not already readable; prune closed-PR and superseded
entries. `ci/prune-actions-cache.sh` is the same file in all four repositories.
The others differed from this in ways that cost them, and have a pull request
each: aqualink-automate's hosted Linux and macOS legs restored a ~200-byte
cache because `lukka/run-vcpkg` redirected vcpkg to another directory
([#223](https://github.com/iainchesworthlabs/aqualink-automate/pull/223));
ICL Forge threw a failed job's built packages away
([#1569](https://github.com/iainchesworthlabs/iclforge/pull/1569)) and had
4.8 GB of ccache generations and merge-queue copies
([#1568](https://github.com/iainchesworthlabs/iclforge/pull/1568));
CountdownSolver's vcpkg set is Catch2 only, and its quota problem is Qt
installs stored per pull request
([#111](https://github.com/iainchesworth/CountdownSolver/pull/111)).

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
| Emscripten | 5.0.5 (`.github/actions/web-build`) | **No: Qt decides.** Qt 6.12.0 requires exactly 5.0.5 and its toolchain file stops the configure on any other (doc.qt.io/qt-6/wasm.html). Qt 6.11 and 6.10 named 4.0.7. Move it when Qt moves, to the version Qt names for that release. |
| Playwright | 1.61.1 (`tests/web/package.json`, exact) | Maybe: the browser tests' only dependency. Pinned exactly because the Chromium it drives is the build that release names; a bump is a deliberate change to that file and the lockfile, checked by the `web-test` leg. Dependabot does not watch it (an npm entry would put test-runner bumps in the auto-merge lane). |
| Android | NDK r28c (28.2.13676358), JDK 21, platform 36, build-tools 36.0.0 | r30 exists. r28c is newer than the r27c Qt 6.12 names and builds its libraries with: Qt recommends matching it but does not rule a newer NDK out, so this is outside Qt's tested configuration on purpose (r27c is the fallback). r28c's libc++ has `std::expected::transform`/`and_then`, `std::format`/`std::print`, `ranges::contains`/`fold_left` and `mdspan`, which r26b lacks; it still lacks float `from_chars`, `views::enumerate` and `flat_map`. |
| CMake | 4.4.2, exact (`lukka/get-cmake`'s catalog; the project's policies go to 4.4) | Yes: Kitware has 4.4.4, which the pinned action cannot install until it is bumped (see [What CI pins](#what-ci-pins)). |
| mkdocs / mkdocs-material | 1.6.x / 9.7.x | mkdocs 2.0 is still only a development release and mkdocs-material has no series newer than 9, so the range caps stay. |
| vcpkg libraries | OpenCV 4.x, ONNX Runtime 1.23, Catch2, fmt, GLFW | Upstream is ahead (OpenCV 5.0, ONNX Runtime 1.31) but vcpkg's registry has no newer major of either, so these cannot move without a port of our own. A newer baseline only brings minors. |
| iOS deployment target | 18.0 (`CMakePresets.json`) | No - 18 is Qt 6.12's minimum iOS (it was 16.3 for Qt 6.8), built with Xcode 16's iOS 18 SDK. iOS CI is build-only and does not build Qt, so this is the project's own floor, not something Qt exercises there. |

## What CI pins

Nothing CI runs floats, so a green run is reproducible and a supply-chain
change is a reviewed diff:

| | pinned to | kept current by |
|---|---|---|
| Every GitHub Action, and the composite actions in `.github/actions` | A full commit SHA, with the version in a trailing comment | Dependabot, weekly, grouped, with a **7-day cooldown**: a release is not proposed until it has been out a week |
| CMake and Ninja (`lukka/get-cmake`) | `cmakeVersion` / `ninjaVersion`, exact, and one the pinned action's catalog lists: a CMake newer than the action (4.4.4 against `get-cmake` 4.4.2, in October 2026) fails with "Cannot match ... in the catalog" until the action is bumped | By hand, with the actions |
| NSIS (`choco install nsis`) | `--version`, exact | By hand |
| `gcovr` | `==`, exact | By hand |
| zizmor | `ci/requirements-zizmor.txt`, exact | Dependabot (`pip`, `/ci`), weekly |
| mkdocs and its theme | `docs/requirements.txt`, exact (their own dependencies still float) | By hand |
| Qt (`install-qt-action`) | `6.12.0`, the LTS line (see Toolchain versions above) | By hand, deliberately: moving Qt moves the app's platform, and its minimum macOS and Windows with it |
| vcpkg and the ports | The submodule commit and `builtin-baseline` | By hand |
| Runner images | The label (`ubuntu-26.04`, `macos-26`, `windows-2025-vs2026`; `macos-15` for iOS) | By hand |

What is **not** pinned: the apt and Homebrew packages installed on a runner
(`libglu1-mesa-dev`, `libomp`, ...), which come with the image and have no
sensible pin short of a mirror.

An action is bumped to the newest release that is at least a week old. A
newer one is not skipped for good: Dependabot proposes it once it has aged,
and the same rule applies when bumping by hand.

## Dependencies

vcpkg (vendored submodule, manifest mode) supplies Catch2, fmt, and OpenCV on every
triplet, desktop and mobile alike, via a
chainloaded toolchain on `arm64-ios`/`arm64-android` — see below. Qt is a
separate prebuilt install (`jurplel/install-qt-action` in CI,
`ci/install-qt.*` locally) — kept out of vcpkg for build size and Windows
`MAX_PATH` reasons.

Mobile (arm64-ios/arm64-android) vcpkg builds of opencv4 are
new and not yet proven out in CI (the `android-build`/`ios-build` jobs stay
`continue-on-error: true` until they are) — see
[Dependency strategy](dependency-strategy.md) for the evidence and the
fallback plan if it doesn't work out.

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
- **Emscripten** — `version:` of `emscripten-core/setup-emsdk` in
  `.github/actions/web-build/action.yml`, with the Qt version in the same file
  and the `emsdk` version in `docs/building.md`. Qt names the one Emscripten
  each release is built with; follow Qt's *Qt for WebAssembly* page.
- **Compilers** — `g++-15`/`gcov-15` in `ci.yml` and `codeql.yml`, and the
  Clang packages the `clang-format`/`clang-tidy` jobs install.
- **Runner images** — every `runs-on:` label, and the `matrix.os` values.
- **CMake policy range** — the upper bound in `cmake_minimum_required()`.
- **Libraries built by vcpkg** (OpenCV, ...) — these move
  only with the `builtin-baseline`, and only as far as the vcpkg registry has
  them.
