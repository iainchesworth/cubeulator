# M3T vcpkg port

[M3T](https://github.com/DLR-RM/3DObjectTracking) (Stoiber et al., DLR-RM)
is the multi-body/multi-modality 3D object tracker evaluated for the
tracking-first pipeline. This page documents how it is consumed as a
**vcpkg overlay port** instead of vendored sources, what the port patches
and why, and the status of upstreaming the port.

## How the port is consumed

- The port lives at `vcpkg-overlay-ports/m3t/` and is picked up
  automatically via the repo's `vcpkg-configuration.json`
  (`overlay-ports: ["./vcpkg-overlay-ports"]`).
- The manifest (`vcpkg.json`) exposes it behind the optional feature
  `m3t`; nothing in the default build depends on it.
- CMake option `CUBEULATOR_WITH_M3T_PORT` (default `OFF`) enables the
  manifest feature and builds `tools/m3t-smoke/` (`m3t_port_smoke`), which
  consumes the installed package strictly through
  `find_package(M3T CONFIG REQUIRED)` and renders an offscreen silhouette
  as an end-to-end check:

```bash
cmake --preset windows-msvc-release-d -DCUBEULATOR_WITH_M3T_PORT=ON
cmake --build --preset windows-msvc-release-d --target m3t_port_smoke
```

The exported target is plain `m3t` (upstream exports it un-namespaced) and
propagates C++17 plus all transitive dependencies (Eigen3, GLEW, glfw3,
OpenGL, OpenCV, OpenMP).

Port features:

| Feature | Default | Content |
|---|---|---|
| (core) | yes | Region + depth modalities, renderers, generator |
| `texture` | no | `TextureModality` (keypoint tracking); needs `opencv4[contrib]` for xfeatures2d |

## The pin

Upstream publishes **no tags or releases**, so the port pins a commit of
the `DLR-RM/3DObjectTracking` monorepo (M3T lives in its `M3T/`
subdirectory):

- `REF f02106186a1e756450226b3b793e309506817c58` — `master` HEAD as of
  2025-05-21 (and still HEAD at the time the port was written).
- Port version scheme: `version-date: 2025-05-21` (the commit date), the
  standard vcpkg scheme for untagged upstreams. A commit pin with
  REF+SHA512 is acceptable for vcpkg; the source archive is the whole
  monorepo (~218 MB), which is worth knowing before rebuilding from
  scratch.

## Patch list

All nine patches are `git format-patch` output of the upstream-prep branch
(below), applied 1:1 by the portfile, so the port and the upstream PR
cannot drift apart. Rationale per patch:

| Patch | Why |
|---|---|
| `0001` require CMake 3.10 | `cmake_minimum_required(3.5)` fails on CMake 4 without a manual `CMAKE_POLICY_VERSION_MINIMUM` escape hatch; `src/` already required 3.10 |
| `0002` fix `Renderer::referenced_body_ptrs` | returned a braced temporary as a const reference — UB (MSVC C4172), found during the feasibility spike |
| `0003` fix missing return in `Body::get_id` | falling off a value-returning function — UB (C4715) |
| `0004` optional Windows static runtime (`M3T_WIN32_STATIC`) | upstream hard-codes `/MT` + static GLEW/OpenCV hints; the port passes `OFF` so the vcpkg toolchain drives CRT linkage per triplet |
| `0005` accept newer Eigen | Eigen's config rejects version requests across majors, so the 3.3.2 pin fails against vcpkg's Eigen 5.x |
| `0006` make `TextureModality` optional (`USE_TEXTURE_MODALITY`) | its only OpenCV-contrib dependency (xfeatures2d) becomes the port's `texture` feature; follows upstream's existing `USE_AZURE_KINECT`/`USE_REALSENSE` pattern incl. `generator.h` guards |
| `0007` gate `test`/`examples`/`doc` subdirs | ~20 example binaries and a Doxygen-in-ALL target are dead weight in a library package; examples also fail to compile with texture off |
| `0008` resolve dependencies in the installed config | upstream installed the raw `EXPORT` file, so `find_package(M3T)` only worked if the consumer had already found every dependency; also exports `cxx_std_17` (public headers need it — the spike's "C++17 in every consuming TU" wart) |
| `0009` link `stdc++fs` only for GCC < 9.1 | unconditional `stdc++fs` on UNIX breaks libc++ platforms (macOS) through the exported interface |

Portfile accommodations that need no patch: `USE_AZURE_KINECT=OFF`,
`USE_REALSENSE=OFF`, `USE_GTEST=OFF`, and pruning the vendored
`nanoflann`/`tiny_obj_loader` headers from the install tree (only the
bundled `filesystem` shim is referenced by installed public headers). The
`-march=native` block upstream is dead code (its flag probe never
succeeds) and is left alone.

## Verification

Evidence recorded on 2026-08-03 (Windows 11, MSVC, x64-windows triplet):

- Classic-mode proof: `deps/vcpkg/vcpkg.exe install m3t:x64-windows
  --overlay-ports=./vcpkg-overlay-ports` — all nine patches applied
  cleanly, post-build validation passed, "All requested installations
  completed successfully in: 6.5 min" (m3t itself 1.2 min; buildtrees on
  `D:`). Install tree contains `lib/m3t.lib` + `debug/lib/m3t.lib`, 31
  headers in `include/m3t/`, `include/filesystem/filesystem.h` (vendored
  `nanoflann`/`tiny_obj_loader` pruned), and
  `share/m3t/{M3TConfig.cmake,M3TTargets*.cmake,usage,copyright}`.
- Feature proof: `vcpkg install m3t[texture]:x64-windows` rebuilt
  `opencv4` with `contrib` and compiled `m3t[core,texture]` clean
  ("All requested installations completed successfully in: 11 min");
  `include/m3t/texture_modality.h` present.
- Manifest-mode proof: `cmake --preset windows-msvc-release-d
  -DCUBEULATOR_WITH_M3T_PORT=ON` resolved `find_package(M3T CONFIG)`
  from the overlay; `m3t_port_smoke D:/.../cube_57mm.obj` printed
  `silhouette nonzero px = 24025` and `M3T_PORT_SMOKE_OK` (offscreen
  GLFW/GLEW silhouette render through the installed package), exit 0.

## Upstreaming status

Two prepared-but-not-pushed branches exist locally (nothing has been
pushed anywhere; pushing/PRs are a manual step):

1. **DLR-RM/3DObjectTracking** — branch `m3t-cmake-portability` in
   `D:\cubeulator-build\m3t-upstream` (9 commits, identical content to the
   port's patches). Draft PR text: [below](#draft-pr-dlr-rm3dobjecttracking).
   If accepted, the port's patch list collapses to nothing on the next
   pin bump.
2. **microsoft/vcpkg** — branch `add-m3t` in
   `D:\cubeulator-build\m3t-spike\vcpkg` (single commit `[m3t] Add new
   port`, version DB entry added via `vcpkg x-add-version`). The same
   commit is exported as a standalone patch at
   `D:\cubeulator-build\m3t-patches-vcpkg\0001-m3t-Add-new-port.patch`
   for replay on a full clone (the local checkout is shallow; shallow
   pushes are often rejected). Draft PR text:
   [below](#draft-pr-microsoftvcpkg).

Checklist to submit (user action; needs a GitHub account):

- [ ] Fork DLR-RM/3DObjectTracking, push `m3t-cmake-portability`, open PR
      with the draft text below.
- [ ] Fork microsoft/vcpkg, apply/push `add-m3t` (replay the exported
      patch on a fresh full clone of current master, re-running
      `vcpkg x-add-version m3t` if master's baseline moved), open PR.
- [ ] After a DLR merge: bump the port's `REF`/`SHA512`, drop merged
      patches, bump `version-date`, re-run `x-add-version`.
- [ ] Once the registry port is merged: delete
      `vcpkg-overlay-ports/m3t` and consume from the registry via a
      baseline bump.

## Draft PR: DLR-RM/3DObjectTracking

> **Title:** M3T: CMake portability and packaging fixes
>
> Hi — while packaging M3T for vcpkg (as a dependency of an application
> that tracks a Rubik's cube), I collected a small series of build-system
> fixes. Each commit is independent and behaviour-preserving by default;
> together they make M3T consumable as an installed library package
> without patches.
>
> - **Require CMake 3.10.** CMake 4 removed compatibility with < 3.5 and
>   deprecates < 3.10, so configuring currently needs a manual
>   `CMAKE_POLICY_VERSION_MINIMUM` override. `src/` already required 3.10.
> - **Two small UB fixes.** `Renderer::referenced_body_ptrs` returned a
>   reference to a temporary (MSVC C4172); `Body::get_id` could fall off
>   the end (C4715).
> - **`M3T_WIN32_STATIC` option (default ON).** Lets packagers link M3T
>   against dynamic dependencies / the dynamic CRT on Windows (e.g.
>   vcpkg's default `x64-windows` triplet); default keeps today's
>   static behaviour.
> - **Accept newer Eigen.** Eigen's package config rejects version
>   requests across major versions, so `find_package(Eigen3 3.3.2)` fails
>   against Eigen 4/5 despite the API M3T uses being unchanged.
> - **`USE_TEXTURE_MODALITY` option (default ON).** TextureModality is
>   the only xfeatures2d (opencv_contrib) user; this follows the existing
>   `USE_AZURE_KINECT`/`USE_REALSENSE` pattern including the `generator.h`
>   guards, and exports the define on the target so installed consumers
>   stay consistent.
> - **`M3T_BUILD_EXAMPLES`/`M3T_BUILD_DOC` options (default ON).** Lets
>   packagers skip the ~20 example/evaluation executables and the
>   Doxygen-in-`ALL` target; also honours `USE_GTEST` for the `test/`
>   subdirectory.
> - **Resolve dependencies in the installed package config.** The
>   installed `M3TConfig.cmake` was the raw `EXPORT` file, so
>   `find_package(M3T)` only worked if the consumer had already found
>   Eigen/GLEW/glfw/OpenCV/OpenMP itself; the config now wraps a
>   `M3TTargets.cmake` with `find_dependency()` calls, and the target now
>   propagates `cxx_std_17` (the public headers need C++17).
> - **Link `stdc++fs` only for GCC < 9.1.** Unconditional `stdc++fs` on
>   UNIX breaks libc++ platforms (macOS) via the exported interface.
>
> Tested on Windows/MSVC against vcpkg-provided dependencies (dynamic
> runtime, texture on and off; default static configuration untouched by
> default option values). The Linux-affecting changes (`stdc++fs`,
> subdirectory gating) are mechanical. Happy to split or drop any part of
> the series.

## Draft PR: microsoft/vcpkg

> **Title:** [m3t] Add new port
>
> Adds M3T from DLR-RM/3DObjectTracking (MIT), a multi-body,
> multi-modality, multi-camera 3D object tracker (Stoiber et al.).
>
> - Upstream publishes no tags or releases, so the port pins current
>   `master` (`f0210618`, 2025-05-21) with `version-date`; the source
>   archive is the whole monorepo (~218 MB) of which the port builds the
>   `M3T/` subdirectory.
> - Static-only library (upstream hard-codes `add_library(m3t STATIC)`),
>   hence `vcpkg_check_linkage(ONLY_STATIC_LIBRARY)`.
> - Default feature set is the region/depth tracker; the `texture`
>   feature enables TextureModality and is gated on `opencv4[contrib]`
>   (xfeatures2d; uses DAISY/FREAK, no `nonfree` needed).
> - Nine small patches, all build-system-level or trivial UB fixes; the
>   identical series has been submitted upstream as
>   DLR-RM/3DObjectTracking#NNN and the patch list shrinks as it lands.
> - `supports` excludes osx (upstream requires OpenMP, unavailable on
>   stock AppleClang), uwp/android/ios/emscripten/xbox (desktop
>   OpenGL/GLEW/glfw3 required).
> - Usage validated locally on x64-windows (classic and manifest mode,
>   default and `texture` feature) against a real consumer; other
>   triplets rely on vcpkg CI.
>
> - [x] Changes comply with the [maintainer guide](https://learn.microsoft.com/en-us/vcpkg/contributing/maintainer-guide)
> - [x] SHA512s are updated for each updated download
> - [x] The "supports" clause reflects platforms that may be fixed by this new version
> - [x] Any fixed [CI baseline](https://github.com/microsoft/vcpkg/blob/master/scripts/ci.baseline.txt) entries are removed from that file
> - [x] Any patches that can be upstreamed have been submitted upstream (link above)
> - [x] Newly added ports contain an appropriate license declaration
