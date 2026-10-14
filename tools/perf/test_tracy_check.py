#!/usr/bin/env python3
"""Self-test for tracy_check.py: the parsing and the comparison, on a canned
tracy-csvexport file, so the tool is held to its own rules without Tracy, a
profiling build or a capture. Registered with CTest as perf_tracy_check_selftest.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tracy_check  # noqa: E402

CSV = """name,src_file,src_line,total_ns,total_perc,counts,mean_ns,min_ns,max_ns,std_ns
perf.calibration,tests/perf/measure.cpp,184,1000000,1.0,10,100000,80000,120000,5000
solver.cross,src/lib/src/solver/cross.cpp,18,500000,0.5,10,50000,40000,90000,3
solver.bfs_search,src/lib/src/solver/bfs_search.hpp,64,200000,0.2,4,50000,24000,90000,3
solver.bfs_search,src/lib/src/solver/bfs_search.hpp,64,100000,0.1,6,16666.7,8000,30000,3
app.load_qml,src/app/qt/app_runtime.cpp,26,9,0,1,9,9,9,0
"""


class ParsingTest(unittest.TestCase):
    def test_instantiations_of_one_zone_are_one_zone(self):
        zones = tracy_check.parse_zones_csv(CSV)
        self.assertEqual(zones["solver.bfs_search"]["count"], 10)
        self.assertEqual(zones["solver.bfs_search"]["total_ns"], 300000)

    def test_ratio_is_the_fastest_call_over_the_fastest_calibration(self):
        current = tracy_check.ratios_to_calibration(tracy_check.parse_zones_csv(CSV))
        self.assertAlmostEqual(current["solver.cross"]["ratio"], 0.5)
        # The fastest of the instantiations' fastest.
        self.assertAlmostEqual(current["solver.bfs_search"]["ratio"], 0.1)
        self.assertAlmostEqual(current["perf.calibration"]["ratio"], 1.0)

    def test_zones_outside_the_library_layers_are_left_out(self):
        current = tracy_check.ratios_to_calibration(tracy_check.parse_zones_csv(CSV))
        self.assertNotIn("app.load_qml", current)

    def test_no_calibration_zone_is_an_error_not_a_pass(self):
        without = "\n".join(line for line in CSV.splitlines() if "perf.calibration" not in line)
        with self.assertRaises(ValueError):
            tracy_check.ratios_to_calibration(tracy_check.parse_zones_csv(without))


class ComparisonTest(unittest.TestCase):
    def setUp(self):
        self.current = tracy_check.ratios_to_calibration(tracy_check.parse_zones_csv(CSV))
        self.baseline = {name: dict(entry) for name, entry in self.current.items()}

    def test_the_baseline_itself_passes(self):
        failures, notes, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(failures, [])
        self.assertEqual(notes, [])

    def test_a_zone_slower_than_the_tolerance_fails(self):
        self.baseline["solver.cross"]["ratio"] = 0.5 / 3.5
        failures, _, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(len(failures), 1)
        self.assertIn("solver.cross", failures[0])

    def test_a_zone_inside_the_tolerance_passes(self):
        self.baseline["solver.cross"]["ratio"] = 0.5 / 2.5
        failures, _, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(failures, [])

    def test_a_zone_too_short_to_time_is_held_to_its_count_only(self):
        self.baseline["solver.cross"]["ratio"] = 1e-6
        failures, _, table = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(failures, [])
        self.assertTrue(any("count only" in line for line in table))
        self.baseline["solver.cross"]["count"] = 11
        failures, _, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(len(failures), 1)

    def test_a_changed_count_fails_whatever_the_time(self):
        self.baseline["solver.cross"]["count"] = 11
        failures, _, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(len(failures), 1)
        self.assertIn("entered 10 times", failures[0])

    def test_a_faster_zone_is_a_note(self):
        self.baseline["solver.cross"]["ratio"] = 0.5 * 4
        failures, notes, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(failures, [])
        self.assertEqual(len(notes), 1)

    def test_a_zone_missing_from_the_capture_fails(self):
        self.baseline["solver.gone"] = {"count": 1, "ratio": 1.0}
        failures, _, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(len(failures), 1)
        self.assertIn("solver.gone", failures[0])

    def test_a_new_zone_is_a_note(self):
        del self.baseline["solver.cross"]
        failures, notes, _ = tracy_check.compare(self.baseline, self.current, 3.0)
        self.assertEqual(failures, [])
        self.assertEqual(len(notes), 1)

    def test_the_baseline_document_round_trips(self):
        document = tracy_check.baseline_document(self.current, 3.0)
        self.assertEqual(document["schema"], tracy_check.SCHEMA)
        failures, _, _ = tracy_check.compare(document["zones"], self.current, 3.0)
        self.assertEqual(failures, [])


if __name__ == "__main__":
    unittest.main()
