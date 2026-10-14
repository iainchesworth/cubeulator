// The suite's fixtures.
//
// `app` is a freshly booted page, one per test, with the browser's storage
// cleared first (the app keeps its settings there), so nothing leaks from one
// test into the next. Booting costs about a second: the 45 MB module compiles
// in under one and the first screen is up in two. The browser context is the
// worker's, made once per set of options (support/app.mjs says why). A test
// that wants the page another way sets `appOptions`:
//
//   test.use({ appOptions: { viewport: PHONE, touch: true } });
//
// After each test the page's problems - console errors, uncaught exceptions,
// requests that failed - fail the test unless it said it expected them
// (`app.expectProblems(/pattern/)`), and its console is attached to a failure.
import { test as base, expect } from '@playwright/test';
import { CubeulatorApp } from './app.mjs';

export const test = base.extend({
  appOptions: [{}, { option: true }],

  // The browser contexts of this worker, by their options.
  appContexts: [async ({}, use) => {
    const contexts = new Map();
    await use(contexts);
    for (const context of contexts.values()) {
      await context.close();
    }
  }, { scope: 'worker' }],

  app: async ({ browser, appOptions, appContexts }, use, testInfo) => {
    const app = await CubeulatorApp.open(browser, appOptions, appContexts);
    await use(app);

    if (testInfo.status !== testInfo.expectedStatus) {
      await testInfo.attach('console', { body: app.log.join('\n'), contentType: 'text/plain' });
    }
    const unexpected = app.unexpectedProblems();
    await app.close();
    expect(unexpected, 'console errors, page errors and failed requests').toEqual([]);
  },
});

export { expect };
