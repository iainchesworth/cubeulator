// Where the site under test is, shared by the configuration and the fixtures.
import { resolve } from 'node:path';

export const siteRoot = resolve(
  process.env.CUBEULATOR_WEB_DIR ?? '../../build/web-emscripten-diagnostics/bin');
export const port = Number(process.env.CUBEULATOR_WEB_PORT ?? 8123);
export const baseURL = `http://127.0.0.1:${port}`;

// How the browser is started. WebGL 2 without a GPU (the app draws with it, and
// a CI runner has none), and timers and rendering that go on for a page that is
// not in front: the suite runs several pages at once.
export const browserArgs = [
  '--use-angle=swiftshader',
  '--enable-unsafe-swiftshader',
  '--ignore-gpu-blocklist',
  '--disable-background-timer-throttling',
  '--disable-renderer-backgrounding',
  '--disable-backgrounding-occluded-windows',
];
