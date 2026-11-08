import struct
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import prepare_ringtones as ring


class RingtoneTests(unittest.TestCase):
    def song(self):
        data = bytearray(512)
        struct.pack_into('>6I', data, 0, 24, 280, 284, 0, 120, 0)
        struct.pack_into('>I', data, 24, 300)
        struct.pack_into('>I', data, 280, 324)
        struct.pack_into('>IBBHHbb', data, 300, 0, 255, 255, 0, 0, 0, 0)
        struct.pack_into('>IBBHHbb', data, 312, 0, 255, 255, 0, 65535, 0, 0)
        struct.pack_into('>3I', data, 324, 0, 0, 0)
        struct.pack_into('>HBBH', data, 336, 384, 60, 100, 192)
        struct.pack_into('>HBB', data, 342, 0, 255, 255)
        return data

    def test_original_tick_units_and_note_duration(self):
        note, = ring.parse_song(self.song())
        self.assertEqual(note, (.5, .25, 0, 60, 100, 0))

    def test_bounds_and_unknown_streams_fail_closed(self):
        with self.assertRaises(ValueError):
            ring.parse_song(b'')
        data = self.song()
        struct.pack_into('>I', data, 328, 20)
        with self.assertRaises(ValueError):
            ring.parse_song(data)

    def test_source_tempo_map_changes_note_duration(self):
        data = self.song()
        struct.pack_into('>I', data, 12, 400)
        struct.pack_into('>4I', data, 400, 0, 120, 480, 60)
        struct.pack_into('>I', data, 416, 0xffffffff)
        note, = ring.parse_song(data)
        self.assertEqual(note[:2], (.5, .375))

    def test_controls_are_preserved_with_source_times(self):
        data = self.song()
        # Volume 64 and expression 32, both at tick zero, then the note.
        struct.pack_into('>HBBHBB', data, 336, 0, 192, 135, 0, 160, 139)
        struct.pack_into('>HBBH', data, 344, 384, 60, 100, 192)
        struct.pack_into('>HBB', data, 350, 0, 255, 255)
        song = ring.parse_song(data)
        self.assertEqual(song.control(0, .5)[7], 64)
        self.assertEqual(song.control(0, .5)[11], 32)
        self.assertEqual(song[0][4], 100)  # velocity remains separate from controls

    def test_unknown_controller_is_not_silently_ignored(self):
        data = self.song()
        struct.pack_into('>HBB', data, 336, 0, 128, 227)
        with self.assertRaisesRegex(ValueError, 'controller 99'):
            ring.parse_song(data)

    def test_truncated_note_extent_fails_closed(self):
        with self.assertRaises(ValueError):
            ring.parse_song(self.song()[:341])

    def test_release_uses_keyoff_level_without_arbitrary_tail_cap(self):
        env = ring.envelope_ms(struct.pack('<4H', 1000, 0, 4096, 1000), None, {}, 100, None)
        self.assertGreater(len(env), 1000)
        self.assertAlmostEqual(env[100], .1, places=5)
        self.assertLessEqual(max(env[100:]), env[100])
        self.assertEqual(env[-1], 0)

    def test_unknown_macro_opcode_fails_closed(self):
        bank = object.__new__(ring.Instruments)
        bank.mac = {7: (0, struct.pack('>II', 0x55, 0))}
        with self.assertRaisesRegex(ValueError, 'opcode 55'):
            list(bank.voices(7, 60, 1))

    def test_source_ui_binding(self):
        self.assertEqual(ring.SOUNDS, (13, 14, 15, 17, 16, 18, 19, 20, 21, 22))
        self.assertEqual(ring.RESOURCE_ORDER[3:5], (4, 3))
        self.assertEqual(len(set(ring.RESOURCES)), 10)

if __name__ == '__main__':
    unittest.main()
