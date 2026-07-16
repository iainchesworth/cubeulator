# Testing & coverage

Two layers, both Catch2:

- **`tests/unit/`** — fast, isolated, one behaviour each. Currently exercises
  `cube::error` (`Result<T>`/`CubeError`), `cube::version`, and
  `ScanSessionConfig`'s fluent builder.
- **`tests/integration/`** — exercises several components together; the
  current test drives the CMake-selected platform implementations
  (`cube::platform::paths`, `cube::platform::inference_env`,
  `cube::platform::camera`) end to end rather than any single interface in
  isolation.

Run them with `ctest --preset <preset>` after building.

## Coverage gate

CI's `Coverage (Linux, GCC)` job builds the `linux-gcc-coverage` preset
(`CUBEULATOR_ENABLE_COVERAGE=ON`, `CUBEULATOR_BUILD_APP=OFF`) and runs:

```
gcovr --root . --filter 'src/.*' \
  --fail-under-line 80 --fail-under-branch 80
```

New behaviour should come with a test in the same PR (TDD by convention);
`src/lib/` in particular has no platform/GUI dependency to complicate
mocking, so it should clear meaningfully more than the 80% floor.
