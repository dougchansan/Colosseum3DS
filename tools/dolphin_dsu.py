#!/usr/bin/env python3
"""Bounded, loopback-only ordinary DSU controller for an owned Dolphin session.

Wire contract: official Dolphin 5e7cc91d8c9a43ca189b288937f65c9763af9c22,
InputCommon/ControllerInterface/DualShockUDPClient/{DualShockUDPClient.cpp,
DualShockUDPProto.h}. No game memory or operating-system input APIs are used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import socket
import struct
import subprocess
import threading
import time
import zlib

VERSION, PORT = 1001, 26761
VERSION_MSG, PORTS_MSG, PAD_MSG = 0x100000, 0x100001, 0x100002
MAC = bytes.fromhex("020000005401")
SERVER_UID = 0x5441494C
ANALOG = {"LEFT": 44, "DOWN": 45, "RIGHT": 46, "UP": 47,
          "X": 48, "A": 49, "B": 50, "Y": 51, "R": 52, "L": 53}


def packet(source, uid, kind, payload=b""):
    data = bytearray(struct.pack("<4sHHIII", source, VERSION, 4 + len(payload), 0, uid, kind) + payload)
    struct.pack_into("<I", data, 8, zlib.crc32(data))
    return bytes(data)


def parse_client(data):
    if len(data) < 20:
        raise ValueError("Short DSU packet")
    magic, version, length, crc, uid, kind = struct.unpack_from("<4sHHIII", data)
    check = bytearray(data)
    check[8:12] = bytes(4)
    if magic != b"DSUC" or version != VERSION or length + 16 != len(data) or zlib.crc32(check) != crc:
        raise ValueError("Invalid DSU header or CRC")
    if kind not in (VERSION_MSG, PORTS_MSG, PAD_MSG):
        raise ValueError("Unknown DSU request")
    if len(data) != (20 if kind == VERSION_MSG else 28):
        raise ValueError("Invalid DSU request extent")
    return uid, kind, data[20:]


def port_info(pad):
    if pad not in range(4):
        raise ValueError("Invalid pad ID")
    connected = pad == 0
    return struct.pack("<BBBB6sBB", pad, 2 if connected else 0,
                       3 if connected else 0, 1 if connected else 0,
                       MAC if connected else bytes(6), 5 if connected else 0, 0)


def pad_data(counter, buttons=(), stick=(128, 128), timestamp=0):
    if not set(buttons) <= set(ANALOG) | {"START"} or any(not 0 <= x <= 255 for x in stick):
        raise ValueError("Invalid controller state")
    data = bytearray(packet(b"DSUS", SERVER_UID, PAD_MSG,
                            port_info(0)[:-1] + b"\x01" + bytes(68)))
    assert len(data) == 100
    struct.pack_into("<I", data, 32, counter & 0xFFFFFFFF)
    data[40:44] = bytes((*stick, 128, 128))
    for button in buttons:
        if button == "START":
            data[36] |= 8  # Dolphin names this digital input Options.
        else:
            data[ANALOG[button]] = 255
    struct.pack_into("<Q", data, 68, timestamp)
    data[8:12] = bytes(4)
    struct.pack_into("<I", data, 8, zlib.crc32(data))
    return bytes(data)


class Controller:
    """One DSU client UID, pad0, expiring registrations and input leases."""
    def __init__(self, clock=time.monotonic):
        self.clock = clock
        self.uid = None
        self.registrations = {}
        self.buttons = ()
        self.stick = (128, 128)
        self.until = 0
        self.counter = 0
        self.requests = 0
        self.lock = threading.Lock()

    def set(self, buttons=(), stick=(128, 128), lease_ms=250):
        pad_data(0, buttons, stick)
        if not 1 <= lease_ms <= 1000:
            raise ValueError("Input lease must be 1..1000ms")
        with self.lock:
            self.buttons, self.stick = tuple(buttons), tuple(stick)
            self.until = self.clock() + lease_ms / 1000

    def neutral(self):
        with self.lock:
            self.buttons, self.stick, self.until = (), (128, 128), 0

    def unregister(self, endpoint):
        self.registrations.pop(endpoint, None)

    def handle(self, data, endpoint):
        if endpoint[0] != "127.0.0.1":
            raise ValueError("Only loopback clients are allowed")
        uid, kind, payload = parse_client(data)
        if self.uid is not None and uid != self.uid:
            raise ValueError("Another DSU client UID already owns this session")
        if kind == PORTS_MSG:
            count = struct.unpack_from("<I", payload)[0]
            pads = list(payload[4:4 + count])
            if not 1 <= count <= 4 or len(set(pads)) != count or any(x > 3 for x in pads):
                raise ValueError("Invalid requested pad IDs")
            replies = [packet(b"DSUS", SERVER_UID, PORTS_MSG, port_info(x)) for x in pads]
        elif kind == PAD_MSG:
            flags, pad, mac = struct.unpack("<BB6s", payload)
            if flags not in (0, 1, 2) or (flags == 1 and pad != 0) or (flags == 2 and mac != MAC):
                raise ValueError("Only pad0 registrations are allowed")
            self.registrations[endpoint] = self.clock() + 2.5
            replies = []
        else:
            replies = [packet(b"DSUS", SERVER_UID, VERSION_MSG, struct.pack("<H2x", VERSION))]
        self.uid = uid
        self.requests += 1
        return replies

    def updates(self):
        now = self.clock()
        for endpoint, expiry in list(self.registrations.items()):
            if now >= expiry:
                self.unregister(endpoint)
        with self.lock:
            if now >= self.until:
                self.buttons, self.stick = (), (128, 128)
            self.counter += 1
            data = pad_data(self.counter, self.buttons, self.stick, int(now * 1_000_000))
        return [(endpoint, data) for endpoint in self.registrations]


class Server:
    def __init__(self, host="127.0.0.1", port=PORT):
        if host != "127.0.0.1" or port != PORT:
            raise ValueError("This controller is restricted to 127.0.0.1:26761")
        self.controller = Controller()
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind((host, port))
        self.socket.settimeout(.01)
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self.run, daemon=True)

    def run(self):
        while not self.stopped.is_set():
            try:
                data, endpoint = self.socket.recvfrom(1024)
                try:
                    for reply in self.controller.handle(data, endpoint):
                        self.socket.sendto(reply, endpoint)
                except ValueError:
                    pass
            except socket.timeout:
                pass
            for endpoint, data in self.controller.updates():
                self.socket.sendto(data, endpoint)

    def close(self):
        self.controller.neutral()
        self.stopped.set()
        if self.thread.is_alive():
            self.thread.join(1)
        # DSU has no unregister command. Send final neutral and drop local leases.
        for endpoint, data in self.controller.updates():
            self.socket.sendto(data, endpoint)
            self.controller.unregister(endpoint)
        self.socket.close()


def process_info(pid):
    if os.name != "nt":
        raise ValueError("Owned official Windows Dolphin session required")
    script = (f"Get-CimInstance Win32_Process -Filter 'ProcessId = {pid}' | "
              "Select-Object ExecutablePath,CommandLine | ConvertTo-Json -Compress")
    result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", script],
                            capture_output=True, text=True, check=True, timeout=5)
    return json.loads(result.stdout or "null")


def validate_session(session, probe=process_info):
    pid = session.get("owned_pid")
    if type(pid) is not int or pid <= 0 or session.get("ready") is not True:
        raise ValueError("Explicit ready owned-session manifest required")
    user = Path(session["user_root"]).resolve()
    root = Path(session["output_root"]).resolve()
    if user == root or root not in user.parents:
        raise ValueError("User must be isolated beneath private output root")
    exe = Path(session["exe"]).resolve()
    if hashlib.sha256(exe.read_bytes()).hexdigest() != session["exe_sha256"]:
        raise ValueError("Dolphin executable hash changed")
    info = probe(pid)
    norm = lambda x: str(x).replace("\\", "/").lower()
    args = shlex.split(info["CommandLine"], posix=False) if info else []
    users = [args[i + 1].strip('"') for i, arg in enumerate(args[:-1]) if arg in ("--user", "-u")]
    movies = [args[i + 1].strip('"') for i, arg in enumerate(args[:-1]) if arg in ("--movie", "-m")]
    if (not info or norm(info["ExecutablePath"]) != norm(exe) or
            [norm(x) for x in users] != [norm(user)]):
        raise ValueError("Session does not own the isolated Dolphin process")
    if movies:
        # This is an explicit inspected EOF checkpoint, not a guessed timer.
        # The operator must first inspect a fresh screenshot after the bounded
        # prefix and verify PauseMovie=False. Actual DSU acceptance is separate.
        if (len(movies) != 1 or session.get("movie_handoff_ready") is not True or
                hashlib.sha256(Path(movies[0]).read_bytes()).hexdigest() != session.get("completed_movie_sha256")):
            raise ValueError("Movie playback requires an explicit verified EOF handoff gate")
    import configparser
    config = configparser.ConfigParser(interpolation=None)
    config.read(user / "Config/Dolphin.ini")
    if config.getboolean("Core", "EnableCheats", fallback=True):
        raise ValueError("Cheats must be disabled")
    if not config.getboolean("Input", "BackgroundInput", fallback=False):
        raise ValueError("Hidden session requires BackgroundInput=True")
    if movies and config.getboolean("Movie", "PauseMovie", fallback=True):
        raise ValueError("Movie EOF handoff requires PauseMovie=False")
    config.read(user / "Config/DSUClient.ini")
    if not config.getboolean("Server", "Enabled", fallback=False) or config.get("Server", "Entries", fallback="") != "TailLoopback:127.0.0.1:26761;":
        raise ValueError("Unexpected DSU server configuration")
    config.read(user / "Config/GCPadNew.ini")
    if config.get("GCPad1", "Device", fallback="") != "DSUClient/0/TailLoopback":
        raise ValueError("Unexpected controller device")
    frames = Path(session["frames_dir"]).resolve()
    if user not in frames.parents:
        raise ValueError("Frame directory must belong to isolated user")
    return frames


def complete_png(data):
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        return False
    offset, saw_image = 8, False
    while offset + 12 <= len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        end = offset + 12 + length
        if end > len(data):
            return False
        kind, body = data[offset + 4:offset + 8], data[offset + 8:end - 4]
        if zlib.crc32(kind + body) != struct.unpack_from(">I", data, end - 4)[0]:
            return False
        if offset == 8 and (kind != b"IHDR" or length != 13 or not all(struct.unpack_from(">II", body))):
            return False
        saw_image |= kind == b"IDAT" and bool(body)
        if kind == b"IEND":
            return saw_image and length == 0 and end == len(data)
        offset = end
    return False


def fresh_frame(directory, after=-1, now=time.time):
    candidates = []
    for path in directory.glob("framedump_*.png"):
        match = re.fullmatch(r"framedump_(\d+)\.png", path.name)
        if match and int(match[1]) > after:
            candidates.append((int(match[1]), path))
    for number, path in sorted(candidates, reverse=True):
        try:
            data = path.read_bytes()
            age = now() - path.stat().st_mtime
        except OSError:
            continue
        if 0 <= age <= 2 and complete_png(data):
            return number, path
    raise ValueError("No fresh complete PNG frame from owned session")


def execute(server, session, actions, evidence, max_seconds=30):
    frames = validate_session(session)
    if not isinstance(actions, list) or len(actions) > 32 or not 1 <= max_seconds <= 120:
        raise ValueError("Bounded action list required")
    deadline, last = time.monotonic() + max_seconds, -1
    def log(event, **fields):
        evidence.write(json.dumps(dict(event=event, utc=time.time(), **fields)) + "\n")
        evidence.flush()
    try:
        log("server_status", client_uid=server.controller.uid,
            validated_requests=server.controller.requests,
            registered_endpoints=list(server.controller.registrations),
            acceptance="protocol registration only, not game acceptance")
        for action in actions:
            if time.monotonic() >= deadline:
                raise ValueError("Action session deadline exceeded")
            validate_session(session)
            number, path = fresh_frame(frames, last)
            if not server.controller.registrations:
                raise ValueError("Dolphin has not registered pad0; no input sent")
            duration = action.get("duration_ms", 100)
            if type(duration) is not int or not 10 <= duration <= 500:
                raise ValueError("Action duration must be 10..500ms")
            if time.monotonic() + duration / 1000 > deadline:
                raise ValueError("Action deadline expired during readiness checks; no input sent")
            buttons = action.get("buttons", [])
            server.controller.set(buttons, tuple(action.get("stick", [128, 128])), duration + 100)
            log("controller_state_sent", frame=number, png=str(path), buttons=buttons, duration_ms=duration)
            time.sleep(duration / 1000)
            server.controller.neutral()
            log("controller_released")
            last = number
            # Freshness records are observations, never a claim the game accepted an action.
            time.sleep(.05)
        log("actions_finished", acceptance="not asserted; inspect fresh screenshots")
    finally:
        server.controller.neutral()
        log("finally_neutral")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path, required=True)
    parser.add_argument("--actions", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()
    session = json.loads(args.session.read_text())
    validate_session(session)
    server = Server()
    try:
        server.thread.start()
        end = time.monotonic() + 3
        while not server.controller.registrations and time.monotonic() < end:
            time.sleep(.02)
        with args.evidence.open("x") as evidence:
            execute(server, session, json.loads(args.actions.read_text()), evidence, args.seconds)
    finally:
        server.close()


if __name__ == "__main__":
    main()
