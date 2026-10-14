// The camera, as a page gets one: refused, absent, busy, and then fine. The
// browser's errors are the ones getUserMedia() rejects with, injected into the
// page (support/fake_camera.js); the app is expected to turn each into a
// screen a person can act on, and to recover once the camera is back.
import { test, expect } from '../support/fixtures.mjs';

const FAILURES = [
  ['permission refused', 'NotAllowedError'],
  ['no camera', 'NotFoundError'],
  ['camera in use by another program', 'NotReadableError'],
];

test.describe('a camera that cannot be started', () => {
  for (const [what, errorName] of FAILURES) {
    test(`${what}: says so, and a new scan works once the camera is back`, async ({ app }) => {
      await app.page.evaluate((name) => window.__fakeCamera.deny(name), errorName);
      await app.click('launch.startScan');
      await app.waitForScreen('scan');

      // The scan reports it, in words and as state; it does not sit on a
      // spinner and it does not take the page down.
      await expect.poll(async () => (await app.texts()).includes('Camera unavailable'),
        { message: 'a message for the person' }).toBe(true);
      const scan = await app.singleton('DirectScan');
      expect(scan.running).toBe(false);
      expect(scan.starting).toBe(false);
      expect(scan.cameraError).not.toBe('');

      // The camera comes back (the permission is granted, the other program
      // lets go). "Start over" is for a scan with sides in it and is not
      // offered here; the way out is Cancel, and then Scan again.
      await app.page.evaluate(() => window.__fakeCamera.deny(null));
      await app.clickText('Cancel');
      await app.waitForScreen('launch');
      await app.click('launch.startScan');
      await app.waitForScreen('scan');
      await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);
      expect((await app.singleton('DirectScan')).cameraError).toBe('');
      expect(await app.texts()).not.toContain('Camera unavailable');
    });
  }

  test('can be left, back to the launch screen', async ({ app }) => {
    await app.page.evaluate(() => window.__fakeCamera.deny('NotAllowedError'));
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.texts()).includes('Camera unavailable')).toBe(true);

    await app.clickText('Cancel');
    await app.waitForScreen('launch');
  });
});

test.describe('the camera that is there', () => {
  test('is opened once, for the scan, and let go of after it', async ({ app }) => {
    expect(await app.page.evaluate(() => window.__fakeCamera.opened())).toBe(0);

    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.live())).toBe(true);
    expect(await app.page.evaluate(() => window.__fakeCamera.opened())).toBe(1);

    await app.clickText('Cancel');
    await app.waitForScreen('launch');
    await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.live())).toBe(false);
  });

  test('delivers frames the scan reads at the size the reader works at', async ({ app }) => {
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).frameSeq,
      { timeout: 60_000 }).toBeGreaterThan(5);

    const scan = await app.singleton('DirectScan');
    // 640 px wide is the reader's working width (kWorkingWidth); a 16:9 camera
    // is 360 high.
    expect(scan.frameWidth).toBe(640);
    expect(scan.frameHeight).toBe(360);
  });

  test('says there is no cube when there is only a room', async ({ app }) => {
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).frameSeq,
      { timeout: 60_000 }).toBeGreaterThan(5);

    const scan = await app.singleton('DirectScan');
    expect(scan.sidesRead).toBe(0);
    expect(scan.reading).toBe(false);
  });

  test('is told apart by name in Settings once the browser lists several', async ({ app }) => {
    await app.page.evaluate(() => window.__fakeCamera.setDevices([
      { deviceId: 'front', label: 'Front camera' },
      { deviceId: 'back', label: 'Back camera' },
    ]));
    await app.click('launch.settings');
    await app.waitForScreen('settings');
    await app.click('settings.refreshCameras');
    await expect.poll(async () => (await app.texts()).includes('Back camera')).toBe(true);
    expect(await app.texts()).toContain('Front camera');
  });
});
