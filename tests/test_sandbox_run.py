"""A visually completed emulator run must not hide a native memory fault."""
import importlib.util
import hashlib
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sandbox_run", ROOT / "tools/sandbox/run.py")
RUN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUN)


class SandboxGuestFaultTests(unittest.TestCase):
    def test_completed_emulator_does_not_hide_application_failure(self):
        log = "field_end result=-1 error=Wes source motion 0 is not prepared\nboot_end result=-1 error=Wes source motion 0 is not prepared\n"
        self.assertEqual(RUN.application_failures(log)["count"], 2)
        self.assertEqual(RUN.application_failures("field_end result=2 error=\nboot_end result=1 error=\n")["count"], 0)
        self.assertEqual(RUN.application_failures("game_save status=failed reason=write failed\n")["count"], 1)

    def test_stopped_preview_is_failure_before_app_exit(self):
        log = "preview_runtime_failed error=Audio completion cue identity is unknown\n"
        self.assertEqual(RUN.application_failures(log)["count"], 1)
        self.assertEqual(RUN.application_failures("preview_begin build=test\npreview_probe_end result=0 error= save_writes=0\n")["count"], 0)

    def test_real_null_texture_read_is_a_guest_fault(self):
        log = "[19716.303625] HW.Memory <Error> core/memory.cpp:UnmappedAccess:583: unmapped Read32 @ 0x0000000C at PC 0x0011545C\nRUN END frames=8750 video=8750 audio=0 elapsed=19.9 interrupted=0 shutdown=0\n"
        self.assertEqual(RUN.guest_faults(log)["count"], 1)

    def test_explicit_guest_abort_is_a_fault(self):
        for message in ("Undefined instruction", "Prefetch abort", "Data abort", "Fatal exception"):
            self.assertEqual(RUN.guest_faults(f"Core.ARM <Critical> {message}")["count"], 1)

    def test_handled_optional_platform_failures_are_not_guest_faults(self):
        log = "\n".join((
            "Service.NWM <Error> Network isn't initalized",
            "Service.DSP <Error> DSP firmware missing",
            "Service <Error> unknown / unimplemented function 'ConfigureNew3DSCPU'",
            "Core <Error> Failed to find title id for ROM (Error 0)",
        ))
        self.assertEqual(RUN.guest_faults(log)["count"], 0)

    def test_diagnostics_remain_bounded_without_hiding_total(self):
        report = RUN.guest_faults("HW.Memory <Error> unmapped Read32\n" * 50)
        self.assertEqual(report["count"], 50)
        self.assertEqual(len(report["lines"]), 32)
        self.assertTrue(report["truncated"])


class SandboxLiveInputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.evidence = self.root / "live-input.csv"
        self.header = ",".join(RUN.LIVE_HEADER) + "\n"

    def evidence_rows(self, rows):
        self.evidence.write_text(self.header + rows, encoding="ascii")
        return RUN.live_input_evidence(self.evidence, 120)

    def test_empty_acceptance_is_valid_without_claiming_an_input(self):
        report = self.evidence_rows("")
        self.assertEqual(report["accepted_packets"], 0)
        self.assertEqual(report["last_sequence"], 0)
        self.assertIsNone(report["last_start_frame"])

    def test_accepted_frames_axes_and_exact_bytes_are_bound_to_hash(self):
        report = self.evidence_rows(
            "1,0,12,0x0100,-32768,32767,0,1\n"
            "18446744073709551615,119,36000,0xffff,0,0,-32768,32767\n")
        self.assertEqual(report["accepted_packets"], 2)
        self.assertEqual(report["last_start_frame"], 119)
        self.assertEqual(report["last_sequence"], (1 << 64)-1)
        self.assertEqual(report["sha256"], hashlib.sha256(self.evidence.read_bytes()).hexdigest())
        self.assertEqual(report["bytes"], self.evidence.stat().st_size)

    def test_invalid_or_replayed_acceptance_is_rejected(self):
        valid = "1,0,1,0,0,0,0,0\n"
        invalid = (
            "0,1,1,0,0,0,0,0\n",  # Sequence zero.
            "18446744073709551616,1,1,0,0,0,0,0\n",
            "1,-1,1,0,0,0,0,0\n", "1,120,1,0,0,0,0,0\n",
            "1,0,0,0,0,0,0,0\n", "1,0,36001,0,0,0,0,0\n",
            "1,0,1,-1,0,0,0,0\n", "1,0,1,65536,0,0,0,0\n",
            "1,0,1,0,-32769,0,0,0\n", "1,0,1,0,0,32768,0,0\n",
            "1,0,1,0,0,0,0\n", "1,0,1,0,0,0,0,0,extra\n",
            valid + "1,1,1,0,0,0,0,0\n",  # Repeated sequence.
            valid + "2,0,1,0,0,0,0,0\n",  # Two accepts in the same frame.
        )
        for rows in invalid:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                self.evidence_rows(rows)

    def test_evidence_rejects_wrong_header_non_ascii_and_oversize(self):
        for data in (b"wrong,header\n", self.header.encode() + b"\xff\n",
                     b"x" * (4 * 1024 * 1024 + 1)):
            self.evidence.write_bytes(data)
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                RUN.live_input_evidence(self.evidence, 120)

    def test_packet_snapshot_is_bounded_copy_not_an_acceptance(self):
        packet = self.root / "controller.packet"
        self.assertEqual(RUN.live_packet_snapshot(packet, self.root, "initial.txt"), {"exists": False})
        data = b"7,12,0x0100,0,0,0,0\n"
        packet.write_bytes(data)
        report = RUN.live_packet_snapshot(packet, self.root, "initial.txt")
        self.assertEqual((self.root / "initial.txt").read_bytes(), data)
        self.assertEqual(report["sha256"], hashlib.sha256(data).hexdigest())
        self.assertNotIn("accepted_packets", report)

    def test_snapshot_rejects_empty_oversize_links_and_fifo_without_blocking(self):
        packet = self.root / "controller.packet"
        for data in (b"", b"x" * 257):
            packet.write_bytes(data)
            self.assertIn("snapshot_error", RUN.live_packet_snapshot(packet, self.root, "copy.txt"))
        link = self.root / "link.packet"
        link.symlink_to(packet)
        self.assertIn("snapshot_error", RUN.live_packet_snapshot(link, self.root, "copy.txt"))
        fifo = self.root / "fifo.packet"
        os.mkfifo(fifo)
        self.assertIn("snapshot_error", RUN.live_packet_snapshot(fifo, self.root, "copy.txt"))
        self.assertFalse((self.root / "copy.txt").exists())


if __name__ == "__main__":
    unittest.main()
