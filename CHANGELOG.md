# Changelog

All notable changes to this project are documented in this file, in the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format. This
project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- M3T vcpkg overlay port (`vcpkg-overlay-ports/m3t`): the DLR M3T tracker
  pinned at upstream commit `f0210618` with a nine-patch series (mirrored 1:1
  by a prepared upstream PR branch), an optional `texture` feature, opt-in
  consumption via the `m3t` manifest feature +
  `CUBEULATOR_WITH_M3T_PORT` CMake option, a `m3t_port_smoke` proof tool,
  and a prepared microsoft/vcpkg registry submission — see
  [M3T vcpkg port](docs/m3t-port.md).
- Repository skeleton: folder layout, CMake build system (presets, warnings,
  sanitizers, coverage, packaging), vendored vcpkg submodule, CI/security
  workflows (CodeQL, OSV-Scanner, Zizmor, Scorecard, dependency review),
  mkdocs documentation site, and stub `cube::lib`/`cube::platform`/
  `cube::gpu_render` targets. No solver, computer vision, edge-AI, or
  rendering logic yet — see [Architecture](docs/architecture.md).
