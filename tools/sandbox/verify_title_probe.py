#!/usr/bin/env python3
"""Check actual captured pixels from the native texture/TEV graphics probes."""
import argparse
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("kind", choices=("corners", "combiner"))
parser.add_argument("capture")
args = parser.parse_args()
expected = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0))
if args.kind == "combiner":
    expected = ((204, 51, 51), (51, 204, 51), (51, 51, 204), (204, 204, 51))
with Image.open(args.capture) as image:
    if image.size != (400, 480):
        raise SystemExit("Expected the harness's native 400x480 combined screen capture")
    image = image.convert("RGB")
    for label, xy, reference in zip(
            ("top left", "top right", "bottom left", "bottom right"),
            ((150, 70), (250, 70), (150, 170), (250, 170)), expected):
        actual = image.getpixel(xy)
        if any(abs(a - b) > 1 for a, b in zip(actual, reference)):
            raise SystemExit(f"FAIL {label}: {actual}, expected {reference}")
        print(f"PASS {label}: {actual}")
