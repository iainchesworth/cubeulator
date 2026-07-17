# CI & dependencies

## Workflows

| Workflow | Purpose | Blocking? |
| --- | --- | --- |
| `ci.yml` | Build + test matrix (Linux/macOS/Windows, release only), Android/iOS build-only (release only), coverage gate | Yes (desktop legs + coverage) |
| `codeql.yml` | Static analysis (C++) | Yes |
| `branch-name-check.yml` | Enforces gitflow branch naming | Yes |
| `dependency-review.yml` | Fails on a moderate+ vulnerability newly introduced in a PR | Yes |
| `osv-scanner.yml` | Scans `vcpkg.json` against the OSV database | No (visible only) |
| `zizmor.yml` | Lints the workflow YAML itself | No (visible only) |
| `scorecard.yml` | OpenSSF Scorecard supply-chain health score | No (badge/score) |
| `docs.yml` | Builds and publishes the mkdocs site to GitHub Pages | N/A (push to `main` only) |

See [Branch protection](branch-protection.md) for exactly which checks are
configured as required on `main`.

## Dependencies

vcpkg (vendored submodule, manifest mode) supplies Catch2, fmt, OpenCV, ONNX
Runtime, and bgfx on desktop triplets (fmt also resolves on mobile triplets
via a chainloaded toolchain — see below). Qt is a separate prebuilt install
(`jurplel/install-qt-action` in CI, `ci/install-qt.*` locally) — kept out of
vcpkg for build size and Windows `MAX_PATH` reasons.

Mobile (arm64-ios/arm64-android) dependency resolution is an open item — see
[Dependency strategy](dependency-strategy.md) for the evidence and the
planned hybrid approach.
