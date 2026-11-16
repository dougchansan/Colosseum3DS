"""Synthetic audit regressions; no disc, dialogue, or private packs required."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import audit_under_story as audit
from prepare_story import encode_pack


def messages(payload):
    data = bytearray(24)
    struct.pack_into('>H', data, 4, 1)
    struct.pack_into('>II', data, 16, 123, 24)
    return bytes(data)+payload


class UnderAuditTests(unittest.TestCase):
    def test_scope_is_exact_and_disjoint(self):
        self.assertEqual((len(audit.FLOORS_UNDER), len(audit.WALKABLE), len(audit.SPECIAL)), (26, 19, 7))
        self.assertEqual(len(set(audit.FLOORS_UNDER)), 26)
        self.assertNotIn(0x38, audit.FLOORS_UNDER)
        self.assertNotIn(0x7e, audit.FLOORS_UNDER)

    def test_pack_identity_and_extent(self):
        pack = encode_pack(0x2f, [b'\x01\x00'], [messages(b'\x00\x00')])
        self.assertEqual([kind for kind, _ in audit.pack_entries(pack, 0x2f)], ['script', 'messages'])
        with self.assertRaisesRegex(ValueError, 'header'):
            audit.pack_entries(pack, 0x30)
        with self.assertRaises(ValueError):
            audit.pack_entries(pack[:-1], 0x2f)

    def test_pack_rejects_overlapping_original_entries(self):
        pack = bytearray(encode_pack(0x2f, [b'\x01\x00'], [b'\x01\x00']))
        struct.pack_into('<I', pack, 84, struct.unpack_from('<I', pack, 68)[0])
        with self.assertRaisesRegex(ValueError, 'Overlapping'):
            audit.pack_entries(pack, 0x2f)

    def test_font_control_parameter_is_not_another_control(self):
        group = audit.message_inventory(messages(b'\xff\xff\x07\xff\x00A\xff\xff\x2b\x00\x00'))
        self.assertEqual(set(group['controls']), {'07', '2b'})
        self.assertEqual(group['structural_issues'], [])
        self.assertNotIn('text', group)

    def test_truncated_message_is_reported(self):
        group = audit.message_inventory(messages(b'\xff\xff\x07'))
        self.assertTrue(group['structural_issues'])

    def test_unknown_control_stops_before_unknown_payload(self):
        group = audit.message_inventory(messages(b'\xff\xff\xff\xff\xff\xff\x00\x00'))
        self.assertEqual(set(group['controls']), {'ff'})
        self.assertTrue(group['scan_stopped_at_unsupported_control'])
        self.assertEqual(group['structural_issues'], [])

    def test_source_dispatch_is_observation(self):
        natives, controls = audit.source_support()
        self.assertIn(7, natives)
        self.assertNotIn(0xbe, natives)
        self.assertIn(0x2b, controls)
        self.assertNotIn(0xff, controls)

    def test_color_and_wait_parameters_are_not_controls(self):
        group = audit.message_inventory(messages(b'\xff\xff\x08\xff\xff\xff\xff\xff\xff\x09\xff\x00\x00'))
        self.assertEqual(set(group['controls']), {'08', '09'})
        self.assertEqual(group['structural_issues'], [])


if __name__ == '__main__':
    unittest.main()
