"""Offline synthetic navigation checks; never contact a console."""
import importlib.util
import datetime
import errno
import os
from types import SimpleNamespace
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

try:
    from PIL import Image
except ImportError:
    Image = None

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
SPEC = importlib.util.spec_from_file_location('console_navigate', TOOLS / 'console_navigate.py')
nav = importlib.util.module_from_spec(SPEC)
if Image is not None:
    SPEC.loader.exec_module(nav)


class FakeFrames:
    error = None

    def __init__(self, color='red', stale=False):
        self.color = color
        self.count = 0
        self.stale = stale

    def snapshot(self):
        self.count += 1
        stamp = time.monotonic() - (10 if self.stale else 0)
        return ({name: self.count for name in ('top', 'bottom')},
                {name: (Image.new('RGB', (4, 4), self.color), stamp, self.count % 256)
                 for name in ('top', 'bottom')})


@unittest.skipIf(Image is None, "Pillow not installed")
class NavigationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.output = self.root / 'evidence'
        self.output.mkdir()
        self.states = {}
        for name, color in [('ftpd', 'red'), ('model', 'blue')]:
            Image.new('RGB', (4, 4), color).save(self.root / (name + '.png'))
            self.states[name] = {'reference': name + '.png', 'box': [0, 0, 4, 4], 'threshold': 1}
        self.config = {'states': self.states, 'routes': {'model': {
            'ftpd': {'action': 'HOME', 'expected': ['model']}}}}

    def test_classifier_requires_unique_match_and_static_crop(self):
        self.assertEqual(nav.classify(Image.new('RGB', (4, 4), 'red'), self.states, self.root), 'ftpd')
        self.assertIsNone(nav.classify(Image.new('RGB', (4, 4), 'green'), self.states, self.root))
        self.states['duplicate'] = dict(self.states['ftpd'])
        self.assertIsNone(nav.classify(Image.new('RGB', (4, 4), 'red'), self.states, self.root))

    def test_reference_images_are_decoded_once_per_process(self):
        nav.reference_image.cache_clear()
        original_open = Image.open
        with patch.object(nav.Image, 'open', wraps=original_open) as opened:
            for _ in range(3):
                nav.classify(Image.new('RGB', (4, 4), 'red'), self.states, self.root)
        self.assertEqual(opened.call_count, len(self.states))

    def test_route_requires_verified_state_and_writes_evidence(self):
        frames = FakeFrames()
        def action(buttons, duration, evidence):
            self.assertEqual(buttons, ('HOME',))
            self.assertEqual(duration, 0.15)
            frames.color = 'blue'
            return {'status': 'sent'}
        report = nav.navigate(self.config, self.root, 'model', frames, self.output, action, Mock())
        self.assertEqual(report['status'], 'passed')
        self.assertEqual([step['state'] for step in report['steps']], ['ftpd', 'model'])
        self.assertEqual(len(list(self.output.glob('*.png'))), 6)
        self.assertEqual(json.loads((self.output / 'manifest.json').read_text())['status'], 'passed')

    def test_stale_and_unknown_frames_never_authorize_input(self):
        for frames in (FakeFrames(stale=True), FakeFrames('green')):
            dispatch = Mock()
            original_wait = nav.wait_state
            def bounded_wait(*args):
                return original_wait(*args[:5], time.monotonic() + 0.001, args[6])
            with patch.object(nav, 'wait_state', side_effect=bounded_wait):
                report = nav.navigate(self.config, self.root, 'model', frames,
                                      self.output, dispatch, Mock())
            self.assertEqual(report['status'], 'failed')
            dispatch.assert_not_called()

    def test_received_changes_during_evidence_save_never_authorize_old_route(self):
        original_save = Image.Image.save
        for change in ('model', 'unknown', 'ambiguous', 'stale', 'top_stale',
                       'bottom_missing', 'receiver_error'):
            with self.subTest(change=change):
                frames = FakeFrames()
                dispatch = Mock(return_value={'status': 'sent'})
                snapshot = frames.snapshot
                changed = [False]
                def current_snapshot():
                    counters, latest = snapshot()
                    if changed[0] and change == 'top_stale':
                        image, stamp, frame = latest['top']
                        latest['top'] = (image, stamp - 10, frame)
                    elif changed[0] and change == 'bottom_missing':
                        del latest['bottom']
                    return counters, latest
                frames.snapshot = current_snapshot
                def save_and_change(image, *args, **kwargs):
                    original_save(image, *args, **kwargs)
                    changed[0] = True
                    if change == 'model':
                        frames.color = 'blue'
                    elif change == 'unknown':
                        frames.color = 'green'
                    elif change == 'ambiguous':
                        self.states['duplicate'] = dict(self.states['ftpd'])
                    elif change == 'stale':
                        frames.stale = True
                    elif change == 'receiver_error':
                        frames.error = 'receiver failed during evidence save'
                try:
                    with patch.object(Image.Image, 'save', new=save_and_change):
                        report = nav.navigate(self.config, self.root, 'model', frames,
                                              self.output, dispatch, Mock())
                    self.assertEqual(report['status'], 'failed')
                    dispatch.assert_not_called()
                finally:
                    self.states.pop('duplicate', None)

    def test_dispatch_records_current_authorizing_snapshot(self):
        frames = FakeFrames()
        def action(buttons, duration, evidence):
            authorization_counter = frames.count
            frames.color = 'blue'
            action.counter = authorization_counter
            return {'status': 'sent'}
        report = nav.navigate(self.config, self.root, 'model', frames,
                              self.output, action, Mock())
        step = report['steps'][0]
        self.assertEqual(report['status'], 'passed')
        self.assertEqual(step['authorization']['state'], 'ftpd')
        self.assertEqual(step['authorization']['frames']['bottom']['counter'], action.counter)
        self.assertGreater(action.counter, step['frames']['bottom']['counter'])
        with Image.open(self.output / '00-ftpd-authorization-bottom.png') as image:
            self.assertEqual(image.getpixel((0, 0)), (255, 0, 0))

    def test_missing_route_and_forbidden_action_do_not_send(self):
        dispatch = Mock()
        for edge in ({'action': 'POWER', 'expected': ['model']}, None):
            self.config['routes']['model'] = {} if edge is None else {'ftpd': edge}
            report = nav.navigate(self.config, self.root, 'model', FakeFrames(), self.output, dispatch, Mock())
            self.assertEqual(report['status'], 'failed')
            dispatch.assert_not_called()

    def test_calibrated_a_point_two_is_narrow_and_other_hold_limits_remain(self):
        frames = FakeFrames()
        self.config['routes']['model']['ftpd'] = {'action': 'A', 'duration': .2,
                                                  'expected': ['model']}
        def action(buttons, duration, evidence):
            self.assertEqual((buttons, duration), (('A',), .2))
            frames.color = 'blue'
            return {'status': 'sent'}
        report = nav.navigate(self.config, self.root, 'model', frames,
                              self.output, action, Mock())
        self.assertEqual(report['status'], 'passed')
        for button in nav.ACTIONS:
            if button == 'A':
                continue
            with self.subTest(button=button, duration=.2):
                self.config['routes']['model']['ftpd'] = {
                    'action': button, 'duration': .2, 'expected': ['model']}
                send = Mock()
                report = nav.navigate(self.config, self.root, 'model', FakeFrames(),
                                      self.output, send, Mock())
                self.assertEqual(report['status'], 'failed')
                send.assert_not_called()
        for button, duration in [('A', .4), ('HOME', .4), ('LEFT', .4), ('A', .3)]:
            with self.subTest(button=button, duration=duration):
                self.config['routes']['model']['ftpd'] = {
                    'action': button, 'duration': duration, 'expected': ['model']}
                send = Mock()
                report = nav.navigate(self.config, self.root, 'model', FakeFrames(),
                                      self.output, send, Mock())
                self.assertEqual(report['status'], 'failed')
                send.assert_not_called()

    def test_ftpd_requires_handshake_in_addition_to_new_frames(self):
        self.states['ftpd']['ftp_ready'] = True
        self.states['ftpd']['settle_seconds'] = 0
        readiness = Mock(return_value=False)
        clock = [0.0]
        with patch.object(nav.time, 'monotonic', side_effect=lambda: clock[0]), \
             patch.object(nav.time, 'sleep', side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
            with self.assertRaises(RuntimeError):
                nav.wait_state(FakeFrames(), self.config, self.root, {'top': 0, 'bottom': 0},
                               ['ftpd'], 0.025, readiness)
        readiness.assert_called_once()

    def test_candidate_change_restarts_settle_and_requires_new_frames(self):
        frames = FakeFrames()
        snapshot = frames.snapshot
        clock = [0.0]
        def changing_snapshot():
            frames.color = 'blue' if frames.count in (2, 3) else 'red'
            return snapshot()
        frames.snapshot = changing_snapshot
        with patch.object(nav.time, 'monotonic', side_effect=lambda: clock[0]), \
             patch.object(nav.time, 'sleep', side_effect=lambda seconds: clock.__setitem__(0, clock[0] + 0.05)):
            state, counters, latest = nav.wait_state(
                frames, self.config, self.root, {'top': 0, 'bottom': 0}, ['ftpd'], 1, Mock())
        self.assertEqual(state, 'ftpd')
        self.assertGreaterEqual(clock[0], 0.4)
        self.assertGreater(counters['bottom'], 5)

    def test_receiver_bind_failure_closes_socket(self):
        udp = Mock()
        udp.bind.side_effect = OSError('address in use')
        with patch.object(nav.socket, 'socket', return_value=udp):
            with self.assertRaisesRegex(OSError, 'address in use'):
                nav.Frames('192.0.2.1', '192.0.2.2')
        udp.close.assert_called_once()

    def test_cli_receiver_failure_records_manifest_without_input(self):
        self.config.update(host='192.0.2.1', bind='192.0.2.2')
        config_path = self.root / 'config.json'
        config_path.write_text(json.dumps(self.config))
        output = self.root / 'new-evidence'
        with patch.object(nav, 'Frames', side_effect=OSError('address in use')), \
             patch.object(nav.luma_input, 'send_action') as send, \
             patch('builtins.print'):
            result = nav.main(['--config', str(config_path), '--target', 'model',
                               '--output', str(output)])
        self.assertEqual(result, 1)
        report = json.loads((output / 'manifest.json').read_text())
        self.assertEqual(report['status'], 'failed')
        self.assertIn('address in use', report['error'])
        send.assert_not_called()

    def test_failed_neutral_release_stops_route(self):
        dispatch = Mock(return_value={'status': 'release_error'})
        report = nav.navigate(self.config, self.root, 'model', FakeFrames(), self.output, dispatch, Mock())
        self.assertEqual(report['status'], 'failed')
        self.assertEqual(dispatch.call_count, 1)
        self.assertIn('release_error', report['error'])


@unittest.skipIf(Image is None, "Pillow not installed")
class PublishedFramesTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        image = Image.new('RGB', (400, 480), 'red')
        image.paste('green', (0, 240, 400, 480))
        image.paste('blue', (40, 240, 360, 480))
        image.save(self.root / 'both.png')
        stamp = datetime.datetime.now(datetime.timezone.utc).isoformat()
        self.metadata = {screen: {'received_utc': stamp, 'counter': 10, 'frame_id': 255}
                         for screen in ('top', 'bottom')}
        self.publish()

    def publish(self):
        (self.root / 'frames.json').write_text(json.dumps(self.metadata))
        # Explicit ordering avoids filesystem timestamp-resolution assumptions.
        now = time.time_ns()
        os.utime(self.root / 'both.png', ns=(now - 1000000, now - 1000000))
        os.utime(self.root / 'frames.json', ns=(now, now))

    def test_snapshot_crops_and_protocol_without_sockets_or_cached_fallback(self):
        with patch.object(nav.socket, 'socket') as socket, \
             patch.object(nav.socket, 'create_connection') as tcp:
            frames = nav.PublishedFrames(self.root)
            counters, latest = frames.snapshot()
            frames.close()
            self.assertEqual(counters, {'top': 10, 'bottom': 10})
            self.assertEqual(latest['top'][0].size, (400, 240))
            self.assertEqual(latest['bottom'][0].size, (320, 240))
            self.assertEqual(latest['bottom'][0].getpixel((0, 0)), (0, 0, 255))
            self.assertLessEqual(time.monotonic() - latest['top'][1], 1)
            (self.root / 'frames.json').unlink()
            with self.assertRaises(RuntimeError):
                frames.snapshot()
            self.assertIsNotNone(frames.error)
            socket.assert_not_called()
            tcp.assert_not_called()

    def test_freshness_and_metadata_validation(self):
        for field, value in [('received_utc', (datetime.datetime.now(datetime.timezone.utc)
                                              - datetime.timedelta(seconds=5)).isoformat()),
                             ('received_utc', (datetime.datetime.now(datetime.timezone.utc)
                                              + datetime.timedelta(seconds=5)).isoformat()),
                             ('received_utc', datetime.datetime.now().isoformat()),
                             ('counter', True), ('counter', 0), ('frame_id', True),
                             ('frame_id', 256)]:
            with self.subTest(field=field, value=value):
                original = self.metadata['bottom'][field]
                self.metadata['bottom'][field] = value
                self.publish()
                with self.assertRaises(RuntimeError):
                    nav.PublishedFrames(self.root).snapshot()
                self.metadata['bottom'][field] = original
        self.publish()
        Image.new('RGB', (320, 240)).save(self.root / 'both.png')
        self.publish()
        with self.assertRaisesRegex(RuntimeError, '400x480'):
            nav.PublishedFrames(self.root).snapshot()

    def test_incoherent_publication_is_bounded_and_never_returned(self):
        reader = nav.PublishedFrames(self.root)
        # PNG is known to precede metadata in the publisher's replacement order.
        now = time.time_ns()
        os.utime(self.root / 'both.png', ns=(now + 1000000, now + 1000000))
        with patch.object(nav.time, 'sleep') as sleep:
            with self.assertRaisesRegex(RuntimeError, 'racing'):
                reader.snapshot()
        self.assertEqual(sleep.call_count, 12)
        self.publish()
        original = Path.read_bytes
        calls = [0]
        def changed_metadata(path):
            payload = original(path)
            if path.name == 'frames.json':
                calls[0] += 1
                return payload + (b' ' if calls[0] % 2 else b'')
            return payload
        with patch.object(Path, 'read_bytes', changed_metadata), patch.object(nav.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'racing'):
                reader.snapshot()
        self.assertEqual(calls[0], 24)
        original_stat = Path.stat
        calls = [0]
        def changed_png(path, *args, **kwargs):
            result = original_stat(path, *args, **kwargs)
            if path.name == 'both.png':
                calls[0] += 1
                return SimpleNamespace(st_mtime_ns=result.st_mtime_ns + calls[0])
            return result
        with patch.object(Path, 'stat', changed_png), patch.object(nav.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'racing'):
                reader.snapshot()
        self.assertEqual(calls[0], 24)

    def test_published_snapshot_requires_rising_counters_before_state_acceptance(self):
        Image.new('RGB', (320, 240), 'blue').save(self.root / 'blue.png')
        config = {'states': {'blue': {'reference': 'blue.png', 'box': [0, 0, 320, 240],
                                      'threshold': 1, 'settle_seconds': 0}}}
        frames = nav.PublishedFrames(self.root)
        with self.assertRaises(RuntimeError):
            nav.wait_state(frames, config, self.root, {'top': 10, 'bottom': 10},
                           ['blue'], time.monotonic() + .03, Mock())
        # A single publication above baseline is insufficient: stabilization
        # also requires another counter rise after the candidate snapshot.
        with self.assertRaises(RuntimeError):
            nav.wait_state(frames, config, self.root, {'top': 9, 'bottom': 9},
                           ['blue'], time.monotonic() + .03, Mock())

    @staticmethod
    def denied(code=None, number=None):
        error = PermissionError(number, 'publication sharing fixture')
        if code is not None:
            error.winerror = code
        return error

    def test_windows_transient_read_and_stat_errors_retry_full_snapshot(self):
        for method in ('read_bytes', 'stat'):
            for code, number in [(5, None), (32, None), (None, errno.EACCES)]:
                with self.subTest(method=method, code=code, errno=number):
                    self.publish()
                    original = getattr(Path, method)
                    calls = [0]
                    def transient(path, *args, **kwargs):
                        if path.name == 'frames.json':
                            calls[0] += 1
                            if calls[0] == 1:
                                raise self.denied(code, number)
                        return original(path, *args, **kwargs)
                    with patch.object(nav, 'os', SimpleNamespace(name='nt')), \
                         patch.object(Path, method, transient), patch.object(nav.time, 'sleep') as sleep:
                        counters, _ = nav.PublishedFrames(self.root).snapshot()
                    self.assertEqual(counters['bottom'], 10)
                    self.assertGreaterEqual(calls[0], 2)
                    sleep.assert_called_once_with(.01)

    def test_persistent_and_nonwindows_errors_fail_with_exact_bounds(self):
        for platform, code, number, attempts in [('nt', 5, None, 12), ('nt', 32, None, 12),
                                                ('nt', None, errno.EACCES, 12),
                                                ('posix', None, errno.EACCES, 1),
                                                ('nt', 87, errno.EACCES, 1),
                                                ('nt', None, errno.EIO, 1)]:
            with self.subTest(platform=platform, code=code, errno=number):
                calls = [0]
                def persistent(path):
                    calls[0] += 1
                    raise self.denied(code, number)
                with patch.object(nav, 'os', SimpleNamespace(name=platform)), \
                     patch.object(Path, 'read_bytes', persistent), patch.object(nav.time, 'sleep'):
                    with self.assertRaises(RuntimeError):
                        nav.PublishedFrames(self.root).snapshot()
                self.assertEqual(calls[0], attempts)

    def test_sharing_retry_does_not_admit_stale_frames(self):
        self.metadata['top']['received_utc'] = (datetime.datetime.now(datetime.timezone.utc)
                                                - datetime.timedelta(seconds=5)).isoformat()
        self.publish()
        original = Path.read_bytes
        calls = [0]
        def transient(path):
            calls[0] += 1
            if calls[0] == 1:
                raise self.denied(None, errno.EACCES)
            return original(path)
        with patch.object(nav, 'os', SimpleNamespace(name='nt')), \
             patch.object(Path, 'read_bytes', transient), patch.object(nav.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'stale'):
                nav.PublishedFrames(self.root).snapshot()

    def test_cli_live_dir_selects_reader_and_preserves_explicit_ftp_readiness(self):
        config = {'host': '192.0.2.1', 'bind': '192.0.2.2',
                  'states': {'done': {'ftp_ready': True}}, 'routes': {'done': {}}}
        path = self.root / 'config.json'
        path.write_text(json.dumps(config))
        frames = Mock()
        def run(config, root, target, reader, output, dispatch, readiness):
            self.assertIs(reader, frames)
            self.assertTrue(readiness())  # Explicit FTP gate retains its existing callback.
            return {'status': 'passed'}
        with patch.object(nav, 'PublishedFrames', return_value=frames) as published, \
             patch.object(nav, 'Frames') as udp, patch.object(nav, 'navigate', side_effect=run), \
             patch.object(nav, 'ftp_ready', return_value=True) as readiness, \
             patch.object(nav.socket, 'socket') as socket, patch.object(nav.socket, 'create_connection') as tcp, \
             patch.object(nav.luma_input, 'send_action') as send, patch('builtins.print'):
            result = nav.main(['--config', str(path), '--target', 'done', '--output',
                               str(self.root / 'cli-evidence'), '--live-dir', str(self.root)])
        self.assertEqual(result, 0)
        published.assert_called_once_with(self.root)
        readiness.assert_called_once_with('192.0.2.1', '192.0.2.2')
        frames.close.assert_called_once()
        udp.assert_not_called()
        socket.assert_not_called()
        tcp.assert_not_called()
        send.assert_not_called()

    def test_cli_missing_publication_records_failure_and_closes_without_input(self):
        config = {'host': '192.0.2.1', 'bind': '192.0.2.2',
                  'states': {'done': {}}, 'routes': {'done': {}}}
        path = self.root / 'config.json'
        path.write_text(json.dumps(config))
        frames = nav.PublishedFrames(self.root / 'absent')
        with patch.object(nav, 'PublishedFrames', return_value=frames), \
             patch.object(frames, 'close') as close, patch.object(nav.socket, 'socket') as socket, \
             patch.object(nav.luma_input, 'send_action') as send, patch('builtins.print'):
            result = nav.main(['--config', str(path), '--target', 'done', '--output',
                               str(self.root / 'failed-evidence'), '--live-dir', str(self.root / 'absent')])
        self.assertEqual(result, 1)
        report = json.loads((self.root / 'failed-evidence/manifest.json').read_text())
        self.assertEqual(report['status'], 'failed')
        close.assert_called_once()
        socket.assert_not_called()
        send.assert_not_called()


if __name__ == '__main__':
    unittest.main()
