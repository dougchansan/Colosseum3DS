#!/usr/bin/env python3
"""Replay public original numeric observations against the current native API."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
from run_battle_rules_oracle import cases
from qualify_rng import DOL_SHA1

ROOT = Path(__file__).resolve().parents[2]

class RuleResult(ctypes.Structure):
    _fields_ = [('result', ctypes.c_uint16), ('accepted', ctypes.c_uint8)]

class Rng(ctypes.Structure):
    _fields_ = [('seed', ctypes.c_uint32), ('draws', ctypes.c_uint64)]

def validate_vectors(report):
    expected = cases()
    if (report.get('schema') != 1 or report.get('dol_sha1') != DOL_SHA1 or
            report.get('cases') != len(expected) or len(report.get('vectors', [])) != len(expected)):
        raise ValueError('Original vector identity or coverage mismatch')
    if set(report.get('functions', {})) != {'combine_result', 'ai_argmax'}:
        raise ValueError('Original vector function identity mismatch')
    for case, vector in zip(expected, report['vectors']):
        if any(vector.get(key) != value for key, value in case.items()):
            raise ValueError('Original vector corpus mismatch')
        if vector.get('instructions', 0) <= 0 or len(vector.get('instruction_trace_sha256', '')) != 64:
            raise ValueError('Incomplete original instruction observation')
        if vector.get('draws') != len(vector.get('draw_trace', [])):
            raise ValueError('Incomplete original draw observation')
        if case['function'] == 'combine_result':
            if vector['draws'] != 0 or vector['final_seed'] != 0 or vector['value'] not in (0, 1) or not 0 <= vector['result'] <= 7:
                raise ValueError('Invalid original floor observation')
        else:
            expected_draws = int(case['use_random'] == 1 and
                                 sum(value == max([-200, *case['values']]) for value in case['values']) > 1)
            if vector['draws'] != expected_draws or not -1 <= vector['value'] < len(case['values']):
                raise ValueError('Invalid original AI observation')
            if not expected_draws and vector['final_seed'] != case['seed']:
                raise ValueError('Unexpected original seed change')
            for draw in vector['draw_trace']:
                if (int(draw['return_pc'], 0) != 0x801ADD08 or int(draw['caller_return_pc'], 0) != 0x800E0C64 or
                        int(draw['seed_after'], 0) != vector['final_seed'] or not 0 <= int(draw['value'], 0) <= 65535):
                    raise ValueError('Invalid original draw trace')
    return report['vectors']

def check(report, library):
    vectors = validate_vectors(report)
    native = ctypes.CDLL(str(Path(library).resolve()))
    native.c3_battle_rules_combine_result.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint8, ctypes.POINTER(RuleResult)]
    native.c3_battle_rules_combine_result.restype = ctypes.c_int
    native.c3_battle_rules_ai_argmax.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_uint16, ctypes.c_uint8,
                                              ctypes.POINTER(Rng), ctypes.POINTER(ctypes.c_int32)]
    native.c3_battle_rules_ai_argmax.restype = ctypes.c_int
    for vector in vectors:
        if vector['function'] == 'combine_result':
            output = RuleResult(0xFFFF, 0xFF)
            ok = native.c3_battle_rules_combine_result(vector['current'], vector['requested'], bool(vector['draw_flag']), ctypes.byref(output))
            matches = (output.result, output.accepted) == (vector['result'], vector['value'])
        else:
            values = (ctypes.c_int32 * len(vector['values']))(*vector['values'])
            rng, output = Rng(vector['seed'], 0), ctypes.c_int32(-999)
            ok = native.c3_battle_rules_ai_argmax(values, len(values), vector['use_random'], ctypes.byref(rng), ctypes.byref(output))
            matches = (output.value, rng.seed, rng.draws) == (vector['value'], vector['final_seed'], vector['draws'])
        if ok != 1 or not matches:
            raise ValueError('Original/native mismatch: ' + json.dumps(vector))
    return len(vectors)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vectors', type=Path, default=ROOT / 'docs/battle-rules-original-vectors.json')
    parser.add_argument('--library', type=Path, required=True)
    args = parser.parse_args()
    count = check(json.loads(args.vectors.read_text()), args.library)
    print(json.dumps({'matched': count, 'library_sha256': hashlib.sha256(args.library.read_bytes()).hexdigest()}))

if __name__ == '__main__':
    main()
