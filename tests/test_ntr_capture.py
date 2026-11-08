import ast
import errno
import importlib.util
import itertools
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import MagicMock, patch

SPEC = importlib.util.spec_from_file_location("ntr_capture", Path(__file__).resolve().parents[1] / "tools/ntr_capture.py")
ntr = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ntr)


def packet(frame, screen=1, index=0, data=b"jpeg", end=True):
    return bytes((frame, screen | (0x10 if end else 0), 2, index)) + data


class CaptureTests(unittest.TestCase):
    def test_listen_only_receives_existing_stream_without_tcp_or_sends(self):
        state = MagicMock()
        state.counters = {"top": 2, "bottom": 2}
        state.metadata = {"top": {"counter": 2}, "bottom": {"counter": 2}}
        state.received_monotonic = {"top": 0, "bottom": 0}
        udp = MagicMock()
        udp.recvfrom.return_value = (packet(1), ("192.0.2.1", 8001))
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(ntr, "Images", return_value=state), \
             patch.object(ntr.socket, "socket") as factory, \
             patch.object(ntr.socket, "create_connection") as control, \
             patch.object(ntr.select, "select", return_value=([udp], [], [])) as select, \
             patch.object(ntr.time, "monotonic", side_effect=itertools.count(0, 0.1)):
            factory.return_value.__enter__.return_value = udp
            report = ntr.capture("192.0.2.1", "192.0.2.2", Path(directory) / "new", 0.5, listen_only=True)
        control.assert_not_called()
        udp.sendto.assert_not_called()
        self.assertEqual(select.call_args.args[0], [udp])
        state.accept.assert_called_once()
        self.assertEqual(report["status"], "passed")
        self.assertTrue(report["listen_only"])

    def test_atomic_publication_retries_transient_windows_sharing_denial(self):
        for code in (5, 32):
            with self.subTest(winerror=code), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / "frames.json"
                target.write_bytes(b"old metadata")
                original_replace = Path.replace
                attempts = []
                def denied_twice(path, destination):
                    attempts.append(path)
                    self.assertEqual(target.read_bytes(), b"old metadata")
                    self.assertEqual(path.read_bytes(), b"new metadata")
                    if len(attempts) <= 2:
                        exc = PermissionError("Windows transient replacement denial")
                        exc.winerror = code
                        raise exc
                    return original_replace(path, destination)
                with patch.object(Path, "replace", new=denied_twice), patch.object(ntr.time, "sleep") as sleep:
                    ntr.atomic_bytes(target, b"new metadata")
                self.assertEqual(target.read_bytes(), b"new metadata")
                self.assertFalse(target.with_name("frames.json.tmp").exists())
                self.assertEqual(len(attempts), 3)
                self.assertEqual([call.args[0] for call in sleep.call_args_list], [.01, .02])

    def test_atomic_publication_persistent_denial_remains_fatal_and_bounded(self):
        exc = PermissionError("Windows sharing violation")
        exc.winerror = 32
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "frames.json"
            target.write_bytes(b"old metadata")
            with patch.object(Path, "replace", side_effect=exc) as replace, \
                 patch.object(ntr.time, "sleep") as sleep:
                with self.assertRaises(PermissionError):
                    ntr.atomic_bytes(target, b"new metadata")
            self.assertEqual(replace.call_count, 6)
            self.assertEqual(sleep.call_count, 5)
            self.assertAlmostEqual(sum(call.args[0] for call in sleep.call_args_list), .15)
            self.assertEqual(target.read_bytes(), b"old metadata")
            self.assertEqual(target.with_name("frames.json.tmp").read_bytes(), b"new metadata")

    def test_non_windows_and_non_transient_replace_errors_fail_immediately(self):
        errors = [PermissionError(errno.EACCES, "POSIX permission denied"),
                  OSError(errno.ENOSPC, "disk full")]
        other_windows = OSError("other Windows error")
        other_windows.winerror = 112
        errors.append(other_windows)
        for exc in errors:
            with self.subTest(error=exc), tempfile.TemporaryDirectory() as directory:
                target = Path(directory) / "frames.json"
                target.write_bytes(b"old")
                with patch.object(Path, "replace", side_effect=exc) as replace, \
                     patch.object(ntr.time, "sleep") as sleep:
                    with self.assertRaises(OSError) as failure:
                        ntr.atomic_bytes(target, b"new")
                self.assertIs(failure.exception, exc)
                replace.assert_called_once()
                sleep.assert_not_called()
                self.assertEqual(target.read_bytes(), b"old")

    def test_temporary_write_enospc_is_not_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "frames.json"
            target.write_bytes(b"old")
            with patch.object(Path, "write_bytes", side_effect=OSError(errno.ENOSPC, "disk full")), \
                 patch.object(Path, "replace") as replace, patch.object(ntr.time, "sleep") as sleep:
                with self.assertRaises(OSError) as failure:
                    ntr.atomic_bytes(target, b"new")
            self.assertEqual(failure.exception.errno, errno.ENOSPC)
            replace.assert_not_called()
            sleep.assert_not_called()
            self.assertEqual(target.read_bytes(), b"old")

    def test_persistent_windows_metadata_denial_does_not_advance_images_or_freshness(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow not installed")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            images = ntr.Images(output)
            jpeg = io.BytesIO()
            Image.new("RGB", (240, 400), "red").save(jpeg, format="JPEG")
            bottom = io.BytesIO()
            Image.new("RGB", (240, 320), "blue").save(bottom, format="JPEG")
            for frame in (1, 2):
                images.accept("top", frame, jpeg.getvalue())
                images.accept("bottom", frame, bottom.getvalue())
            old_metadata = dict(images.metadata)
            old_received = dict(images.received_monotonic)
            old_latest = dict(images.latest)
            old_counters = dict(images.counters)
            old_published = (output / "frames.json").read_bytes()
            self.assertTrue(ntr.freshness(images, max(old_received.values()) + .1)[0])
            original_replace = Path.replace
            attempts = []
            def deny_metadata(path, destination):
                if destination.name == "frames.json":
                    attempts.append(path)
                    exc = PermissionError("Windows access denied")
                    exc.winerror = 5
                    raise exc
                return original_replace(path, destination)
            with patch.object(Path, "replace", new=deny_metadata), patch.object(ntr.time, "sleep") as sleep:
                with self.assertRaises(ntr.PublicationError):
                    images.accept("top", 3, jpeg.getvalue())
            self.assertEqual(len(attempts), 6)
            self.assertEqual(sleep.call_count, 5)
            self.assertEqual(images.counters, old_counters)
            self.assertEqual(images.metadata, old_metadata)
            self.assertEqual(images.received_monotonic, old_received)
            self.assertEqual(images.latest, old_latest)
            self.assertEqual((output / "frames.json").read_bytes(), old_published)
            self.assertFalse(ntr.freshness(images, max(old_received.values()) + 4)[0])

    def test_header_exact_layout_and_view_command_only(self):
        values = struct.unpack("<21I", ntr.header(7, 901, (257, 70)))
        self.assertEqual(len(ntr.header(0, 0)), 84)
        self.assertEqual(values[:6], (0x12345678, 7, 0, 901, 257, 70))
        self.assertEqual(values[-1], 0)
        with self.assertRaises(ValueError):
            ntr.header(0, 10)

    def test_separate_screens_and_counters_frame_wrap(self):
        assembler = ntr.Assembler()
        self.assertEqual(assembler.feed(packet(255), 0), ("top", 255, b"jpeg"))
        self.assertEqual(assembler.feed(packet(0), 0.1), ("top", 0, b"jpeg"))
        self.assertEqual(assembler.feed(packet(0, screen=0), 0.1), ("bottom", 0, b"jpeg"))
        self.assertIsNone(assembler.feed(packet(255), 0.2))
        self.assertIsNone(assembler.feed(packet(0), 0.2))

    def test_out_of_order_fragments_require_all_indices(self):
        assembler = ntr.Assembler()
        self.assertIsNone(assembler.feed(packet(1, index=1), 0))
        data = b"a" * ntr.PAYLOAD
        self.assertEqual(assembler.feed(packet(1, data=data, end=False), 0.1), ("top", 1, data + b"jpeg"))

    def test_expired_and_stale_partial_frames_rejected(self):
        assembler = ntr.Assembler()
        self.assertIsNone(assembler.feed(packet(2, data=b"a" * ntr.PAYLOAD, end=False), 0))
        self.assertIsNone(assembler.feed(packet(2, index=1), 2))
        self.assertEqual(assembler.feed(packet(3), 2), ("top", 3, b"jpeg"))
        self.assertIsNone(assembler.feed(packet(2, index=1), 2.1))

    def test_packet_index_wrap_matches_upstream(self):
        assembler = ntr.Assembler()
        full = b"a" * ntr.PAYLOAD
        for index in range(256):
            self.assertIsNone(assembler.feed(packet(1, index=index, data=full, end=False), 0))
        result = assembler.feed(packet(1, index=0), 0.1)
        self.assertEqual(len(result[2]), ntr.PAYLOAD * 256 + 4)

    def test_malformed_and_conflicting_packets_rejected(self):
        assembler = ntr.Assembler()
        self.assertIsNone(assembler.feed(b"short", 0))
        self.assertIsNone(assembler.feed(packet(1, end=False), 0))
        self.assertIsNone(assembler.feed(bytes((1, 1, 3, 0)) + b"jpeg", 0))
        self.assertIsNone(assembler.feed(packet(1, data=b"a" * ntr.PAYLOAD, end=False), 0))
        self.assertIsNone(assembler.feed(packet(1, data=b"b" * ntr.PAYLOAD, end=False), 0))
        self.assertIsNone(assembler.feed(packet(1, index=1), 0))

    def test_freshness_requires_repeated_recent_frames_on_both_screens(self):
        class State:
            counters = {"top": 2, "bottom": 2}
            received_monotonic = {"top": 9, "bottom": 8}
        state = State()
        self.assertEqual(ntr.freshness(state, 10), (True, {"top": 1, "bottom": 2}))
        self.assertFalse(ntr.freshness(state, 12)[0])
        state.received_monotonic["bottom"] = 11
        state.counters["bottom"] = 1
        self.assertFalse(ntr.freshness(state, 12)[0])
        state.counters["bottom"] = 2
        del state.received_monotonic["top"]
        self.assertIsNone(ntr.freshness(state, 12)[1]["top"])
        self.assertFalse(ntr.freshness(state, 12)[0])

    def test_network_contract_has_no_datagram_send_or_input_port(self):
        tree = ast.parse(Path(ntr.__file__).read_text())
        attributes = {node.attr for node in ast.walk(tree) if isinstance(node, ast.Attribute)}
        self.assertFalse(attributes & {"sendto", "sendmsg", "send", "connect_ex"})
        ports = {node.value for node in ast.walk(tree) if isinstance(node, ast.Constant) and isinstance(node.value, int)}
        self.assertNotIn(4950, ports)
        self.assertIn(8000, ports)
        self.assertIn(8001, ports)
        # All transmitted data is generated by the command allowlist helper.
        sends = [node for node in ast.walk(tree) if isinstance(node, ast.Call)
                 and isinstance(node.func, ast.Attribute) and node.func.attr == "sendall"]
        self.assertTrue(sends)
        for send in sends:
            self.assertIsInstance(send.args[0], ast.Call)
            self.assertIsInstance(send.args[0].func, ast.Name)
            self.assertEqual(send.args[0].func.id, "header")

    def test_publication_failure_after_good_frames_is_fatal_not_a_decode_error(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow not installed")
        with tempfile.TemporaryDirectory() as directory:
            images = ntr.Images(Path(directory))
            jpeg = io.BytesIO()
            Image.new("RGB", (240, 400), "red").save(jpeg, format="JPEG")
            images.accept("top", 1, jpeg.getvalue())
            original_metadata = dict(images.metadata)
            with patch.object(ntr, "atomic_bytes", side_effect=OSError("disk full")):
                with self.assertRaises(ntr.PublicationError) as failure:
                    images.accept("top", 2, jpeg.getvalue())
            self.assertNotIsInstance(failure.exception, ntr.DecodeError)
            self.assertEqual(images.counters["top"], 1)
            self.assertEqual(images.metadata, original_metadata)

    def test_capture_output_failure_cannot_pass_even_with_fresh_prior_frames(self):
        state = MagicMock()
        state.counters = {"top": 5, "bottom": 5}
        state.metadata = {"top": {"counter": 5}, "bottom": {"counter": 5}}
        state.received_monotonic = {"top": 0, "bottom": 0}
        state.accept.side_effect = ntr.PublicationError("disk full")
        udp = MagicMock()
        udp.recvfrom.return_value = (packet(6), ("192.0.2.1", 8001))
        tcp = MagicMock()
        tcp.getsockname.return_value = ("192.0.2.2", 1234)
        with tempfile.TemporaryDirectory() as directory, \
             patch.object(ntr, "Images", return_value=state), \
             patch.object(ntr.socket, "socket") as udp_factory, \
             patch.object(ntr.socket, "create_connection") as tcp_factory, \
             patch.object(ntr.select, "select", return_value=([udp], [], [])), \
             patch.object(ntr.time, "monotonic", return_value=0):
            udp_factory.return_value.__enter__.return_value = udp
            tcp_factory.return_value.__enter__.return_value = tcp
            report = ntr.capture("192.0.2.1", "192.0.2.2", Path(directory) / "new", 15)
        self.assertEqual(report["status"], "output_error")
        self.assertEqual(report["decode_errors"], 0)
        self.assertEqual(report["error"], "disk full")
        udp.sendto.assert_not_called()
        udp.send.assert_not_called()

    def test_images_atomically_publish_both_screens_and_metadata(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow not installed")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            images = ntr.Images(output)
            for screen, height in (("top", 400), ("bottom", 320)):
                jpeg = io.BytesIO()
                Image.new("RGB", (240, height), "red").save(jpeg, format="JPEG")
                images.accept(screen, 5, jpeg.getvalue())
            self.assertEqual(images.counters, {"top": 1, "bottom": 1})
            for name, size in (("both.png", (400, 480)), ("top.jpg", (400, 240)), ("bottom.jpg", (320, 240))):
                with Image.open(output / name) as image:
                    self.assertEqual(image.size, size)
            self.assertFalse(list(output.glob("*.tmp")))
            self.assertIn("received_utc", images.metadata["bottom"])


if __name__ == "__main__":
    unittest.main()
