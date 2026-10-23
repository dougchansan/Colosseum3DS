#!/usr/bin/env python3
"""Prepare selected source-derived dry MusyX SFX PCM, not a MusyX runtime.

Decodes admitted GC DSP ADPCM and a bounded, documented macro subset. Original
bank members and decoded audio are private. Unsupported opcodes fail closed.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import wave

from prepare_boot_media import verify_disc
from prepare_battle_init import COMMON_SHA1, SOURCE_COMMIT
from prepare_outskirt import member, rel_data

PROFILES = {
    0x01B10000: (165200, '047718d80e7355f5a9e500401f4333c6481e6bed10b7af3e6697808c8d69a498'),
    0x01B20000: (6700, '427f88c6427ff86f4f6b2e2d0e2dafff3c3f0bdfa5c08d28d48eb8193809df03'),
    0x01B40000: (4540, '791acef23f8473d32be6c43b385341a1af81d7ab97ad0a7ba370926b39ddcb00'),
}
SAMP_PROFILE = (1325920, 'caa0ad205c1f33f3310ca7b5137ec619aa514e9bc16c3c6d0d2b59d93c44d062')
RATE = 32000
MAX_FRAMES = RATE * 10


def extent(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError('Audio resource extent exceeds source bounds')
    return data[offset:offset + size]


def be16(data, offset):
    return struct.unpack('>H', extent(data, offset, 2))[0]


def be32(data, offset):
    return struct.unpack('>I', extent(data, offset, 4))[0]


def admit(data, profile):
    if len(data) != profile[0] or hashlib.sha256(data).hexdigest() != profile[1]:
        raise ValueError('Audio source differs from admitted GC6E01 bank')
    return data


def macros(pool):
    """s_data.c GetPoolAddr: relative next offsets, eight-byte entry header."""
    offset = be32(pool, 0)
    result = {}
    for _ in range(4096):
        size = be32(pool, offset)
        if size == 0xFFFFFFFF:
            return result
        if size < 16 or size % 8:
            raise ValueError('Invalid macro extent/alignment')
        data = extent(pool, offset, size)
        identity = be16(data, 4)
        if identity in result or be16(data, 6):
            raise ValueError('Duplicate macro or reserved pool field')
        result[identity] = (offset, data[8:])
        offset += size
    raise ValueError('Macro pool traversal exceeds bound')


def fx_table(project):
    # Source bank group5, GROUP_DATA->data.fx.tableOff, FX_DATA->fx stride10.
    if be16(project, 4) != 5 or be16(project, 6) != 1:
        raise ValueError('Expected battle FX group5')
    offset = be32(project, 28)
    count = be16(project, offset)
    if not 1 <= count <= 2048 or be16(project, offset + 2):
        raise ValueError('Invalid FX table header')
    result = {}
    for i in range(count):
        row = extent(project, offset + 4 + i * 10, 10)
        identity = be16(row, 0)
        if identity in result:
            raise ValueError('Duplicate FX identity')
        result[identity] = {'macro': be16(row, 2), 'volume': row[6],
                            'pan': row[7], 'key': row[8], 'offset': offset + 4 + i * 10}
    return result


def sample_record(sdir, identity):
    # synthdata.c SdirDataT, terminating entry ID0xFFFF before coefficient data.
    for offset in range(0, min(len(sdir), 4096 * 32), 32):
        row = extent(sdir, offset, 32)
        sid = be16(row, 0)
        if sid == 0xFFFF:
            break
        if sid == identity:
            info, length = be32(row, 12), be32(row, 16)
            if length >> 24 or be32(row, 20) or be32(row, 24):
                raise ValueError('Only nonlooping GC DSP ADPCM samples are qualified')
            frames = length & 0xFFFFFF
            rate = info & 0xFFFFFF
            extra = be32(row, 28)
            coeff = extent(sdir, extra, 40)
            if be16(coeff, 0) != 8 or not frames or frames > 48000 * 10 or not 8000 <= rate <= 48000:
                raise ValueError('Unsupported ADPCM coefficients/sample timing')
            return {'id': identity, 'sdir_offset': offset, 'offset': be32(row, 4),
                    'frames': frames, 'rate': rate, 'root_key': info >> 24,
                    'initial_ps': coeff[2], 'coefficients': list(struct.unpack('>16h', coeff[8:40]))}
    raise ValueError('Missing source sample ID %04x' % identity)


def decode_adpcm(data, frames, coefficients):
    """GC DSP frame: predictor/scale byte +14 signed4bit samples, history0.

    Source hw_dspctrl_exact_8015B250.c programs coefficient pairs and zero
    histories; Nintendo DSP predictor arithmetic is signed11bit fixed-point.
    """
    if len(coefficients) != 16 or not 0 < frames <= 480000:
        raise ValueError('Invalid ADPCM output bound')
    required = ((frames + 13) // 14) * 8
    encoded = extent(data, 0, required)
    out, y1, y2 = [], 0, 0
    for off in range(0, required, 8):
        ps = encoded[off]
        predictor, scale = ps >> 4, ps & 15
        if predictor >= 8:
            raise ValueError('Invalid ADPCM predictor')
        c1, c2 = coefficients[predictor * 2:predictor * 2 + 2]
        for nibble in range(14):
            packed = encoded[off + 1 + nibble // 2]
            value = (packed >> (4 if nibble % 2 == 0 else 0)) & 15
            if value >= 8:
                value -= 16
            decoded = ((value * (1 << scale) << 11) + c1 * y1 + c2 * y2 + 1024) >> 11
            decoded = max(-32768, min(32767, decoded))
            out.append(decoded)
            y2, y1 = y1, decoded
            if len(out) == frames:
                return out
    raise ValueError('Truncated ADPCM')


def resolve_layers(table, macro, *, key=60, volume=127, start_ms=0, depth=0, stack=()):
    """Reached Bite macros only; stop sample-end waits, preserve child starts.

    Priority/age and zero reverb-controller selection are recorded, with no
    audible synthesis side effect in this dry conversion. No unknown no-ops.
    """
    if depth >= 8 or macro in stack or macro not in table:
        raise ValueError('Macro recursion/missing resource exceeds bound')
    offset, code = table[macro]
    layers, audible, pan, sweep = [], False, 64, None
    for pc in range(min(len(code) // 8, 64)):
        a, b = struct.unpack_from('>II', code, pc * 8)
        op = a & 127
        if op in (0, 1):
            return layers
        if op == 0x38:
            if (a, b) != (0x38, 0x107AC0):
                raise ValueError('Unqualified age setup')
        elif op == 8:
            if a & 0xFF00 or b != 0xFF320000:
                raise ValueError('Unqualified child macro setup')
            layers += resolve_layers(table, a >> 16, key=key, volume=volume,
                                     start_ms=start_ms, depth=depth + 1, stack=stack + (macro,))
        elif op == 0x46:
            if a not in (0x140146, 0x1E0146) or b:
                raise ValueError('Unqualified reverb controller')
        elif op == 0x40:
            if (a & 0xFFFF) != 0x0740 or b or not 0 <= a >> 16 <= 127:
                raise ValueError('Unqualified volume controller')
            volume = min(127, (127 * (a >> 16)) // 100)
        elif op == 0x0E:
            if a != 0x400E or b:
                raise ValueError('Only centered dry panning is qualified')
            pan = 64
        elif op == 0x15:
            if a != 0x4015 or b:
                raise ValueError('Only centered surround setup is qualified for mono output')
        elif op == 0x1D:
            if (a, b) != (0x01F4141D, 0x100) or audible:
                raise ValueError('Unqualified source pitch-sweep setup')
            # mcmdPitchSweep:20 counts, hwFrq2Pitch(+500Hz), no wait.
            sweep = {'count': 20, 'frequency': 500, 'mix_rate': 32000,
                     'low_job_ms': 15, 'first_update_ms': 0}
        elif op == 0x19:
            if a >> 16 or b != 0x100:
                raise ValueError('Unqualified key/detune setup')
            key = (a >> 8) & 127
        elif op == 0x10:
            if a >> 24 or b or audible:
                raise ValueError('Unqualified sample offset/retrigger')
            layers.append({'macro': macro, 'macro_offset': offset, 'pc': pc,
                           'sample': (a >> 8) & 0xFFFF, 'key': key,
                           'volume': volume, 'pan': pan, 'start_ms': start_ms,
                           **({'sweep': dict(sweep)} if sweep else {})})
            audible = True
        elif op == 7:
            # Opcode7 forces real-time (para[1] bit8) per macHandleActive.
            delay = b >> 16
            if a == 0x01000007 and delay == 0xFFFF and audible:
                return layers  # Sample-end wait; trailing keyoff/end have no tail.
            if a != 7 or b & 0xFFFF or delay > 1000 or audible:
                raise ValueError('Unqualified macro wait')
            start_ms += delay
        else:
            raise ValueError('Unsupported macro opcode %02x at macro%04x pc%d' % (op, macro, pc))
    raise ValueError('Macro instruction bound/missing end')


def pitch_sweep(spec):
    """Selected source mcmdPitchSweep/LowPrecisionHandler integer recurrence.

    synthAddJob(LOW,0xF00) schedules the next low job15ms later; time units
    are1/256ms. Offline phase starts at the macro's first sample frame. Original
    job/DSP phase within that15ms interval remains an explicit substitution.
    """
    if spec != {'count': 20, 'frequency': 500, 'mix_rate': 32000,
                'low_job_ms': 15, 'first_update_ms': 0}:
        raise ValueError('Unqualified pitch-sweep profile')
    counter = spec['count'] << 16
    offset = 0
    delta = spec['low_job_ms'] * 256
    pitch = int(spec['frequency'] * 4096 / spec['mix_rate'])
    while True:
        counter -= delta * 16
        if counter <= 0:
            counter = spec['count'] << 16
            offset = 0
        else:
            offset += ((pitch << 16) >> 12) * delta
        yield offset


def render_layers(layers, sdir, samples):
    output = [0] * MAX_FRAMES
    end = 0
    for layer in layers:
        record = sample_record(sdir, layer['sample'])
        encoded = extent(samples, record['offset'], ((record['frames'] + 13) // 14) * 8)
        if encoded[0] != record['initial_ps']:
            raise ValueError('ADPCM initial predictor differs from sample directory')
        pcm = decode_adpcm(encoded, record['frames'], record['coefficients'])
        # Offline linear resampling, not the original DSP polyphase SRC/mixer.
        step = record['rate'] * 2 ** ((layer['key'] - record['root_key']) / 12) / RATE
        begin = (layer['start_ms'] * RATE) // 1000
        if 'sweep' not in layer:
            count = math.ceil(len(pcm) / step)
            if begin + count > MAX_FRAMES:
                raise ValueError('Rendered cue exceeds ten-second bound')
            for i in range(count):
                x = i * step
                first = min(int(x), len(pcm) - 1)
                second = min(first + 1, len(pcm) - 1)
                value = pcm[first] + (pcm[second] - pcm[first]) * (x - first)
                output[begin + i] += round(value * layer['volume'] / 127)
        else:
            steps = pitch_sweep(layer['sweep'])
            job_frames = RATE * layer['sweep']['low_job_ms'] // 1000
            pitch = next(steps)
            x, count = 0., 0
            while x < len(pcm):
                if begin + count >= MAX_FRAMES:
                    raise ValueError('Rendered swept cue exceeds ten-second bound')
                if count and count % job_frames == 0:
                    pitch = next(steps)
                first = int(x)
                second = min(first + 1, len(pcm) - 1)
                value = pcm[first] + (pcm[second] - pcm[first]) * (x - first)
                output[begin + count] += round(value * layer['volume'] / 127)
                # Source cpitch addition is Q16 then truncates to Q12 DSPpitch.
                base_q16 = int(step * 4096 * 65536)
                x += ((base_q16 + pitch) >> 16) / 4096.
                count += 1
        end = max(end, begin + count)
        layer['source'] = record
        layer['adpcm_sha256'] = hashlib.sha256(encoded).hexdigest()
    if not end:
        raise ValueError('Cue has no audible source sample')
    clipped = sum(abs(v) > 32767 for v in output[:end])
    return struct.pack('<%dh' % end, *(max(-32768, min(32767, v)) for v in output[:end])), clipped


def checksum(data):
    result = 2166136261
    for value in data:
        result = ((result ^ value) * 16777619) & 0xFFFFFFFF
    return result


def encode_pack(cues):
    if not 1 <= len(cues) <= 8:
        raise ValueError('Cue count exceeds bound')
    rows, payload = bytearray(), bytearray()
    offset = 64 + len(cues) * 32
    for sound, pcm in cues:
        if not pcm or len(pcm) % 2 or len(pcm) // 2 > MAX_FRAMES:
            raise ValueError('Invalid PCM cue extent')
        rows += struct.pack('<8I', sound, RATE, len(pcm) // 2, offset, len(pcm), 1, 0, 0)
        payload += pcm
        offset += len(pcm)
    body = rows + payload
    header = struct.pack('<8I', 0x55413343, 1, 64 + len(body), len(cues), checksum(body), 0, 0, 0)
    return header + bytes(32) + body


def prepare(disc, output):
    disc = verify_disc(disc)
    common_path = disc / 'files/common.fsys'
    sample_path = disc / 'files/sound/snd_se_battle.samp'
    if common_path.is_symlink() or sample_path.is_symlink():
        raise ValueError('Audio inputs may not be symlinks')
    archive = common_path.read_bytes()
    common = member(archive, 0x01C71C00)
    if hashlib.sha1(common).hexdigest() != COMMON_SHA1:
        raise ValueError('common REL differs from admitted source')
    rel = rel_data(common, 125)
    pool, proj, sdir = [admit(member(archive, identity), PROFILES[identity]) for identity in PROFILES]
    samples = admit(sample_path.read_bytes(), SAMP_PROFILE)
    macro_table, fx = macros(pool), fx_table(proj)
    cues, report = [], {'source_commit': SOURCE_COMMIT, 'common_sha1': COMMON_SHA1, 'cues': [],
        'sample_archive': {'file': 'sound/snd_se_battle.samp', 'sha256': SAMP_PROFILE[1]},
        'bank_members': [{'id': identity, 'bytes': size, 'sha256': digest} for identity, (size, digest) in PROFILES.items()],
        'limitations': ['Selected dry offline macro rendering; no MusyX runtime or reverb.',
            'Linear SRC and musical semitone ratios replace original DSP polyphase SRC and fixed-point pitch table.',
            'Centered mono PCM uses source controller volume scales; original mixer envelope/output gain not qualified.',
            'Reflect pitch sweep uses source integer recurrence at15ms low-job cadence, with offline first-sample phase; original within-job/DSP phase not qualified.']}
    for sound in (0x85, 0x86, 0x220):
        row = extent(rel, 0x13FE08 + sound * 12, 12)
        if row[0] != 0x80 or row[1] != 5:
            raise ValueError('Sound request no longer resolves to battle bank5')
        identity = be16(row, 4)
        if identity not in fx:
            raise ValueError('Missing mapped FX ID')
        effect = fx[identity]
        layers = resolve_layers(macro_table, effect['macro'], key=effect['key'], volume=effect['volume'])
        pcm, clipped = render_layers(layers, sdir, samples)
        cues.append((sound, pcm))
        report['cues'].append({'sound_id': sound, 'sound_table_offset': 0x13FE08 + sound * 12,
            'fx_id': identity, 'fx': effect, 'layers': layers, 'frames': len(pcm) // 2,
            'rate': RATE, 'clipped_frames': clipped, 'pcm_sha256': hashlib.sha256(pcm).hexdigest()})
    pack = encode_pack(cues)
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output / 'audio.c3u').write_bytes(pack)
    for sound, pcm in cues:
        with wave.open(str(output / ('sound_%04x.wav' % sound)), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm)
    report['pack_sha256'] = hashlib.sha256(pack).hexdigest()
    (output / 'audio.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = prepare(args.disc, args.output)
    print('Prepared source-derived dry SFX: ' + ', '.join('%04x' % x['sound_id'] for x in report['cues']))
