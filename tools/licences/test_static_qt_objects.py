#!/usr/bin/env python3
"""The notices generator on a static Qt's link line (CTest case
notices_static_qt_selftest).

A static Qt (the browser build) links its QML plugins' registration objects,
their resource bundles and the platform plugin as object files, not libraries.
cmake/ThirdPartyNoticesGenerate.cmake attributes those by where they sit under
the Qt prefix (cn_qt_object_key), against the library keys third_party/notices/
qt-modules.json lists. This runs the real generator, in script mode, over a
file API reply made up of such objects and holds the rule:

* each shape of object resolves, and to the component that owns it;
* an object that follows no known layout stays unresolved (a Qt layout change
  is a failure to look at, not a quiet miss);
* the browser platform, whose deployedRoots is an empty list, is not warned
  about a missing list.

It needs only CMake and the repository (no Qt, no build tree).
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "cmake" / "ThirdPartyNoticesGenerate.cmake"
CMAKE = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
# the generator tells an absolute path by "^([A-Za-z]:)?/"
QT = ("C:" if os.name == "nt" else "") + "/sim/Qt/6.12.0/wasm_multithread"


def run_generator(objects: list[str], platform: str = "web") -> dict:
    """Run the generator over a link line of `objects` (paths under the Qt
    prefix) and return its third-party-notices.json."""
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
        build = Path(tmp) / "build"
        reply = build / ".cmake" / "api" / "v1" / "reply"
        reply.mkdir(parents=True)
        tokens = [f"{QT}/{o}" for o in objects]
        (reply / "index-1.json").write_text(
            json.dumps({"reply": {"codemodel-v2": {"jsonFile": "codemodel.json"}}}), encoding="utf-8"
        )
        (reply / "codemodel.json").write_text(
            json.dumps(
                {
                    "configurations": [
                        {"name": "Release", "targets": [{"name": "cube_app", "jsonFile": "target.json"}]}
                    ]
                }
            ),
            encoding="utf-8",
        )
        (reply / "target.json").write_text(
            json.dumps({"link": {"commandFragments": [{"role": "libraries", "fragment": " ".join(tokens)}]}}),
            encoding="utf-8",
        )
        out = Path(tmp) / "out"
        out.mkdir()
        cmd = [
            CMAKE,
            f"-DCUBE_NOTICES_SOURCE_DIR={ROOT.as_posix()}",
            f"-DCUBE_NOTICES_BINARY_DIR={build.as_posix()}",
            "-DCUBE_NOTICES_TARGET=cube_app",
            f"-DCUBE_NOTICES_PLATFORM={platform}",
            f"-DCUBE_NOTICES_QT_PREFIX={QT}",
            "-DCUBE_NOTICES_QT_VERSION=6.12.0",
            f"-DCUBE_NOTICES_OUT_DIR={out.as_posix()}",
            "-P",
            str(GENERATOR),
        ]
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
        produced = out / "third-party-notices.json"
        if not produced.exists():
            raise AssertionError(f"no output (exit {proc.returncode}):\n{proc.stdout}\n{proc.stderr}")
        return json.loads(produced.read_text(encoding="utf-8"))


@unittest.skipUnless(CMAKE, "cmake not found")
class StaticQtObjects(unittest.TestCase):
    def linked(self, doc: dict) -> dict[str, tuple[str, str]]:
        return {x["input"].rsplit("/", 1)[-1]: (x["kind"], x["component"]) for x in doc["linked"]}

    def test_each_shape_of_object_resolves_to_its_owner(self):
        doc = run_generator(
            [
                # a QML plugin's registration object, keyed by its path and name
                "qml/QtQuick/objects-Release/qtquick2plugin_init/qtquick2plugin_init.cpp.o",
                "qml/QtQuick3D/objects-Release/qquick3dplugin_init/qquick3dplugin_init.cpp.o",
                # ... by its name alone where the plugin's library is keyed that way
                "qml/QtQuick/Window/objects-Release/quickwindow_init/quickwindow_init.cpp.o",
                # a module's resource bundle: the module's library
                "qml/QtQuick/objects-Release/Quick_resources_1/.qt/rcc/qrc_qmake_QtQuick_init.cpp.o",
                "lib/objects-Release/Quick_resources_2/.qt/rcc/qrc_scenegraph_shaders_init.cpp.o",
                # the platform plugin's
                "lib/objects-Release/QWasmIntegrationPlugin_resources_1/.qt/rcc/qrc_wasmfonts_init.cpp.o",
            ]
        )
        self.assertEqual(doc["unresolved"], [], doc["unresolved"])
        owners = self.linked(doc)
        self.assertEqual(owners["qtquick2plugin_init.cpp.o"][1], "qtdeclarative/qtquick2plugin")
        self.assertEqual(owners["qquick3dplugin_init.cpp.o"][1], "qtquick3d/qquick3dplugin")
        self.assertEqual(owners["qrc_qmake_QtQuick_init.cpp.o"][1], "qtdeclarative/Quick")
        self.assertEqual(owners["qrc_scenegraph_shaders_init.cpp.o"][1], "qtdeclarative/Quick")
        self.assertEqual(owners["qrc_wasmfonts_init.cpp.o"][1], "qtbase/QWasmIntegrationPlugin")
        self.assertTrue(all(kind == "qt" for kind, _ in owners.values()))

    def test_an_object_that_follows_no_known_layout_stays_unresolved(self):
        doc = run_generator(
            [
                "lib/objects-Release/Mystery_init/mystery.cpp.o",
                "lib/objects-Release/NoSuchTarget_resources_1/.qt/rcc/qrc_x_init.cpp.o",
                "lib/something/else.o",
            ]
        )
        self.assertEqual(len(doc["unresolved"]), 3, doc["unresolved"])
        for name in ("mystery.cpp.o", "qrc_x_init.cpp.o", "else.o"):
            self.assertTrue(any(name in u for u in doc["unresolved"]), name)

    def test_an_empty_deployed_roots_list_is_an_answer_not_a_missing_one(self):
        doc = run_generator(["qml/QtQuick/objects-Release/qtquick2plugin_init/qtquick2plugin_init.cpp.o"])
        self.assertFalse(
            any("deployedRoots" in w for w in doc["warnings"]),
            [w for w in doc["warnings"] if "deployedRoots" in w],
        )
        # while a platform the manifest knows nothing about is still told so
        other = run_generator(
            ["qml/QtQuick/objects-Release/qtquick2plugin_init/qtquick2plugin_init.cpp.o"], platform="plan9"
        )
        self.assertTrue(any("deployedRoots" in w for w in other["warnings"]))

    def test_the_browser_platform_is_a_verified_one(self):
        doc = run_generator(["qml/QtQuick/objects-Release/qtquick2plugin_init/qtquick2plugin_init.cpp.o"])
        self.assertTrue(doc["qtPlatformVerified"])


if __name__ == "__main__":
    unittest.main()
