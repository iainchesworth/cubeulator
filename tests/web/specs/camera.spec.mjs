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
    test(`${what}: says so, and Try again works once the camera is back`, async ({ app }) => {
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

      // The way on is on the screen: "Start over" is for a scan with sides in
      // it and gives way to Try again, which asks for the camera as the scan
      // did on entry. Cancel is still there.
      await app.waitFor('scan.retry');
      expect(await app.texts()).toContain('Try again');
      expect(await app.texts()).not.toContain('Start over');
      expect(await app.texts()).toContain('Cancel');

      // Asked while the camera is still refused: the same screen again, the
      // browser asked a second time, and nothing taken down.
      const asked = await app.page.evaluate(() => window.__fakeCamera.attempts());
      await app.click('scan.retry');
      await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.attempts()),
        { message: 'Try again asks the browser for the camera' }).toBeGreaterThan(asked);
      await app.waitFor('scan.retry');
      expect((await app.singleton('DirectScan')).cameraError).not.toBe('');
      expect(await app.texts()).toContain('Camera unavailable');

      // The camera comes back (the permission is granted, the other program
      // lets go) and Try again gets it, on the screen it is on.
      await app.page.evaluate(() => window.__fakeCamera.deny(null));
      await app.click('scan.retry');
      await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);
      expect((await app.singleton('DirectScan')).cameraError).toBe('');
      expect(await app.screen()).toBe('scan');
      expect(await app.find('scan.retry')).toBeNull();
      expect(await app.texts()).not.toContain('Camera unavailable');
      expect(await app.texts()).toContain('Start over');
      expect(await app.page.evaluate(() => window.__fakeCamera.live())).toBe(true);
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

// A scan wants one side to read the same colours while the cube is turned, so
// the page asks the camera to hold its exposure and white balance where its
// automatics settled (camera_web.js). A browser has the control only in
// Chromium, and a camera offers it only if it is a webcam or a phone that
// does, so what matters here is what is said of each: `DirectScan.lock` is the
// report the app has, and it claims a lock only for what the page read back.
//
// The camera is the fake's, with the controls a driver has (support/
// fake_camera.js `controls`), because there are things a real camera does that
// a test has to be able to make it do - and not do.
const CONTROLS = {
  capabilities: {
    exposureMode: ['continuous', 'manual'],
    exposureTime: { min: 3, max: 2047, step: 1 },
    whiteBalanceMode: ['continuous', 'manual'],
    colorTemperature: { min: 2000, max: 7500, step: 50 },
  },
  settings: {
    exposureMode: 'continuous',
    exposureTime: 166,
    whiteBalanceMode: 'continuous',
    colorTemperature: 4650,
  },
};
const AUTOMATIC = { ...CONTROLS.settings };
const HELD = { ...CONTROLS.settings, exposureMode: 'manual', whiteBalanceMode: 'manual' };

async function cameraWith(app, extra = null) {
  await app.page.evaluate((config) => window.__fakeCamera.controls(config),
    extra && { ...CONTROLS, ...extra });
}

/** Starts a scan and waits until the camera has said what became of its lock. */
async function scanUntilLockReported(app) {
  await app.click('launch.startScan');
  await app.waitForScreen('scan');
  await expect.poll(async () => (await app.singleton('DirectScan')).lock.reported,
    { message: 'the camera says what its lock came to', timeout: 60_000 }).toBe(true);
  return (await app.singleton('DirectScan')).lock;
}

/** The scan went on whatever the lock came to: frames still arrive and are read. */
async function expectScanGoesOn(app) {
  const scan = await app.singleton('DirectScan');
  expect(scan.running).toBe(true);
  expect(scan.settingUp).toBe(false);
  expect(scan.cameraError).toBe('');
  await expect.poll(async () => (await app.singleton('DirectScan')).frameSeq,
    { timeout: 60_000 }).toBeGreaterThan(scan.frameSeq + 3);
}

test.describe('the exposure and white balance lock', () => {
  test('holds them where the automatics settled, and gives them back when the scan is left',
    async ({ app }) => {
      await cameraWith(app, {});
      const lock = await scanUntilLockReported(app);

      expect(lock.exposure).toBe('applied');
      expect(lock.whiteBalance).toBe('applied');
      expect(lock.supported).toBe(true);
      expect(lock.summary).toBe('Camera locked: auto white balance and exposure.');

      // What the page asked for is what the camera was doing: the values it
      // had chosen, and nothing of the page's own.
      const asked = await app.page.evaluate(() => window.__fakeCamera.applied());
      expect(asked).toEqual([[{
        exposureMode: 'manual', exposureTime: 166, whiteBalanceMode: 'manual', colorTemperature: 4650,
      }]]);
      expect(await app.page.evaluate(() => window.__fakeCamera.settings())).toEqual(HELD);
      // The scan has its camera, and nobody was asked for anything new.
      expect(await app.page.evaluate(() => window.__fakeCamera.attempts())).toBe(1);
      await expectScanGoesOn(app);

      // Leaving puts the camera's own automatics back before the track is
      // stopped: a webcam keeps what it was told, for the next program to open.
      await app.clickText('Cancel');
      await app.waitForScreen('launch');
      await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.live())).toBe(false);
      expect(await app.page.evaluate(() => window.__fakeCamera.settings())).toEqual(AUTOMATIC);
    });

  test('finds the controls through ImageCapture where the track does not have them', async ({ app }) => {
    await cameraWith(app, { where: 'imageCapture' });
    const lock = await scanUntilLockReported(app);

    expect(lock.exposure).toBe('applied');
    expect(lock.whiteBalance).toBe('applied');
    expect(lock.supported).toBe(true);
    expect(await app.page.evaluate(() => window.__fakeCamera.settings())).toEqual(HELD);
  });

  test('is not claimed of a camera that lists no control, and the scan runs on its automatics',
    async ({ app }) => {
      // No controls() call: a laptop's camera in a browser that has none, or a
      // phone's front lens.
      const lock = await scanUntilLockReported(app);

      expect(lock.exposure).toBe('unsupported');
      expect(lock.whiteBalance).toBe('unsupported');
      expect(lock.supported).toBe(false);
      expect(lock.summary).toContain('not available on this platform');
      expect(lock.summary).toContain('lighting unchanged');
      await expectScanGoesOn(app);
    });

  test('is said to be unsupported at once by a browser that has no way to ask', async ({ app }) => {
    // Safari and Firefox: neither the controls on the track nor ImageCapture.
    await app.page.evaluate(() => { window.ImageCapture = undefined; });
    const lock = await scanUntilLockReported(app);

    expect(lock.exposure).toBe('unsupported');
    expect(lock.whiteBalance).toBe('unsupported');
    expect(lock.supported).toBe(false);
    await expectScanGoesOn(app);
  });

  test('is not claimed when the camera refuses, and the scan goes on', async ({ app }) => {
    await cameraWith(app, { refuse: ['exposureMode', 'whiteBalanceMode'] });
    const lock = await scanUntilLockReported(app);

    expect(lock.exposure).toBe('rejected');
    expect(lock.whiteBalance).toBe('rejected');
    expect(lock.supported).toBe(false);
    expect(lock.summary).not.toContain('Camera locked');
    expect(lock.summary).toContain('refused every setting');
    expect(await app.page.evaluate(() => window.__fakeCamera.settings())).toEqual(AUTOMATIC);
    await expectScanGoesOn(app);
  });

  test('is not taken for done when the browser quietly skips the constraint', async ({ app }) => {
    // An advanced constraint set the browser cannot satisfy is skipped and the
    // promise still resolves: only the read-back says it did nothing.
    await cameraWith(app, { ignore: ['exposureMode', 'whiteBalanceMode'] });
    const lock = await scanUntilLockReported(app);

    expect(lock.exposure).toBe('rejected');
    expect(lock.whiteBalance).toBe('rejected');
    expect(lock.supported).toBe(false);
    expect(lock.summary).not.toContain('Camera locked');
    await expectScanGoesOn(app);
  });

  test('is put back when the camera goes to manual at a value of its own', async ({ app }) => {
    // Manual at the camera's stored value is a picture that jumps; it is not
    // the lock, and the automatics are the better of the two.
    await cameraWith(app, { jump: { exposureTime: 40, colorTemperature: 3000 } });
    const lock = await scanUntilLockReported(app);

    expect(lock.exposure).toBe('rejected');
    expect(lock.whiteBalance).toBe('rejected');
    expect(lock.supported).toBe(false);
    await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.settings()),
      { message: 'the modes are put back' })
      .toMatchObject({ exposureMode: 'continuous', whiteBalanceMode: 'continuous' });
    await expectScanGoesOn(app);
  });

  test('can hold one control and say so of the other', async ({ app }) => {
    // A set with a control the camera refuses is refused whole; the other is
    // then asked for on its own.
    await cameraWith(app, { refuse: ['whiteBalanceMode'] });
    const lock = await scanUntilLockReported(app);

    expect(lock.exposure).toBe('applied');
    expect(lock.whiteBalance).toBe('rejected');
    expect(lock.supported).toBe(true);
    expect(lock.summary).toContain('Camera locked: exposure.');
    expect(lock.summary).toContain('would not lock auto white balance');
    expect(lock.summary).toContain('lighting unchanged');
    expect(await app.page.evaluate(() => window.__fakeCamera.settings()))
      .toMatchObject({ exposureMode: 'manual', exposureTime: 166, whiteBalanceMode: 'continuous' });
    await expectScanGoesOn(app);
  });

  test('is not left in the camera by a scan left while it was being asked for', async ({ app }) => {
    // The camera takes its time. The scan is left while the page is waiting for
    // it, so the track must not be stopped until what was asked has been put
    // back: a webcam keeps what it is told.
    await cameraWith(app, { delay: 700 });
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.applied().length),
      { message: 'the page is asking the camera', timeout: 60_000 }).toBeGreaterThan(0);

    await app.clickText('Cancel');
    await app.waitForScreen('launch');
    await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.live()),
      { message: 'the camera is let go of' }).toBe(false);
    expect(await app.page.evaluate(() => window.__fakeCamera.settings())).toEqual(AUTOMATIC);
  });

  test('does not hold the scan up when the camera never answers', async ({ app }) => {
    await cameraWith(app, { hang: true });
    const lock = await scanUntilLockReported(app);

    expect(lock.exposure).toBe('rejected');
    expect(lock.whiteBalance).toBe('rejected');
    expect(lock.supported).toBe(false);
    await expectScanGoesOn(app);
  });
});
