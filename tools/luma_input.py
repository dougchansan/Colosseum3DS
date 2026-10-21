#!/usr/bin/env python3
"""Send one bounded Luma button action, followed by repeated neutral release.

Protocol checked against Luma3DS input_redirection.c at
3253fdb255b311455625db2b89f23137981230df and xzn/ntrviewer-hr
ui_input_redirection.c at 556c6d06b21cef38c29efd0cdad59a5771956268.
UDP send success does not prove console receipt or a visible action.
"""
import argparse
import datetime
import json
import math
from pathlib import Path
import socket
import struct
import time

HID_BITS = {"A": 0, "B": 1, "SELECT": 2, "START": 3,
            "RIGHT": 4, "LEFT": 5, "UP": 6, "DOWN": 7,
            "R": 8, "L": 9, "X": 10, "Y": 11}
BUTTONS = tuple(HID_BITS) + ("ZL", "ZR")
NEUTRAL = struct.pack("<5I", 0xfff, 0x02000000, 0x007ff7ff, 0x80800081, 0)


def packet(buttons=(), circle_pad=(0, 0), c_stick=(0, 0)):
    for axes in (circle_pad, c_stick):
        if len(axes) != 2 or any(not math.isfinite(value) or not -1 <= value <= 1 for value in axes):
            raise ValueError("Analog axes must be finite values in [-1, 1]")
    hid = 0xfff
    cpp = 0x80800081
    for button in buttons:
        if button in HID_BITS:
            hid &= ~(1 << HID_BITS[button])
        elif button == "ZL":
            cpp |= 1 << 10
        elif button == "ZR":
            cpp |= 1 << 9
        else:
            raise ValueError("Unsupported button: " + button)
    circle = 0x007ff7ff
    if any(circle_pad):
        x, y = (int(value * 32767) for value in circle_pad)
        circle = ((round(y * 0x5d0 / 32767) + 0x800) << 12) | (round(x * 0x5d0 / 32767) + 0x800)
    if any(c_stick):
        x, y = (int(value * 32767) for value in c_stick)
        def component(value):
            scaled = value * (2 ** -0.5) * 127
            rounded = math.copysign(math.floor(abs(scaled) + 0.5), scaled)
            return min(255, max(1, int(rounded / 32767) + 128))
        cpp = (component(y - x) << 24) | (component(x + y) << 16) | (cpp & 0xffff)
    # Touch is neutral; special HOME/POWER bits are always zero.
    return struct.pack("<5I", hid, 0x02000000, circle, cpp, 0)


def send_action(host, buttons, duration, evidence, bind=None, circle_pad=(0, 0), c_stick=(0, 0)):
    if not 0 < duration <= 0.5:
        raise ValueError("duration must be in (0, 0.5] seconds")
    if len(set(buttons)) > 1 and set(buttons) != {"START", "SELECT"}:
        raise ValueError("Only one button or the START+SELECT exit combination is supported")
    socket.inet_aton(host)
    if bind is not None:
        socket.inet_aton(bind)
    held = packet(buttons, circle_pad, c_stick)
    report = {"host": host, "port": 4950, "buttons": list(buttons), "duration_seconds": duration,
              "circle_pad": circle_pad, "c_stick": c_stick,
              "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "hold_sent": 0, "release_attempted": 0, "release_sent": 0,
              "release_errors": [], "status": "failed", "receipt_verified": False,
              "limits": "UDP sends only; console receipt and visible response require observation"}
    # Exclusive creation reserves the evidence destination before any network write.
    with Path(evidence).open("x") as log:
        json.dump(report, log, indent=2)
        log.flush()
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
                udp.settimeout(0.25)
                if bind is not None:
                    udp.bind((bind, 0))
                def send(data):
                    if udp.sendto(data, (host, 4950)) != len(data):
                        raise OSError("Incomplete UDP send")
                started = time.monotonic()
                try:
                    for index in range(math.ceil(duration * 60)):
                        if index and time.monotonic() - started >= duration:
                            break
                        send(held)
                        report["hold_sent"] += 1
                        remaining = duration - (time.monotonic() - started)
                        if remaining > 0:
                            time.sleep(min(1 / 60, remaining))
                finally:
                    for _ in range(3):
                        report["release_attempted"] += 1
                        try:
                            send(NEUTRAL)
                            report["release_sent"] += 1
                        except OSError as exc:
                            report["release_errors"].append(str(exc))
                        time.sleep(1 / 60)
                report["local_source_address"] = udp.getsockname()[0]
                report["status"] = "sent" if report["release_sent"] == 3 else "release_error"
        except (OSError, KeyboardInterrupt) as exc:
            report["error"] = str(exc) or "Interrupted"
        finally:
            log.seek(0)
            log.truncate()
            json.dump(report, log, indent=2)
            log.write("\n")
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Console IPv4")
    parser.add_argument("--bind", help="Optional local source IPv4")
    parser.add_argument("--button", default=[], choices=BUTTONS, action="append")
    parser.add_argument("--circle", "--circle-pad", dest="circle_pad", nargs=2, type=float, default=None, metavar=("X", "Y"), help="[-1,1], right/up positive")
    parser.add_argument("--cstick", "--c-stick", dest="c_stick", nargs=2, type=float, default=None, metavar=("X", "Y"), help="[-1,1], right/up positive")
    parser.add_argument("--duration", type=float, default=0.15)
    parser.add_argument("--evidence", required=True, type=Path, help="New private JSON evidence file")
    args = parser.parse_args(argv)
    if not args.button and args.circle_pad is None and args.c_stick is None:
        parser.error("Supply a button or analog action")
    if not 0 < args.duration <= 0.5:
        parser.error("duration must be in (0, 0.5] seconds")
    report = send_action(args.host, args.button, args.duration, args.evidence, args.bind, args.circle_pad or (0, 0), args.c_stick or (0, 0))
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "sent" else 1


if __name__ == "__main__":
    raise SystemExit(main())
