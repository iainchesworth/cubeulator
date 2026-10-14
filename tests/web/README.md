# Browser tests

Playwright tests of Cubeulator's WebAssembly build: the app, driven in Chromium
through its own page. What they are, how they read the app (a canvas has no DOM
to locate in), how to build the site under test and how to run them is in
[docs/testing.md, "Browser tests"](../../docs/testing.md#browser-tests).

```sh
cmake --preset web-emscripten-diagnostics && cmake --build --preset web-emscripten-diagnostics
npm ci
npx playwright install chromium
CUBEULATOR_WEB_DIR=../../build/web-emscripten-diagnostics/bin npx playwright test
```

| Environment variable | |
|---|---|
| `CUBEULATOR_WEB_DIR` | the built site (`bin/` of the diagnostics build); the default is where the preset puts it |
| `CUBEULATOR_WEB_PORT` | the port the static server uses (8123) |
| `CUBEULATOR_WEB_PERF_TIMING=1` | enforce the wall-time budgets of `perf.spec.mjs` (they are always recorded) |

`support/` is the harness: `serve.mjs` (static server with the isolation
headers), `app.mjs` (the app in a page: find, press, wait, read),
`fake_camera.js` (the camera, injected into the page), `fixtures.mjs`.
