# Try it in your browser

Cubeulator runs in a web browser, as WebAssembly, alongside the Windows, macOS
and Linux apps. It is the same app: the same solver, the same camera reader
and the same screens, built from the same source.

<p style="text-align: center; margin: 2em 0;">
  <a class="md-button md-button--primary" href="../app/" target="_blank" rel="noopener">Open Cubeulator</a>
</p>

It opens in its own tab, because it wants the whole window and, on a phone,
the whole screen.

No camera to hand? <a href="../app/?demo=7" target="_blank" rel="noopener">Open a
scrambled cube</a> to see the solve screen, with a solution already worked
out. Any number after `demo=` is a different scramble.

What the screens are and how to use them is in the [user guide](guide/index.md);
this page is about running it in a browser.

## What you need

- A current Chrome, Edge, Firefox or Safari. The page needs WebAssembly
  threads, WebAssembly SIMD and WebGL 2; Safari has had all three since 16.4.
- A camera, if you want to scan a cube. A phone's rear camera is the one to
  use; a laptop's webcam works too.
- A secure page. The camera is only available to `https://` pages (and to
  `localhost`), which is how this one is served.
- Service workers allowed for the site. Some browsers turn them off in
  private windows; the page says so when it cannot start (see
  [How it works](#how-it-works) for why it needs one).

The first visit downloads the app and then reloads the page once. Later visits
start from the browser's cache.

## Privacy

The page is static files. There is no server for the app to talk to, so
nothing it sees leaves your device: the camera frames are read, and the cube
solved, inside the page, in your browser. The browser asks for camera access
the first time the app opens the camera, shows its own indicator while the
camera is on, and you can withdraw the permission from the site settings in
the address bar at any time. The app's settings are kept in your browser's
local storage for this site, and nowhere else.

## Differences from the desktop apps

| | Desktop apps | In the browser |
|---|---|---|
| **Camera permission** | The OS asks the first time a camera is opened | The browser asks the same way. It also hides the cameras' names until you have allowed one, so the list says *Back camera* and *Front camera* until then |
| **Exposure and white balance** | Locked for a scan where the camera allows it (Windows) | Not locked: the camera's own automatics run. Colours can drift between sides under changing light, so scan under steady, even light |
| **Settings** | Kept between launches | Kept in the browser's local storage for this site: they survive a reload and are lost with the site's data |
| **Text** | The system's fonts | A browser has no system fonts, so the app carries its own Japanese and Simplified Chinese faces (Noto Sans JP and SC, cut down to the characters of the Japanese and Chinese translations and the language names: about 350 and 270 kilobytes). Everything else is drawn with the face Qt bundles |
| **Speed** | Native | The solver and the reader are WebAssembly. Expect the same results, somewhat slower |

## How it works

The app is built for the browser with [Emscripten](https://emscripten.org/)
and Qt for WebAssembly, and drawn with WebGL. In source it is one more
platform in `src/app/platform/` (`web/`), selected by CMake like the others -
see [Platform isolation](platform-isolation.md). What differs from the
desktops is small:

- **The camera** is the browser's `getUserMedia()`. A `<video>` element is
  drawn to a canvas, scaled to the 640 px the reader works at, and copied into
  the same BGR frame every other platform hands over.
- **Threads** are Web Workers on shared memory, which browsers only allow for
  a page that is *cross-origin isolated*: served with
  `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp`. GitHub Pages cannot send
  headers, so the app ships a small service worker
  (`cubeulator-isolation.js`) that adds the two to the app's own files and
  reloads the page once. A host that sends the headers itself needs none of
  it, and the worker steps aside. Its scope is the `/app/` directory: nothing
  else on this site is touched.
- **Nothing else** is platform code: the solver, the vision pipeline and the
  QML are the shared ones.

## Hosting it yourself

Each release carries the same site as `Cubeulator-<version>-web.zip`: the
files above and nothing else, with the licence, the third-party notices and a
`HOSTING.txt` that says what a host has to do. Unzip it behind any static
server, over https:

```sh
unzip Cubeulator-<version>-web.zip
python3 -m http.server 8000 --directory Cubeulator-<version>-web
# then open http://localhost:8000/
```

The zip is built by the same workflow, from the same commit, as the
installers, and is covered by the release's `SHA512SUMS`, GPG signatures and
provenance attestation (see [Cutting a release](releasing.md#verifying-a-download)).
It is what the hosted copy serves, so the two behave alike.

## Building it yourself

See [Building & packaging](building.md#webassembly-browser) for the preset,
the Emscripten and Qt versions it needs, and how to serve the result locally.
