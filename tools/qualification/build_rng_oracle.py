#!/usr/bin/env python3
"""Build the RNG sidecar against an existing Linux ModernGekko Ninja build.

This does not configure, update or rebuild the external dependency. Its exact
local inputs are recorded, including dirty source state; it does not assert the
old semantic pilot's pristine dependency attestation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    build = args.build.resolve()
    cache = (build / "CMakeCache.txt").read_text()
    home = next(line.split("=", 1)[1] for line in cache.splitlines()
                if line.startswith("CMAKE_HOME_DIRECTORY:"))
    home = Path(home)
    ninja = (build / "build.ninja").read_text()
    marker = "build vendor/dolphin/Source/Core/Core/CMakeFiles/core.dir/PowerPC/Interpreter/Interpreter.cpp.o:"
    block = ninja[ninja.index(marker):].split("\n\n", 1)[0]
    includes = shlex.split(next(line.split(" = ", 1)[1] for line in block.splitlines()
                                 if line.startswith("  INCLUDES = ")))
    defines = shlex.split(next(line.split(" = ", 1)[1] for line in block.splitlines()
                                if line.startswith("  DEFINES = ")))
    # Pin the real component identities into the existing sidecar's metadata.
    repositories = {"ModernGekko": home, "RecompCore": home / "vendor/dolphin",
                    "DolRecomp": home / "vendor/dolphin/DolRecomp"}
    commits = {name: subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
               for name, path in repositories.items()}
    definitions = [f'-DORACLE_{name.upper()}_COMMIT="{commit}"' for name, commit in commits.items()]
    # picojson is included directly by the sidecar, unlike most core objects.
    includes += ["-I" + str(home / "vendor/picojson")]
    command_text = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "moderngekko-run"], text=True)
    link_line = command_text.strip().splitlines()[-1]
    words = shlex.split(link_line)
    libraries = words[words.index("libmoderngekko.a"):]
    libraries = libraries[:libraries.index("&&")] if "&&" in libraries else libraries
    args.output.parent.mkdir(parents=True, exist_ok=True)
    object_path = args.output.with_suffix(".o")
    compile_command = ["/usr/bin/c++", "-std=c++23", "-O2", "-pthread", *defines, *definitions, *includes,
                       "-c", str(args.source.resolve()), "-o", str(object_path.resolve())]
    subprocess.run(compile_command, check=True, cwd=build)
    stub_source = home / "vendor/dolphin/Source/UnitTests/StubHost.cpp"
    stub_object = args.output.with_suffix(".host.o")
    stub_command = ["/usr/bin/c++", "-std=c++23", "-O2", "-pthread", *defines, *includes,
                    "-c", str(stub_source), "-o", str(stub_object.resolve())]
    subprocess.run(stub_command, check=True, cwd=build)
    link_command = ["/usr/bin/c++", "-pthread", str(object_path.resolve()), str(stub_object.resolve()),
                    "-o", str(args.output.resolve()), *libraries]
    subprocess.run(link_command, check=True, cwd=build)
    archives = {str((build / word).resolve()): digest(build / word) for word in libraries
                if word.endswith(".a") and (build / word).is_file()}
    dolphin = home / "vendor/dolphin"
    diff = subprocess.check_output(["git", "-C", str(dolphin), "diff", "--binary", "HEAD"], text=True)
    source_files = ["PowerPC/Interpreter/Interpreter.cpp", "PowerPC/Interpreter/Interpreter_Integer.cpp",
                    "PowerPC/Interpreter/Interpreter_LoadStore.cpp", "PowerPC/Interpreter/Interpreter_Branch.cpp",
                    "PowerPC/Interpreter/Interpreter_Tables.cpp"]
    hashes = {name: digest(dolphin / "Source/Core/Core" / name) for name in source_files}
    report = {"schema": 1, "commits": commits, "dependency_build": str(build),
              "engine_source_sha256": digest(args.source), "engine_binary_sha256": digest(args.output),
              "interpreter_source_sha256": hashes, "static_archives_sha256": archives,
              "dependency_diff_sha256": hashlib.sha256(diff.encode()).hexdigest(),
              "dependency_dirty": bool(diff), "compile_command": compile_command, "link_command": link_command,
              "host_stub_sha256": digest(stub_source), "host_stub_command": stub_command,
              "compiler_version": subprocess.check_output(["/usr/bin/c++", "--version"], text=True).splitlines()[0],
              "authority": "Existing locally built Dolphin interpreter; local modifications disclosed; no clean-pilot attestation"}
    args.output.with_suffix(".build.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
