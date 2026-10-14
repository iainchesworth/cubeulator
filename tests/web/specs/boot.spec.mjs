// The page and its start-up: what has to be true before the app can be used at
// all. The app's own behaviour is in the other files.
import { test, expect } from '../support/fixtures.mjs';

test.describe('the page', () => {
  test('is cross-origin isolated, which is what its threads need', async ({ app }) => {
    const facts = await app.page.evaluate(() => ({
      isolated: window.crossOriginIsolated,
      sharedMemory: typeof SharedArrayBuffer === 'function',
      webgl2: !!document.createElement('canvas').getContext('webgl2'),
      wasm: typeof WebAssembly === 'object',
    }));
    expect(facts).toEqual({ isolated: true, sharedMemory: true, webgl2: true, wasm: true });
  });

  test('is titled, described and in English', async ({ app }) => {
    await expect(app.page).toHaveTitle('Cubeulator');
    expect(await app.page.locator('html').getAttribute('lang')).toBe('en');
    expect(await app.page.locator('meta[name=description]').getAttribute('content'))
      .toMatch(/point a camera at a scrambled Rubik's Cube/);
  });

  test('hands the window to the app once it is up', async ({ app }) => {
    // The loading message is for the seconds before the first screen.
    await expect(app.page.locator('#status')).toBeHidden();

    const canvas = await app.canvasBox();
    const viewport = app.page.viewportSize();
    expect(Math.round(canvas.width)).toBe(viewport.width);
    expect(Math.round(canvas.height)).toBe(viewport.height);

    // The page does not scroll: the app is the page.
    const scrolls = await app.page.evaluate(() =>
      document.documentElement.scrollHeight > window.innerHeight ||
      document.documentElement.scrollWidth > window.innerWidth);
    expect(scrolls).toBe(false);
  });

  test('starts without a console error, an exception or a failed request', async ({ app }) => {
    // Everything the page has said since it loaded.
    expect(app.problems).toEqual([]);
    expect(app.log.some((line) => /error|exception|abort/i.test(line) &&
      !/GPU stall|fallback to software/i.test(line))).toBe(false);
  });
});

test.describe('the launch screen', () => {
  test('is the first screen, with its four controls', async ({ app }) => {
    expect(await app.screen()).toBe('launch');
    for (const name of ['launch.startScan', 'launch.solveHistory', 'launch.language', 'launch.settings']) {
      expect(await app.find(name), `${name} is on show`).not.toBeNull();
    }
  });

  test('says what the app is for', async ({ app }) => {
    const texts = await app.texts();
    expect(texts).toContain('Cubeulator');
    expect(texts.join(' ')).toMatch(/Scan\. Solve\./);
    expect(texts).toContain('Scan a cube');
    expect(texts).toContain('Settings');
  });

  test('has not asked for the camera yet', async ({ app }) => {
    // The browser asks when a camera is first opened, and nothing here has.
    expect(await app.page.evaluate(() => window.__fakeCamera.opened())).toBe(0);
  });
});

test.describe('the diagnostics build', () => {
  test('records what the app does, under the build it was made from', async ({ app }) => {
    const facts = await app.page.evaluate(() => ({
      recording: window.cubeulatorProfile.recording,
      version: window.cubeulatorProfile.version,
      driver: window.cubeulatorDriver.version,
    }));
    expect(facts).toEqual({ recording: true, version: 1, driver: 1 });

    const summary = await app.profile();
    expect(summary.app).toMatch(/^Cubeulator /);
    expect(summary.app).toMatch(/target:\s+Web/);
    // Start-up is itself instrumented.
    expect(summary.zones['app.load_qml'].count).toBe(1);
    expect(summary.zones['app.i18n_install'].count).toBe(1);
  });
});
