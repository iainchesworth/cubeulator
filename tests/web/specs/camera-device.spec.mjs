// The browser's own camera path, with Chromium's built-in fake device in place
// of a webcam (--use-fake-device-for-media-stream): a real getUserMedia() and
// enumerateDevices(), a real MediaStream and <video> element, frames the
// browser makes. What the other specs inject (support/fake_camera.js) is the
// page's view of a camera; this is the browser's.
import { test, expect } from '../support/fixtures.mjs';
import { browserArgs } from '../support/env.mjs';

test.use({
  launchOptions: {
    args: [
      ...browserArgs,
      '--use-fake-device-for-media-stream',
      // The permission prompt is answered "allow", as a person would.
      '--use-fake-ui-for-media-stream',
    ],
  },
  appOptions: { camera: 'browser' },
});

test.describe("the browser's fake camera", () => {
  test('is listed in Settings, by the name the browser gives it', async ({ app }) => {
    await app.click('launch.settings');
    await app.waitForScreen('settings');
    await expect.poll(async () => (await app.find('settings.camera'))?.name ?? '').toBe('settings.camera');
    // Chromium's device is called "fake_device_0"; a browser that has not been
    // given permission lists no label at all, and the app falls back to
    // "Back camera"/"Front camera" - either way there is one entry.
    const texts = await app.texts();
    expect(texts.some((t) => /fake|camera/i.test(t))).toBe(true);
  });

  test('is opened by the scan and its frames reach the reader', async ({ app }) => {
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).running,
      { timeout: 60_000 }).toBe(true);
    await expect.poll(async () => (await app.singleton('DirectScan')).frameSeq,
      { timeout: 60_000 }).toBeGreaterThan(5);

    const scan = await app.singleton('DirectScan');
    expect(scan.cameraError).toBe('');
    expect(scan.frameWidth).toBeGreaterThan(0);
    expect(scan.frameHeight).toBeGreaterThan(0);
    // Chromium's test pattern is not a cube.
    expect(scan.sidesRead).toBe(0);
  });

  test('is let go of when the scan is left', async ({ app }) => {
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).running,
      { timeout: 60_000 }).toBe(true);

    await app.clickText('Cancel');
    await app.waitForScreen('launch');
    await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(false);
  });
});
