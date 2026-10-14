// The page's half of keeping settings across a reload (settings_web.cpp is the
// other).
//
// An Emscripten JS library (--js-library). The one function at the bottom is
// called by the module as it starts and listens for the page being put away:
// the tab going to the background (`visibilitychange` to hidden) and the page
// being left, by a reload, a navigation or the tab closing (`pagehide`). Each
// asks the app to write what it still holds, synchronously, because that is
// the last the page runs: whatever is not in the browser's localStorage when
// the handler returns is lost.
//
// Both events are listened for because neither is enough alone. A tab that is
// closed or reloaded always gets `pagehide`; Safari does not always send
// `visibilitychange` first. A tab sent to the background gets
// `visibilitychange` and not `pagehide`, and on a phone that is the last event
// a page that is then killed by the system ever gets.
//
// A library is compiled by Emscripten, not run as written: the object below is
// serialised member by member, so nothing in it may close over a variable
// outside it.
mergeInto(LibraryManager.library, {
  cube_web_settings_install__proxy: 'sync',
  cube_web_settings_install: function () {
    var flush = function () {
      try {
        _cube_web_settings_flush();
      } catch (error) {
        // A module that has already failed cannot write anything, and an
        // exception thrown from a handler during unload helps nobody.
        console.warn('Cubeulator: could not write the settings', error);
      }
    };

    document.addEventListener('visibilitychange', function () {
      if (document.visibilityState === 'hidden') {
        flush();
      }
    });
    window.addEventListener('pagehide', flush);
  },
});
