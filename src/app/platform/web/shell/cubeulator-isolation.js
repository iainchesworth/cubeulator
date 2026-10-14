// Cross-origin isolation for a host that cannot send the headers itself.
//
// The threaded WebAssembly build needs SharedArrayBuffer, which a browser only
// hands to a page that is "cross-origin isolated": served with
//     Cross-Origin-Opener-Policy:   same-origin
//     Cross-Origin-Embedder-Policy: require-corp
// GitHub Pages - where the documentation, and so this app, is published - has
// no way to set response headers. A service worker can: it sits between the
// page and the network and may add them to the responses it hands back.
//
// This one file does both halves. index.html loads it as a classic script,
// where it registers itself as the worker for this directory and reloads the
// page once the worker is in control; the browser then loads it again as the
// worker, where it adds the headers. It is a few dozen lines on purpose: it
// runs with the page's full privileges and sees every request, so there should
// be nothing in it to audit but what it says.
//
// Nothing here is needed (and nothing is done) when the server already sends
// the headers: `crossOriginIsolated` is true on the first load.

if (typeof window === 'undefined') {
  // ---- the service worker -------------------------------------------------
  self.addEventListener('install', function () {
    self.skipWaiting();
  });

  self.addEventListener('activate', function (event) {
    // Take over the page that registered us without waiting for a reload, so
    // the page's one reload is the only one.
    event.waitUntil(self.clients.claim());
  });

  self.addEventListener('fetch', function (event) {
    const request = event.request;
    // Chrome throws on this combination if it is fetched; it is DevTools'
    // own request for a cached resource and needs no headers.
    if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') {
      return;
    }

    event.respondWith(
      fetch(request).then(function (response) {
        // An opaque response (status 0) cannot be rebuilt; pass it through.
        if (response.status === 0) {
          return response;
        }
        const headers = new Headers(response.headers);
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
        return new Response(response.body, {
          status: response.status,
          statusText: response.statusText,
          headers: headers,
        });
      })
    );
  });
} else {
  // ---- the page ------------------------------------------------------------
  (function () {
    'use strict';

    // Becomes true once isolation is in place; index.html waits on it.
    window.cubeulatorIsolation = { ready: false, error: null };

    function done() {
      window.cubeulatorIsolation.ready = true;
      window.dispatchEvent(new Event('cubeulator-isolated'));
    }

    function fail(message) {
      window.cubeulatorIsolation.error = message;
      window.dispatchEvent(new Event('cubeulator-isolated'));
    }

    if (window.crossOriginIsolated) {
      done();
      return;
    }
    if (!window.isSecureContext) {
      fail('This page must be opened over https (or from localhost) so that the camera and threads are available.');
      return;
    }
    if (!('serviceWorker' in navigator)) {
      fail('This browser cannot run service workers here (private browsing in some browsers turns them off), and Cubeulator needs one to use threads.');
      return;
    }

    const script = document.currentScript && document.currentScript.src;
    navigator.serviceWorker.register(script || 'cubeulator-isolation.js').then(function (registration) {
      // A worker that is already controlling this page but did not isolate it
      // is stale or was broken by a hard reload; reloading once more would
      // loop, so say so instead.
      if (navigator.serviceWorker.controller) {
        fail('The page is controlled by its helper worker but is still not isolated. Try reloading with the cache cleared.');
        return;
      }
      const reload = function () { window.location.reload(); };
      if (registration.active) {
        reload();
      } else {
        const worker = registration.installing || registration.waiting;
        if (!worker) {
          reload();
          return;
        }
        worker.addEventListener('statechange', function () {
          if (worker.state === 'activated') {
            reload();
          }
        });
      }
    }, function (error) {
      fail('The helper worker could not be registered: ' + error);
    });
  })();
}
