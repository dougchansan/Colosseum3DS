#!/usr/bin/env python3
"""Execute pinned original battle result/AI instructions; retain raw material privately."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from qualify_rng import DOL_SHA1, dol_read, STACK, SDA, RETURN, SEED, SEED_POINTER, SEEDS
from prepare_battle_rules_oracle import AI_RANGES, FLOOR_RANGES

ROOT = Path(__file__).resolve().parents[2]
FLOOR = 0x81601000
RESULT = 0x8047B5D8
TABLE = 0x803754AC
CALLEE = {str(i): (i * 0x01010101) for i in range(14, 32)}

def cases():
    floor = [{'function': 'combine_result', 'current': current, 'requested': requested, 'draw_flag': draw}
             for draw in (0, 1) for current in range(8) for requested in range(8)]
    # Nonbinary original flags exercise the original byte comparison. Public
    # native input remains a validated boolean, compared through nonzero.
    floor += [{'function': 'combine_result', 'current': c, 'requested': r, 'draw_flag': d}
              for d in (2, 127, 255) for c, r in ((3, 2), (2, 3), (4, 5), (5, 4), (7, 2), (6, 3))]
    arrays = ([], [-201], [-200], [-201, -202], [-200, -200], [-199],
              [1, 9, 3], [9, 1, 9], [-200, -199, -199],
              [2147483647, -2147483648, 2147483647], list(range(16)), [5] * 16,
              [-200] * 16, [-201] * 16)
    ai = [{'function': 'ai_argmax', 'values': values, 'use_random': random, 'seed': seed}
          for values in arrays for random in (0, 1, 2) for seed in SEEDS]
    return floor + ai

def patch(address, data):
    return {'address': address, 'data_hex': data.hex()}

def fixture(case, index, data):
    registers = {'1': STACK, '13': SDA, **CALLEE}
    memory = [patch(SEED_POINTER, struct.pack('>I', SEED)), patch(SEED, struct.pack('>I', case.get('seed', 0)))]
    if case['function'] == 'ai_argmax':
        registers.update({'3': FLOOR, '4': len(case['values']), '5': case['use_random']})
        memory.append(patch(FLOOR, b''.join(struct.pack('>i', value) for value in case['values']) or bytes(4)))
    else:
        registers.update({'3': FLOOR, '4': case['requested']})
        memory += [patch(RESULT, struct.pack('>H', case['current'])), patch(FLOOR, bytes(0x100)),
                   patch(TABLE, dol_read(data, TABLE, 0x178)),
                   patch(0x80478F50, struct.pack('>II', 0x81604000, 0x81602000)),
                   patch(0x80478F40, struct.pack('>II', 0x81604004, 0x81603000)),
                   patch(0x81604000, struct.pack('>II', 1, 0)), patch(0x81602000, bytes(0x38)),
                   patch(0x81603000, bytes(7) + bytes([case['draw_flag']]) + bytes(0x18))]
    return {'id': f'{case["function"]}-{index}', 'initial': {'gpr': registers, 'lr': RETURN, 'memory': memory},
            'observe': {'gpr': [1, 3, 13, *range(14, 32)], 'memory':
                        [{'address': SEED, 'size': 4}, {'address': RESULT, 'size': 2},
                         {'address': SEED_POINTER, 'size': 4}]}}

def validate(result, case, expected_id):
    if result.get('id') != expected_id:
        raise ValueError('Original fixture identity mismatch')
    if result.get('status') != 'returned' or int(result['final']['pc'], 0) != RETURN or int(result['final']['lr'], 0) != RETURN:
        raise ValueError('Original execution failed: ' + json.dumps(result)[:500])
    registers = result['final']['gpr']
    if any(int(registers[index], 0) != value for index, value in {'1': STACK, '13': SDA, **CALLEE}.items()):
        raise ValueError('Original ABI preservation failure')
    pcs = [int(pc, 0) for pc in result['instruction_pcs']]
    ranges = AI_RANGES if case['function'] == 'ai_argmax' else FLOOR_RANGES
    if len(pcs) != result['instructions'] or not pcs or any(pc % 4 or not any(lo <= pc < hi for lo, hi in ranges) for pc in pcs):
        raise ValueError('Original instruction path failure')
    if (result['ram_changes_truncated'] or
            sum(len(change['after_hex']) // 2 for change in result['ram_changes']) != result['ram_changed_bytes']):
        raise ValueError('Truncated memory changes')
    allowed = [(STACK - 0x200, STACK + 8)]
    allowed += [(SEED, SEED + 4)] if case['function'] == 'ai_argmax' else [(RESULT, RESULT + 2)]
    for change in result['ram_changes']:
        address = int(change['address'], 0)
        if not any(lo <= address and address + len(change['after_hex']) // 2 <= hi for lo, hi in allowed):
            raise ValueError('Unexpected original RAM write: ' + str(change))
    if int(result['final']['memory'][2]['data_hex'], 16) != SEED:
        raise ValueError('Changed seed pointer')
    trace = result['draw_trace']
    seed = int(result['final']['memory'][0]['data_hex'], 16)
    expected_draws = int(case['function'] == 'ai_argmax' and case['use_random'] == 1 and
                         sum(v == max([-200, *case['values']]) for v in case['values']) > 1)
    if len(trace) != expected_draws:
        raise ValueError('Unexpected original draw count')
    for draw in trace:
        if int(draw['return_pc'], 0) != 0x801ADD08 or int(draw['caller_return_pc'], 0) != 0x800E0C64:
            raise ValueError('Unexpected HSD return path')
        if not 0 <= int(draw['value'], 0) <= 0xFFFF or int(draw['seed_after'], 0) != seed:
            raise ValueError('Incomplete original HSD trace')
    if not trace and seed != case.get('seed', 0):
        raise ValueError('Unexpected seed write')
    value = int(registers['3'], 0)
    if case['function'] == 'ai_argmax' and value >= 0x80000000:
        value -= 0x100000000
    observation = {'value': value, 'final_seed': seed, 'draws': len(trace),
                   'instructions': result['instructions'], 'instruction_trace_sha256':
                   hashlib.sha256(b''.join(struct.pack('>I', pc) for pc in pcs)).hexdigest(), 'draw_trace': trace}
    if case['function'] == 'combine_result':
        observation['result'] = int(result['final']['memory'][1]['data_hex'], 16)
    return observation

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dol', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--private-output', type=Path, required=True)
    parser.add_argument('--vectors', type=Path, required=True)
    args = parser.parse_args()
    data = args.dol.read_bytes()
    if hashlib.sha1(data).hexdigest() != DOL_SHA1:
        raise ValueError('Original DOL identity mismatch')
    args.private_output.mkdir(parents=True, exist_ok=False)
    vectors, identities = [], {}
    corpus = cases()
    for name, entry, base, end in (('combine_result', 0x801F3984, 0x801EF62C, 0x8020E4B4),
                                   ('ai_argmax', 0x802397B8, 0x800E0C54, 0x802398E4)):
        selected = [case for case in corpus if case['function'] == name]
        code = dol_read(data, base, end - base)
        fixtures = [fixture(case, index, data) for index, case in enumerate(selected)]
        request = {'schema_version': 1, 'function': {'name': name, 'entry_pc': entry, 'code_base': base,
                    'code_hex': code.hex(), 'max_instructions': 4096}, 'fixtures': fixtures}
        request_path = args.private_output / (name + '-request.json')
        result_path = args.private_output / (name + '-result.json')
        request_path.write_text(json.dumps(request))
        subprocess.run([str(args.oracle), '--request-file', str(request_path), '--result-file', str(result_path)], check=True)
        response = json.loads(result_path.read_text())
        if response['engine'] != 'dolphin-interpreter-battle-rules-qualification' or len(response['results']) != len(selected):
            raise ValueError('Unexpected engine/fixture count')
        identities[name] = {'entry': entry, 'code_base': base, 'installed_text_bytes': len(code),
                            'installed_text_sha256': hashlib.sha256(code).hexdigest(),
                            'ranges': [{'begin': lo, 'end': hi, 'sha256': hashlib.sha256(dol_read(data, lo, hi-lo)).hexdigest()}
                                       for lo, hi in (FLOOR_RANGES if name == 'combine_result' else AI_RANGES)],
                            'engine_provenance': response['provenance']}
        for case, original_fixture, result in zip(selected, fixtures, response['results']):
            vectors.append({**case, **validate(result, case, original_fixture['id'])})
    build_path = args.oracle.with_suffix('.build.json')
    build = json.loads(build_path.read_text())
    if build['engine_binary_sha256'] != hashlib.sha256(args.oracle.read_bytes()).hexdigest():
        raise ValueError('Engine build metadata does not match executable')
    engine_build = {key: build[key] for key in ('commits', 'engine_source_sha256', 'engine_binary_sha256',
                    'interpreter_source_sha256', 'dependency_diff_sha256', 'dependency_dirty',
                    'host_stub_sha256', 'compiler_version', 'authority')}
    engine_build['static_archives_sha256'] = [{'name': Path(path).name, 'sha256': digest}
                                             for path, digest in build['static_archives_sha256'].items()]
    report = {'schema': 1, 'engine_build': engine_build, 'authority': 'Original DOL instructions executed by Dolphin interpreter, isolated synthetic ABI fixtures',
              'dol_sha1': DOL_SHA1, 'dol_sha256': hashlib.sha256(data).hexdigest(),
              'engine_binary_sha256': hashlib.sha256(args.oracle.read_bytes()).hexdigest(),
              'functions': identities, 'status_table_sha256': hashlib.sha256(dol_read(data, TABLE, 0x178)).hexdigest(),
              'cases': len(vectors), 'vectors': vectors,
              'limits': ['No full battle entry, turn, result writeback, trainer-side resolution or original-game orchestration claim',
                         'Native hero-win predicate has source tests only; original hero-side resolver graph remains unqualified']}
    args.vectors.parent.mkdir(parents=True, exist_ok=True)
    args.vectors.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Observed {len(vectors)} original instruction fixtures: {args.vectors}')

if __name__ == '__main__':
    main()
