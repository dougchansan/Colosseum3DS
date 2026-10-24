#!/usr/bin/env python3
"""Navigate calibrated console states using fresh receive-only NTR video.

Requires private calibrated bottom-screen templates. By default receives an
already active UDP8001 stream; --live-dir reads an existing recorder publication
without opening a video socket. No TCP8000 connection is opened.
Unknown/ambiguous states cannot authorize navigation.
"""
import argparse
import datetime
import errno
import os
from functools import lru_cache
import io
import json
from pathlib import Path
import socket
import threading
import time

from PIL import Image, ImageChops, ImageStat
import luma_input
from ntr_capture import Assembler

ACTIONS = {name: (name,) for name in ('HOME', 'LEFT', 'RIGHT', 'UP', 'DOWN', 'A')}
ACTIONS['STARTSELECT'] = ('START', 'SELECT')


@lru_cache(maxsize=64)
def reference_image(path):
    with Image.open(path) as image:
        return image.convert('RGB')


def classify(image, states, root):
    matches = []
    for name, state in states.items():
        box = tuple(state['box'])
        reference = reference_image((root / state['reference']).resolve())
        crop = image.crop(box).convert('RGB')
        if reference.size == image.size:
            reference = reference.crop(box)
        if crop.size != reference.size:
            raise ValueError('Reference size mismatch: ' + name)
        rms = ImageStat.Stat(ImageChops.difference(crop, reference)).rms
        error = (sum(value * value for value in rms) / 3) ** 0.5
        if error <= state.get('threshold', 8):
            matches.append(name)
    return matches[0] if len(matches) == 1 else None


class Frames:
    """One persistent receiver; decode in memory while input actions run."""
    def __init__(self, host, bind):
        self.host = host
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.udp.bind((bind, 8001))
            self.udp.settimeout(0.1)
            self.lock = threading.Lock()
            self.latest = {}
            self.counters = {'top': 0, 'bottom': 0}
            self.closed = threading.Event()
            self.error = None
            self.thread = threading.Thread(target=self.receive, daemon=True)
            self.thread.start()
        except BaseException:
            self.udp.close()
            raise

    def receive(self):
        assembler = Assembler()
        try:
            while not self.closed.is_set():
                try:
                    data, sender = self.udp.recvfrom(65535)
                except socket.timeout:
                    continue
                if sender[0] != self.host:
                    continue
                result = assembler.feed(data, time.monotonic())
                if not result:
                    continue
                screen, frame, jpeg = result
                try:
                    with Image.open(io.BytesIO(jpeg)) as raw:
                        if raw.format != 'JPEG' or raw.size != (240, 400 if screen == 'top' else 320):
                            continue
                        image = raw.convert('RGB').transpose(Image.Transpose.ROTATE_90)
                except (OSError, ValueError):
                    continue
                with self.lock:
                    self.counters[screen] += 1
                    self.latest[screen] = (image, time.monotonic(), frame)
        except OSError as exc:
            if not self.closed.is_set():
                self.error = str(exc)

    def snapshot(self):
        with self.lock:
            return dict(self.counters), dict(self.latest)

    def close(self):
        self.closed.set()
        self.thread.join(timeout=0.3)
        self.udp.close()



def _publication_sharing_error(exc):
    winerror = getattr(exc, 'winerror', None)
    return winerror in (5, 32) or (os.name == 'nt' and winerror is None
                                  and exc.errno == errno.EACCES)


class PublishedFrames:
    """Read coherent live PNG/metadata files; owns no sockets or cached frames."""
    def __init__(self, live):
        self.live = Path(live)
        self.error = None

    def snapshot(self):
        try:
            for _ in range(12):
                metadata_path = self.live / 'frames.json'
                png_path = self.live / 'both.png'
                try:
                    first = metadata_path.read_bytes()
                    metadata_stamp = metadata_path.stat().st_mtime_ns
                    png_stamp = png_path.stat().st_mtime_ns
                    payload = png_path.read_bytes()
                    second = metadata_path.read_bytes()
                    final_png_stamp = png_path.stat().st_mtime_ns
                except OSError as exc:
                    # Windows atomic publisher replacement can briefly deny a
                    # read/stat; CRT may expose only errno13 on Windows. Retry within
                    # the existing 12-attempt budget; never reuse cached frames.
                    if not _publication_sharing_error(exc):
                        raise
                    time.sleep(.01)
                    continue
                # Publisher replaces PNG before replacing metadata. Do not use
                # a PNG known to be newer than its metadata publication.
                if first != second or png_stamp != final_png_stamp or png_stamp > metadata_stamp:
                    time.sleep(.01)
                    continue
                data = json.loads(second)
                wall = datetime.datetime.now(datetime.timezone.utc).timestamp()
                monotonic = time.monotonic()
                counters, latest = {}, {}
                with Image.open(io.BytesIO(payload)) as raw:
                    if raw.format != 'PNG' or raw.size != (400, 480):
                        raise ValueError('Live combined image must be a 400x480 PNG')
                    both = raw.convert('RGB')
                for screen, box in (('top', (0, 0, 400, 240)), ('bottom', (40, 240, 360, 480))):
                    item = data[screen]
                    stamp = datetime.datetime.fromisoformat(item['received_utc'])
                    if stamp.tzinfo is None:
                        raise ValueError('Live receive timestamp requires a timezone')
                    age = wall - stamp.timestamp()
                    if not 0 <= age <= 1:
                        raise RuntimeError('Live ' + screen + ' frame stale or from the future')
                    counter, frame_id = item['counter'], item['frame_id']
                    if not isinstance(counter, int) or isinstance(counter, bool) or counter < 1:
                        raise ValueError('Invalid live frame counter')
                    if not isinstance(frame_id, int) or isinstance(frame_id, bool) or not 0 <= frame_id <= 255:
                        raise ValueError('Invalid live frame id')
                    counters[screen] = counter
                    latest[screen] = (both.crop(box), monotonic - age, frame_id)
                self.error = None
                return counters, latest
            raise RuntimeError('Live publication kept racing; no coherent snapshot')
        except (OSError, ValueError, RuntimeError, KeyError, TypeError) as exc:
            self.error = str(exc) or type(exc).__name__
            raise RuntimeError(self.error) from exc

    def close(self):
        pass


def ftp_ready(host, bind):
    try:
        with socket.create_connection((host, 5000), timeout=0.5,
                                      source_address=(bind, 0)) as connection:
            return connection.recv(1024).startswith(b'220 ')
    except OSError:
        return False


def wait_state(frames, config, root, baseline, expected, deadline, readiness):
    candidate = None
    stable_since = None
    stable_counters = None
    while time.monotonic() < deadline:
        if frames.error:
            raise RuntimeError(frames.error)
        counters, latest = frames.snapshot()
        now = time.monotonic()
        if all(screen in latest and counters[screen] > baseline[screen]
               and now - latest[screen][1] <= 1 for screen in ('top', 'bottom')):
            state = classify(latest['bottom'][0], config['states'], root)
            if state and (expected is None or state in expected):
                settle = config['states'][state].get('settle_seconds', 0.2)
                if not 0 <= settle <= 1:
                    raise ValueError('settle_seconds must be between 0 and 1')
                if state != candidate:
                    candidate, stable_since, stable_counters = state, now, counters
                if now - stable_since >= settle and all(
                        counters[screen] > stable_counters[screen] for screen in counters):
                    if not config['states'][state].get('ftp_ready') or readiness():
                        return state, counters, latest
            else:
                candidate = None
        else:
            candidate = None
        time.sleep(0.02)
    raise RuntimeError('No fresh, uniquely recognized expected state before deadline')


def navigate(config, root, target, frames, output, dispatch, readiness):
    started = time.monotonic()
    deadline = started + 90
    report = {'status': 'failed', 'target': target, 'steps': []}
    baseline = frames.snapshot()[0]
    expected = None
    try:
        for index in range(13):
            state, counters, latest = wait_state(
                frames, config, root, baseline, expected,
                min(deadline, time.monotonic() + 12), readiness)
            record = {'state': state, 'elapsed_seconds': time.monotonic() - started,
                      'frames': {screen: {'counter': counters[screen], 'frame_id': value[2],
                                          'received_monotonic': value[1]}
                                 for screen, value in latest.items()}}
            report['steps'].append(record)
            for screen, value in latest.items():
                value[0].save(output / f'{index:02d}-{state}-{screen}.png')
            if state == target:
                report['status'] = 'passed'
                break
            if index == 12 or time.monotonic() + 0.7 >= deadline:
                raise RuntimeError('Navigation action/time limit reached')
            edge = config['routes'][target].get(state)
            if not edge or edge['action'] not in ACTIONS:
                raise ValueError('No allowed calibrated route from ' + state)
            duration = edge.get('duration', 0.15)
            if duration not in (0.1, 0.15, 0.4) or (duration == 0.4 and edge['action'] != 'DOWN'):
                raise ValueError('Unsupported action duration')
            expected = edge['expected']
            if not expected or any(name not in config['states'] for name in expected):
                raise ValueError('Unknown expected state')
            # Saving PNGs can allow contradictory frames to arrive. Authorize
            # input from the receiver's current snapshot after that I/O.
            authorization_counters, authorization_latest = frames.snapshot()
            if frames.error:
                raise RuntimeError(frames.error)
            now = time.monotonic()
            if any(screen not in authorization_latest or
                   now - authorization_latest[screen][1] > 1
                   for screen in ('top', 'bottom')):
                raise RuntimeError('State became stale before input')
            authorization_state = classify(authorization_latest['bottom'][0],
                                           config['states'], root)
            if authorization_state != state:
                raise RuntimeError('Current state no longer matches route authorization')
            if time.monotonic() + 0.7 >= deadline:
                raise RuntimeError('Navigation action/time limit reached')
            record['authorization'] = {
                'state': authorization_state,
                'frames': {screen: {'counter': authorization_counters[screen],
                                    'frame_id': value[2],
                                    'received_monotonic': value[1]}
                           for screen, value in authorization_latest.items()}}
            record['action'] = edge['action']
            record['duration_seconds'] = duration
            result = dispatch(ACTIONS[edge['action']], duration,
                              output / f'{index:02d}-input.json')
            # Persist the exact authorizing images after dispatch so evidence
            # writes cannot separate the current-state check from the action.
            for screen, value in authorization_latest.items():
                value[0].save(output / f'{index:02d}-{state}-authorization-{screen}.png')
            if result['status'] != 'sent':
                raise RuntimeError('Input/release failed: ' + result['status'])
            # Only frames received after bounded action/release may prove transition.
            baseline = frames.snapshot()[0]
        else:
            raise RuntimeError('Navigation action limit reached')
    except (OSError, ValueError, RuntimeError, KeyError, KeyboardInterrupt) as exc:
        report['error'] = str(exc) or type(exc).__name__
    finally:
        report['duration_seconds'] = time.monotonic() - started
        (output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--target', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--live-dir', type=Path,
                        help='Read existing recorder frames.json/both.png; open no video receiver')
    args = parser.parse_args(argv)
    config = json.loads(args.config.read_text())
    if args.target not in config['states'] or args.target not in config['routes']:
        parser.error('Target must be a declared state and calibrated route')
    args.output.mkdir(parents=True, exist_ok=False)
    frames = None
    try:
        frames = (PublishedFrames(args.live_dir) if args.live_dir is not None
                  else Frames(config['host'], config['bind']))
        report = navigate(config, args.config.parent, args.target, frames, args.output,
                          lambda buttons, duration, evidence: luma_input.send_action(
                              config['host'], buttons, duration, evidence, config['bind']),
                          lambda: ftp_ready(config['host'], config['bind']))
    except (OSError, ValueError, RuntimeError) as exc:
        report = {'status': 'failed', 'target': args.target, 'steps': [],
                  'error': str(exc) or type(exc).__name__}
        (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    finally:
        if frames is not None:
            frames.close()
    print(json.dumps(report, indent=2))
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
