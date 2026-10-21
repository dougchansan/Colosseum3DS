import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import MagicMock, patch

SPEC = importlib.util.spec_from_file_location("luma_input", Path(__file__).resolve().parents[1] / "tools/luma_input.py")
luma = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(luma)


class LumaTests(unittest.TestCase):
    def test_neutral_and_buttons_match_receiver_layout(self):
        self.assertEqual(struct.unpack("<5I", luma.NEUTRAL), (0xfff, 0x02000000, 0x007ff7ff, 0x80800081, 0))
        for button, bit in luma.HID_BITS.items():
            values = struct.unpack("<5I", luma.packet([button]))
            self.assertEqual(values[0], 0xfff & ~(1 << bit))
            self.assertEqual(values[1:], (0x02000000, 0x007ff7ff, 0x80800081, 0))
        self.assertEqual(struct.unpack("<5I", luma.packet(["ZL", "ZR"]))[3], 0x80800681)
        for unsupported in ("HOME", "POWER", "TOUCH"):
            with self.assertRaises(ValueError):
                luma.packet([unsupported])

    def test_analog_packing_and_bounds_match_viewer_transform(self):
        values = struct.unpack("<5I", luma.packet(circle_pad=(1, 0), c_stick=(1, 0)))
        self.assertEqual(values[2], (0x800 << 12) | (0x800 + 0x5d0))
        self.assertEqual(values[3], (39 << 24) | (217 << 16) | 0x81)
        self.assertEqual(values[4], 0)
        self.assertEqual(luma.packet(circle_pad=(0, 0), c_stick=(0, 0)), luma.NEUTRAL)
        for axes in ((1.1, 0), (0, -2), (float("nan"), 0)):
            with self.assertRaises(ValueError):
                luma.packet(circle_pad=axes)
            with self.assertRaises(ValueError):
                luma.packet(c_stick=axes)

    def test_only_single_button_or_exit_combo_can_be_transmitted(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(luma.socket, "socket") as factory:
            for buttons in (["L", "DOWN", "SELECT"], ["A", "B"], ["ZL", "ZR"]):
                with self.assertRaises(ValueError):
                    luma.send_action("192.0.2.1", buttons, 0.1, Path(directory) / "action.json")
            factory.assert_not_called()
        self.assertEqual(struct.unpack("<5I", luma.packet(["START", "SELECT"]))[0], 0xff3)

    def run_action(self, udp, evidence, duration=0.15):
        with patch.object(luma.socket, "socket") as factory, \
             patch.object(luma.time, "sleep"), patch.object(luma.time, "monotonic", return_value=0):
            factory.return_value.__enter__.return_value = udp
            return luma.send_action("192.0.2.1", ["A"], duration, evidence)

    def test_hold_and_three_neutral_releases_are_bounded_and_logged(self):
        udp = MagicMock()
        udp.sendto.return_value = 20
        udp.getsockname.return_value = ("192.0.2.2", 1234)
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory) / "action.json"
            report = self.run_action(udp, evidence, 0.5)
            self.assertEqual(json.loads(evidence.read_text())["status"], "sent")
        self.assertEqual(report["hold_sent"], 30)
        self.assertEqual(report["release_sent"], 3)
        self.assertFalse(report["receipt_verified"])
        calls = udp.sendto.call_args_list
        self.assertTrue(all(call.args[0] == luma.NEUTRAL for call in calls[-3:]))
        self.assertTrue(all(call.args[1] == ("192.0.2.1", 4950) for call in calls))

    def test_hold_failure_still_attempts_all_releases(self):
        udp = MagicMock()
        udp.sendto.side_effect = [OSError("hold failed"), 20, OSError("release failed"), 20]
        with tempfile.TemporaryDirectory() as directory:
            report = self.run_action(udp, Path(directory) / "action.json")
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["release_attempted"], 3)
        self.assertEqual(report["release_sent"], 2)
        self.assertEqual(len(report["release_errors"]), 1)

    def test_invalid_bounds_and_existing_evidence_never_send(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(luma.socket, "socket") as factory:
            path = Path(directory) / "action.json"
            for duration in (0, -1, 0.51, float("nan"), float("inf")):
                with self.assertRaises(ValueError):
                    luma.send_action("192.0.2.1", ["A"], duration, path)
            path.write_text("existing")
            with self.assertRaises(FileExistsError):
                luma.send_action("192.0.2.1", ["A"], 0.1, path)
            factory.assert_not_called()


if __name__ == "__main__":
    unittest.main()
