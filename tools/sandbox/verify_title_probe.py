#!/usr/bin/env python3
"""Check actual captured pixels from the native texture/TEV graphics probes."""
import argparse
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("kind", choices=("corners", "combiner", "stage", "arrival", "arrival-fade"))
parser.add_argument("capture")
args = parser.parse_args()
expected = ((255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0))
labels = ("top left", "top right", "bottom left", "bottom right")
positions = ((150, 70), (250, 70), (150, 170), (250, 170))
if args.kind == "combiner":
    expected = ((204, 51, 51), (51, 204, 51), (51, 51, 204), (204, 204, 51))
elif args.kind == "stage":
    # The second independent UV channel reverses only the corner texture.
    # RGB is tint * corner * (127/255)^2, then destination-source or
    # destination+source over the separate opaque gray model pack. One byte
    # tolerance accounts for per-stage fixed-point GPU quantization.
    tinted = ((0, 32, 0), (63, 0, 0), (63, 32, 0), (0, 0, 16))
    expected = tuple(tuple(128 - c for c in rgb) for rgb in tinted) + \
               tuple(tuple(128 + c for c in rgb) for rgb in tinted)
    labels = tuple(f"{mode} {corner}" for mode in ("subtract", "add") for corner in labels)
    positions = tuple((x, y) for y in (60, 100, 140, 180) for x in (150, 250))
elif args.kind in ("arrival", "arrival-fade"):
    # GX shininess50: t^2/(25-24*t^2). The first normal gives t=.8,
    # rounding the light channel to17/255. The second owning joint is at
    # (2,0,0), giving t=cos(pi/8) and a48/255 light channel. Add each to64
    # after multiplication by material specular(255,128,64)/255.
    # Reflection normals select the green corner, or red after(u,v)->(v,1-u).
    labels = ("GX rational highlight", "alpha zero rejection", "joint-origin highlight",
              "normal-generated reflection", "affine reflection rotation")
    positions = ((150,70),(220,70),(280,70),(150,170),(250,170))
    expected = ((81,73,68),(0,0,0),(112,88,76),(32,160,32),(160,32,32))
    if args.kind == "arrival-fade":
        # First normal now faces the half vector exactly. Diffuse+specular
        # saturates to(255,192,128) BEFORE the global half-brightness overlay.
        expected = ((127,96,64),(0,0,0),(56,44,38),(16,80,16),(80,16,16))
with Image.open(args.capture) as image:
    if image.size != (400, 480):
        raise SystemExit("Expected the harness's native 400x480 combined screen capture")
    image = image.convert("RGB")
    for label, xy, reference in zip(labels, positions, expected):
        actual = image.getpixel(xy)
        if any(abs(a - b) > 1 for a, b in zip(actual, reference)):
            raise SystemExit(f"FAIL {label}: {actual}, expected {reference}")
        print(f"PASS {label}: {actual}")
