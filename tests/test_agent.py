import json
import tempfile
import unittest
from pathlib import Path

import agent
from benchmark import score_candidate


class AgentSafetyTests(unittest.TestCase):
    def test_rejects_path_traversal_and_extra_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(ValueError):
                agent._safe_output_path(root, "../secret")
            with self.assertRaises(ValueError):
                agent._safe_output_path(root, "surprise.txt")

    def test_collect_inputs_is_stable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "b.md").write_text("two", encoding="utf-8")
            (root / "a.txt").write_text("one", encoding="utf-8")
            first = agent.collect_inputs(root)
            second = agent.collect_inputs(root)
            self.assertEqual(first, second)
            self.assertLess(first[0].find("a.txt"), first[0].find("b.md"))

    def test_recorded_paths_are_portable(self):
        root = Path.cwd()
        self.assertEqual(agent.portable_path(root / "runs/plan.json", root),
                         "runs/plan.json")
        self.assertEqual(agent.portable_path(root.parent / "elsewhere/plan.json", root),
                         "plan.json")

    def test_rubric_must_total_100(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rubric = root / "rubric.json"
            rubric.write_text(json.dumps({
                "name": "bad", "version": 1, "threshold": 1,
                "checks": [{"id": "x", "category": "x", "points": 1,
                            "description": "x", "path": "x"}],
            }), encoding="utf-8")
            with self.assertRaises(ValueError):
                score_candidate(root, rubric)


if __name__ == "__main__":
    unittest.main()
