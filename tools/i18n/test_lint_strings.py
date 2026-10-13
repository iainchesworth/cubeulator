#!/usr/bin/env python3
"""The lint's own tests: it must fail on what it exists to catch, and pass on
what a converted file looks like. Run as a CTest case (i18n_lint_selftest).
"""

import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lint_strings as lint  # noqa: E402


def qml(source: str):
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "Example.qml"
        path.write_text(textwrap.dedent(source), encoding="utf-8")
        return lint.lint_qml(path, rel="Example.qml")


def cpp(source: str):
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / "example.cpp"
        path.write_text(textwrap.dedent(source), encoding="utf-8")
        return lint.lint_cpp(path, rel="example.cpp")


class QmlLint(unittest.TestCase):
    def test_a_literal_text_is_caught(self):
        hits = qml('Text { text: "Start scan" }')
        self.assertEqual([h.literal for h in hits], ["Start scan"])
        self.assertEqual(hits[0].kind, "literal")

    def test_an_id_lookup_passes(self):
        self.assertEqual(qml('Text { text: qsTrId("home.scan") }'), [])

    def test_an_id_with_an_argument_passes(self):
        self.assertEqual(qml('Text { text: qsTrId("home.resume.body").arg(a).arg(b) }'), [])

    def test_a_literal_in_a_ternary_is_caught_beside_an_id(self):
        hits = qml('Text { text: ok ? qsTrId("a.b") : "Not ok" }')
        self.assertEqual([h.literal for h in hits], ["Not ok"])

    def test_a_multiline_ternary_is_followed(self):
        hits = qml(
            """
            Text {
                text: busy
                    ? qsTrId("a.busy")
                    : ready
                        ? "Ready"
                        : qsTrId("a.none")
            }
            """
        )
        self.assertEqual([h.literal for h in hits], ["Ready"])

    def test_a_sentence_made_of_fragments_is_called_that(self):
        hits = qml('Text { text: "Running " + backend + "." }')
        self.assertTrue(hits)
        self.assertTrue(all(h.kind == "fragment" for h in hits))

    def test_qsTr_is_not_an_id_lookup(self):
        hits = qml('Text { text: qsTr("Scan") }')
        self.assertEqual([(h.literal, h.kind) for h in hits], [("Scan", "qsTr")])

    def test_the_accessible_name_is_text(self):
        hits = qml('Item { Accessible.name: "Scan the cube" }')
        self.assertEqual([h.literal for h in hits], ["Scan the cube"])

    def test_a_label_in_an_object_literal_is_text(self):
        hits = qml('QtObject { readonly property var modes: [{ key: "light", label: "Light" }] }')
        self.assertEqual([h.literal for h in hits], ["Light"])

    def test_a_phrase_returned_through_a_custom_property_is_caught(self):
        hits = qml(
            """
            Item {
                readonly property string guidance: {
                    if (a) return "Hold it still";
                    return "";
                }
            }
            """
        )
        self.assertEqual([h.literal for h in hits], ["Hold it still"])

    def test_names_and_codes_are_not_text(self):
        src = """
            Item {
                objectName: "launch.startScan"
                state: "wide layout"
                font.family: "IBM Plex Mono"
                color: "#EC3013"
                source: "qrc:/some thing/here.png"
                variant: "primary"
                Text { text: "·" }
                Text { text: "%1" }
            }
        """
        self.assertEqual(qml(src), [])

    def test_a_comment_is_not_text(self):
        self.assertEqual(qml('// text: "Hello there"\n/* text: "Hello there" */\nItem {}'), [])

    def test_a_canvas_font_is_not_text(self):
        self.assertEqual(qml('Canvas { onPaint: { ctx.font = "bold 14px sans-serif"; } }'), [])


class CppLint(unittest.TestCase):
    def test_a_sentence_in_a_controller_is_caught(self):
        hits = cpp('reason_ = QStringLiteral("No AI inference hardware detected.");')
        self.assertEqual([h.literal for h in hits], ["No AI inference hardware detected."])

    def test_a_log_line_is_not(self):
        self.assertEqual(cpp('qWarning("camera failed to open: %s", x);'), [])
        self.assertEqual(cpp('qInfo() << QStringLiteral("started the capture loop");'), [])

    def test_a_key_or_path_is_not(self):
        self.assertEqual(cpp('settings.value(QStringLiteral("language/code"));'), [])
        self.assertEqual(cpp('load(QStringLiteral(":/i18n/cubeulator_en.qm"));'), [])


class Allowlist(unittest.TestCase):
    def test_the_shipped_allowlist_is_well_formed_and_names_an_owner(self):
        entries = lint.load_allowlist()
        self.assertTrue(entries)
        for f, e in entries.items():
            self.assertTrue(e.get("owner"), f)
            self.assertTrue(e.get("literals"), f)

    def test_normalisation_matches_a_literal_to_its_id(self):
        index = lint.english_index()
        self.assertEqual(index.get(lint._norm("Back")), "common.back")
        self.assertEqual(index.get(lint._norm("Looking for cameras…")), "settings.camera.finding")


if __name__ == "__main__":
    unittest.main(verbosity=2)
