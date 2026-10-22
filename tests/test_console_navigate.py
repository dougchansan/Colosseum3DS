"""Offline synthetic navigation checks; never contact a console."""
import importlib.util
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
        self.assertEqual(len(list(self.output.glob('*.png'))), 4)
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

    def test_missing_route_and_forbidden_action_do_not_send(self):
        dispatch = Mock()
        for edge in ({'action': 'POWER', 'expected': ['model']}, None):
            self.config['routes']['model'] = {} if edge is None else {'ftpd': edge}
            report = nav.navigate(self.config, self.root, 'model', FakeFrames(), self.output, dispatch, Mock())
            self.assertEqual(report['status'], 'failed')
            dispatch.assert_not_called()

    def test_ftpd_requires_handshake_in_addition_to_new_frames(self):
        self.states['ftpd']['ftp_ready'] = True
        self.states['ftpd']['settle_seconds'] = 0
        readiness = Mock(return_value=False)
        with self.assertRaises(RuntimeError):
            nav.wait_state(FakeFrames(), self.config, self.root, {'top': 0, 'bottom': 0},
                           ['ftpd'], time.monotonic() + 0.025, readiness)
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


if __name__ == '__main__':
    unittest.main()
