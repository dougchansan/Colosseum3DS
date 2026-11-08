import hashlib
import importlib.util
import io
import json
from pathlib import Path
import socket
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("dolphin_dsu", ROOT / "tools/dolphin_dsu.py")
dsu = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dsu)


class Protocol(unittest.TestCase):
    def request(self, kind, payload=b"", uid=42):
        return dsu.packet(b"DSUC", uid, kind, payload)

    def test_exact_layout_crc_and_controller_bytes(self):
        data = dsu.pad_data(0x12345678, ["A", "DOWN", "START"], (0, 255), 123)
        self.assertEqual(len(data), 100)
        self.assertEqual(struct.unpack_from("<4sHH", data), (b"DSUS", 1001, 84))
        check = bytearray(data)
        check[8:12] = bytes(4)
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], zlib.crc32(check))
        self.assertEqual(data[20:24], bytes([0, 2, 3, 1]))
        self.assertEqual(data[24:30], dsu.MAC)
        self.assertEqual(data[31], 1)
        self.assertEqual(struct.unpack_from("<I", data, 32)[0], 0x12345678)
        self.assertEqual(data[36], 8)
        self.assertEqual(data[40:44], bytes([0, 255, 128, 128]))
        self.assertEqual(data[45], 255)
        self.assertEqual(data[49], 255)
        self.assertEqual(struct.unpack_from("<Q", data, 68)[0], 123)
        self.assertFalse(any(data[76:]))

    def test_port_handshake_and_pad0_identity(self):
        c = dsu.Controller()
        endpoint = ("127.0.0.1", 12345)
        replies = c.handle(self.request(dsu.PORTS_MSG, struct.pack("<I4B", 4, 0, 1, 2, 3)), endpoint)
        self.assertEqual([len(x) for x in replies], [32] * 4)
        self.assertEqual([x[21] for x in replies], [2, 0, 0, 0])
        self.assertEqual([x[20] for x in replies], [0, 1, 2, 3])
        self.assertEqual(len(c.handle(self.request(dsu.VERSION_MSG), endpoint)[0]), 24)
        c.handle(self.request(dsu.PAD_MSG, struct.pack("<BB6s", 1, 0, bytes(6))), endpoint)
        self.assertIn(endpoint, c.registrations)
        # Separate hotplug/device UDP sockets share one Dolphin client UID.
        other_endpoint = ("127.0.0.1", 12346)
        c.handle(self.request(dsu.PAD_MSG, struct.pack("<BB6s", 2, 0, dsu.MAC)), other_endpoint)
        self.assertEqual(len(c.registrations), 2)
        c.unregister(endpoint)
        self.assertNotIn(endpoint, c.registrations)

    def test_reject_malformed_other_client_and_nonloopback(self):
        c = dsu.Controller()
        endpoint = ("127.0.0.1", 12345)
        c.handle(self.request(dsu.VERSION_MSG), endpoint)
        invalid = [b"", self.request(dsu.VERSION_MSG)[:-1],
                   self.request(dsu.PAD_MSG, struct.pack("<BB6s", 1, 1, bytes(6))),
                   self.request(dsu.PAD_MSG, struct.pack("<BB6s", 2, 0, bytes(6))),
                   self.request(dsu.PORTS_MSG, struct.pack("<I4B", 2, 0, 0, 0, 0)),
                   self.request(dsu.VERSION_MSG, uid=43)]
        corrupt = bytearray(self.request(dsu.VERSION_MSG))
        corrupt[12] ^= 1
        invalid.append(corrupt)
        for data in invalid:
            with self.assertRaises(ValueError):
                c.handle(data, endpoint)
        with self.assertRaises(ValueError):
            c.handle(self.request(dsu.VERSION_MSG), ("192.168.1.1", 12345))
        self.assertEqual(c.uid, 42)
        self.assertFalse(c.registrations)
        for host, port in [("0.0.0.0", 26761), ("127.0.0.1", 26760)]:
            with self.assertRaises(ValueError):
                dsu.Server(host, port)

    def test_expired_input_and_registration_leases(self):
        now = [10.0]
        c = dsu.Controller(lambda: now[0])
        endpoint = ("127.0.0.1", 12345)
        c.handle(self.request(dsu.PAD_MSG, struct.pack("<BB6s", 1, 0, bytes(6))), endpoint)
        c.set(["A"], lease_ms=100)
        self.assertEqual(c.updates()[0][1][49], 255)
        now[0] += .101
        self.assertEqual(c.updates()[0][1][49], 0)
        self.assertEqual(c.stick, (128, 128))
        c.set(["RIGHT"])
        c.neutral()
        self.assertEqual(c.updates()[0][1][46], 0)
        now[0] += 3
        self.assertEqual(c.updates(), [])

    def test_cleanup_sends_final_neutral(self):
        class FakeSocket:
            def __init__(self): self.sent = []; self.closed = False
            def sendto(self, data, endpoint): self.sent.append((data, endpoint))
            def close(self): self.closed = True
        server = object.__new__(dsu.Server)
        import threading
        server.stopped = threading.Event()
        server.thread = threading.Thread(target=lambda: None)
        server.socket = FakeSocket()
        server.controller = dsu.Controller()
        server.controller.registrations[("127.0.0.1", 12345)] = server.controller.clock() + 1
        server.controller.set(["A"])
        server.close()
        self.assertEqual(server.socket.sent[0][0][49], 0)
        self.assertFalse(server.controller.registrations)
        self.assertTrue(server.socket.closed)


class EvidenceGate(unittest.TestCase):
    def test_deadline_expiring_during_gate_sends_nothing(self):
        class FixtureServer:
            controller = dsu.Controller()
        server = FixtureServer()
        server.controller.registrations[("127.0.0.1", 12345)] = 100
        evidence = io.StringIO()
        with patch.object(dsu, "validate_session", return_value=Path("unused")), \
                patch.object(dsu, "fresh_frame", return_value=(42, Path("fixture.png"))), \
                patch.object(dsu.time, "monotonic", side_effect=[0, 0, 20]):
            with self.assertRaises(ValueError):
                dsu.execute(server, {}, [{"buttons": ["A"]}], evidence, max_seconds=10)
        self.assertNotIn("controller_state_sent", evidence.getvalue())
        self.assertEqual(server.controller.buttons, ())

    def test_action_failure_still_neutralizes(self):
        class FixtureServer:
            controller = dsu.Controller()
        server = FixtureServer()
        server.controller.set(["A"])
        evidence = io.StringIO()
        with patch.object(dsu, "validate_session", return_value=Path("unused")), \
                patch.object(dsu, "fresh_frame", side_effect=ValueError("stale fixture")):
            with self.assertRaises(ValueError):
                dsu.execute(server, {}, [{"buttons": ["A"]}], evidence)
        self.assertEqual(server.controller.buttons, ())
        self.assertEqual(json.loads(evidence.getvalue().splitlines()[-1])["event"], "finally_neutral")

    def test_stale_incomplete_and_old_frames_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            p = directory / "framedump_42.png"
            def chunk(kind, body):
                return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))
            png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
                   chunk(b"IDAT", zlib.compress(bytes(4))) + chunk(b"IEND", b""))
            p.write_bytes(png)
            self.assertEqual(dsu.fresh_frame(directory)[0], 42)
            with self.assertRaises(ValueError): dsu.fresh_frame(directory, after=42)
            with self.assertRaises(ValueError): dsu.fresh_frame(directory, now=lambda: p.stat().st_mtime + 3)
            p.write_bytes(b"\x89PNG\r\n\x1a\n")
            with self.assertRaises(ValueError): dsu.fresh_frame(directory)
            corrupt = bytearray(png)
            corrupt[20] ^= 1
            self.assertFalse(dsu.complete_png(corrupt))

    def test_session_identity_config_and_ready(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            user = root / "user"
            config = user / "Config"
            config.mkdir(parents=True)
            (config / "Dolphin.ini").write_text("[Core]\nEnableCheats=False\n[Input]\nBackgroundInput=True\n")
            (config / "DSUClient.ini").write_text("[Server]\nEnabled=True\nEntries=TailLoopback:127.0.0.1:26761;\n")
            (config / "GCPadNew.ini").write_text("[GCPad1]\nDevice=DSUClient/0/TailLoopback\n")
            exe = root / "Dolphin.exe"
            exe.write_bytes(b"test identity fixture, not an emulator")
            session = dict(owned_pid=42, ready=True, user_root=str(user), output_root=str(root),
                           exe=str(exe), exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                           frames_dir=str(user / "Dump/Frames"))
            probe = lambda _: dict(ExecutablePath=str(exe), CommandLine=f'--user "{user}"')
            self.assertEqual(dsu.validate_session(session, probe), user / "Dump/Frames")
            for change in [dict(ready=False), dict(owned_pid=-1), dict(exe_sha256="bad"),
                           dict(user_root=str(root)), dict(frames_dir=str(root / "other"))]:
                with self.assertRaises(ValueError): dsu.validate_session(dict(session, **change), probe)
            with self.assertRaises(ValueError): dsu.validate_session(session, lambda _: None)
            for command in [f'--user "{user}extra"', f'--user "{user}" --movie test.dtm']:
                with self.assertRaises(ValueError):
                    dsu.validate_session(session, lambda _: dict(ExecutablePath=str(exe), CommandLine=command))
            movie = root / "prefix.dtm"
            movie.write_bytes(b"private verified EOF fixture")
            command = f'--user "{user}" --movie "{movie}"'
            handoff = dict(session, movie_handoff_ready=True,
                           completed_movie_sha256=hashlib.sha256(movie.read_bytes()).hexdigest())
            with self.assertRaises(ValueError):
                dsu.validate_session(handoff, lambda _: dict(ExecutablePath=str(exe), CommandLine=command))
            with (config / "Dolphin.ini").open("a") as output:
                output.write("[Movie]\nPauseMovie=False\n")
            self.assertEqual(dsu.validate_session(handoff, lambda _: dict(ExecutablePath=str(exe), CommandLine=command)), user / "Dump/Frames")


if __name__ == "__main__":
    unittest.main()
