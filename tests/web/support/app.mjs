// The app in a page, for a test: the browser counterpart of tests/system/app.hpp.
//
// A test drives the app the way a person does - real pointer and key events to
// the page, controls found by objectName - and reads back what a person sees.
// The reading is through window.cubeulatorDriver (src/app/platform/web/
// driver_web.cpp), because Qt for WebAssembly draws into a canvas and mirrors
// nothing into the DOM that a locator could find.
import { expect } from '@playwright/test';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { baseURL } from './env.mjs';

const fakeCameraScript = fileURLToPath(new URL('./fake_camera.js', import.meta.url));

/**
 * The sides of a cube with one edge flipped in place: the stickers of the edge
 * between the top and the front sides traded, so that every piece is there and
 * every pair of stickers is a pair a cube has - six sides that can be drawn and
 * read, and that make no cube. It is the scan's dearest judgement: the listing
 * of the cubes the sides make finds none, and each pair of centres is asked
 * whether trading their names would. The top side's bottom-middle sticker and
 * the front side's top-middle one are that edge in the camera's view of both
 * (the driver's sides are the solver's layout, mirrored on D, R and B, and these
 * two are not).
 * @param {{faces: {face: string, stickers: string[]}[]}} sides
 */
export function withEdgeFlipped(sides) {
  const faces = sides.faces.map((entry) => ({ ...entry, stickers: [...entry.stickers] }));
  const top = faces.find((entry) => entry.face === 'U');
  const front = faces.find((entry) => entry.face === 'F');
  [top.stickers[7], front.stickers[1]] = [front.stickers[1], top.stickers[7]];
  return { ...sides, faces };
}

export const DESKTOP = { width: 900, height: 640 };
export const PHONE = { width: 390, height: 844 };

// Console output that is not a defect of the app: the browser's notes about
// WebGL on the software renderer this suite runs on.
const IGNORED_CONSOLE = [
  /GPU stall due to ReadPixels/i,
  /Automatic fallback to software WebGL/i,
];

export class CubeulatorApp {
  /** @param {import('@playwright/test').Page} page */
  constructor(page, context) {
    this.page = page;
    this.context = context;
    /** every console error, page error and failed request since launch */
    this.problems = [];
    /** every console message, for a failure's report */
    this.log = [];
    this.expected = [];
    /** controls that did not move when the wheel turned, by name: where they were */
    this.pinned = new Map();
  }

  /** Problems a test expects (a camera that is refused logs an error). */
  expectProblems(...patterns) {
    this.expected.push(...patterns);
  }

  unexpectedProblems() {
    return this.problems.filter((p) => !this.expected.some((pattern) => pattern.test(p)));
  }

  /**
   * Opens a fresh page on the app and waits until it has drawn its first screen.
   * @param {import('@playwright/test').Browser} browser
   * @param {{viewport?: {width:number,height:number}, camera?: 'fake'|'browser', query?: string,
   *          touch?: boolean, permissions?: string[],
   *          screen?: string, storage?: Record<string,string>}} [options]
   *          `screen` is the one to wait for (launch); `storage` is what the
   *          browser's localStorage holds when the app starts, as an earlier
   *          visit would have left it
   */
  static async open(browser, options = {}, contexts = new Map()) {
    // One browser context per worker and option set, and a page per test. A
    // context of its own for every test is the obvious isolation and the wrong
    // one: with a renderer holding a 2 GB shared-memory reservation and a pool
    // of workers, Chromium takes half a minute to make the next context after
    // some of them. Storage is cleared instead, below.
    const key = JSON.stringify({
      viewport: options.viewport ?? DESKTOP,
      touch: options.touch ?? false,
      camera: options.camera ?? 'fake',
      permissions: options.permissions ?? ['camera'],
    });
    let context = contexts.get(key);
    if (!context) {
      context = await browser.newContext({
        baseURL,
        viewport: options.viewport ?? DESKTOP,
        hasTouch: options.touch ?? false,
        isMobile: options.touch ?? false,
        permissions: options.permissions ?? ['camera'],
      });
      if ((options.camera ?? 'fake') === 'fake') {
        await context.addInitScript({ content: await readFile(fakeCameraScript, 'utf8') });
      }
      contexts.set(key, context);
    }
    const page = await context.newPage();
    const app = new CubeulatorApp(page, context);

    page.on('console', (message) => {
      const line = `[${message.type()}] ${message.text()}`;
      app.log.push(line);
      if (message.type() === 'error' && !IGNORED_CONSOLE.some((p) => p.test(message.text()))) {
        app.problems.push(line);
      }
    });
    page.on('pageerror', (error) => app.problems.push(`[pageerror] ${error.message}`));
    page.on('requestfailed', (request) => {
      app.problems.push(`[requestfailed] ${request.url()} ${request.failure()?.errorText ?? ''}`);
    });

    // A clean origin: nothing a previous test of this context kept (the app
    // keeps its settings in the browser's storage), and then only what the
    // test says an earlier visit left.
    await context.clearCookies();
    await page.goto('/icon.svg');
    await page.evaluate(async (seed) => {
      localStorage.clear();
      sessionStorage.clear();
      if (indexedDB.databases) {
        for (const db of await indexedDB.databases()) {
          if (db.name) {
            indexedDB.deleteDatabase(db.name);
          }
        }
      }
      for (const [key, value] of Object.entries(seed)) {
        localStorage.setItem(key, value);
      }
    }, options.storage ?? {});

    const began = Date.now();
    await page.goto(`/index.html${options.query ?? ''}`);
    await app.waitUntilDrawn(options.screen ?? 'launch');
    /** milliseconds from navigation to the first screen being up */
    app.bootMs = Date.now() - began;
    return app;
  }

  /**
   * Waits for the module to start, the driver to be defined and `screen` to be
   * up: the launch screen, or the solve screen for a ?demo= page, which makes
   * its solution first.
   */
  async waitUntilDrawn(screen = 'launch', timeout = 120_000) {
    await this.page.waitForFunction(
      () => window.cubeulatorDriver && window.cubeulatorDriver.state().window !== null &&
            window.cubeulatorDriver.screen() !== '',
      undefined, { timeout });
    await this.waitForScreen(screen, timeout);
  }

  /** Closes the page; the context stays for the worker's next test. */
  async close() {
    await this.page.close();
  }

  // ---- reading -------------------------------------------------------------

  /** The driver's state: { screen, window, items }. */
  state() {
    return this.page.evaluate(() => window.cubeulatorDriver.state());
  }
  async screen() {
    return (await this.state()).screen;
  }
  /** Every item with this objectName, on show or not. */
  items(name) {
    return this.page.evaluate((n) => window.cubeulatorDriver.items(n), name);
  }
  /** The one on show, or null. Throws if two are. */
  find(name) {
    return this.page.evaluate((n) => window.cubeulatorDriver.find(n), name);
  }
  /** What a person can read on the screen. */
  texts() {
    return this.page.evaluate(() => window.cubeulatorDriver.texts());
  }
  /** One singleton's properties (AppState, SettingsState, DirectScan, ...). */
  singleton(name) {
    return this.page.evaluate((n) => window.cubeulatorDriver.singleton(n), name);
  }
  /** The six sides of a scrambled cube as the camera should be shown them. */
  sides(seed, moves) {
    return this.page.evaluate(([s, m]) => window.cubeulatorDriver.sides(s, m), [seed, moves]);
  }

  // ---- waiting -------------------------------------------------------------

  async waitForScreen(name, timeout = 30_000) {
    await expect.poll(() => this.screen(), { timeout, message: `waiting for the ${name} screen` })
      .toBe(name);
    // Every screen fades in; a press during the fade lands on a control that
    // is already there, but a read of its position would not be final.
    await this.settle();
  }

  /** Waits until a named item is on show, and returns it. */
  async waitFor(name, timeout = 20_000) {
    let found = null;
    await expect.poll(async () => (found = await this.find(name)) !== null,
      { timeout, message: `waiting for '${name}' to be on show` }).toBe(true);
    return found;
  }

  /**
   * Waits until the screen has stood still: three reads of it, a moment apart,
   * agree on where everything is. Two are not enough after a scroll, which
   * eases to a stop - and a press on a Flickable that is still moving only
   * stops it.
   */
  async settle(timeout = 10_000) {
    const deadline = Date.now() + timeout;
    let last = '';
    let agreed = 0;
    while (Date.now() < deadline) {
      const now = JSON.stringify((await this.state()).items
        .filter((i) => i.visible && i.name !== '')
        .map((i) => [i.name, Math.round(i.x), Math.round(i.y), Math.round(i.opacity * 10)]));
      agreed = now === last ? agreed + 1 : 0;
      if (agreed >= 2) {
        return;
      }
      last = now;
      await this.page.waitForTimeout(120);
    }
  }

  // ---- input ---------------------------------------------------------------

  /**
   * Scrolls the screen, with the wheel, until the named control can be pressed:
   * the settings screen is longer than a desktop window, and its scrolling area
   * stops short of the window's bottom edge (a margin of about forty pixels in
   * which nothing answers a press). A control that does not move when the wheel
   * turns is not in a scrolling area - the buttons that sit at the bottom of the
   * solve screen are like that - and is pressed where it is.
   */
  async reveal(name) {
    for (let attempt = 0; attempt < 12; attempt++) {
      const [item, state] = await Promise.all([this.waitFor(name), this.state()]);
      const height = state.window.height;
      const middle = item.y + item.height / 2;
      if (middle > 24 && middle < height - 64) {
        return;
      }
      if (this.pinned.get(name) === item.y) {
        return;
      }
      const outside = middle <= 0 || middle >= height;
      // Over the item's own column, so that the right Flickable gets it.
      const box = await this.canvasBox();
      await this.page.mouse.move(box.left + Math.max(8, Math.min(item.x + 8, box.width - 8)),
        box.top + height / 2);
      await this.page.mouse.wheel(0, Math.round(middle - height / 2));
      await this.settle();
      const after = await this.find(name);
      if (after && Math.abs(after.y - item.y) < 1) {
        if (outside) {
          throw new Error(`'${name}' is outside the window and does not scroll`);
        }
        this.pinned.set(name, item.y);
        return;
      }
    }
    throw new Error(`could not scroll '${name}' into view`);
  }

  /** A mouse click on the centre of the named control. */
  async click(name) {
    await this.reveal(name);
    const point = await this.page.evaluate((n) => window.cubeulatorDriver.center(n), name);
    expect(point, `'${name}' has a position`).not.toBeNull();
    await this.page.mouse.click(point.x, point.y);
  }

  /**
   * A click on the box of a settings toggle, which is the 20 px square at the
   * left of the row: its label is not part of what answers a press today.
   */
  async clickToggle(name) {
    await this.reveal(name);
    const item = await this.waitFor(name);
    const box = await this.canvasBox();
    await this.page.mouse.click(box.left + item.x + 10, box.top + item.y + item.height / 2);
  }

  /** A finger: touch press and release on the centre of the named control. */
  async tap(name) {
    await this.reveal(name);
    const point = await this.page.evaluate((n) => window.cubeulatorDriver.center(n), name);
    await this.page.touchscreen.tap(point.x, point.y);
  }

  /**
   * A click on the visible text that reads exactly `label`: for controls whose
   * objectName has not been given yet (the scan screen's buttons), as the
   * native tests' find_button(). A click on a label reaches the button under it.
   */
  async clickText(label) {
    let point = null;
    await expect.poll(async () => {
      const items = (await this.state()).items.filter((i) =>
        i.text === label && i.visible && i.opacity > 0.01 && i.width > 0);
      if (items.length === 0) {
        return 'none';
      }
      // Narrow and wide layouts are both built; one is on show. The last
      // painted is the one on top.
      const item = items[items.length - 1];
      const box = await this.canvasBox();
      point = { x: box.left + item.x + item.width / 2, y: box.top + item.y + item.height / 2 };
      return 'found';
    }, { timeout: 20_000, message: `waiting for the text '${label}' on show` }).toBe('found');
    await this.page.mouse.click(point.x, point.y);
  }

  /** Where Qt's canvas is in the page: { left, top, width, height }. */
  canvasBox() {
    return this.page.evaluate(() => window.cubeulatorDriver.canvasBox());
  }

  async key(name) {
    await this.page.keyboard.press(name);
  }

  /**
   * What the browser's localStorage holds for the page, key to value: where
   * the app keeps its settings (`qt-v0-<organisation>-<application>-<group>/<name>`).
   */
  stored() {
    return this.page.evaluate(() => Object.fromEntries(Object.entries(localStorage)));
  }

  /**
   * Does to the page what the browser does when it is put away, and returns
   * once the page's handlers have run (they are synchronous: it is the last a
   * page that is being closed ever runs).
   *   'visibilitychange'  the tab goes to the background: the page is hidden
   *                       for the length of the event, then shown again
   *   'pagehide'          the page is being left
   * A real reload sends both; this is for the case a reload cannot show,
   * a page that is hidden and stays.
   */
  async putAway(kind = 'visibilitychange') {
    await this.page.evaluate((what) => {
      if (what === 'pagehide') {
        window.dispatchEvent(new PageTransitionEvent('pagehide', { persisted: true }));
        return;
      }
      Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
      try {
        document.dispatchEvent(new Event('visibilitychange'));
      } finally {
        delete document.visibilityState;
      }
    }, kind);
  }

  /** Reloads the page and waits for the first screen again. */
  async reload() {
    await this.page.reload();
    await this.waitUntilDrawn();
  }

  /**
   * Items on show that stand across a vertical edge of the window: part in
   * view and part not. An item wholly outside it (a pane slid off to the side)
   * is not on show; one that is taller than the window is scrolled to, not cut.
   */
  async cutOff(tolerance = 1) {
    const { window: win, items } = await this.state();
    return items
      .filter((i) => i.name && i.visible && i.opacity > 0.01 && i.width > 0 && i.height > 0)
      .filter((i) => i.x < win.width - tolerance && i.x + i.width > tolerance)
      .filter((i) => i.x < -tolerance || i.x + i.width > win.width + tolerance)
      .map((i) => `${i.name} at x=${Math.round(i.x)} width=${Math.round(i.width)} (window ${win.width})`);
  }

  // ---- the camera ----------------------------------------------------------

  /** What the fake camera shows next: one side's stickers, or the room. */
  showSide(stickers, pose, noise = 0) {
    return this.page.evaluate(([s, p, n]) => window.__fakeCamera.side(s, p, n),
      [stickers, pose, noise]);
  }
  showRoom() {
    return this.page.evaluate(() => window.__fakeCamera.room());
  }

  /**
   * Shows every side of the cube `sides` describes, one at a time, each until
   * the scan has taken it, then the empty room while the cube is turned.
   * Event-driven, not timed: the app reads at its own pace.
   */
  async showCube(sides, { timeout = 90_000, noise = 0 } = {}) {
    let taken = (await this.singleton('DirectScan')).sidesRead;
    for (const { stickers } of sides.faces) {
      await this.showSide(stickers, undefined, noise);
      await expect.poll(async () => (await this.singleton('DirectScan')).sidesRead,
        { timeout, message: `waiting for side ${taken + 1} to be read` }).toBeGreaterThan(taken);
      taken = (await this.singleton('DirectScan')).sidesRead;
      await this.showRoom();
      if (taken >= 6) {
        break;
      }
      await this.page.waitForTimeout(250);
    }
  }

  // ---- the recorder --------------------------------------------------------

  /**
   * Waits until the app has presented `count` frames (the recorder's "UI"
   * set). The first frames of a page take seconds each on a software renderer;
   * whatever is measured is measured after them.
   */
  async waitForFrames(count = 5, timeout = 180_000) {
    await expect.poll(async () => (await this.profile()).frames.UI ?? 0,
      { timeout, message: `waiting for ${count} presented frames` }).toBeGreaterThan(count);
    await this.settle();
  }

  profile() {
    return this.page.evaluate(() => window.cubeulatorProfile.summary());
  }

  /**
   * Slows the page's main thread by `rate` (Chromium's CPU throttling, through
   * the DevTools protocol): 4 is about what a mid-range phone is to a desktop,
   * which is the machine the numbers are otherwise taken on. Work on the
   * workers is not slowed; the scan, which this is for, is on the main thread.
   */
  async throttleCpu(rate) {
    const session = await this.context.newCDPSession(this.page);
    await session.send('Emulation.setCPUThrottlingRate', { rate });
  }
  clearProfile() {
    return this.page.evaluate(() => window.cubeulatorProfile.clear());
  }
  mark(text) {
    return this.page.evaluate((t) => window.cubeulatorProfile.mark(t), text);
  }
}
