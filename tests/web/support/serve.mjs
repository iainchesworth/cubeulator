// A static file server for the built site, for the Playwright suite.
//
//   node support/serve.mjs <directory> [port]
//
// What it adds to "serve this directory": the two headers that make a page
// cross-origin isolated (Cross-Origin-Opener-Policy and -Embedder-Policy),
// which the app's threads need (SharedArrayBuffer), so the app starts at once
// the way it does behind a host that sends them, rather than through its
// service worker's reload. docs/web.md says why a static host cannot send them.
// It also types .wasm correctly (application/wasm: streaming compilation
// refuses anything else) and refuses to leave the directory.
import { createServer } from 'node:http';
import { createReadStream, statSync } from 'node:fs';
import { extname, join, normalize, resolve, sep } from 'node:path';

const root = resolve(process.argv[2] ?? '.');
const port = Number(process.argv[3] ?? process.env.PORT ?? 8123);

const types = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.css': 'text/css; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
};

const server = createServer((request, response) => {
  const url = new URL(request.url ?? '/', 'http://localhost');
  let path = normalize(join(root, decodeURIComponent(url.pathname)));
  if (path !== root && !path.startsWith(root + sep)) {
    response.writeHead(403).end();
    return;
  }
  try {
    if (statSync(path).isDirectory()) {
      path = join(path, 'index.html');
    }
    const { size } = statSync(path);
    response.writeHead(200, {
      'Content-Type': types[extname(path)] ?? 'application/octet-stream',
      'Content-Length': size,
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cache-Control': 'no-cache',
    });
    createReadStream(path).pipe(response);
  } catch {
    response.writeHead(404).end('not found');
  }
});

server.listen(port, '127.0.0.1', () => {
  console.log(`serving ${root} on http://127.0.0.1:${port}/`);
});
