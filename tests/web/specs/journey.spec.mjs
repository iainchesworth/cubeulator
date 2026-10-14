// The journey, through the browser: launch, scan a cube held to the camera,
// follow the solution to the end. The camera is a canvas the test draws the six
// sides of a scrambled cube on (support/fake_camera.js), so everything from the
// <video> element on is the page's real path; the cube is the one the app's own
// library scrambles from a seed (cubeulatorDriver.sides).
import { test, expect } from '../support/fixtures.mjs';

const SCRAMBLE_SEED = 1;
const SCRAMBLE_MOVES = 10;

async function scanTheCube(app) {
  await app.click('launch.startScan');
  await app.waitForScreen('scan');
  await expect.poll(async () => (await app.singleton('DirectScan')).running,
    { message: 'the camera is opened and the scan is running' }).toBe(true);

  const sides = await app.sides(SCRAMBLE_SEED, SCRAMBLE_MOVES);
  await app.showCube(sides);
  return sides;
}

test.describe('scan, solve, finish', () => {
  test('a scrambled cube is read and solved, and the solution walked to the end', async ({ app }) => {
    expect(await app.screen()).toBe('launch');
    await scanTheCube(app);

    // The six sides are in, and are all of one cube.
    const scan = await app.singleton('DirectScan');
    expect(scan.sidesRead).toBe(6);
    expect(scan.complete).toBe(true);
    expect(scan.conflicted).toBe(false);
    expect(scan.ambiguous).toBe(false);
    expect(scan.cameraError).toBe('');
    expect(scan.sides.map((s) => s.colors.length)).toEqual([9, 9, 9, 9, 9, 9]);

    // The solution follows the scan.
    await app.waitForScreen('solve', 120_000);
    await expect.poll(async () => (await app.singleton('SolveController')).isSolving).toBe(false);
    const solve = await app.singleton('SolveController');
    expect(solve.totalMoves).toBeGreaterThan(0);
    expect(solve.moveIndex).toBe(0);
    expect(solve.atStart).toBe(true);
    expect(solve.atEnd).toBe(false);
    expect(solve.moves.length).toBe(solve.totalMoves);
    // Beginner's method, which is the default, solves a ten-move scramble in
    // well under its 100-turn average.
    expect(solve.method).toBe('Beginner');
    expect(solve.totalMoves).toBeLessThan(120);
    expect((await app.texts()).some((t) => t.startsWith('STAGE 1 OF'))).toBe(true);

    // Every move, in order: the counter follows, and Previous undoes one.
    await app.click('solve.next');
    await expect.poll(async () => (await app.singleton('SolveController')).moveIndex).toBe(1);
    await app.click('solve.previous');
    await expect.poll(async () => (await app.singleton('SolveController')).moveIndex).toBe(0);

    for (let move = 0; move < solve.totalMoves; move++) {
      await app.click('solve.next');
      await expect.poll(async () => (await app.singleton('SolveController')).moveIndex,
        { message: `after move ${move + 1}` }).toBe(Math.min(move + 1, solve.totalMoves));
    }
    const end = await app.singleton('SolveController');
    expect(end.atEnd).toBe(true);
    // Every side one colour, and six different colours: solved.
    const faces = Object.values(end.stickers);
    expect(faces).toHaveLength(6);
    for (const face of faces) {
      expect(new Set(face).size).toBe(1);
    }
    expect(new Set(faces.map((face) => face[0])).size).toBe(6);

    // The last press is Finish, and the cube is solved: every side one colour.
    expect((await app.find('solve.next')).text).toBe('Finish');
    await app.click('solve.next');
    await app.waitForScreen('complete');
    const texts = await app.texts();
    expect(texts).toContain('Solve complete');
    expect(texts).toContain(String(solve.totalMoves));
  });

  test('the scan can be abandoned, and lets the camera go', async ({ app }) => {
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.live())).toBe(true);

    await app.clickText('Cancel');
    await app.waitForScreen('launch');
    await expect.poll(() => app.page.evaluate(() => window.__fakeCamera.live())).toBe(false);
    expect((await app.singleton('DirectScan')).running).toBe(false);
  });

  test('a scan can be started over', async ({ app }) => {
    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);

    // One side in, then start over: nothing of it is kept.
    const sides = await app.sides(SCRAMBLE_SEED, SCRAMBLE_MOVES);
    await app.showSide(sides.faces[0].stickers);
    await expect.poll(async () => (await app.singleton('DirectScan')).sidesRead, { timeout: 60_000 })
      .toBe(1);
    await app.showRoom();

    await app.clickText('Start over');
    await expect.poll(async () => (await app.singleton('DirectScan')).sidesRead).toBe(0);
    expect(await app.screen()).toBe('scan');
  });

  test('a cube out of step with the solution can be rescanned from the solve screen', async ({ app }) => {
    await scanTheCube(app);
    await app.waitForScreen('solve', 120_000);

    await app.clickText('Out of step? Rescan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).sidesRead).toBe(0);
  });
});
