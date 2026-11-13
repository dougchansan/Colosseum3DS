"""Steady layers for flickering translucent overlays (synthetic tracks only)."""
from pathlib import Path
import sys
import unittest

try:
    import numpy as np
except ImportError:
    np = None

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
if np is not None:
    import prepare_field_geometry as geometry


def material(offset_t, alpha=0.5, offset_s=0.0, visible=1.0):
    # transform0 (scale s, scale t, offset s, offset t), alpha, visible, tev0, konst, ...
    return (1.0, 1.0, offset_s, offset_t, alpha, visible, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0)


@unittest.skipIf(np is None, "numpy required")
class SteadyShimmerTests(unittest.TestCase):
    def run_layers(self, track):
        mesh = {"index": 7}
        tracks = {id(mesh): track}
        meshes, report = geometry.steady_shimmer_layers([mesh], tracks)
        return mesh, meshes, tracks, report

    def test_flip_book_becomes_weighted_steady_layers(self):
        # Two rows switching every second sample, two brief rare rows, as on
        # Phenac's sign glows; the samples wander slightly around the 0.25 row.
        track = [material(0.0 if (i // 2) % 2 == 0 else 0.244 + (i % 3) * 0.003) for i in range(96)]
        track[40:42] = [material(0.5)] * 2
        track[60:62] = [material(0.75)] * 2
        mesh, meshes, tracks, report = self.run_layers(track)
        self.assertEqual(len(meshes), 2)
        self.assertNotIn(mesh, meshes)
        self.assertEqual([m["steady_shimmer"] for m in meshes], [(0.0, 0.0), (0.0, 0.25)])
        for layer in meshes:
            offsets = {(m[2], m[3]) for m in tracks[id(layer)]}
            self.assertEqual(offsets, {layer["steady_shimmer"]})
        shares = [entry["share"] for entry in report[0]["layers"]]
        self.assertAlmostEqual(sum(shares), 92 / 96, places=3)
        self.assertAlmostEqual(tracks[id(meshes[0])][0][4], 0.5 * shares[0], places=3)
        self.assertEqual([d["offset"] for d in report[0]["dropped"]], [[0.0, 0.5], [0.0, 0.75]])
        # Visibility and the other values keep their per-sample tracks.
        self.assertEqual(tracks[id(meshes[1])][5][5:], track[5][5:])

    def test_smooth_scroll_opaque_and_steady_meshes_are_unchanged(self):
        scroll = [material(-2.0 + i / 32) for i in range(96)]          # water: many values
        opaque = [material(0.0 if i % 2 else 0.25, alpha=1.0) for i in range(96)]
        steady = [material(0.25) for _ in range(96)]
        rare = [material(0.25 if i in (10, 50) else 0.0) for i in range(96)]  # changes too seldom
        for track in (scroll, opaque, steady, rare):
            mesh, meshes, tracks, report = self.run_layers(track)
            self.assertEqual(meshes, [mesh])
            self.assertEqual(report, [])
            self.assertIs(tracks[id(mesh)], track)


if __name__ == "__main__":
    unittest.main()
