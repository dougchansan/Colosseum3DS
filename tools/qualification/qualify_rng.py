#!/usr/bin/env python3
"""Run admitted original DOL RNG instructions in the separate Dolphin sidecar.

Requests/results contain original code and stay private. The optional vectors
file contains numeric observations and identities only, suitable for review.
Native API results are comparison data; they never supply Dolphin callbacks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

DOL_SHA1 = "870e8b9693ca780782d80f22a6a4572d8ba9458f"
NEXT = 0x801ADCD8
BOUNDED = 0x800E0C04
SEED = 0x81600000
SEED_POINTER = 0x80478C94
SDA = 0x80480820
RETURN = 0x817FF000
STACK = 0x81700000
ROOT = Path(__file__).resolve().parents[2]
SEEDS = (0, 1, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x12345678, 0xDEADBEEF, 0xFFFF0000)
MODULI = (1, 2, 3, 65535, 65536, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF)


def dol_read(data, address, size):
    for index in range(18):
        offset = struct.unpack_from(">I", data, index * 4)[0]
        start = struct.unpack_from(">I", data, 0x48 + index * 4)[0]
        extent = struct.unpack_from(">I", data, 0x90 + index * 4)[0]
        if start <= address and address + size <= start + extent:
            return data[offset + address - start:offset + address - start + size]
    raise ValueError("Unmapped original DOL extent")


def fixture(name, seed, modulus=None):
    registers = {"1": STACK, "13": SDA, "30": 0x30303030, "31": 0x31313131}
    if modulus is not None:
        registers["3"] = modulus
    return {"id": f"{name}-{seed:08x}" + (f"-{modulus:08x}" if modulus is not None else ""),
            "initial": {"gpr": registers, "lr": RETURN, "memory": [
                {"address": SEED_POINTER, "data_hex": struct.pack(">I", SEED).hex()},
                {"address": SEED, "data_hex": struct.pack(">I", seed).hex()}]},
            "observe": {"gpr": [1, 3, 13, 30, 31], "memory": [
                {"address": SEED, "size": 4}, {"address": SEED_POINTER, "size": 4}]}}


def compare(result, native, name, expected_id):
    if result.get("id") != expected_id:
        raise ValueError("Original fixture identity mismatch")
    if result["status"] != "returned" or int(result["final"]["pc"], 0) != RETURN:
        raise ValueError("Original instruction execution did not return: " + result["id"])
    registers = result["final"]["gpr"]
    value = int(registers["3"], 0)
    seed = int(result["final"]["memory"][0]["data_hex"], 16)
    trace = result["draw_trace"]
    expected_draws = 1 if name == "next16" else 2
    if not native["ok"] or value != native["value"] or seed != native["seed"] or len(trace) != native["draws"] or len(trace) != expected_draws:
        raise ValueError("Original/native RNG mismatch: " + result["id"])
    if int(registers["1"], 0) != STACK or int(registers["13"], 0) != SDA:
        raise ValueError("Original ABI stack/SDA changed: " + result["id"])
    if int(registers["30"], 0) != 0x30303030 or int(registers["31"], 0) != 0x31313131:
        raise ValueError("Original callee-saved registers changed: " + result["id"])
    if int(result["final"]["memory"][1]["data_hex"], 16) != SEED:
        raise ValueError("Original seed pointer changed")
    caller_returns = [int(draw["caller_return_pc"], 0) for draw in trace]
    if caller_returns != ([RETURN] if name == "next16" else [BOUNDED + 0x1C, BOUNDED + 0x24]):
        raise ValueError("Unexpected original call graph: " + result["id"])
    if any(int(draw["return_pc"], 0) != NEXT + 0x30 or
           not 0 <= int(draw["value"], 0) <= 0xFFFF or
           not 0 <= int(draw["seed_after"], 0) <= 0xFFFFFFFF for draw in trace):
        raise ValueError("Incomplete original draw trace: " + result["id"])
    if int(trace[-1]["seed_after"], 0) != seed:
        raise ValueError("Original final seed differs from final draw trace")
    leaf = list(range(NEXT, NEXT + 0x34, 4))
    expected_pcs = leaf if name == "next16" else (
        list(range(BOUNDED, BOUNDED + 0x1C, 4)) + leaf + [BOUNDED + 0x1C, BOUNDED + 0x20]
        + leaf + list(range(BOUNDED + 0x24, BOUNDED + 0x50, 4)))
    if (result["instructions"] != len(expected_pcs)
            or len(result["instruction_pcs"]) != len(expected_pcs)
            or [int(pc, 0) for pc in result["instruction_pcs"]] != expected_pcs):
        raise ValueError("Unexpected original instruction path: " + result["id"])
    return {"value": value, "final_seed": seed, "draws": len(trace),
            "instructions": result["instructions"], "draw_trace": trace}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dol", type=Path, required=True)
    parser.add_argument("--oracle", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--private-output", type=Path, required=True)
    parser.add_argument("--vectors", type=Path, required=True)
    args = parser.parse_args()
    data = args.dol.read_bytes()
    if hashlib.sha1(data).hexdigest() != DOL_SHA1:
        raise ValueError("Original DOL identity mismatch")
    args.private_output.mkdir(parents=True, exist_ok=False)
    vectors = []
    identities = {}
    for name, entry, extent in (("next16", NEXT, 0x34), ("bounded32", BOUNDED, NEXT + 0x34 - BOUNDED)):
        code = dol_read(data, entry, extent)
        cases = [(seed, None) for seed in SEEDS] if name == "next16" else [(seed, modulus) for seed in SEEDS for modulus in MODULI]
        request = {"schema_version": 1, "function": {"name": name, "entry_pc": entry,
                   "max_instructions": 128, "code_hex": code.hex()},
                   "fixtures": [fixture(name, seed, modulus) for seed, modulus in cases]}
        request_path = args.private_output / (name + "-request.json")
        result_path = args.private_output / (name + "-result.json")
        request_path.write_text(json.dumps(request))
        subprocess.run([str(args.oracle), "--request-file", str(request_path), "--result-file", str(result_path)], check=True)
        response = json.loads(result_path.read_text())
        if response["engine"] != "dolphin-interpreter-rng-qualification" or len(response["results"]) != len(cases):
            raise ValueError("Unexpected oracle engine or fixture count")
        identities[name] = {"entry": entry, "installed_text_bytes": extent,
                            "installed_text_sha256": hashlib.sha256(code).hexdigest(),
                            "function_bytes": 0x34 if name == "next16" else 0x50,
                            "function_sha256": hashlib.sha256(dol_read(data, entry, 0x34 if name == "next16" else 0x50)).hexdigest(),
                            "engine_provenance": response["provenance"]}
        for (seed, modulus), result in zip(cases, response["results"]):
            command = [str(args.native), name, hex(seed)] + ([hex(modulus)] if modulus is not None else [])
            native = json.loads(subprocess.check_output(command, text=True))
            record = {"function": name, "seed": seed}
            if modulus is not None:
                record["modulus"] = modulus
            record.update(compare(result, native, name, fixture(name, seed, modulus)["id"]))
            vectors.append(record)
    rejected = json.loads(subprocess.check_output([str(args.native), "bounded32", "0x12345678", "0"], text=True))
    if rejected != {"ok": 0, "value": 0xDEADBEEF, "seed": 0x12345678, "draws": 0}:
        raise ValueError("Native zero-modulus rejection contract failed")
    report = {"schema": 1, "authority": "Original DOL instructions executed by Dolphin interpreter; native API comparison",
              "dol_sha1": DOL_SHA1, "dol_sha256": hashlib.sha256(data).hexdigest(),
              "engine_binary_sha256": hashlib.sha256(args.oracle.read_bytes()).hexdigest(),
              "native_binary_sha256": hashlib.sha256(args.native.read_bytes()).hexdigest(),
              "native_source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
                  for name in ("src/battle_rng.c", "include/colosseum3ds/battle_rng.h",
                               "tools/prepare_battle_rng.py", "config/battle-rng-source-files.json")},
              "setup": {"r1": STACK, "r13": SDA, "seed_pointer_address": SEED_POINTER, "seed_address": SEED,
                        "return_pc": RETURN, "dependencies": "Actual HSD next16 calls, no arithmetic callbacks or game OS calls"},
              "functions": identities, "cases": len(vectors), "native_zero_modulus_rejection": rejected,
              "original_zero_modulus_invoked": False, "vectors": vectors}
    args.vectors.parent.mkdir(parents=True, exist_ok=True)
    args.vectors.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Qualified {len(vectors)} original-instruction RNG cases; numeric vectors: {args.vectors}")


if __name__ == "__main__":
    main()
