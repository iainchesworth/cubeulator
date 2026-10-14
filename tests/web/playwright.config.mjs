// Playwright's configuration for the browser tests of Cubeulator's web build.
//
// The site under test is a *diagnostics* build (cmake --preset
// web-emscripten-diagnostics): the same app as the published one, with the
// read-only driver (window.cubeulatorDriver) and the in-process recorder
// (window.cubeulatorProfile) linked in. Point CUBEULATOR_WEB_DIR at its bin/
// directory; the default is where the preset puts it.
import { defineConfig } from '@playwright/test';
import { baseURL, browserArgs, port, siteRoot as root } from './support/env.mjs';

export default defineConfig({
  testDir: 'specs',
  // A scan on a software renderer takes most of a minute (six sides, each held
  // until the app has read it, at the few frames a second a CPU-drawn page
  // manages), and a case that scans and then walks a whole solution more.
  timeout: 240_000,
  expect: { timeout: 30_000 },
  // A page per test, the browser's storage cleared first: the app boots in
  // about a second and nothing it keeps in the browser leaks between cases.
  // One worker by default: a page is drawn by SwiftShader, whose threads spin
  // on every core they find, and two pages at once measured seven times slower
  // each than one (CUBEULATOR_WEB_WORKERS raises it on a machine with cores to
  // spare).
  fullyParallel: true,
  workers: Number(process.env.CUBEULATOR_WEB_WORKERS ?? 1),
  retries: process.env.CI ? 1 : 0,
  reporter: [['list'], ['json', { outputFile: 'reports/results.json' }]],
  outputDir: 'reports/artifacts',
  use: {
    baseURL,
    browserName: 'chromium',
    launchOptions: { args: browserArgs },
    viewport: { width: 900, height: 640 },
    // Tracing records screenshots of a page that draws sixty frames a second
    // with WebGL, which costs the page most of its speed: a failure is traced
    // on its one retry, not on the first run.
    trace: 'on-first-retry',
    screenshot: 'only-on-failure',
  },
  projects: [
    { name: 'app', testIgnore: /(perf|package)\.spec\.mjs/ },
    { name: 'perf', testMatch: /perf\.spec\.mjs/, workers: 1 },
    // Any built copy, with nothing of the diagnostics build: the release zip.
    { name: 'package', testMatch: /package\.spec\.mjs/ },
  ],
  webServer: {
    command: `node support/serve.mjs "${root}" ${port}`,
    url: `${baseURL}/index.html`,
    reuseExistingServer: !process.env.CI,
    timeout: 30_000,
  },
});
