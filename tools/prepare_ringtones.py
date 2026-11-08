#!/usr/bin/env python3
"""Private offline dry rendition of the original ten PDA MusyX arrangements.

Uses original notes, PAGE/layer/keymap instruments, DSP samples, loops and
linear ADSR curves. This is a bounded offline renderer, not the original DSP:
mono downmix, linear resampling and offline hardware job phase. Manifest
records these substitutions; unsupported arrangement/macro commands fail.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from prepare_outskirt import member
from prepare_battle_audio import be16, be32, extent, macros, decode_adpcm, encode_pack, RATE

SOUNDS = (13, 14, 15, 17, 16, 18, 19, 20, 21, 22)
# UI swaps original resource 03 and 04.
RESOURCE_ORDER = (0, 1, 2, 4, 3, 5, 6, 7, 8, 9)
RESOURCES = (0x02EE0800,) + tuple((0x02F0 + i) * 65536 + 0x0800 for i in range(1, 10))


def pool_entries(pool, word):
    result = {}
    offset = be32(pool, word)
    for _ in range(4096):
        size = be32(pool, offset)
        if size == 0xffffffff:
            return result
        if size < 8 or offset + size > len(pool):
            raise ValueError('Invalid pool extent')
        identity = be16(pool, offset + 4)
        if identity in result:
            raise ValueError('Duplicate pool identity')
        result[identity] = extent(pool, offset + 8, size - 8)
        offset += size
    raise ValueError('Unterminated pool')


def parse_song(data):
    tracks, patterns, midi, tempo, bpm, loop = struct.unpack('>6I', extent(data, 0, 24))
    if loop or not 20 <= bpm <= 400:
        raise ValueError('Unqualified tempo/loop arrangement')
    changes = [(0, bpm)]
    if tempo:
        for i in range(256):
            tick = be32(data, tempo + i * 8)
            if tick == 0xffffffff:
                break
            value = be32(data, tempo + i * 8 + 4)
            if not 20 <= value <= 400 or tick < changes[-1][0]:
                raise ValueError('Invalid tempo map')
            changes.append((tick, value))
        else:
            raise ValueError('Unterminated tempo map')
    def seconds(tick):
        total, previous, current = 0, 0, bpm
        for at, value in changes:
            if at > tick:
                break
            total += (at - previous) * 60 / current / 384
            previous, current = at, value
        return total + (tick - previous) * 60 / current / 384
    notes = Song()
    for track in range(64):
        entry = be32(data, tracks + track * 4)
        if not entry:
            continue
        channel = extent(data, midi + track, 1)[0]
        if channel > 15:
            raise ValueError('Invalid MIDI channel')
        program, volume = 0, 127
        for _ in range(256):
            start, prg, vel, _, pattern, transpose, add = struct.unpack('>IBBH Hbb', extent(data, entry, 12))
            entry += 12
            if pattern == 0xffff:
                break
            if pattern >= 0xfffe:
                raise ValueError('Unqualified arrangement loop')
            if prg != 255:
                program = prg
            if vel != 255:
                volume = vel
            pattern_offset = be32(data, patterns + pattern * 4)
            _, bend, mod = struct.unpack('>3I', extent(data, pattern_offset, 12))
            if bend or mod:
                raise ValueError('Unqualified continuous controller stream')
            cursor, tick = pattern_offset + 12, start
            for _ in range(4096):
                delta, key, velocity = struct.unpack('>HBB', extent(data, cursor, 4))
                if key == velocity == 255:
                    break
                tick += delta
                cursor += 4
                if key & 128:
                    if velocity == 0:
                        program = key & 127
                    elif velocity & 128:
                        controller = velocity & 127
                        if controller not in (1, 7, 10, 11, 20, 22, 23, 24, 91):
                            raise ValueError('Unknown arrangement controller %d' % controller)
                        notes.controllers.setdefault(channel, []).append((seconds(tick), controller, key & 127))
                    else:
                        raise ValueError('Unknown arrangement command')
                elif key or velocity:
                    if not 0 <= key + transpose <= 127:
                        raise ValueError('Transposed note exceeds MIDI range')
                    duration = be16(data, cursor)
                    cursor += 2
                    notes.append((seconds(tick), seconds(tick + duration) - seconds(tick),
                                  program, key + transpose, max(0, min(127, velocity + add)) * volume / 127, channel))
            else:
                raise ValueError('Pattern exceeds event bound')
        else:
            raise ValueError('Track exceeds entry bound')
    if not notes:
        raise ValueError('Empty ringtone')
    for events in notes.controllers.values():
        events.sort()
    return notes


class Song(list):
    def __init__(self):
        self.controllers = {}

    def control(self, channel, at):
        values = {7: 127, 11: 127, 10: 64, 1: 0, 20: 0, 22: 0, 23: 0, 24: 0, 91: 0}
        for time, controller, value in self.controllers.get(channel, ()):
            if time > at:
                break
            values[controller] = value
        return values


class Instruments:
    def __init__(self, pool, project, sdir, samples, tables=None):
        self.tables = tables
        self.mac = macros(pool)
        self.curves = pool_entries(pool, 4)
        self.keys = pool_entries(pool, 8)
        self.layers = pool_entries(pool, 12)
        self.pages = []
        for word in (28, 32):
            page, offset = {}, be32(project, word)
            for _ in range(256):
                identity = be16(project, offset)
                if identity == 65535:
                    break
                page[project[offset + 4]] = identity
                offset += 6
            self.pages.append(page)
        self.sdir, self.samples, self.cache = sdir, samples, {}

    def voices(self, identity, key, volume, depth=0, pan=64):
        if depth > 8:
            raise ValueError('Instrument nesting exceeds bound')
        if identity & 0xc000 == 0x8000:
            layer = self.layers[identity]
            for i in range(be32(layer, 0)):
                row = extent(layer, 4 + i * 12, 12)
                if row[2] <= key <= row[3]:
                    yield from self.voices(be16(row, 0), key + struct.unpack('b', row[4:5])[0], volume * row[5] / 127, depth + 1, max(0, min(127, pan + row[8] - 64)) if not row[8] & 128 else 128)
        elif identity & 0xc000 == 0x4000:
            row = extent(self.keys[identity], key * 8, 8)
            if be16(row, 0) != 65535:
                yield from self.voices(be16(row, 0), key + struct.unpack('b', row[2:3])[0], volume, depth + 1, max(0, min(127, pan + row[3] - 64)) if not row[3] & 128 else 128)
        else:
            curve, sample, override, vibrato = None, None, None, None
            release_delay, release_scale, waiting, one_shot = 0, 1, False, False
            for a, b in struct.iter_unpack('>II', self.mac[identity][1]):
                op = a & 127
                if op == 12:
                    curve = self.curves[(a >> 8) & 65535]
                elif op == 24:
                    if b not in (0, 0x100):
                        raise ValueError('Timed key change unsupported')
                    key = max(0, min(127, key + struct.unpack('b', bytes([(a >> 8) & 255]))[0])) + struct.unpack('b', bytes([(a >> 16) & 255]))[0] / 100
                elif op == 16:
                    sample = (a >> 8) & 65535
                elif op == 0x16:
                    override = ((a >> 8) & 255, (a >> 16) & 255, a >> 24, b & 255)
                elif op == 0x1c:
                    if (a, b) != (0x010f001c, 0xc80100):
                        raise ValueError('Unqualified instrument vibrato')
                    vibrato = (0, 15, 200, True)
                elif op == 7:
                    if not waiting and (a, b) == (0x01000107, 0xffff0000):
                        waiting = True
                    elif not waiting and (a, b) == (0x01000007, 0xffff0000):
                        one_shot = True
                        break
                    elif waiting:
                        break
                    else:
                        raise ValueError('Unqualified macro wait %04x %08x %08x' % (identity,a,b))
                elif op == 0x0f:
                    if not waiting or (a, b) != (0xff00600f, 0x7801ff):
                        raise ValueError('Unqualified macro volume envelope')
                    release_delay, release_scale = .120, 96 / 128
                elif op in (0, 1, 0x12):
                    break
                elif op not in (0x38, 0x30):
                    raise ValueError('Unsupported instrument macro opcode %02x' % op)
            if sample is None:
                raise ValueError('Instrument has no sample')
            yield sample, key, volume, curve, override, vibrato, pan, release_delay, release_scale, one_shot

    def sample(self, identity):
        if identity in self.cache:
            return self.cache[identity]
        for offset in range(0, len(self.sdir), 32):
            if be16(self.sdir, offset) == 65535:
                break
            if be16(self.sdir, offset) != identity:
                continue
            row = extent(self.sdir, offset, 32)
            info, length, loop_start, loop_length, extra = struct.unpack_from('>5I', row, 12)
            if length >> 24:
                raise ValueError('Unsupported sample codec')
            frames = length & 0xffffff
            coeff = extent(self.sdir, extra, 40)
            pcm = decode_adpcm(extent(self.samples, be32(row, 4), ((frames + 13) // 14) * 8), frames,
                               struct.unpack('>16h', coeff[8:40]))
            if loop_start + loop_length > frames:
                raise ValueError('Sample loop exceeds extent')
            result = pcm, info & 0xffffff, info >> 24, loop_start, loop_length
            self.cache[identity] = result
            return result
        raise ValueError('Missing instrument sample')


class Tables:
    def __init__(self, path):
        from prepare_game_state import Dol
        dol = Dol(Path(path).read_bytes())
        def read(address, size, digest):
            value = dol.read(address, size)
            if hashlib.sha256(value).hexdigest() != digest:
                raise ValueError('Original MusyX DOL table differs from admitted source')
            return value
        self.attenuation = struct.unpack('>194H', read(0x803692c8, 388, '941feb66b1398a27b761e18ffdd1b963a567b7d2fdcbde8bbd74c42d8e7e486b'))
        self.indices = read(0x8036944c, 1024, 'a429f3f91588cff481648053bcf6ae506df023a337cd6ad707688c0de1da21d6')
        self.dls = struct.unpack('>129f', read(0x8036984c, 516, '2ee7dd4981ec604d3069e9ae5cc51fd886dcc8b577946ecdb276c90aed85ab7a'))
        self.volume = struct.unpack('>129f', read(0x80369a68, 516, '3e0b06a0d15ecdf9b514e0fe79e1a9f5074efc8e19508afd11396ad2c684861d'))
        self.sine = struct.unpack('>1024h', read(0x80369d20, 2048, 'aeab0f3fd8f52860e77db653864b644271098f6a827d303674502861f55824b3'))
        # mcmdSetADSRFromCtrl's literal integer table, checked against its source.
        import re
        source = (Path(__file__).resolve().parents[1] / 'vendor/colosseum/src/musyx/runtime/synthmacros.c').read_text()
        body = source.split('static s32 midi2TimeTab[129] = {', 1)[1].split('};', 1)[0]
        self.times = tuple(map(int, re.findall(r'\d+', body)))
        if len(self.times) != 128:
            raise ValueError('Missing original MIDI envelope time table')


def envelope_ms(curve, override, controls, duration_ms, tables):
    """synth_adsr.c integer 1ms state recurrence, including exact key-off level.

    DSP ramps interpolate those millisecond endpoints across 32 output samples.
    The release count derives from current attenuation; no arbitrary tail cap.
    """
    if override:
        ca, cd, cs, cr = override
        attack, decay, release = (tables.times[controls[c]] for c in (ca, cd, cr))
        sustain = 193 - tables.indices[int(1023 * tables.dls[controls[cs]])]
        mode = 1
    else:
        attack, decay, sustain, release = struct.unpack('<4H', extent(curve, 0, 8)) if curve else (0, 0, 4096, 0)
        sustain = min(32767, sustain << 3)
        mode = 0
    count, state, value, index, delta = 0, 0, 0, 0, 0
    def next_state():
        nonlocal count, state, value, index, delta
        if state == 0:
            state = 1
            count = attack
            value = 0
            delta = 0x7fff0000 // count if count else 0
            if count:
                return
        if state == 1:
            state = 2
            value = 0x7fff0000
            if mode:
                index = 193 << 16
                count = decay * (((193 - sustain) << 16) // 193) >> 16
                delta = -(((193 - sustain) << 16) // count) if count else 0
            else:
                count = decay
                delta = -((0x7fff0000 - (sustain << 16)) // count) if count else 0
            if count:
                return
        if state == 2:
            state = 3
            index = sustain << 16
            value = (tables.attenuation[193 - sustain] if mode else sustain) << 16
            delta = 0
    next_state()
    output = [value / 0x7fff0000]
    for ms in range(10001):
        if ms == duration_ms:
            if mode:
                if state == 1:
                    index = (193 - tables.indices[min(1023, max(0, value >> 21))]) << 16
                # PPC float operations in adsrStartRelease use single precision.
                f32 = lambda n: struct.unpack('>f', struct.pack('>f', n))[0]
                count = int(f32(f32(f32(3.238342e-4) * f32(index)) * f32(release))) >> 12
                delta = -(index // count) if count else 0
            else:
                count = release
                delta = -(value // count) if count else 0
            state = 4
            if not count:
                output.append(0)
                return output
        if state != 3:
            if mode and state != 1:
                index += delta
                table_index = max(0, min(193, 193 - ((index + 0x8000) >> 16)))
                value = tables.attenuation[table_index] << 16
            else:
                value += delta
            count -= 1
            if not count:
                if state == 4:
                    output.append(0)
                    return output
                next_state()
        output.append(max(0, value / 0x7fff0000))
    raise ValueError('Source envelope exceeds ten-second runtime cue bound')


def mono_gain(volume, pan, tables):
    """hw_volconv.c CalcBus, then arithmetic mean of original L/R dry buses."""
    if pan == 128:
        return 0  # source special pan routes solely to surround; front L/R are zero
    v = max(0, min(1, volume)) * 127
    i = int(v)
    amplitude = tables.volume[i] * (1 - (v - i)) + tables.volume[i + 1] * (v - i)
    p = max(0, min(127, pan) - 1) * 65536 * 2.4220301e-7
    def pan_value(x):
        table = (0, .7079, 1, 1)
        i = int(x)
        return table[i] + (table[i + 1] - table[i]) * (x - i)
    return amplitude * (pan_value(p) + pan_value(2 - p)) / 2


def render(notes, bank):
    voices = []
    for start, duration, program, key, velocity, channel in notes:
        pages = bank.pages[channel == 9]
        if program not in pages:
            raise ValueError('Missing original PAGE program')
        controls = notes.control(channel, start)
        for sid, pitch, gain, curve, override, vibrato, pan, release_delay, release_scale, one_shot in bank.voices(pages[program], key, velocity / 127):
            if one_shot:
                pcm, rate, root, loop_start, loop_length = bank.sample(sid)
                if loop_length:
                    raise ValueError('Looped one-shot sample')
                duration = len(pcm) / rate / 2 ** ((pitch - root) / 12)
            envelope = envelope_ms(curve, override, controls, round((duration + release_delay) * 1000), bank.tables)
            voices.append((start, duration, channel, sid, pitch, gain, envelope, vibrato, pan, release_delay, release_scale))
    frames = max(round(start * RATE) + (len(env) - 1) * 32 for start, _, _, _, _, _, env, *_ in voices)
    if not 0 < frames <= RATE * 10:
        raise ValueError('Ringtone exceeds runtime cue bound')
    mix = [0.0] * frames
    for start, duration, channel, sid, pitch, gain, env, vibrato, pan, release_delay, release_scale in voices:
        pcm, rate, root, loop_start, loop_length = bank.sample(sid)
        step = rate / RATE * 2 ** ((pitch - root) / 12)
        first, pos = round(start * RATE), 0.0
        controls = notes.control(channel, start)
        automation = [(round((at - start) * RATE), controller, value) for at, controller, value in notes.controllers.get(channel, ()) if at > start]
        event = 0
        for i in range((len(env) - 1) * 32):
            while event < len(automation) and automation[event][0] <= i:
                _, controller, value = automation[event]
                controls[controller] = value
                event += 1
            if controls[91]:
                raise ValueError('Nonzero aux reverb requires source effect renderer')
            if loop_length and pos >= loop_start + loop_length:
                pos = loop_start + (pos - loop_start) % loop_length
            if pos >= len(pcm) - 1:
                break
            elapsed = i / RATE
            e = env[i // 32] + (env[i // 32 + 1] - env[i // 32]) * (i % 32) / 32
            macro_gain = 1 - (1 - release_scale) * min(1, max(0, (elapsed - duration) / release_delay)) if release_delay else 1
            output_gain = mono_gain(gain * macro_gain * (((controls[7] << 7) * (controls[11] << 7)) >> 14) / 16383, max(0, min(127, pan + controls[10] - 64)), bank.tables)
            index = int(pos)
            following = index + 1
            if loop_length and following >= loop_start + loop_length:
                following = loop_start
            value = pcm[index] + (pcm[min(following, len(pcm) - 1)] - pcm[index]) * (pos - index)
            mix[first + i] += value * output_gain * e
            # Every reached vibrato uses modulation scaling (mcmdVibrato bit0).
            # Preserve source 15ms phase and sine/quadrant lookup if modulation changes.
            offset = 0
            if vibrato and controls[1]:
                phase = ((i // 480 + 1) * 15 * 256) % (200 * 256)
                angle = ((phase * 16) // 200) & 4095
                sine_index = angle & 1023
                if angle & 1024:
                    sine_index = 1023 - sine_index
                sine = bank.tables.sine[sine_index] * (-1 if angle & 2048 else 1)
                voff = (sine * controls[1]) >> 7
                offset = ((15 * 256 // 100) * voff >> 4) / 65536
            pos += step * 2 ** (offset / 12)
    return struct.pack('<%dh' % frames, *(max(-32768, min(32767, round(x))) for x in mix))


def prepare(source, output, dol=None):
    source, output = Path(source), Path(output)
    profiles = {
        'common.fsys': (743136, 'f83b73c524498c1ec3ea1c04d06d391bcd6ca7e1ddd318fa33bcca0d6cf5b19a'),
        'bgm_archive.fsys': (271392, 'e5973cc859067245f9ed07e977b27ebfadd4670c834725cfeef4f123c1d82dc1'),
        'sound/snd_music.samp': (3659136, 'e702da18457ad57fa6ebff304d0914f59565bc7687c6fde110db1e1f884bf9e7'),
    }
    for name, (size, digest) in profiles.items():
        path = source / name
        data = path.read_bytes()
        if path.is_symlink() or len(data) != size or hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('Source differs from admitted GC6E01 bank: ' + name)
    if output.resolve() == source.resolve() or source.resolve() in output.resolve().parents:
        raise ValueError('Keep prepared audio outside original source directory')
    common = (source / 'common.fsys').read_bytes()
    archive = (source / 'bgm_archive.fsys').read_bytes()
    bank = Instruments(member(common, 0x1a50000), member(common, 0x1a60000), member(common, 0x1a80000), (source / 'sound/snd_music.samp').read_bytes(), Tables(dol or source / 'sys/main.dol'))
    cues, rows = [], []
    for ui, resource in enumerate(RESOURCE_ORDER):
        song = member(archive, RESOURCES[resource])
        notes = parse_song(song)
        pcm = render(notes, bank)
        cues.append((SOUNDS[ui], pcm))
        rows.append({'tone': ui, 'sound_id': SOUNDS[ui], 'resource_id': RESOURCES[resource], 'song_sha256': hashlib.sha256(song).hexdigest(), 'notes': len(notes), 'frames': len(pcm) // 2, 'controller_ids': sorted({ctrl for events in notes.controllers.values() for _, ctrl, _ in events})})
    output.mkdir(parents=True, exist_ok=True)
    (output / 'audio.c3u').write_bytes(encode_pack(cues))
    (output / 'ringtones.json').write_text(json.dumps({'rendering': 'original MusyX notes and instrument samples; bounded dry mono offline rendition', 'limitations': ['mono arithmetic downmix of original stereo dry buses', 'linear sample interpolation; original DSP resampling filter and hardware job phase differ', 'source global music/master faders use unity offline'], 'source_controllers': 'all encountered controls rendered; original aux send and modulation streams are zero', 'cues': rows}, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--dol', type=Path, help='Original GC6E01 sys/main.dol for admitted MusyX tables')
    args = parser.parse_args()
    prepare(args.source, args.output, args.dol)
