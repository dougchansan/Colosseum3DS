"""C3H1 shop packs as src/shop.c c3_shop_table_open reads them. Synthetic only."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_shop as shop


class ShopPackTests(unittest.TestCase):
    def test_layout(self):
        entries = [bytes([2, 0]) + struct.pack(">H", 0), bytes([1, 1]) + struct.pack(">H", 3)]
        lists = [13, 22, 0, 26, 0]
        pack = shop.encode(entries, lists)
        self.assertEqual(struct.unpack_from("<4s4I", pack), (b"C3H1", 1, 20 + 2 * 4 + 5 * 2, 2, 5))
        self.assertEqual(pack[20:28], bytes([2, 0, 0, 0, 1, 1, 3, 0]))  # list offsets little-endian
        self.assertEqual(struct.unpack_from("<5H", pack, 28), (13, 22, 0, 26, 0))


if __name__ == "__main__":
    unittest.main()
