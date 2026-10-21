#!/usr/bin/env python3
"""Bounded, view-only NTR original-JPEG capture (TCP8000 / receive UDP8001).

Protocol reference: xzn/ntrviewer-hr commit
556c6d06b21cef38c29efd0cdad59a5771956268, ntr_hb.c and ntr_rp.c.
This independently implemented subset excludes input, KCP, audio and lossless.
"""
import argparse
import datetime
import io
import json
from pathlib import Path
import select
import socket
import struct
import time

PAYLOAD = 1444
MAX_PACKETS = 480
SOURCE = "https://github.com/xzn/ntrviewer-hr/tree/556c6d06b21cef38c29efd0cdad59a5771956268"


def header(sequence, command, args=()):
    if command not in (0, 901) or len(args) > 16:
        raise ValueError("Only heartbeat and view-stream commands supported")
    return struct.pack("<21I", 0x12345678, sequence, 0, command,
                       *args, *([0] * (16 - len(args))), 0)


class Assembler:
    """One bounded in-flight original JPEG per screen; reject stale frames."""
    def __init__(self):
        self.screens = {}
        self.rejected = 0

    def feed(self, packet, now):
        if not 4 < len(packet) <= PAYLOAD + 4:
            self.rejected += 1
            return None
        frame, flags, kind, index = packet[:4]
        screen = flags & 1
        final = bool(flags & 0x10)
        data = packet[4:]
        if kind != 2 or flags & ~0x11 or (not final and len(data) != PAYLOAD):
            self.rejected += 1
            return None
        state = self.screens.get(screen)
        if state is not None:
            distance = (frame - state["frame"]) & 255
            if distance >= 128 or (distance == 0 and (state["done"] or now - state["started"] > 1)):
                self.rejected += 1
                return None
        if state is None or state["frame"] != frame:
            state = {"frame": frame, "parts": {}, "end": None, "highest": 0,
                     "started": now, "done": False}
            self.screens[screen] = state
        if index + 128 < state["highest"]:
            index += 256
        if index >= MAX_PACKETS:
            self.rejected += 1
            return None
        previous = state["parts"].get(index)
        if previous is not None and previous != data:
            state["done"] = True
            self.rejected += 1
            return None
        if state["end"] is not None and index > state["end"]:
            self.rejected += 1
            return None
        state["parts"][index] = data
        state["highest"] = max(state["highest"], index)
        if final:
            if state["end"] is not None and state["end"] != index:
                state["done"] = True
                return None
            state["end"] = index
        end = state["end"]
        if end is None or any(i not in state["parts"] for i in range(end + 1)):
            return None
        state["done"] = True
        return ("top" if screen else "bottom", frame,
                b"".join(state["parts"][i] for i in range(end + 1)))


def atomic_bytes(path, data):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_bytes(data)
    for attempt in range(5):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt == 4:
                raise
            time.sleep(0.01)


class DecodeError(ValueError):
    """Malformed or unsupported incoming JPEG."""


class PublicationError(OSError):
    """Local evidence files could not be published."""


class Images:
    def __init__(self, output):
        from PIL import Image
        self.Image = Image
        self.output = output
        self.latest = {}
        self.counters = {"top": 0, "bottom": 0}
        self.metadata = {}
        self.received_monotonic = {}

    def accept(self, screen, frame, jpeg):
        try:
            with self.Image.open(io.BytesIO(jpeg)) as raw:
                expected = (240, 400 if screen == "top" else 320)
                if raw.format != "JPEG" or raw.size != expected:
                    raise ValueError("Unsupported JPEG dimensions or format")
                image = raw.convert("RGB").transpose(self.Image.Transpose.ROTATE_90)
        except (OSError, ValueError) as exc:
            raise DecodeError(str(exc)) from exc
        latest = {**self.latest, screen: image}
        counters = {**self.counters, screen: self.counters[screen] + 1}
        metadata = {**self.metadata, screen: {
            "frame_id": frame, "counter": counters[screen],
            "received_utc": datetime.datetime.now(datetime.timezone.utc).isoformat()}}
        received = time.monotonic()
        try:
            encoded = io.BytesIO()
            image.save(encoded, format="JPEG", quality=95)
            atomic_bytes(self.output / (screen + ".jpg"), encoded.getvalue())
            if len(latest) == 2:
                both = self.Image.new("RGB", (400, 480))
                both.paste(latest["top"], (0, 0))
                both.paste(latest["bottom"], (40, 240))
                encoded = io.BytesIO()
                both.save(encoded, format="PNG")
                atomic_bytes(self.output / "both.png", encoded.getvalue())
            atomic_bytes(self.output / "frames.json", (json.dumps(metadata, indent=2) + "\n").encode())
        except OSError as exc:
            raise PublicationError(str(exc)) from exc
        # Counters describe successfully published evidence, never merely decoded frames.
        self.latest = latest
        self.counters = counters
        self.metadata = metadata
        self.received_monotonic[screen] = received


def freshness(images, now):
    ages = {screen: max(0, now - images.received_monotonic[screen])
            if screen in images.received_monotonic else None
            for screen in ("top", "bottom")}
    passed = all(images.counters[screen] >= 2 and ages[screen] is not None and ages[screen] <= 3
                 for screen in ages)
    return passed, ages


def capture(host, bind, output, duration=15):
    if not 0 < duration <= 15:
        raise ValueError("duration must be in (0, 15] seconds")
    socket.inet_aton(host)
    socket.inet_aton(bind)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    images = Images(output)
    assembler = Assembler()
    report = {"source": SOURCE, "host": host, "bind": bind, "duration_seconds": duration,
              "view_only": True, "input_packets_sent": 0, "udp_packets": 0,
              "decode_errors": 0, "status": "failed"}
    started = time.monotonic()
    deadline = started + duration
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind((bind, 8001))
            with socket.create_connection((host, 8000), timeout=2, source_address=(bind, 0)) as tcp:
                tcp.settimeout(0.5)
                report["local_source_address"] = tcp.getsockname()[0]
                sequence = 0
                tcp.sendall(header(sequence, 0))
                sequence += 1
                tcp.sendall(header(sequence, 901, (257, 70, 2 * 1024 * 1024, 1404036572, 8001, 0, 0)))
                sequence += 1
                next_heartbeat = time.monotonic() + 0.25
                buffered = b""
                while time.monotonic() < deadline:
                    now = time.monotonic()
                    if now >= next_heartbeat:
                        tcp.sendall(header(sequence, 0))
                        sequence += 1
                        next_heartbeat = now + 0.25
                    ready, _, _ = select.select([tcp, udp], [], [], min(0.1, max(0, deadline - now)))
                    if tcp in ready:
                        chunk = tcp.recv(65536)
                        if not chunk:
                            raise OSError("NTR control connection closed")
                        buffered += chunk
                        if len(buffered) > 1024 * 1024:
                            raise ValueError("Control response exceeds bound")
                        while len(buffered) >= 84:
                            values = struct.unpack("<21I", buffered[:84])
                            if values[0] != 0x12345678 or values[-1] > 512 * 1024:
                                raise ValueError("Invalid NTR control response")
                            length = 84 + values[-1]
                            if len(buffered) < length:
                                break
                            buffered = buffered[length:]
                    if udp in ready:
                        packet, sender = udp.recvfrom(65535)
                        if sender[0] != host:
                            continue
                        report["udp_packets"] += 1
                        result = assembler.feed(packet, time.monotonic())
                        if result:
                            try:
                                images.accept(*result)
                            except DecodeError:
                                report["decode_errors"] += 1
                fresh, _ = freshness(images, time.monotonic())
                report["status"] = "passed" if fresh else "insufficient_fresh_frames"
    except PublicationError as exc:
        report["status"] = "output_error"
        report["error"] = str(exc)
    except (OSError, ValueError) as exc:
        report["error"] = str(exc)
    finally:
        _, ages = freshness(images, time.monotonic())
        report.update(frames=images.metadata, counters=images.counters, screen_age_seconds=ages,
                      freshness_requirement="Both screens >=2 decoded frames and latest age <=3 seconds",
                      rejected_packets=assembler.rejected, elapsed_seconds=time.monotonic() - started)
        atomic_bytes(output / "capture.json", (json.dumps(report, indent=2) + "\n").encode())
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Console IPv4 address")
    parser.add_argument("--bind", required=True, help="Local source IPv4 address, e.g. 192.168.50.47")
    parser.add_argument("--output", required=True, type=Path, help="New private output directory")
    parser.add_argument("--duration", type=float, default=15)
    args = parser.parse_args(argv)
    if not 0 < args.duration <= 15:
        parser.error("duration must be in (0, 15] seconds")
    report = capture(args.host, args.bind, args.output, args.duration)
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
