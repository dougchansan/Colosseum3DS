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
SINE_TABLE_ADDRESS = 0x80369D20
SINE_TABLE_SHA256 = 'aeab0f3fd8f52860e77db653864b644271098f6a827d303674502861f55824b3'
TAIL_WHIP_MACROS = {
    0x402: '9116a23659a4d0e46a9efa1972118cb3dd8c8a8f73697a236e4bee43f1e3c4af',
    0x401: '1284da98a5ab921666c3a3132abe5d8053a8fc816eb52123ad779f097245018c',
}
GROWL_MACROS = {
    0x39C: '2c223c311b9acb58008e81b08130bac46461cb4c43a70df83554529780a230cb',
    0x39B: 'b618be0dc1cca7e0dfda3af7416a581704a575ce01a96216e37ceaa11660244a',
    0x39A: '09650cfb359f118fbe20acf59e749510997cbc514e0b1f40cd2ca39e1568019f',
}
ATTACK_DOWN_MACROS = {
    0x22B: 'f4fc355ef1ab89ce33fbfcb2ea64ebe49046b85df5a04aac27507629a237796c',
    0x22C: 'd3f0a021de2330873003acd3948dd1e96b432152752baf9d39d97c9fce776d2d',
}
TACKLE_ATTACK_MACROS = {
    0x260: 'd2e5c99ababd0bdd371631f94dc17e541a75c9d4a5a05c19b364317326e73073',
}
TACKLE_DAMAGE_MACROS = {
    0x62F: 'b3330c4d03c8249fbbaedbabe8769dcba6713e0728a264f690483b87741bafc8',
    0x630: 'ee59d50d1eff1c346ed919851f70a9a7d18a27939408bcb024fe0540fb20eae1',
    0x631: '9b190eff2eca5d2d9871ae6bd900e290982db58cf11194c13c88a0390ed72f3d',
    0x632: 'ef49dde8168aa7792864055bc7689f37029346502ed84f4604f5e5e42efe2666',
}
STAT_DOWN_MACROS = {
    0x22D: '9324fb625f92837f931b288df0d6a21c7b09cc9ccc1f5d95f8d47052613a9817',
    0x22E: '8f3dc09159456177ff2c2c6b4778ad6c70c60b72ffc2b88e1c4bd5b52610900a',
}
GROWL_STOP_SOURCE = {
    'src/game/wazaSequence.c': '975afb18f19c6da6f7132fe3772d8ab3ab9546f528d8381073b59e614459444d',
    'src/game/wazaSequenceEntry.c': '6500a4db7253ce776686b86d470a6f9587cfa3db10cf9fcfb9cca50328d0855f',
}
SOURCE_FILES = {
    'src/musyx/runtime/synthmacros.c': '0b45497b7761c23f52c6a0c0567258b5c5876d3c85c850c8ddbfb6c53d6c43c6',
    'src/musyx/runtime/synth.c': '43991b47e4c75dbc387708b2308085b0716c41d4e1a11da8b965c16bbdc8f473',
    'src/musyx/runtime/snd_service_exact_80162070.c': 'cc9c0949a99d469c6e38f68baf61c6ba071e6b729a796751bd53fc4fb0a01927',
    'src/musyx/runtime/hardware.c': '990f5fe26a684729c9de41de2bbdd5512a5af1b90f209e6e139bf644bd7c375a',
}


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


def resolve_layers(table, macro, *, key=60, volume=127, start_ms=0, depth=0, stack=(), tackle=False):
    """Selected reviewed battle macros; stop sample-end waits, preserve child starts.

    Priority/age and zero reverb-controller selection are recorded, with no
    audible synthesis side effect in this dry conversion. No unknown no-ops.
    """
    if depth >= 8 or macro in stack or macro not in table:
        raise ValueError('Macro recursion/missing resource exceeds bound')
    offset, code = table[macro]
    layers, audible, pan, sweep, vibrato, reverb = [], False, 64, None, None, None
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
                                     start_ms=start_ms, depth=depth + 1, stack=stack + (macro,), tackle=tackle)
        elif op == 0x46:
            tackle_aux = (tackle and a == 0x230146 and macro in (0x62F, 0x630, 0x631)
                          and hashlib.sha256(code).hexdigest() == TACKLE_DAMAGE_MACROS[macro])
            if (a not in (0x0F0146, 0x140146, 0x190146, 0x1E0146) and not tackle_aux) or b:
                raise ValueError('Unqualified reverb controller')
            reverb = {'controller': 1, 'scale_percent': a >> 16,
                      'combine': 0, 'dry_only': True}
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
            if a & 0xFF != 0x1D or b != 0x100 or audible or not (a >> 8) & 255:
                raise ValueError('Unqualified source pitch-sweep setup')
            # mcmdPitchSweep: byte count, signed16 frequency, real-time wait0.
            frequency = struct.unpack('>h', struct.pack('>H', a >> 16))[0]
            sweep = {'count': (a >> 8) & 255, 'frequency': frequency, 'mix_rate': 32000,
                     'low_job_ms': 15, 'first_update_ms': 0}
        elif op == 0x1C:
            # Source dispatch is mcmdVibrato, not MIDI pitch bend. Reached
            # macros use positive key/cents ranges and unmodulated ms periods.
            keys, cents, period = (a >> 8) & 255, (a >> 16) & 255, b >> 16
            if audible or a >> 24 or b & 0xFFFF != 0x100 or not 1 <= period <= 1000 or keys > 127 or cents > 99:
                raise ValueError('Unqualified source vibrato setup')
            vibrato = {'key_range': keys, 'cent_range': cents, 'period_ms': period,
                       'low_job_ms': 15, 'first_update_ms': 0, 'modulation_scaled': False}
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
                           **({'sweep': dict(sweep)} if sweep else {}),
                           **({'vibrato': dict(vibrato)} if vibrato else {}),
                           **({'dry_aux_a_selection': dict(reverb)} if reverb else {})})
            audible = True
        elif op == 7:
            # Opcode7 forces real-time (para[1] bit8) per macHandleActive.
            delay = b >> 16
            if a == 0x01000007 and delay == 0xFFFF and audible:
                return layers  # Sample-end wait; trailing keyoff/end have no tail.
            if a != 7 or b & 0xFFFF or delay > 1000 or audible or sweep or vibrato:
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
    if (set(spec) != {'count', 'frequency', 'mix_rate', 'low_job_ms', 'first_update_ms'} or
        not isinstance(spec['count'], int) or not 1 <= spec['count'] <= 255 or
        not isinstance(spec['frequency'], int) or not -32768 <= spec['frequency'] <= 32767 or
        spec['mix_rate'] != RATE or spec['low_job_ms'] != 15 or spec['first_update_ms'] != 0):
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
            offset = (offset + ((pitch << 16) >> 12) * delta + (1 << 31)) % (1 << 32) - (1 << 31)
        yield offset


def pitch_vibrato(spec, sine):
    """mcmdVibrato/sndSin/LowPrecisionHandler, yielding Q16 semitone offsets.

    This preserves the original table/quadrant/integer arithmetic. Offline
    first-sample low-job phase is the same explicit substitution as the sweep.
    """
    if (set(spec) != {'key_range', 'cent_range', 'period_ms', 'low_job_ms', 'first_update_ms', 'modulation_scaled'} or
        any(type(spec[k]) is not int for k in ('key_range', 'cent_range', 'period_ms')) or
        not 0 <= spec['key_range'] <= 127 or not 0 <= spec['cent_range'] <= 99 or
        not 1 <= spec['period_ms'] <= 1000 or spec['low_job_ms'] != 15 or
        spec['first_update_ms'] != 0 or spec['modulation_scaled'] is not False or
        sine is None or len(sine) != 1024 or any(not isinstance(x, int) or not 0 <= x <= 4095 for x in sine)):
        raise ValueError('Unqualified source vibrato profile/table')
    period = spec['period_ms'] * 256
    phase = 0
    span = spec['key_range'] * 256 + spec['cent_range'] * 256 // 100
    while True:
        phase = (phase + spec['low_job_ms'] * 256) & 0xFFFFFFFF
        angle = ((phase % period * 16) // (period // 256)) & 0xFFF
        index = angle & 0x3FF
        if angle & 0x400:
            index = 0x3FF - index
        value = sine[index]
        if angle & 0x800:
            value = -value
        yield (span * value) >> 4


def render_layers(layers, sdir, samples, sine=None):
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
        if 'sweep' not in layer and 'vibrato' not in layer:
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
            sweeps = pitch_sweep(layer['sweep']) if 'sweep' in layer else None
            vibratos = pitch_vibrato(layer['vibrato'], sine) if 'vibrato' in layer else None
            job_frames = RATE * 15 // 1000
            pitch = next(sweeps) if sweeps else 0
            cents = next(vibratos) if vibratos else 0
            x, count = 0., 0
            while x < len(pcm):
                if begin + count >= MAX_FRAMES:
                    raise ValueError('Rendered swept cue exceeds ten-second bound')
                if count and count % job_frames == 0:
                    pitch = next(sweeps) if sweeps else 0
                    cents = next(vibratos) if vibratos else 0
                first = int(x)
                second = min(first + 1, len(pcm) - 1)
                value = pcm[first] + (pcm[second] - pcm[first]) * (x - first)
                output[begin + count] += round(value * layer['volume'] / 127)
                # Source cpitch addition is Q16 then truncates to Q12 DSPpitch.
                # Existing dry musical ratio remains a substitute for source
                # convert_cents/sndGetPitch's fixed table interpolation.
                modulated = step * 2 ** (cents / (12 * 65536)) if vibratos else step
                base_q16 = int(modulated * 4096 * 65536)
                increment = ((base_q16 + pitch) >> 16) / 4096.
                if increment <= 0:
                    raise ValueError('Source pitch modulation produced nonpositive playback step')
                x += increment
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
    if not 1 <= len(cues) <= 13:
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


def selected_sounds(confusion=False, tail_whip=False, growl=False, stat_down=False, attack_down=False, tackle=False):
    if tackle and not (confusion and tail_whip and growl and stat_down and attack_down):
        raise ValueError('Tackle requires --confusion --tail-whip --growl --stat-down --attack-down to extend the eleven-cue bundle')
    if attack_down and not (confusion and tail_whip and growl and stat_down):
        raise ValueError('Attack down requires --confusion --tail-whip --growl --stat-down to extend the ten-cue bundle')
    if stat_down and not (confusion and tail_whip and growl):
        raise ValueError('Stat down requires --confusion --tail-whip --growl to extend the nine-cue bundle')
    if growl and not (confusion and tail_whip):
        raise ValueError('Growl requires --confusion --tail-whip to extend the eight-cue bundle')
    if tail_whip and not confusion:
        raise ValueError('Tail Whip requires --confusion to extend the seven-cue bundle')
    sounds = (0x85, 0x86, 0x220, 0x443, 0xE9, 0x472, 0x473) if confusion else (0x85, 0x86, 0x220)
    return sounds + ((0x1DB,) if tail_whip else ()) + ((0x210,) if growl else ()) + ((0x19B,) if stat_down else ()) + ((0x19A,) if attack_down else ()) + ((0x4A2, 0xDE) if tackle else ())


def verify_tail_whip_binding(row, effect, table):
    if len(row) != 12:
        raise ValueError('Tail Whip sound record extent differs from admitted source')
    if (row[0], row[1], be16(row, 4), effect['macro'], effect['volume'], effect['pan'], effect['key']) != \
            (0x80, 5, 0x24E, 0x402, 127, 64, 60):
        raise ValueError('Tail Whip sound/FX/macro binding differs from admitted source')
    records = []
    for macro, expected in TAIL_WHIP_MACROS.items():
        if macro not in table or hashlib.sha256(table[macro][1]).hexdigest() != expected:
            raise ValueError('Tail Whip macro body differs from admitted source')
        records.append({'macro': macro, 'pool_offset': table[macro][0],
                        'bytes': len(table[macro][1]), 'sha256': expected})
    return records


def verify_growl_binding(row, effect, table):
    if len(row) != 12:
        raise ValueError('Growl sound record extent differs from admitted source')
    if (row[0], row[1], be16(row, 4), effect['macro'], effect['volume'], effect['pan'], effect['key']) != \
            (0x80, 5, 0x283, 0x39C, 127, 64, 60):
        raise ValueError('Growl sound/FX/macro binding differs from admitted source')
    records = []
    for macro, expected in GROWL_MACROS.items():
        if macro not in table or hashlib.sha256(table[macro][1]).hexdigest() != expected:
            raise ValueError('Growl macro body differs from admitted source')
        records.append({'macro': macro, 'pool_offset': table[macro][0],
                        'bytes': len(table[macro][1]), 'sha256': expected})
    return records


def verify_stat_down_binding(row, effect, table):
    if len(row) != 12:
        raise ValueError('Stat down sound record extent differs from admitted source')
    if (row[0], row[1], be16(row, 4), effect['macro'], effect['volume'], effect['pan'], effect['key']) != \
            (0x80, 5, 0x20E, 0x22D, 127, 64, 60):
        raise ValueError('Stat down sound/FX/macro binding differs from admitted source')
    records = []
    for macro, expected in STAT_DOWN_MACROS.items():
        if macro not in table or hashlib.sha256(table[macro][1]).hexdigest() != expected:
            raise ValueError('Stat down macro body differs from admitted source')
        records.append({'macro': macro, 'pool_offset': table[macro][0],
                        'bytes': len(table[macro][1]), 'sha256': expected})
    return records


def verify_attack_down_binding(row, effect, table):
    if len(row) != 12:
        raise ValueError('Attack down sound record extent differs from admitted source')
    if (row[0], row[1], be16(row, 4), effect['macro'], effect['volume'], effect['pan'], effect['key']) != \
            (0x80, 5, 0x20D, 0x22B, 127, 64, 60):
        raise ValueError('Attack down sound/FX/macro binding differs from admitted source')
    records = []
    for macro, expected in ATTACK_DOWN_MACROS.items():
        if macro not in table or hashlib.sha256(table[macro][1]).hexdigest() != expected:
            raise ValueError('Attack down macro body differs from admitted source')
        records.append({'macro': macro, 'pool_offset': table[macro][0],
                        'bytes': len(table[macro][1]), 'sha256': expected})
    return records


def verify_tackle_binding(sound, row, effect, table):
    if sound not in (0x4A2, 0xDE):
        raise ValueError('Unknown Tackle sound request')
    if len(row) != 12:
        raise ValueError('Tackle sound record extent differs from admitted source')
    fx, root, bodies = ((0x33F, 0x260, TACKLE_ATTACK_MACROS) if sound == 0x4A2
                        else (0x151, 0x62F, TACKLE_DAMAGE_MACROS))
    expected_row = bytes.fromhex('8005010a%04x000000000000' % fx)
    if bytes(row) != expected_row or (effect['macro'], effect['volume'], effect['pan'], effect['key']) != (root, 127, 64, 60):
        raise ValueError('Tackle sound/FX/macro binding differs from admitted source')
    records = []
    for macro, expected in bodies.items():
        if macro not in table or hashlib.sha256(table[macro][1]).hexdigest() != expected:
            raise ValueError('Tackle macro body differs from admitted source')
        records.append({'macro': macro, 'pool_offset': table[macro][0],
                        'bytes': len(table[macro][1]), 'sha256': expected})
    return records


def prepare(disc, output, confusion=False, tail_whip=False, growl=False, stat_down=False, attack_down=False, tackle=False):
    sounds = selected_sounds(confusion, tail_whip, growl, stat_down, attack_down, tackle)
    disc = verify_disc(disc)
    root = Path(__file__).resolve().parents[1]
    for name, digest in {**SOURCE_FILES, **(GROWL_STOP_SOURCE if growl else {})}.items():
        path = root / 'vendor/colosseum' / name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Audio macro source drift: ' + name)
    sine = None
    if confusion:
        from prepare_game_state import Dol
        data = Dol((disc / 'sys/main.dol').read_bytes()).read(SINE_TABLE_ADDRESS, 2048)
        if hashlib.sha256(data).hexdigest() != SINE_TABLE_SHA256:
            raise ValueError('Original MusyX sine table differs from admitted DOL')
        sine = struct.unpack('>1024h', data)
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
        'macro_source_sha256': SOURCE_FILES,
        'sample_archive': {'file': 'sound/snd_se_battle.samp', 'sha256': SAMP_PROFILE[1]},
        'bank_members': [{'id': identity, 'bytes': size, 'sha256': digest} for identity, (size, digest) in PROFILES.items()],
        'limitations': ['Selected dry offline macro rendering; no MusyX runtime or reverb.',
            'Linear SRC and musical semitone ratios replace original DSP polyphase SRC and fixed-point pitch table.',
            'Centered mono PCM uses source controller volume scales; original mixer envelope/output gain not qualified.',
            'Pitch sweep and vibrato use source integer recurrence at15ms low-job cadence, with offline first-sample phase; original within-job/DSP phase not qualified.',
            'Opcode1C is mcmdVibrato, not MIDI pitch bend; reached positive unmodulated real-time ranges only.',
            'Confusion472/473 reverb selection is retained as dry auxiliary-controller metadata; no reverb tail or wet mix is rendered.']}
    if confusion:
        report['vibrato_sine_table'] = {'dol_address': SINE_TABLE_ADDRESS, 'bytes': 2048,
                                      'sha256': SINE_TABLE_SHA256, 'entries': 1024}
    for sound in sounds:
        row = extent(rel, 0x13FE08 + sound * 12, 12)
        if row[0] != 0x80 or row[1] != 5:
            raise ValueError('Sound request no longer resolves to battle bank5')
        identity = be16(row, 4)
        if identity not in fx:
            raise ValueError('Missing mapped FX ID')
        effect = fx[identity]
        if sound in (0x4A2, 0xDE):
            report.setdefault('tackle_bindings', []).append({
                'sound_id': sound, 'fx_id': identity, 'root_macro': effect['macro'],
                'sound_start_tick': 130 if sound == 0x4A2 else 0,
                'phase': 'attack' if sound == 0x4A2 else 'damage',
                'macros': verify_tackle_binding(sound, row, effect, macro_table),
                'auxiliary_selection': 'Source auxiliary selections retained as dry metadata; no wet mix, original DSP or release-tail parity.'})
        if sound == 0x1DB:
            report['tail_whip_binding'] = {'sound_id': sound, 'fx_id': identity,
                'root_macro': effect['macro'], 'macros': verify_tail_whip_binding(row, effect, macro_table)}
        if sound == 0x19A:
            report['attack_down_binding'] = {'sound_id': sound, 'special_id': 0x27, 'fx_id': identity,
                'root_macro': effect['macro'], 'macros': verify_attack_down_binding(row, effect, macro_table),
                'auxiliary_selection': 'Source root25percent and child15percent auxiliary reverb retained as dry metadata; no wet mix or release-tail parity.'}
        if sound == 0x19B:
            report['stat_down_binding'] = {'sound_id': sound, 'special_id': 0x29, 'fx_id': identity,
                'root_macro': effect['macro'], 'macros': verify_stat_down_binding(row, effect, macro_table),
                'auxiliary_selection': 'Source root25percent and child15percent auxiliary reverb retained as dry metadata; no wet mix or release-tail parity.'}
        if sound == 0x210:
            report['growl_binding'] = {'sound_id': sound, 'fx_id': identity,
                'root_macro': effect['macro'], 'macros': verify_growl_binding(row, effect, macro_table),
                'stop_source_sha256': GROWL_STOP_SOURCE, 'sound_start_tick': 130,
                'control_start_tick': 130, 'control_delay_ticks': 100,
                'stop_tick': 230,
                'stop_semantics': 'wazaSequenceApplyStop -> wazaSequenceEntryStop case5 -> fn_80166B18 -> fn_80167070(id,1) -> fn_801669E4(id,0,0) -> _sndStopSE -> sndFXKeyOff',
                'native_stop_limit': 'Immediate dry PCM buffer clear adapts source key-off; original release-envelope/tail timing is unqualified. STOPPED is not full PCM finished.'}
        layers = resolve_layers(macro_table, effect['macro'], key=effect['key'], volume=effect['volume'], tackle=tackle)
        pcm, clipped = render_layers(layers, sdir, samples, sine)
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
    parser.add_argument('--confusion', action='store_true', help='Include the four source Confusion requests; fail closed on unsupported macro branches')
    parser.add_argument('--tail-whip', action='store_true', help='Extend --confusion with admitted Tail Whip request 0x1DB; defaults stay unchanged')
    parser.add_argument('--growl', action='store_true', help='Extend --confusion --tail-whip with admitted Growl request 0x210')
    parser.add_argument('--stat-down', action='store_true', help='Extend --confusion --tail-whip --growl with green Defense-down special29 request0x19B')
    parser.add_argument('--attack-down', action='store_true', help='Extend --stat-down and all prior options with red Attack-down special27 request0x19A')
    parser.add_argument('--tackle', action='store_true', help='Extend --attack-down and all prior options with Tackle attack0x4A2 and damage0xDE')
    args = parser.parse_args()
    report = prepare(args.disc, args.output, args.confusion, args.tail_whip, args.growl, args.stat_down, args.attack_down, args.tackle)
    print('Prepared source-derived dry SFX: ' + ', '.join('%04x' % x['sound_id'] for x in report['cues']))
