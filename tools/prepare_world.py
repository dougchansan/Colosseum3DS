#!/usr/bin/env python3
"""Prepare the headless whole-game story data for every surveyed floor.

Runs after prepare_story.py has written the story packs and area metadata
(area-events.json, area-lifecycle.json, actor-names.json) into BOOT for the
same floor list. For each story floor this adds, without any model:

  <prefix>.c3f  original collision, entrances and REL field data
                (prepare_floor.prepare_field_only)
  <prefix>.c3a  actor records, event surfaces and links
                (prepare_area.prepare_floor_actors(models=False), then
                 prepare_area_runtime.prepare over all floors)

Floors that fail are reported with their reason and skipped; the rest are
still written. Set C3_FLOOR_SURVEY to the floor_survey.py output so the
surveyed floors are profiled. Everything goes to a private directory.
"""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

import prepare_title as title
from floor_profiles import story_floors


def prepare_world(disc, boot, parser, names=None):
    from prepare_area import prepare_floor_actors
    from prepare_floor import prepare_field_only
    import prepare_area_runtime as runtime

    boot = Path(boot)
    floors = story_floors()
    names = sorted(names or floors)
    areas, report = [], {"fields": [], "areas": [], "failures": []}
    with tempfile.TemporaryDirectory(prefix=".world-", dir=boot.parent) as temporary:
        for name in names:
            profile = floors[name]
            try:
                if (boot / (profile["prefix"] + ".c3f")).exists():
                    raise ValueError("field output exists")
                field = prepare_field_only(disc, Path(temporary) / (name + "-field"), parser, name)
                shutil.move(str(Path(temporary) / (name + "-field") / field["file"]), str(boot / field["file"]))
                report["fields"].append({"floor_profile": name, "file": field["file"], "bytes": field["bytes"]})
            except (OSError, ValueError, KeyError, IndexError) as error:
                report["failures"].append({"floor_profile": name, "step": "field", "reason": str(error)})
            try:
                prepare_floor_actors(disc, Path(temporary) / (name + "-actors"), parser, name, models=False)
                areas += json.loads((Path(temporary) / (name + "-actors") / "area-actors.json").read_text())
            except (OSError, ValueError, KeyError, IndexError) as error:
                report["failures"].append({"floor_profile": name, "step": "actors", "reason": str(error)})
    metadata = {key: json.loads((boot / file).read_text()) for key, file in
                (("links", "area-events.json"), ("lifecycle", "area-lifecycle.json"), ("names", "actor-names.json"))}
    for area in areas:
        try:
            files, reports = runtime.prepare([area], metadata["links"], metadata["lifecycle"], metadata["names"])
        except (ValueError, KeyError, IndexError) as error:
            report["failures"].append({"floor": area["floor"], "step": "runtime", "reason": str(error)})
            continue
        for file, data in files.items():
            if (boot / file).exists():
                raise ValueError("area output exists: " + file)
            (boot / file).write_bytes(data)
        report["areas"] += reports
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--boot", required=True, type=Path, help="prepare_story.py output directory")
    parser.add_argument("--parser", type=Path, default=title.ROOT / "vendor/hsd-importer")
    parser.add_argument("--floor", action="append", help="Floor profile (repeatable; default all story floors)")
    args = parser.parse_args()
    report = prepare_world(args.disc, args.boot, args.parser, args.floor)
    (args.boot / "world-manifest.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({"fields": len(report["fields"]), "areas": len(report["areas"]),
                      "failures": report["failures"]}, indent=1))


if __name__ == "__main__":
    main()
