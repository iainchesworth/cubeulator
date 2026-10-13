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

`legs` is a comma-separated list (no spaces) of `clang-format`, `clang-tidy`,
`linux`, `macos`, `windows`, `coverage`, `system`, `android`, `ios`, or `all`
(the default). An unknown name fails the run rather than running nothing.
`system` is the Linux job that runs the system tests under Xvfb (see
[testing.md](testing.md#system-tests)); on Windows and macOS the same suite
runs as a non-blocking step of their build legs.

Jobs of a dispatched run are named `dispatch - <leg>`, deliberately not the
names of the required checks: a run that skips legs would otherwise report
"skipped" (which counts as a pass) under a required check's name on the
branch's head commit.

## Dependencies

vcpkg (vendored submodule, manifest mode) supplies Catch2, fmt, OpenCV, ONNX
Runtime, and bgfx on every triplet, desktop and mobile alike, via a
chainloaded toolchain on `arm64-ios`/`arm64-android` — see below. Qt is a
separate prebuilt install (`jurplel/install-qt-action` in CI,
`ci/install-qt.*` locally) — kept out of vcpkg for build size and Windows
`MAX_PATH` reasons.

Mobile (arm64-ios/arm64-android) vcpkg builds of opencv4/onnxruntime/bgfx are
new and not yet proven out in CI (the `android-build`/`ios-build` jobs stay
`continue-on-error: true` until they are) — see
[Dependency strategy](dependency-strategy.md) for the evidence and the
fallback plan if any of the three doesn't work out.

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
- **Libraries built by vcpkg** (OpenCV, ONNX Runtime, bgfx, ...) — these move
  only with the `builtin-baseline`, and only as far as the vcpkg registry has
  them.
