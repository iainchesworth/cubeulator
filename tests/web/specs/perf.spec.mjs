// How the app performs in the browser, read from its own recorder
// (window.cubeulatorProfile, src/lib/include/cube/profile/recorder.hpp): the
// same zones Tracy shows on a desktop, taken from inside the page, for journeys
// a person makes. docs/profiling.md says how the numbers are produced and what
// to keep in mind reading them.
//
// Two kinds of check, as in tests/perf (docs/testing.md, "Performance tests"):
//
//   * what the app DID - how many solves, frames, search nodes; how large the
//     heap grew. Exact or bounded, the same on any machine, and always run;
//   * how long it TOOK - wall time, against budgets set well above what a
//     software-rendered CI runner measures, so that a regression of an order of
//     magnitude fails and noise does not. A stopwatch on a shared runner is the
//     one measurement that cannot be trusted, so these only run when asked for
//     (CUBEULATOR_WEB_PERF_TIMING=1, which the perf project of the CI leg sets).
//
// Either way each journey leaves its numbers behind: reports/perf/<name>.json
// (the summary), <name>.trace.json (open it in the DevTools Performance panel
// or Perfetto) and <name>.csv (tracy-csvexport's columns), attached to the
// test and uploaded by CI.
import { test, expect } from '../support/fixtures.mjs';
import { withEdgeFlipped } from '../support/app.mjs';
import { mkdir, writeFile } from 'node:fs/promises';
import { readFileSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';

const TIMING = process.env.CUBEULATOR_WEB_PERF_TIMING === '1';
const REPORTS = resolve('reports', 'perf');

// A wall-time budget: always recorded, enforced only when timing is on.
function withinBudget(label, value, budget, report) {
  report.budgets.push({ label, value, budget, enforced: TIMING });
  if (TIMING) {
    expect(value, `${label} (budget ${budget})`).toBeLessThanOrEqual(budget);
  }
}

async function leaveReport(app, name, report) {
  await mkdir(REPORTS, { recursive: true });
  const profile = await app.page.evaluate(() => ({
    summary: window.cubeulatorProfile.summary(),
    trace: window.cubeulatorProfile.trace(),
    csv: window.cubeulatorProfile.csv(),
    memory: window.cubeulatorProfile.memory(),
  }));
  report.memory = profile.memory;
  report.summary = profile.summary;
  report.renderer = await app.page.evaluate(() => {
    const gl = document.createElement('canvas').getContext('webgl2');
    const info = gl && gl.getExtension('WEBGL_debug_renderer_info');
    return info ? gl.getParameter(info.UNMASKED_RENDERER_WEBGL) : 'unknown';
  });
  await writeFile(resolve(REPORTS, `${name}.json`), JSON.stringify(report, null, 2));
  await writeFile(resolve(REPORTS, `${name}.trace.json`), profile.trace);
  await writeFile(resolve(REPORTS, `${name}.csv`), profile.csv);
  await test.info().attach(`${name}.json`, {
    body: JSON.stringify(report, null, 2), contentType: 'application/json' });
  await test.info().attach(`${name}.trace.json`, {
    body: profile.trace, contentType: 'application/json' });
}

const zone = (summary, name) => {
  const entry = summary.zones[name];
  expect(entry, `zone ${name} was recorded`).toBeDefined();
  return entry;
};


// What each frame of the scan cost, from the recorder's trace: the summary has
// a zone's mean and its longest, and a ceiling wants the tail as well.
//
// With `cold`, the dearest of the first ten frames is reported apart (`cold`)
// and left out of the rest: the first read of a real picture costs the page
// a quarter to a half of a second, once, wherever it falls among the frames
// before it (docs/investigations/2026-10-15-web-scan-frame-cost.md), and what a
// ceiling is for is the frames that follow it.
async function frameCosts(app, { cold = false } = {}) {
  return app.page.evaluate((withCold) => {
    const trace = JSON.parse(window.cubeulatorProfile.trace());
    const all = trace.traceEvents.filter((e) => e.ph === 'X' && e.name === 'direct_scan.process_frame')
      .sort((a, b) => a.ts - b.ts).map((e) => e.dur / 1000);
    let coldMs = 0;
    let kept = all;
    if (withCold && all.length > 0) {
      const head = all.slice(0, 10);
      const slowest = head.indexOf(Math.max(...head));
      coldMs = head[slowest];
      kept = all.filter((_, i) => i !== slowest);
    }
    const rest = [...kept].sort((a, b) => a - b);
    const at = (p) => (rest.length > 0 ? rest[Math.min(rest.length - 1, Math.floor(p * rest.length))] : 0);
    return {
      count: all.length,
      dropped: trace.otherData.dropped,
      cold: coldMs,
      mean: rest.length > 0 ? rest.reduce((a, b) => a + b, 0) / rest.length : 0,
      p50: at(0.5),
      p95: at(0.95),
      max: rest.length > 0 ? rest[rest.length - 1] : 0,
      over33: rest.filter((v) => v > 33).length,
      over50: rest.filter((v) => v > 50).length,
      over100: rest.filter((v) => v > 100).length,
    };
  }, cold);
}

// How many times the scan put its six named sides together into a cube while it
// was recorded (a search over the 4^6 turns of the sides, and for sides that make
// no cube a search for each pair of centres whose names might be traded): the
// recorder's `vision.direct_scan.assemble` zone, which has none for a judgement
// answered as the last one was (SideScan::assemblies(), kRejudgeLabDistance).
// `later` are those in the second half of the time the scan was read for.
async function assembliesRun(app) {
  return app.page.evaluate(() => {
    const events = JSON.parse(window.cubeulatorProfile.trace()).traceEvents;
    const frames = events.filter((e) => e.ph === 'X' && e.name === 'direct_scan.process_frame')
      .map((e) => e.ts);
    const assembled = events.filter((e) => e.ph === 'X' && e.name === 'vision.direct_scan.assemble')
      .sort((a, b) => a.ts - b.ts);
    const first = frames.length > 0 ? Math.min(...frames) : 0;
    const last = frames.length > 0 ? Math.max(...frames) : 0;
    const middle = (first + last) / 2;
    return {
      count: assembled.length,
      later: assembled.filter((e) => e.ts > middle).length,
      most_ms: assembled.reduce((m, e) => Math.max(m, e.dur / 1000), 0),
    };
  });
}

// One camera frame is 33 ms, and the scan runs on the page's main thread: a
// frame that takes longer is one the page does not draw, and the camera's
// frames wait. These are the ceilings a frame of a scan is held to, set well
// above what a software-rendered runner measures (it draws with the same CPU
// the scan reads with): the mean inside one camera interval, the tail inside
// two, and the longest of a held scan inside a quarter of a second.
const FRAME_MEAN_MS = 33;
const FRAME_P95_MS = 66;
const FRAME_MAX_MS = 250;
// The page's one cold frame, the first read of a real picture (see frameCosts).
const COLD_FRAME_MS = 1_500;

function leaveFrameBudgets(frames, report) {
  report.frames = frames;
  withinBudget('mean frame of the scan (ms)', frames.mean, FRAME_MEAN_MS, report);
  withinBudget('95th percentile frame of the scan (ms)', frames.p95, FRAME_P95_MS, report);
  withinBudget('longest frame of the scan (ms)', frames.max, FRAME_MAX_MS, report);
  if (frames.cold > 0) {
    withinBudget('cold frame of the scan (ms)', frames.cold, COLD_FRAME_MS, report);
  }
}

test.describe('performance', () => {
  test('start-up: from navigation to the first screen', async ({ app }) => {
    const report = { journey: 'startup', budgets: [] };
    const summary = await app.profile();

    // What start-up did: the QML loaded once, the catalogues installed once.
    expect(zone(summary, 'app.load_qml').count).toBe(1);
    expect(zone(summary, 'app.i18n_install').count).toBe(1);
    expect(summary.dropped).toBe(0);

    report.bootMs = app.bootMs;
    withinBudget('time to the first screen (ms)', app.bootMs, 60_000, report);
    withinBudget('QML load (ms)', zone(summary, 'app.load_qml').total_ms, 5_000, report);

    // Memory at rest: the wasm heap the module asked for, in MB.
    const memory = await app.page.evaluate(() => window.cubeulatorProfile.memory());
    report.heapMB = memory.wasmHeapBytes / 1048576;
    expect(memory.wasmHeapBytes).toBeLessThan(1024 * 1048576);

    await leaveReport(app, 'startup', report);
  });

  test('scan and solve: six sides read, one solution made', async ({ app }) => {
    const report = { journey: 'scan-and-solve', budgets: [] };
    await app.waitForFrames();
    await app.clearProfile();

    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);

    const sides = await app.sides(1, 10);
    await app.mark('journey.scan.begin');
    const began = Date.now();
    await app.showCube(sides);
    const scanned = Date.now() - began;
    await app.mark('journey.scan.end');

    await app.waitForScreen('solve', 120_000);
    await expect.poll(async () => (await app.singleton('SolveController')).isSolving).toBe(false);
    await app.mark('journey.solve.shown');
    const summary = await app.profile();

    // What the app did: one scan, read frame by frame; one solve.
    expect((await app.singleton('DirectScan')).sidesRead).toBe(6);
    expect(zone(summary, 'solver.solve').count).toBe(1);
    expect(zone(summary, 'app.solve_stepped').count).toBe(1);
    expect(zone(summary, 'vision.direct_scan.read_side').count).toBeGreaterThanOrEqual(6);
    const frames = zone(summary, 'direct_scan.process_frame');
    expect(frames.count).toBeGreaterThan(20);
    expect(summary.dropped).toBe(0);
    // Marks the test left on the timeline are in the summary, in order.
    expect(summary.messages['journey.scan.begin']).toBe(1);
    expect(summary.messages['journey.solve.shown']).toBe(1);

    report.scanMs = scanned;
    report.framesProcessed = frames.count;
    report.frameMeanMs = frames.mean_ms;
    report.frameMaxMs = frames.max_ms;
    report.solveMs = zone(summary, 'solver.solve').total_ms;
    report.uiFrames = summary.frames.UI ?? 0;

    // A frame has to be read in less than the 33 ms between a camera's frames,
    // on average, or the scan falls behind the person holding the cube.
    withinBudget('mean frame read (ms)', frames.mean_ms, 33, report);
    withinBudget('solve, Beginner (ms)', report.solveMs, 1_500, report);
    withinBudget('scan of six sides, wall (ms)', scanned, 150_000, report);

    await leaveReport(app, 'scan-and-solve', report);
  });

  test('six sides that make no cube, held in view: they are put together once, not on every frame',
    async ({ app }) => {
      test.setTimeout(300_000);
      const report = { journey: 'no-cube-held', budgets: [] };
      await app.waitForFrames();

      await app.click('launch.startScan');
      await app.waitForScreen('scan');
      await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);

      // A cube with one edge flipped in place: its six sides are read as six
      // sides, and no cube comes of them, so putting them together asks the
      // listing and then each of the fifteen pairs of centres. The camera's own
      // noise is on the stickers, so that each frame is a read that moves a
      // fused colour a little, which is what a held cube is.
      const noise = 3;
      const sides = withEdgeFlipped(await app.sides(1, 10));
      await app.showCube(sides, { noise });
      const scan = await app.singleton('DirectScan');
      expect(scan.sidesRead).toBe(6);
      expect(scan.complete).toBe(false);
      expect(scan.conflicted, 'six sides that make no cube').toBe(true);

      // The last side stays in front of the camera.
      await app.showSide(sides.faces[5].stickers, undefined, noise);
      await app.clearProfile();
      await app.mark('journey.hold.begin');
      await app.page.waitForTimeout(8_000);
      await app.mark('journey.hold.end');

      const summary = await app.profile();
      const frames = await frameCosts(app);
      const assembled = await assembliesRun(app);
      expect(summary.dropped).toBe(0);
      report.assembled = assembled;
      report.framesProcessed = frames.count;

      // What the scan did, counted: it was judged on frames of the held side,
      // and the sides were put together afresh for a naming it had not met and
      // for none it had. The first reads of the held side move its fused
      // colours and so its naming a few times; once they settle a frame is
      // judged for nothing, which is what a scan held for seconds is. Once for
      // every frame is what it was.
      expect(frames.count, 'frames of the held scan').toBeGreaterThan(20);
      expect(assembled.count, 'times the sides were put together').toBeLessThanOrEqual(16);
      expect(assembled.later, 'times in the second half of the hold').toBe(0);

      leaveFrameBudgets(frames, report);
      await leaveReport(app, 'no-cube-held', report);
    });

  // The browser's counterpart of `cube_scan cost`, on a session the simulator
  // drew (tools/scan; docs/testing.md "Browser tests"), which is not in the
  // repository: a recording shows a person, and a drawing is made by running the
  // tool. Skipped unless one is named:
  //
  //   cube_tumble_sim /tmp/tumble "<layout>" 60 30
  //   TUMBLE_SESSION=/tmp/tumble CUBEULATOR_WEB_PERF_TIMING=1 \
  //     npx playwright test --project=perf -g tumbled
  test('a tumbled scan: what a frame costs, and how often the sides are put together', async ({ app }) => {
    test.skip(!process.env.TUMBLE_SESSION, 'set TUMBLE_SESSION to a directory cube_tumble_sim wrote');
    test.setTimeout(600_000);
    const session = process.env.TUMBLE_SESSION;
    const report = { journey: 'tumble', session, budgets: [] };
    const names = readdirSync(session).filter((n) => /^rec_\d+\.jpg$/.test(n)).sort();
    const stamped = readFileSync(`${session}/index.csv`, 'utf8').trim().split('\n').slice(1)
      .map((row) => Number(row.split(',')[1]));
    // A drawn session's times start at zero, a recording's are the clock's.
    const times = stamped.map((t) => t - stamped[0]);
    expect(names.length).toBeGreaterThan(0);
    await app.waitForFrames();

    await app.click('launch.startScan');
    await app.waitForScreen('scan');
    await expect.poll(async () => (await app.singleton('DirectScan')).running).toBe(true);
    await app.clearProfile();

    // As the camera gives them, at their own times. Each stretch of frames is
    // read out of the recorder before it is overwritten.
    const costs = [];
    const started = Date.now();
    let assembled = 0;
    for (let i = 0; i < names.length; i++) {
      const wait = times[i] - (Date.now() - started);
      if (wait > 0) {
        await new Promise((resolve) => setTimeout(resolve, wait));
      }
      const picture = readFileSync(`${session}/${names[i]}`).toString('base64');
      await app.page.evaluate((data) => window.__fakeCamera.image(`data:image/jpeg;base64,${data}`), picture);
      if (i % 60 === 59 || i === names.length - 1) {
        costs.push(await frameCosts(app, { cold: costs.length === 0 }));
        assembled += (await assembliesRun(app)).count;
        await app.clearProfile();
        if ((await app.singleton('DirectScan')).complete) {
          break;
        }
      }
    }

    // The stretches, folded into one: the means by count, the tail by the worst
    // stretch (a tail of the whole is no more than the worst tail of a part).
    const count = costs.reduce((n, c) => n + c.count, 0);
    const frames = {
      count,
      dropped: costs.reduce((n, c) => n + c.dropped, 0),
      cold: costs.length > 0 ? costs[0].cold : 0,
      mean: count > 0 ? costs.reduce((s, c) => s + c.mean * c.count, 0) / count : 0,
      p50: Math.max(0, ...costs.map((c) => c.p50)),
      p95: Math.max(0, ...costs.map((c) => c.p95)),
      max: Math.max(0, ...costs.map((c) => c.max)),
      over33: costs.reduce((n, c) => n + c.over33, 0),
      over50: costs.reduce((n, c) => n + c.over50, 0),
      over100: costs.reduce((n, c) => n + c.over100, 0),
    };
    report.assembled = assembled;
    expect(frames.count).toBeGreaterThan(0);
    // Counted, so always: one assembly for each frame is what the scan did
    // before it kept the last assembly that failed, and is not what it is now.
    expect(assembled, 'times the sides were put together').toBeLessThan(frames.count / 4);

    leaveFrameBudgets(frames, report);
    await leaveReport(app, 'tumble', report);
  });

  test('solution methods: what each costs to make', async ({ app }) => {
    const report = { journey: 'methods', budgets: [], methods: {} };
    // Not through a scan: the demo entry goes straight to a solution, so what
    // is measured here is the solver in this page and nothing else.
    await app.page.goto('/index.html?demo=3');
    await app.waitUntilDrawn('solve');
    await expect.poll(async () => (await app.singleton('SolveController')).isSolving).toBe(false);

    const methods = [['Beginner', 0], ['Intermediate', 1], ['Advanced', 2]];
    for (const [name, index] of methods) {
      // Beginner is what the page opened with: its solve is already in the
      // recorder, from before the page was drawn.
      if (name !== 'Beginner') {
        await app.clearProfile();
        await app.click(`solve.method.${index}`);
        await expect.poll(async () => (await app.singleton('SolveController')).method).toBe(name);
      }
      await expect.poll(async () => (await app.singleton('SolveController')).isSolving,
        { timeout: 60_000 }).toBe(false);
      const summary = await app.profile();
      const solve = (await app.singleton('SolveController'));
      report.methods[name] = {
        moves: solve.totalMoves,
        solveMs: summary.zones['solver.solve']?.total_ms ?? null,
        tablesMs: summary.zones['solver.two_phase.prepare']?.total_ms ?? null,
      };
      expect(solve.totalMoves).toBeGreaterThan(0);
    }

    // Fewer turns for more thinking, in that order.
    const moves = report.methods;
    expect(moves.Intermediate.moves).toBeLessThan(moves.Beginner.moves);
    expect(moves.Advanced.moves).toBeLessThan(moves.Intermediate.moves);

    withinBudget('Beginner solve (ms)', moves.Beginner.solveMs, 1_500, report);
    withinBudget('Intermediate solve (ms)', moves.Intermediate.solveMs, 3_000, report);
    // The first Advanced solve builds the two-phase tables.
    withinBudget('Advanced solve with its tables (ms)',
      moves.Advanced.solveMs + (moves.Advanced.tablesMs ?? 0), 15_000, report);

    await leaveReport(app, 'methods', report);
  });

  test('walking a solution: a press is answered at once, and nothing leaks', async ({ app }) => {
    const report = { journey: 'walk', budgets: [] };
    await app.page.goto('/index.html?demo=3');
    await app.waitUntilDrawn('solve');
    // A press is measured once the first frames, which take seconds each on a
    // software renderer, are out of the way.
    await app.waitForFrames();
    await app.clearProfile();

    const before = await app.page.evaluate(() => window.cubeulatorProfile.memory().wasmHeapBytes);
    const presses = 40;
    const latencies = [];
    for (let i = 1; i <= presses; i++) {
      const began = Date.now();
      await app.click('solve.next');
      // Polled every 20 ms in the page: the default back-off of expect.poll
      // reaches a second, which would be the number measured.
      await app.page.waitForFunction(
        (index) => window.cubeulatorDriver.singleton('SolveController').moveIndex === index,
        i, { polling: 20, timeout: 30_000 });
      latencies.push(Date.now() - began);
    }
    latencies.sort((a, b) => a - b);
    const after = await app.page.evaluate(() => window.cubeulatorProfile.memory().wasmHeapBytes);

    report.pressMedianMs = latencies[Math.floor(latencies.length / 2)];
    report.pressWorstMs = latencies[latencies.length - 1];
    report.heapGrowthMB = (after - before) / 1048576;

    // Always: forty moves do not grow the heap by more than a few megabytes.
    expect(after - before).toBeLessThan(64 * 1048576);
    withinBudget('median press to counter (ms)', report.pressMedianMs, 1_000, report);

    await leaveReport(app, 'walk', report);
  });
});
