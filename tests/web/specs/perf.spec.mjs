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
import { mkdir, writeFile } from 'node:fs/promises';
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

// The app as it is shipped, light probe and all: what a visitor's page does.
test.use({ appOptions: { lightProbe: true } });

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
    // A press is measured once the first frames, which take seconds each with
    // the light probe on a software renderer, are out of the way.
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
