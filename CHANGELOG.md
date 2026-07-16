# Changelog

All notable changes to this project are documented in this file, in the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format. This
project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Repository skeleton: folder layout, CMake build system (presets, warnings,
  sanitizers, coverage, packaging), vendored vcpkg submodule, CI/security
  workflows (CodeQL, OSV-Scanner, Zizmor, Scorecard, dependency review),
  mkdocs documentation site, and stub `cube::lib`/`cube::platform`/
  `cube::gpu_render` targets. No solver, computer vision, edge-AI, or
  rendering logic yet — see [Architecture](docs/architecture.md).
