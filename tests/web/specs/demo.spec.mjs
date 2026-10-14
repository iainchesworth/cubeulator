// The solve screen reached without a camera: ?demo=<seed>, which the page
// documents (docs/web.md) as the way to see the app with none. It is also the
// quick way here to a solution to walk, solve methods to switch between and a
// finish to reach.
import { test, expect } from '../support/fixtures.mjs';

test.describe('?demo=', () => {
  test.describe('a seed and a move', () => {
    test.use({ appOptions: { query: '?demo=3&move=5', screen: 'solve' } });

    test('opens the solve screen n moves in', async ({ app }) => {
      expect(await app.screen()).toBe('solve');
      const solve = await app.singleton('SolveController');
      expect(solve.moveIndex).toBe(5);
      expect(solve.totalMoves).toBeGreaterThan(5);
      expect(solve.isSolving).toBe(false);
    });

    test('has no camera to open, so none is asked for', async ({ app }) => {
      expect(await app.page.evaluate(() => window.__fakeCamera.opened())).toBe(0);
      expect((await app.singleton('AppState')).solveCamera).toBe(false);
    });

    test('steps forward and back through the solution', async ({ app }) => {
      const index = async () => (await app.singleton('SolveController')).moveIndex;
      await app.click('solve.next');
      await expect.poll(index).toBe(6);
      await app.click('solve.next');
      await expect.poll(index).toBe(7);
      await app.click('solve.previous');
      await expect.poll(index).toBe(6);
    });

    test('shows the move to make, and which stage it belongs to', async ({ app }) => {
      const solve = await app.singleton('SolveController');
      const texts = await app.texts();
      expect(texts).toContain(solve.currentMoveNotation);
      expect(texts.some((t) => /^STAGE \d OF \d$/.test(t))).toBe(true);
      expect(solve.stageRanges.length).toBeGreaterThan(1);
      expect(solve.currentStageIndex).toBeGreaterThanOrEqual(0);
    });

    test('switches method, and the solution with it', async ({ app }) => {
      const total = async () => (await app.singleton('SolveController')).totalMoves;
      const beginner = await total();

      await app.click('solve.method.1'); // Intermediate
      await expect.poll(async () => (await app.singleton('SolveController')).method).toBe('Intermediate');
      await expect.poll(async () => (await app.singleton('SolveController')).isSolving).toBe(false);
      const intermediate = await total();
      expect(intermediate).toBeLessThan(beginner);

      await app.click('solve.method.2'); // Advanced
      await expect.poll(async () => (await app.singleton('SolveController')).method).toBe('Advanced');
      await expect.poll(async () => (await app.singleton('SolveController')).isSolving,
        { timeout: 60_000 }).toBe(false);
      const advanced = await total();
      // Two-phase: a couple of dozen moves where the others take dozens more.
      expect(advanced).toBeLessThan(intermediate);
      expect(advanced).toBeLessThanOrEqual(30);
    });

    test('ends on the completion screen', async ({ app }) => {
      for (let guard = 0; guard < 400; guard++) {
        if ((await app.singleton('SolveController')).atEnd) {
          break;
        }
        await app.click('solve.next');
      }
      expect((await app.find('solve.next')).text).toBe('Finish');
      await app.click('solve.next');
      await app.waitForScreen('complete');

      const texts = await app.texts();
      expect(texts).toContain('Solve complete');
      expect(texts).toContain('Solved');
      expect(texts).toContain('Beginner');

      // EXPECTED TO CHANGE (tests/system/known_today_test.cpp says the same
      // of the desktop build): "Review the solution" is on the screen and does
      // nothing yet.
      await app.click('complete.review');
      await app.page.waitForTimeout(500);
      expect(await app.screen()).toBe('complete');
    });

    test('goes back to scanning from the completion screen', async ({ app }) => {
      for (let guard = 0; guard < 400; guard++) {
        if ((await app.singleton('SolveController')).atEnd) {
          break;
        }
        await app.click('solve.next');
      }
      await app.click('solve.next');
      await app.waitForScreen('complete');
      await app.click('complete.scanAnother');
      await app.waitForScreen('scan');
    });
  });

  test.describe('a seed alone', () => {
    test.use({ appOptions: { query: '?demo=11', screen: 'solve' } });

    test('opens at the first move', async ({ app }) => {
      expect(await app.screen()).toBe('solve');
      const solve = await app.singleton('SolveController');
      expect(solve.moveIndex).toBe(0);
      expect(solve.atStart).toBe(true);
    });
  });

  test.describe('anything but digits', () => {
    test.use({ appOptions: { query: '?demo=1;rm%20-rf&move=x' } });

    test('is ignored: the app opens on its launch screen', async ({ app }) => {
      expect(await app.screen()).toBe('launch');
    });
  });
});
