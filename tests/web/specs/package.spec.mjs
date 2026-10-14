// A smoke test for any built copy of the site - the release zip unzipped, the
// hosted /app/ directory, a local build - that needs nothing from the build but
// what ships: no driver, no recorder. It is the one project that runs against
// the published build (--project=package):
//
//   cd tests/web && npm ci && npx playwright install chromium
//   CUBEULATOR_WEB_DIR=<the directory with index.html> npx playwright test --project=package
//
// The app has to start, draw its first screen into a canvas, be isolated for
// its threads, and say nothing to the console. What it does is the app
// project's, against the diagnostics build.
import { test, expect } from '@playwright/test';

test.use({
  permissions: ['camera'],
  viewport: { width: 900, height: 640 },
});

test('the published copy starts and draws', async ({ page }) => {
  const problems = [];
  page.on('console', (message) => {
    if (message.type() === 'error' && !/GPU stall|fallback to software/i.test(message.text())) {
      problems.push(`[console] ${message.text()}`);
    }
  });
  page.on('pageerror', (error) => problems.push(`[pageerror] ${error.message}`));
  page.on('requestfailed', (request) => problems.push(`[requestfailed] ${request.url()}`));

  await page.goto('/index.html');

  // Qt hides the loading message once the first screen is up.
  await expect(page.locator('#status')).toBeHidden({ timeout: 120_000 });
  await expect(page).toHaveTitle('Cubeulator');

  // Threads need isolation, from the host's headers or the page's own worker.
  expect(await page.evaluate(() => window.crossOriginIsolated)).toBe(true);

  // The canvas is inside a shadow root of Qt's, and has the window's size.
  const canvas = await page.evaluate(() => {
    const find = (root) => {
      for (const element of root.querySelectorAll('*')) {
        if (element.tagName === 'CANVAS') {
          return element;
        }
        if (element.shadowRoot) {
          const inner = find(element.shadowRoot);
          if (inner) {
            return inner;
          }
        }
      }
      return null;
    };
    const element = find(document);
    if (!element) {
      return null;
    }
    const box = element.getBoundingClientRect();
    return { width: box.width, height: box.height };
  });
  expect(canvas).not.toBeNull();
  expect(Math.round(canvas.width)).toBe(900);
  expect(Math.round(canvas.height)).toBe(640);

  // Something has been drawn: the canvas is not one flat colour. A screenshot
  // of the page, compared with itself a moment later, would not tell; the
  // launch screen has text, a button and a cube, so its pixels vary.
  const png = await page.screenshot();
  const distinct = await page.evaluate(async (base64) => {
    const image = new Image();
    image.src = `data:image/png;base64,${base64}`;
    await image.decode();
    const surface = document.createElement('canvas');
    surface.width = image.width;
    surface.height = image.height;
    const context = surface.getContext('2d');
    context.drawImage(image, 0, 0);
    const data = context.getImageData(0, 0, image.width, image.height).data;
    const colours = new Set();
    for (let i = 0; i < data.length; i += 4 * 97) {
      colours.add((data[i] >> 3) << 10 | (data[i + 1] >> 3) << 5 | (data[i + 2] >> 3));
    }
    return colours.size;
  }, png.toString('base64'));
  expect(distinct).toBeGreaterThan(8);

  expect(problems).toEqual([]);
});
