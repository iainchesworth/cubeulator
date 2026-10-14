// The page's half of the web profiling export (profile_web.cpp is the other).
//
// An Emscripten JS library (--js-library). The one function at the bottom is
// called by the module as it starts and defines `window.cubeulatorProfile`: how
// a person at the DevTools console, or a test driving the page, reads what the
// in-process recorder (cube/profile/recorder.hpp) has recorded. Tracy cannot be
// used here - its client opens a TCP listener and a tab has none to offer.
//
//   cubeulatorProfile.recording   true when this build records zones (the
//                                 web-emscripten-profiling preset); false
//                                 otherwise, and everything below is empty
//   cubeulatorProfile.summary()   statistics per zone, counts of frames and
//                                 messages, as an object
//   cubeulatorProfile.trace()     Chrome Trace Event Format, as a string: load
//                                 it with "Load profile" in the DevTools
//                                 Performance panel, or at ui.perfetto.dev
//   cubeulatorProfile.csv()       zone statistics in tracy-csvexport's columns
//   cubeulatorProfile.clear()     forget what has been recorded
//   cubeulatorProfile.mark(text)  a message on the timeline, and a User Timing
//                                 mark of the same name
//   cubeulatorProfile.now()       the recorder's clock, microseconds
//   cubeulatorProfile.memory()    the wasm heap's size and, in Chromium, the
//                                 JavaScript heap's
//   cubeulatorProfile.download(kind)  'trace' | 'csv' | 'summary': saves it
//
// The functions run on the main thread and read the recorder while the app's
// threads write it; the recorder is built for that.
//
// A library is compiled by Emscripten, not run as written: the object below is
// serialised member by member, so nothing in it may close over a variable
// outside it.
mergeInto(LibraryManager.library, {
  cube_web_profile_install__deps: ['$UTF8ToString'],
  cube_web_profile_install__proxy: 'sync',
  cube_web_profile_install: function (recording) {
    var api = {
      version: 1,
      recording: !!recording,

      summary: function () {
        return JSON.parse(UTF8ToString(_cube_web_profile_summary()));
      },
      trace: function () {
        return UTF8ToString(_cube_web_profile_trace());
      },
      csv: function () {
        return UTF8ToString(_cube_web_profile_csv());
      },
      clear: function () {
        _cube_web_profile_clear();
      },
      now: function () {
        return _cube_web_profile_now();
      },

      mark: function (text) {
        var bytes = new TextEncoder().encode(String(text));
        var length = Math.min(bytes.length, _cube_web_profile_label_capacity());
        HEAPU8.set(bytes.subarray(0, length), _cube_web_profile_label_buffer());
        _cube_web_profile_mark(length);
        if (typeof performance !== 'undefined' && typeof performance.mark === 'function') {
          performance.mark(String(text));
        }
      },

      memory: function () {
        var js = typeof performance !== 'undefined' && performance.memory ?
            performance.memory.usedJSHeapSize : null;
        return { wasmHeapBytes: HEAPU8.byteLength, jsHeapBytes: js };
      },

      download: function (kind) {
        var names = { trace: 'cubeulator-trace.json', csv: 'cubeulator-zones.csv', summary: 'cubeulator-summary.json' };
        if (!Object.prototype.hasOwnProperty.call(names, kind)) {
          throw new Error("cubeulatorProfile.download: kind is 'trace', 'csv' or 'summary'");
        }
        var text = kind === 'csv' ? api.csv() : kind === 'trace' ? api.trace() : JSON.stringify(api.summary());
        var type = kind === 'csv' ? 'text/csv' : 'application/json';
        var url = URL.createObjectURL(new Blob([text], { type: type }));
        var link = document.createElement('a');
        link.href = url;
        link.download = names[kind];
        document.body.appendChild(link);
        link.click();
        link.remove();
        setTimeout(function () { URL.revokeObjectURL(url); }, 5000);
      },
    };
    globalThis.cubeulatorProfile = api;
  },
});
