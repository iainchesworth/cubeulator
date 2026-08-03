# M3T (vendored)

Region-based 6-DoF object tracker used by the tracking-first vision pipeline
(see `docs/pipeline-plan.md`, Phase 2). Consume it only through the
`cube::m3t` target defined in `third_party/CMakeLists.txt`.

## Provenance

- Upstream: <https://github.com/DLR-RM/3DObjectTracking> (subdirectory `M3T/`)
- Commit: `f02106186a1e756450226b3b793e309506817c58`
  ("Merge pull request #66 from agimus-project/omp")
- Licence: MIT (Manuel Stoiber, German Aerospace Center - DLR). See `LICENSE`
  (copied from the upstream repository root).

Vendored subset, upstream layout preserved: `CMakeLists.txt`,
`src/` (including `src/CMakeLists.txt`), `include/m3t/`, `third_party/`,
`LICENSE`. Upstream's `test/`, `examples/`, `doc/` and `data/` are **not**
carried (gtest, datasets, Doxygen - nothing the library needs).

Bundled third-party headers under `third_party/` carry their full licence
texts in-file, reproduced here as required notices for source and binary
distribution:

- `third_party/filesystem/` - MIT, Copyright (c) 2018 BrainStone
  (`SPDX-License-Identifier: MIT`)
- `third_party/nanoflann/` - BSD licence, Copyright 2008-2009 Marius Muja and
  David G. Lowe, Copyright 2011-2021 Jose Luis Blanco
  (`SPDX-License-Identifier: BSD-2-Clause`)
- `third_party/tiny_obj_loader/` - MIT, Copyright (c) 2012-2018 Syoyo Fujita
  and many contributors (`SPDX-License-Identifier: MIT`)

## Local patches

All divergence from upstream is greppable as `SPIKE PATCH` (from the Phase 2
feasibility spike) or `CUBEULATOR PATCH` (added at vendoring time).

| Patch | File | Rationale |
| --- | --- | --- |
| SPIKE PATCH 1 | `CMakeLists.txt` | Upstream force-sets static GLEW/OpenCV and the /MT runtime on Windows; made optional (`M3T_WIN32_STATIC`, our wrapper sets it OFF) so dynamic vcpkg x64-windows /MD libraries link. |
| SPIKE PATCH 2 | `CMakeLists.txt` | Drop the `xfeatures2d` OpenCV component: it lives in opencv-contrib, and only the (excised) texture modality used it. |
| SPIKE PATCH 3 | `src/CMakeLists.txt` | Texture modality excised from the build (source kept on disk): needs opencv-contrib, and the region modality is the content-agnostic one the pipeline wants. |
| SPIKE PATCH 4 | `CMakeLists.txt` | Drop the Eigen `3.3.2` version pin: vcpkg ships Eigen 5.x, whose package config rejects a cross-major version request. |
| CUBEULATOR PATCH 1 | `CMakeLists.txt` | Do not `add_subdirectory` the non-vendored `test/`, `examples/`, `doc/`. |
| CUBEULATOR PATCH 2 | `src/renderer.cpp` | `Renderer::referenced_body_ptrs()` returned a reference to a temporary (`return {};`) - real undefined behaviour (MSVC C4172); now returns a function-local static empty vector. |
| CUBEULATOR PATCH 3 | `src/body.cpp` | `Body::get_id()` had no return on the fall-through path (MSVC C4715, UB if reached); the enum's only remaining case is REGION, so return `region_id_`. |
| CUBEULATOR PATCH 4 | `CMakeLists.txt` | Upstream adds MSVC's `-openmp:experimental` only for Release, but the OpenMP `simd` pragmas in `color_histograms.cpp` need it in every configuration (C7660 in RelWithDebInfo/Debug otherwise); add it to the all-config flags on WIN32. |

Build-configuration choices that are *not* source patches (upstream options,
set by the wrapper in `third_party/CMakeLists.txt`): `M3T_WIN32_STATIC=OFF`,
`USE_AZURE_KINECT=OFF`, `USE_REALSENSE=OFF`, `USE_GTEST=OFF`. The
`azure_kinect_camera` / `realsense_camera` / `texture_modality` sources stay
on disk, un-compiled, to keep the tree diffable against upstream.

## Updating

1. Clone upstream and check out the target commit; note it here.
2. Diff `M3T/` against this directory *excluding* the patch blocks:
   `grep -rn "SPIKE PATCH\|CUBEULATOR PATCH" third_party/m3t/` lists every
   local change; re-apply each one that upstream has not fixed (upstream
   issues for patches 2-4 and the two UB fixes may make some obsolete).
3. Copy the subset listed above (no `test/`/`examples/`/`doc/`/`data/`),
   update the commit hash + patch table in this README.
4. Rebuild, then run the smoke checks:
   `CUBE_M3T_SMOKE=1 cube_m3t_tests "[.m3t-smoke]"` (offscreen silhouette
   render + 100-frame deterministic tracking; see
   `tests/unit/m3t_vendored_test.cpp`).

## Headless / determinism contract

The Phase 2 spike (2026-08, `D:\cubeulator-build\m3t-spike\`) established on
this stack: MSVC build with the four spike patches; GLFW hidden-window
offscreen rendering works in a plain console process (no visible UI); a
100-frame synthetic track costs ~0.8 ms/frame and produces bit-identical
poses across repeated runs (including regenerate-model vs load-cached-model
runs). The smoke test re-verifies all of this on demand.
