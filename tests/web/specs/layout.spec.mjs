// Both layouts, on every screen: a phone held upright and a desktop-sized
// window, either side of the app's breakpoint (Theme.wideBreakpoint). The
// counterpart of tests/system/layout_test.cpp: what a person would call cut
// off, found by the rectangles of the controls on show.
import { test, expect } from '../support/fixtures.mjs';
import { DESKTOP, PHONE } from '../support/app.mjs';

const LAYOUTS = [
  { name: 'desktop', viewport: DESKTOP, touch: false, wide: true },
  { name: 'phone', viewport: PHONE, touch: true, wide: false },
];

for (const layout of LAYOUTS) {
  test.describe(`${layout.name} layout`, () => {
    test.use({ appOptions: { viewport: layout.viewport, touch: layout.touch } });

    test('the launch screen fits, and chooses its layout', async ({ app }) => {
      expect(await app.cutOff()).toEqual([]);

      const window = (await app.state()).window;
      expect(window.width).toBe(layout.viewport.width);
      const start = await app.find('launch.startScan');
      if (layout.wide) {
        // Text on the left, the cube on the right.
        expect(start.width).toBeLessThan(window.width / 2);
      } else {
        // One column, the button the width of it.
        expect(start.width).toBeGreaterThan(window.width - 80);
      }
    });

    test('the settings screen fits', async ({ app }) => {
      await app.click('launch.settings');
      await app.waitForScreen('settings');
      expect(await app.cutOff()).toEqual([]);
    });

    test('the scan screen fits, with the camera failed and with it running', async ({ app }) => {
      await app.page.evaluate(() => window.__fakeCamera.deny('NotAllowedError'));
      await app.click('launch.startScan');
      await app.waitForScreen('scan');
      await expect.poll(async () => (await app.texts()).includes('Camera unavailable')).toBe(true);
      expect(await app.cutOff()).toEqual([]);

      await app.page.evaluate(() => window.__fakeCamera.deny(null));
      await app.clickText('Cancel');
      await app.waitForScreen('launch');
      await app.click('launch.startScan');
      await app.waitForScreen('scan');
      await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);
      await app.settle();
      expect(await app.cutOff()).toEqual([]);
    });
  });

  test.describe(`${layout.name} layout, solving`, () => {
    test.use({ appOptions: { viewport: layout.viewport, touch: layout.touch, query: '?demo=3&move=5', screen: 'solve' } });

    test('the solve screen fits', async ({ app }) => {
      expect(await app.screen()).toBe('solve');
      expect(await app.cutOff()).toEqual([]);
      for (const name of ['solve.cube', 'solve.next', 'solve.previous']) {
        expect(await app.find(name), `${name} is on show`).not.toBeNull();
      }
    });

    test('the completion screen fits', async ({ app }) => {
      // Straight to the end: the demo's own Finish.
      for (let guard = 0; guard < 400; guard++) {
        if ((await app.singleton('SolveController')).atEnd) {
          break;
        }
        await app.click('solve.next');
      }
      await app.click('solve.next'); // Finish
      await app.waitForScreen('complete');
      expect(await app.cutOff()).toEqual([]);
    });
  });
}
