pragma Singleton
import QtQuick

// The navigation stack and the one `back()` every platform's back binding
// calls (docs/design/journey.md, "Rules that hold everywhere" and "What
// 'back' does"; docs/design/platforms.md, "Back").
//
// This holds only where the person is and where back goes. It does not draw
// anything and it knows nothing of the scan or the solve: a screen is an id
// ("home", "scan", "solve"...) and some parameters. What the legacy AppState
// does to route screens moves here in the E2a split proposed in docs/kit.md;
// until that is agreed AppState keeps routing and this runs beside it,
// exercised by the gallery and the tests.
//
// The rules, from journey.md:
//   1. Back goes where the person came from, not to a fixed parent.
//   2. Three exceptions, each to stop someone landing in a finished or broken
//      place: back from "complete" goes to "home", not into the finished
//      solve; back from "solve" goes to "home" and keeps the solve; back from
//      "scan" after a camera failure goes to "home".
//   3. An open dialog or sheet takes back first and chooses its safe option.
//      After that a screen's guard may take it (leaving a scan that has read
//      sides asks first).
//   4. Back never quits the app. At the root it reports that nothing was
//      done and the platform decides (Android leaves from "home"; no other
//      platform does anything).
QtObject {
    id: nav

    // Entries are { id, params, backTo }. The first is the root; the last is
    // where the person is.
    property var stack: []
    // Open dialogs and sheets, innermost last. Each has a `requestBack()`
    // that chooses its safe option and closes it.
    property var overlays: []
    // Functions that get first say on back, innermost last. A guard returns
    // true if it handled back (the scan screen opens "Leave the scan?" and
    // returns true while a side has been read); false lets back carry on.
    property var guards: []

    readonly property string current: stack.length > 0 ? stack[stack.length - 1].id : ""
    readonly property var currentParams: stack.length > 0 ? stack[stack.length - 1].params : ({})
    readonly property int depth: stack.length
    // Would back do anything? Used to decide whether a back control is shown
    // and whether an Android back gesture is ours.
    readonly property bool canGoBack: overlays.length > 0 || stack.length > 1

    // Screens whose back goes to a fixed place whatever the stack holds.
    // A screen id maps to the id it goes to.
    readonly property var backTargets: ({
        complete: "home",
        solve: "home"
    })
    // What "home" is: the screen the stack is rebuilt on when a back target
    // is not in the stack (after a restart that resumed straight into a solve).
    readonly property string homeId: "home"

    // kind: "push" | "pop" | "replace" | "reset"
    signal navigated(string id, var params, string kind)
    // Back was pressed with nothing to go back to. The platform decides what
    // that means: only Android quits, and only from home.
    signal backAtRoot()

    // ---- Moving -----------------------------------------------------------------
    // Start again from this screen: app start, or after the stack is no
    // longer meaningful.
    function reset(id, params) {
        stack = [{ id: id, params: params || ({}), backTo: "" }];
        navigated(id, stack[0].params, "reset");
    }

    function push(id, params, options) {
        const entry = {
            id: id,
            params: params || ({}),
            backTo: (options && options.backTo) ? options.backTo : ""
        };
        stack = stack.concat([entry]);
        navigated(id, entry.params, "push");
    }

    // The current screen becomes this one without a back entry for it.
    function replace(id, params, options) {
        const entry = {
            id: id,
            params: params || ({}),
            backTo: (options && options.backTo) ? options.backTo : ""
        };
        stack = stack.length > 0 ? stack.slice(0, stack.length - 1).concat([entry]) : [entry];
        navigated(id, entry.params, "replace");
    }

    // One step back, ignoring the exceptions. Back is `back()`; this is for
    // code that already knows where it wants to be.
    function pop() {
        if (stack.length <= 1)
            return false;
        stack = stack.slice(0, stack.length - 1);
        const top = stack[stack.length - 1];
        navigated(top.id, top.params, "pop");
        return true;
    }

    // Back to the nearest entry with this id; false if there is none.
    function popTo(id) {
        for (let i = stack.length - 1; i >= 0; --i) {
            if (stack[i].id === id) {
                stack = stack.slice(0, i + 1);
                navigated(id, stack[i].params, "pop");
                return true;
            }
        }
        return false;
    }

    // Marks where back goes from the screen the person is on now, for a
    // failure that makes the way they came a bad place to return to: the
    // camera failing under "scan".
    function setBackTarget(id) {
        if (stack.length === 0)
            return;
        const copy = stack.slice();
        const last = copy[copy.length - 1];
        copy[copy.length - 1] = { id: last.id, params: last.params, backTo: id };
        stack = copy;
    }

    // ---- Overlays ---------------------------------------------------------------------
    function registerOverlay(overlay) {
        if (overlays.indexOf(overlay) < 0)
            overlays = overlays.concat([overlay]);
    }

    function unregisterOverlay(overlay) {
        const index = overlays.indexOf(overlay);
        if (index >= 0)
            overlays = overlays.slice(0, index).concat(overlays.slice(index + 1));
    }

    // ---- Guards -------------------------------------------------------------------------
    function addGuard(guard) {
        if (guards.indexOf(guard) < 0)
            guards = guards.concat([guard]);
    }

    function removeGuard(guard) {
        const index = guards.indexOf(guard);
        if (index >= 0)
            guards = guards.slice(0, index).concat(guards.slice(index + 1));
    }

    // ---- Back ---------------------------------------------------------------------------
    // Returns true if back did something. False at the root: the app is not
    // quit from here.
    function back() {
        if (overlays.length > 0) {
            const top = overlays[overlays.length - 1];
            if (top && typeof top.requestBack === "function")
                top.requestBack();
            return true;
        }
        for (let i = guards.length - 1; i >= 0; --i) {
            if (guards[i]() === true)
                return true;
        }
        if (stack.length === 0) {
            backAtRoot();
            return false;
        }

        const entry = stack[stack.length - 1];
        const target = entry.backTo !== "" ? entry.backTo : (backTargets[entry.id] || "");
        if (target !== "" && target !== entry.id) {
            if (popTo(target))
                return true;
            // Not on the stack (the app resumed straight into the solve):
            // rebuild with the target as the root.
            reset(target, {});
            return true;
        }

        if (stack.length > 1)
            return pop();

        backAtRoot();
        return false;
    }
}
