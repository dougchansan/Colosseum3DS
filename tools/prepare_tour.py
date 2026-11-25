#!/usr/bin/env python3
"""Prepare full field packs (map, objects, Wes, NPCs) for the warp tour.

For every surveyed floor without reviewed packs (C3_FLOOR_SURVEY set) this
runs prepare_floor.prepare (auto_geometry) and prepare_area with NPC models,
falling back to model-free actors when an NPC pack cannot be prepared, then
writes the floor's C3A with prepare_area_runtime. The story packs and area
metadata come from prepare_story.py in the same BOOT directory. Failures are
reported per floor and skipped. Everything stays in a private directory.
"""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

import prepare_title as title
from floor_profiles import FLOORS


def tour_floors():
    return sorted(name for name, row in FLOORS.items() if row.get("auto") and row.get("map") and row.get("script"))


def prepare_tour(disc, boot, parser, names=None, approximate_all=False):
    import prepare_field_geometry as geometry
    from prepare_area import prepare_floor_actors
    from prepare_floor import prepare as prepare_floor
    import prepare_area_runtime as runtime

    boot = Path(boot)
    metadata = {key: json.loads((boot / file).read_text()) for key, file in
                (("links", "area-events.json"), ("lifecycle", "area-lifecycle.json"), ("names", "actor-names.json"))}
    report = {"floors": [], "failures": []}
    for name in names or tour_floors():
        profile = FLOORS[name]
        row = {"floor_profile": name, "floor": profile["floor"]}
        # Surveyed floors only: recorded approximations instead of stopping
        # on unreviewed materials and lights (prepare_field_geometry).
        geometry.APPROXIMATE["enabled"] = bool(profile.get("auto")) or approximate_all
        geometry.APPROXIMATIONS.clear()
        title.APPROXIMATE_HOOK = geometry.approximate if profile.get("auto") or approximate_all else None
        with tempfile.TemporaryDirectory(prefix=".tour-", dir=boot.parent) as temporary:
            stage = Path(temporary)
            try:
                prepare_floor(disc, stage / "floor", parser, name)
            except (OSError, ValueError, KeyError, IndexError, ZeroDivisionError) as error:
                report["failures"].append({**row, "step": "floor", "reason": str(error)})
                continue
            area_dir, row["npc_models"] = stage / "area", True
            try:
                prepare_floor_actors(disc, area_dir, parser, name, models=True)
            except (OSError, ValueError, KeyError, IndexError, ZeroDivisionError) as error:
                row["npc_models"], row["npc_reason"] = False, str(error)
                shutil.rmtree(area_dir, ignore_errors=True)
                try:
                    prepare_floor_actors(disc, area_dir, parser, name, models=False)
                except (OSError, ValueError, KeyError, IndexError) as error2:
                    report["failures"].append({**row, "step": "actors", "reason": str(error2)})
                    continue
            areas = json.loads((area_dir / "area-actors.json").read_text())
            try:
                files, _ = runtime.prepare(areas, metadata["links"], metadata["lifecycle"], metadata["names"])
            except (ValueError, KeyError, IndexError) as error:
                report["failures"].append({**row, "step": "runtime", "reason": str(error)})
                continue
            for directory in (stage / "floor", area_dir):
                for item in directory.iterdir():
                    if item.suffix in (".c3t", ".c3f", ".c3c", ".c3v"):
                        shutil.copyfile(item, boot / item.name)
            for file, data in files.items():
                if file.endswith(".c3a"):
                    (boot / file).write_bytes(data)
            row["approximations"] = dict(geometry.APPROXIMATIONS)
            report["floors"].append(row)
        print("%s %s" % (name, "ok" if row in report["floors"] else "failed"), flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--boot", required=True, type=Path, help="directory holding prepare_story.py output")
    parser.add_argument("--parser", type=Path, default=title.ROOT / "vendor/hsd-importer")
    parser.add_argument("--floor", action="append", help="Floor profile (repeatable; default every surveyed floor)")
    parser.add_argument("--approximate", action="store_true",
                        help="Also record approximations on reviewed floors (emulator campaign packs)")
    args = parser.parse_args()
    report = prepare_tour(args.disc, args.boot, args.parser, args.floor, args.approximate)
    (args.boot / ("tour-manifest.json" if not args.floor else "tour-manifest-partial.json")).write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({"floors": len(report["floors"]),
                      "without_npc_models": [r["floor_profile"] for r in report["floors"] if not r["npc_models"]],
                      "failures": report["failures"]}, indent=1))


if __name__ == "__main__":
    main()
