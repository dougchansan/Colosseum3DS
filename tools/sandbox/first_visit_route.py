#!/usr/bin/env python3
"""Drive the admitted first visit through controller input and trace feedback.

This runs the native executable in Azahar. It never writes guest memory or
game flags. Fresh mode refuses an installed save; Continue mode uses the save
created by that run. The combat backend must remain pending.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
SAVE = Path("3ds/Colosseum3DS/saves/pokemon_colosseum.gci")
TRACE = Path("3ds/Colosseum3DS/boot-trace.log")
TICK = re.compile(r"field_tick floor=(\d+) phase=(\d+) menu=(\d+) control=(\d+) "
                  r"message=([0-9a-f]+).*?xyz=([-\d.]+),([-\d.]+),([-\d.]+)")


class Route:
    def __init__(self, args, process, packet):
        self.args, self.process, self.packet = args, process, packet
        self.trace = args.state / "save/Azahar/sdmc" / TRACE
        self.core = args.run / "core.log"
        self.sequence = 0
        self.stages = []
        self.deadline = time.monotonic() + args.seconds - 5

    def text(self, path):
        try:
            return path.read_text(errors="replace")
        except FileNotFoundError:
            return ""

    def log(self):
        return self.text(self.trace)

    def tick(self):
        matches = list(TICK.finditer(self.log()))
        if not matches:
            return None
        m = matches[-1]
        return dict(floor=int(m[1]), phase=int(m[2]), menu=int(m[3]),
                    control=int(m[4]), message=int(m[5], 16),
                    xyz=[float(m[i]) for i in (6, 7, 8)])

    def wait(self, predicate, reason):
        while not predicate():
            if self.process.poll() is not None:
                raise RuntimeError("Runner ended before " + reason)
            if time.monotonic() >= self.deadline:
                raise TimeoutError(reason)
            log = self.log()
            if re.search(r"(?:field|boot)_end result=-\d+", log):
                raise RuntimeError("Native application failed while " + reason)
            time.sleep(.01)

    def pulse(self, count=40, mask=0, x=0, y=0):
        self.sequence += 1
        temporary = self.packet.with_suffix(".tmp")
        temporary.write_text(f"{self.sequence},{count},0x{mask:04x},{x},{y},0,0\n")
        temporary.replace(self.packet)
        marker = f"LIVE INPUT expired sequence={self.sequence} "
        self.wait(lambda: marker in self.text(self.core), "controller pulse expiry")

    def press(self, mask, count=30):
        self.pulse(count, mask)
        self.pulse(60)

    def sample(self):
        # SELECT alone has no action in this field adapter. Its press causes
        # a fresh diagnostic tick without invoking Talk or opening a menu.
        self.press(0x0004, 30)
        tick = self.tick()
        if tick is None:
            raise RuntimeError("No field tick after controller sample")
        return tick

    def stage(self, name):
        self.pulse(120)
        captures = re.findall(r"Captured frame (\d+) \(", self.text(self.core))
        if not captures:
            raise RuntimeError("No framebuffer capture for " + name)
        frame = int(captures[-1])
        source = self.args.run / f"frame-{frame:06d}.ppm"
        from PIL import Image
        target = self.args.run / "stages" / (name + ".png")
        target.parent.mkdir(exist_ok=True)
        with Image.open(source) as screenshot:
            screenshot.save(target)
        entry = dict(name=name, frame=frame, image=str(target), tick=self.tick(),
                     sha256=hashlib.sha256(target.read_bytes()).hexdigest())
        self.stages.append(entry)
        print(json.dumps({"stage": name, "frame": frame, "tick": entry["tick"]}), flush=True)

    def ready(self, floor):
        tick = self.tick()
        return bool(tick and tick["floor"] == floor and tick["phase"] == 4
                    and tick["control"] and tick["menu"] == 0)

    def event(self, done, name):
        seen = set()
        for _ in range(180):
            if done():
                return
            tick = self.tick()
            if tick and tick["message"]:
                ident = tick["message"]
                if ident not in seen:
                    self.stage(f"{name}-{ident:05x}")
                    seen.add(ident)
                self.press(0x0100)
            else:
                self.pulse(60)
        raise RuntimeError("Original event did not finish: " + name)

    def move(self, axis, target, *, tolerance=.28, until=None):
        for _ in range(24):
            tick = self.sample()
            if until and until():
                return
            if not tick["control"] or tick["menu"]:
                raise RuntimeError("Movement attempted without field control")
            delta = target - tick["xyz"][axis]
            print(json.dumps({"move_axis": axis, "target": target, "xyz": tick["xyz"]}), flush=True)
            if abs(delta) <= tolerance:
                return
            count = max(12, min(90, int(abs(delta) * 15)))
            magnitude = 9000 if abs(delta) < 1.0 else 30000
            direction = magnitude if delta > 0 else -magnitude
            self.pulse(count, x=direction if axis == 0 else 0,
                       y=direction if axis == 2 else 0)
        raise RuntimeError(f"Controller route stuck on axis {axis} toward {target}")

    def save(self):
        before = self.log().count("game_save status=ok ")
        self.press(0x0200)
        if self.sample()["menu"] != 1:
            raise RuntimeError("Pause menu did not open for Save")
        self.press(0x0020, 20)
        self.press(0x0020, 20)
        self.press(0x0100)
        if self.sample()["menu"] != 3:
            raise RuntimeError("Save confirmation did not open")
        self.press(0x0010, 20)
        self.press(0x0100)
        self.wait(lambda: self.log().count("game_save status=ok ") > before,
                  "explicit fresh-game save")
        self.stage("saved-fresh-wes-shop")
        self.press(0x0100)
        self.press(0x0001)

    def fresh(self):
        self.wait(lambda: "field_begin floor=1" in self.log(), "fresh opening movie")
        self.pulse(1)  # Take over from the startup CSV; all later input is live.
        self.wait(lambda: self.ready(1), "fresh arrival and field control")
        if "field_arrival_complete flag=0af8" not in self.log():
            raise RuntimeError("Fresh arrival flag was not written by the app")
        self.stage("fresh-wes-outskirt")
        self.move(2, -1.0)
        self.move(0, -.35, until=lambda: not self.ready(1))
        self.event(lambda: "story_flag id=03da value=1" in self.log() and self.ready(1),
                   "original-departure")
        self.stage("departure-complete")
        self.move(2, 2.5)
        self.move(0, -1.7)
        self.move(2, -4.0, until=lambda: "story_door source=4 " in self.log())
        self.event(lambda: "story_flag id=03db value=1" in self.log() and self.ready(2),
                   "original-shop-tv")
        self.stage("shop-control")
        self.move(0, -3.3)
        self.move(2, -.38, tolerance=.18)
        self.move(0, 2.7)
        self.press(0x0020, 18)  # Face Willie across the original counter.
        self.press(0x0100)
        self.event(lambda: "story_flag id=03dc value=1" in self.log() and self.ready(2),
                   "willie-conversation")
        self.stage("willie-talk-complete")
        self.save()

    def continued(self):
        self.wait(lambda: self.log() != self.args.prior_trace and
                  "game_load status=ok name=WES floor=2 " in self.log(),
                  "this process to load the fresh WES save")
        self.wait(lambda: self.ready(2), "Continue of the freshly saved shop")
        self.pulse(1)
        if "id=0426e" in self.log():
            raise RuntimeError("Continue replayed the first-visit TV report")
        self.stage("continued-fresh-wes-shop")
        self.move(2, 3.0, until=lambda: "story_door source=2 " in self.log())
        self.event(lambda: "story_battle encounter=10 status=waiting_backend result_written=0" in self.log(),
                   "outside-challenge")
        if "story_flag id=03de value=1" in self.log():
            raise RuntimeError("Battle victory flag was advanced without a backend")
        self.stage("first-battle-pending")

    def exit(self):
        self.press(0x000c, 60)
        self.wait(lambda: "boot_end result=1 error=\n" in self.log(), "clean native exit")
        print(json.dumps({"stage": "native-clean-exit"}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("fresh", "continue"), required=True)
    for name in ("app", "state", "run", "video"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--frames", type=int, default=18000)
    parser.add_argument("--seconds", type=int, default=180)
    args = parser.parse_args()
    for name in ("app", "state", "run", "video"):
        setattr(args, name, getattr(args, name).resolve())
    sdmc = args.state / "save/Azahar/sdmc"
    save = sdmc / SAVE
    if args.mode == "fresh" and (save.exists() or save.with_suffix(".gci.bak").exists()):
        parser.error("Fresh mode requires an isolated SD without a primary or backup GCI")
    if args.mode == "continue" and not save.is_file():
        parser.error("Continue requires the save made by the fresh route")
    args.run.parent.mkdir(parents=True, exist_ok=True)
    packet = args.run.with_name(args.run.name + "-controller.packet")
    startup = args.run.with_name(args.run.name + "-startup.csv")
    if packet.exists() or startup.exists():
        parser.error("Use a new run name; existing controller evidence is preserved")
    original = "new-game-wes-input.csv" if args.mode == "fresh" else "continue-wes-input.csv"
    lines = [line for line in (HERE / original).read_text().splitlines()
             if line and not line.startswith("#") and int(line.split(",", 1)[0]) < 2000]
    startup.write_text("\n".join(lines) + "\n")
    command = [sys.executable, str(HERE / "record_run.py"), "--video", str(args.video), "--",
               str(args.app), "--state", str(args.state), "--out", str(args.run), "--gl",
               "--frames", str(args.frames), "--seconds", str(args.seconds), "--capture-every", "60",
               "--events", str(startup), "--live-input", str(packet),
               "--app-trace", str(TRACE), "--sd-file", str(SAVE)]
    error = None
    trace = sdmc / TRACE
    args.prior_trace = trace.read_text(errors="replace") if trace.exists() else ""
    with args.run.with_name(args.run.name + "-runner.log").open("x") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)
        route = Route(args, process, packet)
        try:
            route.fresh() if args.mode == "fresh" else route.continued()
            route.exit()
        except Exception as problem:
            error = str(problem)
            print(json.dumps({"route_error": error, "tick": route.tick()}), flush=True)
            try:
                route.pulse(60, 0x000c)
            except Exception:
                pass
        code = process.wait(timeout=args.seconds + 20)
    record = dict(mode=args.mode, controller_completed=error is None,
                  error=error, runner_exit_code=code, stages=route.stages,
                  scope="Actual controller input; no guest-memory or game-flag writes. "
                        "Emulator evidence only; combat stays pending.")
    (args.run / "controller-result.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))
    return 0 if error is None and code == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
