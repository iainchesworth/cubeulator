// A cube turned about one axis and then another, played to the page's camera at
// the frame times it was recorded with, and what the scan made of it.
//
// The session is one `cube_tumble_sim` drew (tools/scan): a directory of
// rec_NNNNNN.jpg and an index.csv. It is the report this was written for - the
// browser build saying "the six sides fit together in more than one way" - run
// in the build, with the cube's pose known, rather than argued from a native
// replay. There is no such session in the repository (a recording shows a
// person), so the case is skipped unless one is named:
//
//   cube_tumble_sim /tmp/tumble "U:YYWYYYWWY D:YWWWWWWYY L:RRRRRRRRR R:OOOOOOOOO F:BBGGGGGGB B:BBGBBBGGB" 60 30
//   TUMBLE_SESSION=/tmp/tumble npx playwright test specs/tumble.spec.mjs
import { readFileSync, readdirSync } from 'node:fs';
import { test, expect } from '../support/fixtures.mjs';

const session = process.env.TUMBLE_SESSION;

test.skip(!session, 'set TUMBLE_SESSION to a directory cube_tumble_sim wrote');

test('a cube tumbled in front of the camera is read, however its sides fit together', async ({ app }) => {
  test.setTimeout(600_000);
  const frames = readdirSync(session).filter((name) => /^rec_\d+\.jpg$/.test(name)).sort();
  const times = readFileSync(`${session}/index.csv`, 'utf8').trim().split('\n').slice(1)
    .map((row) => Number(row.split(',')[1]));
  expect(frames.length).toBeGreaterThan(0);

  await app.click('launch.startScan');
  await app.waitForScreen('scan');
  await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);

  // One frame at a time at its own time, the way a camera gives them; the page
  // takes the newest it has when it asks, and drops what it was too slow for.
  const started = Date.now();
  for (let i = 0; i < frames.length; i++) {
    const wait = times[i] - (Date.now() - started);
    if (wait > 0) {
      await new Promise((resolve) => setTimeout(resolve, wait));
    }
    const picture = readFileSync(`${session}/${frames[i]}`).toString('base64');
    await app.page.evaluate((data) => window.__fakeCamera.image(`data:image/jpeg;base64,${data}`), picture);
    if (i % 10 === 0 && (await app.singleton('DirectScan')).complete) {
      break;
    }
  }

  const scan = await app.singleton('DirectScan');
  expect(scan.sidesRead).toBe(6);
  expect(scan.ambiguous, 'the sides fit together in more than one way, and the tumble did not say which').toBe(false);
  expect(scan.conflicted).toBe(false);
  expect(scan.complete).toBe(true);
});
