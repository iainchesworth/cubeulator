// The page's half of the test driver (driver_web.cpp is the other).
//
// An Emscripten JS library (--js-library), linked only into the diagnostics
// build (CUBEULATOR_WEB_TEST_DRIVER). The one function at the bottom is called
// by the module as it starts and defines `window.cubeulatorDriver`: a
// read-only window onto the running app for tests/web, the browser's
// counterpart of tests/system/app.hpp. Pressing is not here - a test sends
// real pointer and key events to the page.
//
//   cubeulatorDriver.state()          { screen, window, items: [...] } now
//   cubeulatorDriver.screen()         AppState.screen
//   cubeulatorDriver.items(name)      every item with that objectName
//   cubeulatorDriver.find(name)       the one a person can see, or null; throws
//                                     if two are showing (a name that is
//                                     ambiguous is a test bug, as natively)
//   cubeulatorDriver.center(name)     where to click it, in the page's CSS
//                                     pixels (the canvas's own box added)
//   cubeulatorDriver.texts()          what a person can read: the visible text
//   cubeulatorDriver.singleton(name)  a singleton's properties, or null
//   cubeulatorDriver.sides(seed, n)   the six sides of a scrambled cube, as the
//                                     camera should show them
//
// A library is compiled by Emscripten, not run as written: the object below is
// serialised member by member, so nothing in it may close over a variable
// outside it.
mergeInto(LibraryManager.library, {
  cube_web_driver_install__deps: ['$UTF8ToString', '$stringToNewUTF8', 'free'],
  cube_web_driver_install__proxy: 'sync',
  cube_web_driver_install: function () {
    var json = function (pointer) {
      return JSON.parse(UTF8ToString(pointer));
    };

    // What a person can see and press: shown, and not faded out, and having
    // size. (Each screen builds a narrow and a wide layout side by side and
    // shows one.)
    var shown = function (item) {
      return item.visible && item.opacity > 0.01 && item.width > 0 && item.height > 0;
    };

    var driver = {
      version: 1,

      state: function () {
        return json(_cube_web_driver_state());
      },
      screen: function () {
        return driver.state().screen;
      },
      items: function (name) {
        return (driver.state().items || []).filter(function (item) { return item.name === name; });
      },
      find: function (name) {
        var visible = driver.items(name).filter(shown);
        if (visible.length > 1) {
          throw new Error("'" + name + "' matches " + visible.length + ' items on show');
        }
        return visible.length === 1 ? visible[0] : null;
      },
      center: function (name) {
        var item = driver.find(name);
        if (!item) {
          return null;
        }
        var canvas = document.querySelector('#screen canvas') || document.querySelector('canvas');
        var box = canvas ? canvas.getBoundingClientRect() : { left: 0, top: 0 };
        return {
          x: box.left + item.x + item.width / 2,
          y: box.top + item.y + item.height / 2,
        };
      },
      texts: function () {
        return (driver.state().items || [])
          .filter(function (item) { return shown(item) && typeof item.text === 'string' && item.text !== ''; })
          .map(function (item) { return item.text; });
      },
      singleton: function (name) {
        var buffer = stringToNewUTF8(name);
        try {
          return json(_cube_web_driver_singleton(buffer));
        } finally {
          _free(buffer);
        }
      },
      sides: function (seed, moves) {
        return json(_cube_web_driver_sides(seed >>> 0, moves === undefined ? 10 : moves | 0));
      },
    };
    globalThis.cubeulatorDriver = driver;
  },
});
