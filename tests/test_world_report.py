import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import world_report  # noqa: E402

REPORT = """\
world prefix=f00f floor=00f event=arrival id=0 script=00000000 status=failed frames=12 next=000 reason=Unported native 97 in GSVM 0fe00000 at 01000002
world prefix=f00f floor=00f event=door id=3 script=01000005 status=ok frames=90 next=010 reason=
world prefix=f010 floor=010 event=talk id=2 script=01000009 status=failed frames=40 next=000 reason=Unported native 97 in GSVM 10e00000 at 01000009
world prefix=f010 floor=010 event=touch id=1 script=0100000a status=skipped frames=0 next=000 reason=not started
"""


class WorldReportTest(unittest.TestCase):
    def test_groups_failures_by_cause_and_lists_connectors(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "report.txt"
            path.write_text(REPORT)
            rows = world_report.parse(path)
        self.assertEqual(len(rows), 4)
        self.assertEqual(rows[1]["next"], "010")
        text = world_report.summary(rows)
        self.assertIn("floors 2, events 4, ok 1, failed 2, skipped 1", text)
        self.assertIn("     2   2  Unported native 97 in GSVM X at X", text)
        self.assertIn("arrival not ok (1):", text)
        self.assertIn("connectors requested by scripts (1): 00f>010", text)


if __name__ == "__main__":
    unittest.main()
