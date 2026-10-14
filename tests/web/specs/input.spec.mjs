// Input, the ways a browser delivers it: a mouse, a finger, a key. The counterpart
// of tests/system/input_test.cpp.
import { test, expect } from '../support/fixtures.mjs';
import { PHONE } from '../support/app.mjs';

// How many of the pixels in a region differ between two screenshots.
async function pixelsChanged(page, clip, act) {
  const before = await page.screenshot({ clip });
  await act();
  const after = await page.screenshot({ clip });
  return page.evaluate(async ([a, b]) => {
    const load = async (base64) => {
      const image = new Image();
      image.src = `data:image/png;base64,${base64}`;
      await image.decode();
      const canvas = document.createElement('canvas');
      canvas.width = image.width;
      canvas.height = image.height;
      const context = canvas.getContext('2d');
      context.drawImage(image, 0, 0);
      return context.getImageData(0, 0, image.width, image.height).data;
    };
    const [x, y] = await Promise.all([load(a), load(b)]);
    let changed = 0;
    for (let i = 0; i < x.length; i += 4) {
      if (Math.abs(x[i] - y[i]) + Math.abs(x[i + 1] - y[i + 1]) + Math.abs(x[i + 2] - y[i + 2]) > 24) {
        changed++;
      }
    }
    return changed;
  }, [before.toString('base64'), after.toString('base64')]);
}

test.describe('a mouse', () => {
  test.use({ appOptions: { query: '?demo=3&move=5', screen: 'solve' } });

  test('drags the cube round', async ({ app }) => {
    const cube = await app.find('solve.cube');
    const box = await app.canvasBox();
    const clip = {
      x: Math.round(box.left + cube.x), y: Math.round(box.top + cube.y),
      width: Math.round(cube.width), height: Math.round(cube.height),
    };
    const centre = { x: clip.x + clip.width / 2, y: clip.y + clip.height / 2 };

    // While the button is down: the cube eases home once it is let go.
    const changed = await pixelsChanged(app.page, clip, async () => {
      await app.page.mouse.move(centre.x, centre.y);
      await app.page.mouse.down();
      await app.page.mouse.move(centre.x + 90, centre.y + 40, { steps: 8 });
      await app.page.waitForTimeout(300);
    });
    await app.page.mouse.up();
    expect(changed).toBeGreaterThan(500);
  });

  test('presses a button once', async ({ app }) => {
    await app.click('solve.next');
    await expect.poll(async () => (await app.singleton('SolveController')).moveIndex).toBe(6);
    await app.page.waitForTimeout(500);
    expect((await app.singleton('SolveController')).moveIndex).toBe(6);
  });
});

test.describe('a finger', () => {
  test.use({ appOptions: { viewport: PHONE, touch: true } });

  test('taps through the launch screen and back', async ({ app }) => {
    await app.tap('launch.settings');
    await app.waitForScreen('settings');
    await app.tap('settings.back');
    await app.waitForScreen('launch');
  });

  test('taps a chip', async ({ app }) => {
    await app.tap('launch.settings');
    await app.waitForScreen('settings');
    await app.tap('settings.theme.1'); // Light
    await expect.poll(async () => (await app.singleton('SettingsState')).themeMode).toBe('light');
  });
});

test.describe('a keyboard', () => {
  // EXPECTED TO CHANGE (tests/system/known_today_test.cpp, and the plan's
  // Accessibility item): the controls are an Item and a MouseArea, not focus
  // items, and nothing handles Escape or Back. These say what is true today so
  // that the change shows up as a failure to update.
  test('reaches no control with Tab, and Escape leaves nothing', async ({ app }) => {
    for (let i = 0; i < 6; i++) {
      await app.key('Tab');
    }
    const focused = (await app.state()).items.filter((item) => item.activeFocus && item.name &&
      !['Main', 'ApplicationWindowContentControl', 'ApplicationWindow'].includes(item.name));
    expect(focused).toEqual([]);

    await app.click('launch.settings');
    await app.waitForScreen('settings');
    await app.key('Escape');
    await app.page.waitForTimeout(300);
    expect(await app.screen()).toBe('settings');
  });
});
