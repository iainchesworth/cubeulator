// A cube that is not a 3x3, in the browser: ?demo=<seed>&size=<n> opens the solve
// screen on one with no camera (docs/web.md), and the screens for typing a cube in
// are reached from Home. What is held here is that the page runs the bigger cube's
// whole journey: the solution is made (in the page, on its own thread), the cube is
// drawn and turned layer by layer, the 3x3 stage can be skipped to, and the solve
// ends on the completion screen.
import { test, expect } from '../support/fixtures.mjs';

test.describe('?demo=&size=', () => {
  test.describe('a 5x5, five moves in', () => {
    test.use({ appOptions: { query: '?demo=3&size=5&move=5', screen: 'solve' } });

    test('opens the solve screen on a 5x5, n moves in', async ({ app }) => {
      expect(await app.screen()).toBe('solve');
      const solve = await app.singleton('SolveController');
      expect(solve.size).toBe(5);
      expect(solve.moveIndex).toBe(5);
      // A 5x5 takes a few hundred moves, not a few dozen.
      expect(solve.totalMoves).toBeGreaterThan(100);
      expect(solve.isSolving).toBe(false);
      // Its reduction, then the 3x3's stages.
      expect(solve.stageRanges.length).toBeGreaterThan(4);
    });

    test('has no camera and no rescan: they are the 3x3\'s', async ({ app }) => {
      expect(await app.page.evaluate(() => window.__fakeCamera.opened())).toBe(0);
      expect(await app.find('solve.rescan')).toBeNull();
    });

    test('says which layer to turn, and steps forward and back', async ({ app }) => {
      const solve = await app.singleton('SolveController');
      expect(await app.texts()).toContain(solve.currentMoveNotation);
      const index = async () => (await app.singleton('SolveController')).moveIndex;
      await app.click('solve.next');
      await expect.poll(index).toBe(6);
      await app.click('solve.previous');
      await expect.poll(index).toBe(5);
    });

    test('can be skipped to its 3x3 stage, which shows the reduced cube', async ({ app }) => {
      const before = await app.singleton('SolveController');
      expect(before.canSkipToCore).toBe(true);
      expect(before.coreStartIndex).toBeGreaterThan(before.moveIndex);
      expect(await app.find('solve.skip.button')).not.toBeNull();

      await app.click('solve.skip.button');
      await expect.poll(async () => (await app.singleton('SolveController')).moveIndex)
        .toBe(before.coreStartIndex);
      expect((await app.singleton('SolveController')).canSkipToCore).toBe(false);
      await expect.poll(async () => await app.find('solve.skip.button')).toBeNull();
    });

    test('is finished from the 3x3 stage, ending on the completion screen', async ({ app }) => {
      // The fewest-turns method, so that what is left after the reduction is short.
      await app.click('solve.method.2');
      await expect.poll(async () => (await app.singleton('SolveController')).method).toBe('Advanced');
      await expect.poll(async () => (await app.singleton('SolveController')).isSolving,
        { timeout: 120_000 }).toBe(false);
      await app.click('solve.skip.button');
      await expect.poll(async () => (await app.singleton('SolveController')).canSkipToCore).toBe(false);
      for (let guard = 0; guard < 120; guard++) {
        if ((await app.singleton('SolveController')).atEnd) {
          break;
        }
        await app.click('solve.next');
      }
      expect((await app.singleton('SolveController')).atEnd).toBe(true);
      await app.click('solve.next');
      await app.waitForScreen('complete');
    });
  });

  test.describe('a 2x2', () => {
    test.use({ appOptions: { query: '?demo=3&size=2', screen: 'solve' } });

    test('opens on a 2x2, which has no reduction to skip', async ({ app }) => {
      const solve = await app.singleton('SolveController');
      expect(solve.size).toBe(2);
      expect(solve.totalMoves).toBeGreaterThan(0);
      expect(solve.canSkipToCore).toBe(false);
    });
  });

  test.describe('a size that is no size', () => {
    test.use({ appOptions: { query: '?demo=3&size=x', screen: 'solve' } });

    test('is ignored: the demo is a 3x3', async ({ app }) => {
      expect((await app.singleton('SolveController')).size).toBe(3);
    });
  });
});

test.describe('another size, from Home', () => {
  test('a scramble typed for a 4x4 is solved with no camera', async ({ app }) => {
    expect(await app.screen()).toBe('launch');
    await app.click('launch.other');
    await app.waitForScreen('entry');
    await app.click('entry.size.4');
    // The page has no keyboard focus on a text box to type into until it is
    // clicked; the scramble goes in as a person would type it.
    await app.click('entry.scramble');
    await app.page.keyboard.type("R U 2R' Fw 3Fw' Uw2 L' D2");
    await app.click('entry.solve');
    await app.waitForScreen('solve', 120_000);
    await expect.poll(async () => (await app.singleton('SolveController')).isSolving,
      { timeout: 120_000 }).toBe(false);
    const solve = await app.singleton('SolveController');
    expect(solve.size).toBe(4);
    expect(solve.totalMoves).toBeGreaterThan(0);
    expect((await app.singleton('AppState')).solveCamera).toBe(false);
  });

  test('a scramble that is not one is said, and the screen stays', async ({ app }) => {
    await app.click('launch.other');
    await app.waitForScreen('entry');
    await app.click('entry.scramble');
    await app.page.keyboard.type('R U Q F');
    await app.click('entry.solve');
    await app.waitFor('entry.error');
    expect(await app.screen()).toBe('entry');
  });
});
