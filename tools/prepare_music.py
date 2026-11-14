#!/usr/bin/env python3
"""Render original MusyX music to a streamed DSP-ADPCM pack for the 3DS port.

Pokemon Colosseum sequences its music: GSsnd starts a MusyX song (an old,
pre-2.0 arrangement in bgm_archive.fsys) on song group 0 of snd_music_proj,
and the DSP mixes DSP-ADPCM samples from sound/snd_music.samp. The 3DS port
has no MusyX runtime, so this exporter plays each song offline and writes a
stream the console can feed to NDSP without decoding anything on the CPU.

What follows the decompilation exactly (vendor/colosseum/src/musyx/runtime):
  * seq_residual_801485FC.c / seq_exact_80146E88.c: track entries, pattern
    notes and controllers, pitch/modulation streams, programme pages, the
    master tempo track, 384 ticks per beat at a 1 ms sequencer step, track
    loop entries (0xFFFE) and the rewind to ARR.loopPoint[0].
  * synthmacros.c: the macro subset used by the music bank (wait, ADSR,
    ADSR from controllers, start sample, key off, add/set key, scale volume,
    envelope/fade-in, vibrato, key group; age counters only affect voice
    stealing and are skipped).
  * synth_adsr.c / hardware.c: linear and DLS ADSR at 1 ms resolution.
  * synth.c / hw_volconv.c: voice volume, MIDI volume/expression, the MusyX
    volume curve and pan law (read from main.dol).
Approximations (reported in the JSON beside the pack): linear interpolation
instead of the DSP polyphase resampler, no reverb/chorus aux buses (dry
mix), control updates at 5 ms blocks, voice-steal priorities ignored, and a
soft limiter when the dry mix exceeds full scale.

Outputs contain original game audio. Write them to a private directory and
copy them to the SD card (3ds/Colosseum3DS/music); never commit them.
"""
import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path
import struct
import sys

import numpy as np

from prepare_boot_media import verify_disc
from prepare_outskirt import member, rel_data
from prepare_game_state import Dol
from prepare_battle_init import COMMON_SHA1

RATE = 32000
BLOCK_MS = 5
BLOCK = RATE * BLOCK_MS // 1000
TICKS_PER_BEAT = 384
MAX_SECONDS = 600
CHUNK_FRAMES = 512                      # DSP-ADPCM frames per stream chunk
CHUNK_SAMPLES = CHUNK_FRAMES * 14
SEAM = 256                              # loop-seam crossfade, samples
MASTER = 0.8                            # fixed headroom for the dry mix
MAGIC = b"C3MU"
VERSION = 1

COMMON_MEMBER = 0x01C71C00
# Song groups by GSsndEntry.unk1: 0 = snd_music, 1 = snd_music_atmos.
BANKS = {0: (0x01A60000, 0x01A50000, 0x01A80000, "snd_music.samp"),
         1: (0x01AA0000, 0x01A90000, 0x01AC0000, "snd_music_atmos.samp")}
SOUND_TABLE = 0x13FE08                 # GSsndEntry[], 12-byte rows, REL125 section 4
SOUND_COUNT = 0x4D4                    # rows before the following float table
WAVE_TABLE = 0x1133D8                  # {fsys id, name offset} pairs in the raw common REL
WAVE_COUNT = 79
VOL_TAB = 0x80369A68                   # hw_volconv.c musyx_vol_tab[129], then pan_tab[4]
ATTEN_TAB = 0x803692C8                 # dspAttenuationTab u16[194]
SCALE2INDEX = 0x8036944C               # dspScale2IndexTab u8[1024]
DLS_VOL_TAB = 0x8036984C               # dspDLSVolTab f32[129]
MIDI2TIME = [
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 110, 120, 130, 140, 150, 160, 170, 190, 200,
    220, 230, 250, 270, 290, 310, 330, 350, 380, 410, 440, 470, 500, 540, 580, 620, 660, 710, 760,
    820, 880, 940, 1000, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 2000, 2100, 2300,
    2400, 2600, 2800, 3000, 3200, 3500, 3700, 4000, 4300, 4600, 4900, 5300, 5700, 6100, 6500, 7000,
    7500, 8100, 8600, 9300, 9900, 10000, 11000, 12000, 13000, 14000, 15000, 16000, 17000, 18000,
    19000, 21000, 22000, 24000, 26000, 28000, 30000, 32000, 34000, 37000, 39000, 42000, 45000,
    49000, 50000, 55000, 60000, 65000, 70000, 75000, 80000, 85000, 90000, 95000, 100000, 105000,
    110000, 115000, 120000, 125000, 130000, 135000, 140000, 145000, 150000, 155000, 160000, 165000,
    170000, 175000, 180000, 0]         # synthmacros.c midi2TimeTab (source, not disc data)

# Call sites that start music outside the field scripts (decomp evidence).
FIXED_SOUNDS = {
    0x46A: "title menu: gs_title_8001EF78.c fn_8001FD48 fn_80165A20(0x46A, 0, 0xFF)",
    0x4D1: "GS logo movie: movie.c fn_800364C8 fn_80165A20(0x04D1, 0, 0x7F)",
}


def be16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def s8(v):
    return v - 256 if v & 0x80 else v


# ----------------------------------------------------------------- bank data

class Bank:
    def __init__(self, proj, pool, sdir, samp, dol, group=0):
        self.pool, self.sdir, self.samp = pool, sdir, samp
        self.macros = self._objects(be32(pool, 0))
        self.tables = self._objects(be32(pool, 4))
        self.keymaps = self._objects(be32(pool, 8))
        self.layers = self._objects(be32(pool, 12))
        self._group(proj, group)
        self._samples = {}
        self.records = {}
        o = 0
        while be16(sdir, o) != 0xFFFF:
            r = sdir[o:o + 32]
            self.records[be16(r, 0)] = r
            o += 32
        d = Dol(dol)
        tabs = struct.unpack(">133f", d.read(VOL_TAB, 133 * 4))
        self.vol_tab = np.array(tabs[:129] + (1.0,), dtype=np.float64)
        self.pan_tab = list(tabs[129:133]) + [1.0]
        if abs(self.vol_tab[127] - 1.0) > 1e-6 or self.pan_tab[:4] != [0.0, struct.unpack(">f", struct.pack(">f", 0.7079))[0], 1.0, 1.0]:
            raise ValueError("main.dol volume curve/pan table differs from hw_volconv.c")
        self.atten = struct.unpack(">194H", d.read(ATTEN_TAB, 388))
        self.scale2index = d.read(SCALE2INDEX, 1024)
        self.dls_vol = struct.unpack(">129f", d.read(DLS_VOL_TAB, 129 * 4))

    def _objects(self, off):
        out = {}
        if not off:
            return out
        for _ in range(4096):
            size = be32(self.pool, off)
            if size == 0xFFFFFFFF:
                return out
            if size < 8 or off + size > len(self.pool):
                raise ValueError("Invalid MusyX pool object")
            out[be16(self.pool, off + 4)] = self.pool[off + 8:off + size]
            off += size
        raise ValueError("MusyX pool traversal exceeds bound")

    def _group(self, proj, group):
        # One song group (type 0): page tables and per-song MIDI setups.
        if be16(proj, 4) != group or be16(proj, 6) != 0 or be32(proj, be32(proj, 0)) != 0xFFFFFFFF:
            raise ValueError("Expected exactly one MusyX song group %d" % group)
        def pages(p):
            out = []
            while be16(proj, p) != 0xFFFF:
                out.append(struct.unpack_from(">HBBBB", proj, p))
                p += 6
                if len(out) > 128:
                    raise ValueError("Page table exceeds bound")
            return out
        self.normal, self.drum = pages(be32(proj, 28)), pages(be32(proj, 32))
        self.setups = {}
        p = be32(proj, 36)
        while be16(proj, p) != 0xFFFF:
            self.setups[be16(proj, p)] = [proj[p + 4 + 5 * c:p + 9 + 5 * c] for c in range(16)]
            p += 84

    def sample(self, sid):
        """DSP-ADPCM sample decoded once; returns (pcm float32, rate, root, loop_start, loop_end)."""
        if sid in self._samples:
            return self._samples[sid]
        r = self.records.get(sid)
        if r is None:
            self._samples[sid] = None
            return None
        offset, info, length = be32(r, 4), be32(r, 12), be32(r, 16)
        loop_start, loop_len = be32(r, 20), be32(r, 24)
        if length >> 24:
            raise ValueError("Only DSP-ADPCM music samples are supported")
        frames = length & 0xFFFFFF
        if loop_start == 0xFFFFFFFF or loop_start + loop_len > frames:
            loop_len = 0                      # sentinel / out-of-range loop: one-shot
        end = frames
        coef = struct.unpack_from(">16h", self.sdir, be32(r, 28) + 8)
        pcm = decode_adpcm(self.samp, offset, end, coef)
        rec = (pcm, info & 0xFFFFFF, info >> 24, loop_start if loop_len else None,
               loop_start + loop_len if loop_len else end)
        self._samples[sid] = rec
        return rec


def decode_adpcm(data, offset, frames, coef):
    nframes = (frames + 13) // 14
    raw = np.frombuffer(data, dtype=np.uint8, count=nframes * 8, offset=offset).reshape(nframes, 8)
    hi = (raw[:, 1:] >> 4).astype(np.int32)
    lo = (raw[:, 1:] & 15).astype(np.int32)
    nib = np.empty((nframes, 14), dtype=np.int32)
    nib[:, 0::2], nib[:, 1::2] = hi, lo
    nib[nib >= 8] -= 16
    ps = raw[:, 0].astype(np.int32)
    scaled = (nib << (ps & 15)[:, None]) << 11
    c1 = np.array(coef[0::2], dtype=np.int64)[ps >> 4 & 7]
    c2 = np.array(coef[1::2], dtype=np.int64)[ps >> 4 & 7]
    out = np.empty(nframes * 14, dtype=np.int32)
    h1 = h2 = 0
    sc = scaled.tolist()
    c1l, c2l = c1.tolist(), c2.tolist()
    k = 0
    for f in range(nframes):
        a, b, row = c1l[f], c2l[f], sc[f]
        for n in range(14):
            v = (row[n] + a * h1 + b * h2 + 1024) >> 11
            v = -32768 if v < -32768 else 32767 if v > 32767 else v
            out[k] = v
            h2, h1 = h1, v
            k += 1
    return (out[:frames].astype(np.float32) / 32768.0)


# ------------------------------------------------------------------ sequencer

class SongData:
    def __init__(self, data):
        self.d = data
        h = struct.unpack_from(">5I", data, 0)
        self.tTab, self.pTab, self.tmTab, self.mTrack, self.info = h
        self.loop_point = be32(data, 0x14)
        if self.info & 0x80000000:
            raise ValueError("Sectioned songs are not used by this bank")
        bpm = self.info & 0x0FFFFFFF
        self.bpm = bpm if self.info & 0x40000000 else bpm << 10
        self.tracks = [be32(data, self.tTab + 4 * i) for i in range(64)]
        self.midi = data[self.tmTab:self.tmTab + 64]
        self.tempo = []
        if self.mTrack:
            o = self.mTrack
            while be32(data, o) != 0xFFFFFFFF:
                t, b = be32(data, o), be32(data, o + 4)
                self.tempo.append((t, b >> 10 << 10 if self.info & 0x40000000 else b << 10))
                o += 8
                if len(self.tempo) > 4096:
                    raise ValueError("Tempo track exceeds bound")


def stream_values(d, o):
    """seq GetStreamValue: (delta time, signed delta) pairs until 0x80 0x00."""
    while True:
        b1, b2 = d[o], d[o + 1]
        if b1 == 0x80 and b2 == 0:
            return
        if b1 & 0x80:
            dt = ((b1 & 0x7F) << 8) | b2
            o += 2
        else:
            dt = b1
            o += 1
        b1, b2 = d[o], d[o + 1]
        if b1 & 0x80:
            v = ((b1 & 0x7F) << 8) | b2
            v |= (v & 0x4000) << 1
            v = v - 0x10000 if v & 0x8000 else v
            o += 2
        else:
            v = b1 | ((b1 & 0x40) << 1)
            v = s8(v)
            o += 1
        yield dt, v


def track_events(song, track, index):
    """GenerateNextTrackEvent for one track from entry `index`.

    Yields (tick, kind, payload): 'pattern', 'note', 'ctrl', 'pitch', 'mod',
    'loop' (payload = loop target entry index). Ends with the track."""
    d = song.d
    base = song.tracks[track]
    while True:
        e = base + index * 12
        time, prg, vel = be32(d, e), d[e + 4], d[e + 5]
        pattern = be16(d, e + 8)
        if pattern == 0xFFFF:
            return
        if pattern == 0xFFFE:
            yield time, "loop", be16(d, e + 10)
            return
        transpose, vel_add = s8(d[e + 10]), s8(d[e + 11])
        yield time, "pattern", (prg, vel)
        p = be32(d, song.pTab + 4 * pattern)
        pitch_off, mod_off = be32(d, p + 4), be32(d, p + 8)
        notes = p + 12
        pitch = stream_values(d, pitch_off) if pitch_off else iter(())
        mod = stream_values(d, mod_off) if mod_off else iter(())
        def nxt(it, at):
            for dt, v in it:
                return at + dt, v
            return 0x7FFFFFFF, 0
        pt, pv = nxt(pitch, 0)
        mt, mv = nxt(mod, 0)
        pval, mval, ltime = 0x2000, 0, 0
        while True:
            ptime = be16(d, notes) + ltime
            key, velo = d[notes + 2], d[notes + 3]
            if ptime >= pt or ptime >= mt:
                if pt <= mt:
                    pval = (pval + pv) & 0xFFFF
                    yield pt + time, "pitch", pval
                    pt, pv = nxt(pitch, pt)
                else:
                    mval = (mval + mv) & 0xFFFF
                    yield mt + time, "mod", mval
                    mt, mv = nxt(mod, mt)
                continue
            if key == 0xFF and velo == 0xFF:
                break
            ltime = ptime
            if key & 0x80:
                notes += 4
                yield ptime + time, "ctrl", (key & 0x7F, velo)
                continue
            if (key | velo) == 0:
                notes += 4
                continue
            length = be16(d, notes + 4)
            notes += 6
            yield ptime + time, "note", (key, velo, length, transpose, vel_add)
        index += 1


class Channel:
    def __init__(self):
        self.page = None
        self.cc = {7: 127, 10: 64, 11: 127, 1: 0, 64: 0}
        self.pitch = 0x2000
        self.mod = 0


class Sequencer:
    def __init__(self, bank, song, setup):
        self.bank, self.song = bank, song
        self.chan = [Channel() for _ in range(16)]
        self.norm_trans = self._trans(bank.normal)
        self.drum_trans = self._trans(bank.drum)
        for c in range(16 if setup else 0):   # no MIDI setup: seq start skips this (midiSetup NULL)
            prg, vol, pan, _rev, _cho = setup[c]
            self.prg_change(c, prg)
            self.chan[c].cc[7], self.chan[c].cc[10] = vol, pan
        self.bpm = song.bpm
        self.tempo_i = 0
        self.time = 0.0                       # song ticks (time[].high + low)
        self.abs = 0.0                        # monotonic ticks for note ends
        self.index = [0] * 64
        self.loops = 0
        self.ended = False
        self._init_events()
        self._apply_tempo()

    @staticmethod
    def _trans(pages):
        t = [0xFF] * 128
        for i, page in enumerate(pages):
            t[page[3]] = i
        return t

    def prg_change(self, c, prg):
        pages, trans = (self.bank.drum, self.drum_trans) if c == 9 else (self.bank.normal, self.norm_trans)
        i = trans[prg & 0x7F]
        if i != 0xFF:
            self.chan[c].page = pages[i]

    def _init_events(self):
        self.heap, self.gens, self.seq = [], {}, 0
        self.loop_flag = False
        for t in range(64):
            if self.song.tracks[t] and self.index[t] is not None:
                g = track_events(self.song, t, self.index[t])
                self.gens[t] = g
                self._push(t)

    def _push(self, t):
        g = self.gens.get(t)
        if g is None:
            return
        for ev in g:
            self.seq += 1
            heapq.heappush(self.heap, (ev[0], self.seq, t, ev[1], ev[2]))
            return
        self.gens.pop(t)
        self.index[t] = None

    def _apply_tempo(self):
        tempo = self.song.tempo
        while self.tempo_i < len(tempo) and tempo[self.tempo_i][0] <= int(self.time):
            self.bpm = tempo[self.tempo_i][1]
            self.tempo_i += 1

    def step_ms(self, synth):
        """One fn_801496A0(256) call: tempo, events, note-offs, advance."""
        self._apply_tempo()
        delta = self.bpm * 256 / 40960000.0
        now = int(self.time)
        events_active = True
        while True:
            if self.heap and self.heap[0][0] <= now:
                tick, _, t, kind, data = heapq.heappop(self.heap)
                if kind == "loop":
                    self.loop_flag = True
                    self.index[t] = data
                    self.gens.pop(t, None)
                    continue
                self._event(synth, t, tick, kind, data)
                self._push(t)
                continue
            if self.heap:
                break
            if not self.loop_flag:
                events_active = False
                break
            # All tracks ended or looped: rewind to loopPoint[0].
            self.time = float(self.song.loop_point) + (self.time - int(self.time))
            now = int(self.time)
            self.tempo_i = 0
            self.bpm = self.song.bpm
            self._apply_tempo()
            self.loops += 1
            synth.on_rewind(self.loops)
            self._init_events()
        synth.note_offs(self.abs)
        self.time += delta
        self.abs += delta
        if not events_active:
            self.ended = True

    def _event(self, synth, t, tick, kind, data):
        c = self.song.midi[t]
        if c == 0xFF:
            return
        ch = self.chan[c]
        if kind == "pattern":
            prg, vel = data
            ch.pitch, ch.mod = 0x2000, 0
            synth.controller(c, "pitch")
            if prg != 0xFF:
                self.prg_change(c, prg)
            if vel != 0xFF:
                ch.cc[7] = vel
                synth.controller(c, 7)
        elif kind == "pitch":
            ch.pitch = data & 0x3FFF
            synth.controller(c, "pitch")
        elif kind == "mod":
            ch.mod = data & 0x3FFF
            synth.controller(c, "mod")
        elif kind == "ctrl":
            value, sel = data
            if sel == 0:
                self.prg_change(c, value)
            elif sel == 1:
                pass                          # 0x82 channel pressure: no reached consumer
            elif sel & 0x80:
                n = sel & 0x7F
                if n == 0x7B:
                    synth.all_notes_off(c)
                elif n == 0x79:
                    ch.cc.update({1: 0, 64: 0, 11: 127})
                    ch.pitch = 0x2000
                elif n not in (0x68, 0x69, 0x6A):
                    ch.cc[n] = value
                    synth.controller(c, n)
        elif kind == "note":
            key, vel, length, transpose, vel_add = data
            if ch.page is None or ch.page[0] == 0xFFFF:
                return
            key = max(0, min(127, key + transpose))
            vel = max(0, min(127, vel + vel_add))
            end = self.abs - (int(self.time) - tick) + length
            synth.start(ch.page, c, key, vel, t, end)


# ---------------------------------------------------------------------- synth

class Adsr:
    """synth_adsr.c state machine; cnt counts 1 ms handler calls."""

    def __init__(self, bank, mode, a, d, s, r):
        self.bank, self.mode = bank, mode
        self.a, self.d, self.s, self.r = a, d, s, r
        self.state, self.vol, self.index, self.delta, self.cnt = 0, 0, 0, 0, 0
        self.done = self._change()

    def _atten(self):
        i = 193 - ((self.index + 0x8000) >> 16)
        return self.bank.atten[max(0, min(193, i))] << 16

    def _change(self):
        while True:
            if self.mode == 0:
                if self.state == 0:
                    self.cnt = self.a
                    if self.cnt:
                        self.state, self.vol, self.delta = 1, 0, 0x7FFF0000 // self.a
                        return False
                    self.state = 1
                if self.state == 1:
                    self.cnt = self.d
                    if self.cnt:
                        self.state, self.vol = 2, 0x7FFF0000
                        self.delta = -((0x7FFF0000 - self.s * 0x10000) // self.d)
                        return False
                    self.state = 2
                if self.state == 2:
                    if self.s:
                        self.state, self.vol, self.delta = 3, self.s << 16, 0
                        return False
                self.vol = 0
                return True
            if self.state == 0:
                self.cnt = self.a
                if self.cnt:
                    self.state, self.vol, self.delta = 1, 0, 0x7FFF0000 // self.cnt
                    return False
                self.state = 1
            if self.state == 1:
                self.cnt = (self.d * (((0xC1 - self.s) * 0x10000) // 0xC1)) >> 16
                if self.cnt:
                    self.state, self.vol, self.index = 2, 0x7FFF0000, 0xC10000
                    self.delta = -(((0xC1 - self.s) * 0x10000) // self.cnt)
                    return False
                self.state = 2
            if self.state == 2 and self.s:
                self.state, self.index, self.delta = 3, self.s << 16, 0
                self.vol = self._atten()
                return False
            self.vol = 0
            return True

    def release(self):
        if self.mode == 0:
            self.state, self.cnt = 4, self.r
            if not self.r:
                self.vol, self.cnt, self.delta = 0, 1, 0
                return
            self.delta = -(self.vol // self.r)
            return
        if self.state == 1:
            self.index = (193 - self.bank.scale2index[self.vol >> 21]) * 0x10000
        self.cnt = int(3.238342e-4 * self.index * self.r) >> 12
        self.state = 4
        if not self.cnt:
            self.cnt, self.delta, self.index, self.vol = 1, 0, 0, 0
            return
        self.delta = -(self.index // self.cnt)

    def tick(self):
        """Advance 1 ms; returns the level (0..1) at the start of the step."""
        start = self.vol / 0x7FFF0000
        if self.done or self.state == 3:
            return start
        if self.mode == 0 or self.state == 1:
            self.vol += self.delta
        else:
            self.index += self.delta
            self.vol = self._atten()
        self.cnt -= 1
        if self.cnt <= 0:
            if self.state == 4:
                self.vol, self.done = 0, True
            else:
                self.done = self._change()
        return max(0.0, start)


class Voice:
    __slots__ = ("macro", "pc", "ch", "track", "org_note", "note", "detune", "volume", "org_volume",
                 "pan", "wait", "wait_time", "wait_keyoff", "wait_sample", "keyoff", "pedal_off",
                 "adsr", "adsr_spec", "sample", "pos", "step", "playing", "active", "start_time",
                 "vib", "vib_mod", "env", "key_group", "age", "dead", "released", "unsupported")

    def __init__(self):
        for k in self.__slots__:
            setattr(self, k, None)


class Synth:
    def __init__(self, bank, seq_ms):
        self.bank = bank
        self.voices = []
        self.seq = None
        self.ms = 0
        self.unsupported = {}
        self.rewinds = []
        self.notes = []                       # (end_abs_tick, voice ids)
        self.next_id = 0

    # sequencer hooks
    def on_rewind(self, count):
        self.rewinds.append(self.ms)

    def controller(self, c, which):
        pass                                  # values are read live from the channel

    def all_notes_off(self, c):
        for v in self.voices:
            if v.ch == c and not v.keyoff:
                self.external_keyoff(v)

    def note_offs(self, abs_tick):
        keep = []
        for end, vs in self.notes:
            if end <= abs_tick:
                for v in vs:
                    self.external_keyoff(v)
            else:
                keep.append((end, vs))
        self.notes = keep

    def start(self, page, c, key, vel, track, end):
        started = []
        self._start_obj(page[0], c, key, vel, 64, track, started, 0)
        if started:
            self.notes.append((end, started))

    def _start_obj(self, obj, c, key, vel, pan, track, out, depth):
        if depth > 4:
            return
        if obj & 0x8000:
            data = self.bank.layers.get(obj)
            if data is None:
                return
            n = be32(data, 0)
            for i in range(min(n, 64)):
                e = 4 + i * 12
                mac, lo, hi = be16(data, e), s8(data[e + 2]), s8(data[e + 3])
                tr, vol, _prio, _span, lpan = s8(data[e + 4]), data[e + 5], data[e + 6], data[e + 7], data[e + 8]
                if lo <= key <= hi:
                    self._start_obj(mac, c, max(0, min(127, key + tr)), vel * vol // 127, lpan, track, out, depth + 1)
        elif obj & 0x4000:
            data = self.bank.keymaps.get(obj)
            if data is None:
                return
            e = key * 8
            mac, tr, kpan = be16(data, e), s8(data[e + 2]), s8(data[e + 3])
            if mac != 0xFFFF:
                self._start_obj(mac, c, max(0, min(127, key + tr)), vel, kpan if kpan >= 0 else pan, track, out, depth + 1)
        else:
            code = self.bank.macros.get(obj)
            if code is None:
                return
            v = Voice()
            v.macro, v.pc, v.ch, v.track = code, 0, c, track
            v.org_note = v.note = key
            v.detune = 0
            v.volume = v.org_volume = vel << 16
            v.pan = pan << 16
            v.wait = 0
            v.wait_time = v.start_time = self.ms
            v.wait_keyoff = v.wait_sample = False
            v.keyoff = v.pedal_off = v.released = v.dead = False
            v.adsr = v.adsr_spec = v.sample = None
            v.playing, v.active = False, True
            v.vib, v.vib_mod, v.env, v.key_group = None, False, None, 0
            v.age = self.next_id
            self.next_id += 1
            self.voices.append(v)
            out.append(v)
            self.run_macro(v)

    def external_keyoff(self, v):
        """macSetExternalKeyoff, with the sustain pedal holding it back."""
        if v.dead:
            return
        if self.seq.chan[v.ch].cc.get(64, 0) >= 64:
            v.pedal_off = True
            return
        v.keyoff = True
        if v.active and v.wait_keyoff:
            v.wait = 0
            self.run_macro(v)

    def pedal_update(self):
        for v in self.voices:
            if v.pedal_off and self.seq.chan[v.ch].cc.get(64, 0) < 64:
                v.pedal_off = False
                self.external_keyoff(v)

    def hw_keyoff(self, v):
        if v.playing and not v.released:
            v.released = True
            if v.adsr is not None:
                v.adsr.release()
            else:
                v.playing = False

    def ms_to_ticks_ms(self, value, ms_flag):
        if ms_flag:
            return value
        bpm = self.seq.bpm >> 10
        return value * 156.25 / max(1, bpm)

    def do_wait(self, v, a, b):
        ms = b >> 16
        if not ms:
            return False
        if (a >> 8) & 1:
            if v.keyoff:
                return False
            v.wait_keyoff = True
        else:
            v.wait_keyoff = False
        if (a >> 24) & 1:
            if not v.playing:
                return False
            v.wait_sample = True
        else:
            v.wait_sample = False
        if (a >> 16) & 1:
            ms = (v.age * 2654435761 % 4294967291) % ms
        if ms != 0xFFFF:
            t = self.ms_to_ticks_ms(ms, (b >> 8) & 1)
            target = (v.start_time if b & 1 else v.wait_time) + t
            if target <= self.ms:
                v.wait_time = target
                v.wait = 0
                return False
            v.wait = target
        else:
            v.wait = float("inf")
        return True

    def run_macro(self, v):
        if not v.active:
            return
        if v.wait:
            return
        code = v.macro
        for _ in range(256):
            if v.pc * 8 + 8 > len(code):
                v.active = False
                return
            a, b = struct.unpack_from(">II", code, v.pc * 8)
            v.pc += 1
            op = a & 0x7F
            if op == 0x00:                    # end of macro: hardware voice keeps playing
                v.active = False
                return
            if op == 0x07:
                if self.do_wait(v, a, b | 0x100):
                    return
            elif op == 0x0C:
                t = self.bank.tables.get((a >> 8) & 0xFFFF)
                if t is not None and len(t) >= 8:
                    if (a >> 24) & 0xFF:
                        self.note_unsupported("dls_adsr_table")
                    else:
                        at, dt, sl, rt = struct.unpack_from("<4H", t, 0)
                        v.adsr_spec = (0, at, dt, min(0x7FFF, sl << 3), rt)
            elif op == 0x16:
                cc = self.seq.chan[v.ch].cc
                sscale = self.bank.dls_vol[cc.get((a >> 24) & 0xFF, 0) & 0x7F]
                sl = 193 - self.bank.scale2index[min(1023, int(1023.0 * sscale))]
                at = MIDI2TIME[cc.get((a >> 8) & 0xFF, 0) & 0x7F] & 0xFFFF
                dt = MIDI2TIME[cc.get((a >> 16) & 0xFF, 0) & 0x7F] & 0xFFFF
                rt = MIDI2TIME[cc.get(b & 0xFF, 0) & 0x7F]
                v.adsr_spec = (1, at, dt, sl, rt)
            elif op == 0x0D:
                scale = (a >> 8) & 0xFF
                base = v.org_volume if (b >> 8) & 0xFF else v.volume
                v.volume = min(0x7F0000, base * scale // 0x7F + (((a >> 16) & 0xFF) << 16))
            elif op in (0x0F, 0x14):
                t = self.ms_to_ticks_ms(b >> 16, (b >> 8) & 1)
                tvol = min(0x7F0000, (v.volume * ((a >> 8) & 0xFF) >> 7) + (((a >> 16) & 0xFF) << 16))
                start = v.volume if op == 0x0F else 0
                v.env = (self.ms, max(1.0, t), start, tvol)
                v.volume = start
            elif op == 0x10:
                self.start_sample(v, a, b)
            elif op == 0x12:
                if self.seq.chan[v.ch].cc.get(64, 0) < 64:
                    self.hw_keyoff(v)
                else:
                    v.pedal_off = True
            elif op in (0x18, 0x19):
                if op == 0x19:
                    v.note = (a >> 8) & 0x7F
                elif (a >> 24) & 0xFF:
                    v.note = max(0, min(127, v.org_note + s8((a >> 8) & 0xFF)))
                else:
                    v.note = max(0, min(127, v.note + s8((a >> 8) & 0xFF)))
                v.detune = s8((a >> 16) & 0xFF)
                if self.do_wait(v, 4, b):
                    return
            elif op == 0x1C:
                t = self.ms_to_ticks_ms(b >> 16, (b >> 8) & 1)
                if t:
                    kr, cr = s8((a >> 8) & 0xFF), s8((a >> 16) & 0xFF)
                    if kr < 0:
                        cents, phase = -(abs(cr)) - (-kr) * 100, 0.5
                    elif cr < 0:
                        cents, phase = (kr * 100 + cr if kr else -cr), (0.0 if kr else 0.5)
                    else:
                        cents, phase = kr * 100 + cr, 0.0
                    v.vib = (abs(cents), t, phase)
                    v.vib_mod = bool(((a >> 24) & 0xFF) & 3)
                else:
                    v.vib = None
            elif op == 0x59:
                kg, kill = (a >> 8) & 0xFF, ((a >> 16) & 0xFF) != 0
                v.key_group = 0
                if kg:
                    for o in self.voices:
                        if o is not v and o.key_group == kg and not o.dead:
                            if kill:
                                o.dead = True
                            else:
                                self.external_keyoff(o)
                    v.key_group = kg
            elif op in (0x30, 0x31, 0x38):
                pass                          # age counters: voice-steal priority only
            elif op == 0x15:
                pass                          # surround panning: no rear channel in stereo
            else:
                handled = self.extra_op(v, op, a, b)
                if handled is None:
                    self.note_unsupported("macro_op_%02x" % op)
                elif handled:
                    return                    # the voice now waits (or ended)
        v.active = False

    def extra_op(self, v, op, a, b):
        """Hook for macro opcodes outside the music subset (prepare_battle_sfx).

        Returns None when unsupported, True when the voice stops stepping, False
        to continue with the next step."""
        return None

    def note_unsupported(self, name):
        self.unsupported[name] = self.unsupported.get(name, 0) + 1

    def start_sample(self, v, a, b):
        sid = (a >> 8) & 0xFFFF
        rec = self.bank.sample(sid)
        if rec is None:
            self.note_unsupported("missing_sample")
            return
        pcm, rate, root, ls, le = rec
        mode = (a >> 24) & 0xFF
        offset = b if mode == 0 else 0
        if mode == 1:
            offset = (0x7F - (v.volume >> 16)) * b // 0x7F
        elif mode == 2:
            offset = (v.volume >> 16) * b // 0x7F
        v.sample = rec
        v.pos = float(min(offset, len(pcm) - 1))
        v.playing, v.released = True, False
        spec = v.adsr_spec
        v.adsr = Adsr(self.bank, *spec) if spec else None

    def wake_waits(self):
        for v in self.voices:
            if v.active and v.wait and v.wait != float("inf") and v.wait <= self.ms:
                v.wait_time = v.wait
                v.wait = 0
                self.run_macro(v)

    def pitch_ratio(self, v, rate, root):
        ch = self.seq.chan[v.ch]
        semis = v.note - root + v.detune / 100.0 + (ch.pitch - 0x2000) / 8192.0 * 2.0
        if v.vib:
            cents, period, phase = v.vib
            depth = cents * (ch.mod / 16383.0 if v.vib_mod else 1.0)
            if depth:
                ph = ((self.ms - v.start_time) / period + phase) % 1.0
                semis += depth / 100.0 * math.sin(2 * math.pi * ph)
        return rate * 2 ** (semis / 12.0) / RATE

    def gains(self, v):
        ch = self.seq.chan[v.ch]
        vol = v.volume
        if v.env:
            t0, dur, start, target = v.env
            f = (self.ms - t0) / dur
            if f >= 1:
                vol, v.env = target, None
            else:
                vol = start + (target - start) * f
            v.volume = int(vol)
        post = (vol / 8323072.0) * (ch.cc[7] / 127.0) * (ch.cc[11] / 127.0)
        post = min(1.0, max(0.0, post))
        i = int(post * 127)
        frac = post * 127 - i
        tab = self.bank.vol_tab
        level = (1 - frac) * tab[i] + frac * tab[min(128, i + 1)]
        pan = v.pan + (ch.cc[10] - 64) * 0x10000
        pan = max(0, min(0x7F0000, pan))
        pan = 0 if pan <= 0x10000 else pan - 0x10000
        p = pan * 2.4220301e-07
        def interp(x):
            i = min(3, int(x))
            f = x - i
            return (1 - f) * self.bank.pan_tab[i] + f * self.bank.pan_tab[i + 1]
        return level * interp(2.0 - p), level * interp(p)

    def render_block(self, out_l, out_r, env_ms):
        """Mix every sounding voice into one 5 ms block."""
        alive = []
        for v in self.voices:
            if v.dead:
                continue
            if not v.playing:
                if v.active or v.wait:
                    alive.append(v)
                continue
            pcm, rate, root, ls, le = v.sample
            step = self.pitch_ratio(v, rate, root)
            gl, gr = self.gains(v)
            if v.adsr is not None:
                levels = np.array([v.adsr.tick() for _ in range(env_ms)], dtype=np.float32)
                env = np.repeat(levels, BLOCK // env_ms)
                if v.adsr.done:
                    v.playing = False
            else:
                env = 1.0
            x = v.pos + step * np.arange(BLOCK, dtype=np.float64)
            if ls is not None:
                span = le - ls
                over = x >= le
                if over.any():
                    x = np.where(over, ls + np.mod(x - ls, span), x)
                nxt = v.pos + step * BLOCK
                v.pos = ls + (nxt - ls) % span if nxt >= le else nxt
                i0 = x.astype(np.int64)
                i1 = i0 + 1
                i1[i1 >= le] = ls
            else:
                valid = x < len(pcm) - 1
                if not valid.all():
                    n = int(valid.sum())
                    x = x[:n]
                    if not isinstance(env, float):
                        env = env[:n]
                    v.playing = False
                v.pos += step * BLOCK
                i0 = x.astype(np.int64)
                i1 = np.minimum(i0 + 1, len(pcm) - 1)
            frac = (x - i0).astype(np.float32)
            s = pcm[i0] * (1 - frac) + pcm[i1] * frac
            s = s * env
            n = len(s)
            out_l[:n] += s * gl
            out_r[:n] += s * gr
            if self.ms - v.start_time > 120000:
                v.playing = False
            if not v.playing and v.wait_sample and v.active:
                v.wait = 0
                self.run_macro(v)
            if v.playing or v.active:
                alive.append(v)
        self.voices = alive


def render_song(bank, song, setup, loops_wanted):
    """Run sequencer and synth; returns (stereo int16, loop_start, loop_end)."""
    seq = Sequencer(bank, song, setup)
    synth = Synth(bank, 1)
    synth.seq = seq
    blocks_l, blocks_r = [], []
    loop_marks = []
    first_loop_point = None
    max_ms = MAX_SECONDS * 1000
    ms = 0
    tail_ms = 0
    while ms < max_ms:
        for sub in range(BLOCK_MS):
            synth.ms = ms + sub
            before = len(synth.rewinds)
            seq.step_ms(synth)
            synth.pedal_update()
            synth.wake_waits()
            if first_loop_point is None and int(seq.time) >= song.loop_point and seq.loops == 0:
                first_loop_point = ms + sub
            if len(synth.rewinds) != before:
                loop_marks.append(ms)
        l = np.zeros(BLOCK, dtype=np.float32)
        r = np.zeros(BLOCK, dtype=np.float32)
        synth.ms = ms
        synth.render_block(l, r, BLOCK_MS)
        blocks_l.append(l)
        blocks_r.append(r)
        ms += BLOCK_MS
        if loops_wanted and len(loop_marks) >= 2:
            break
        if seq.ended and not seq.loop_flag:
            if not synth.voices:
                tail_ms += BLOCK_MS
                if tail_ms >= 50:
                    break
    if ms >= max_ms and not seq.ended and not loops_wanted:
        raise ValueError("Song did not end within the render bound")
    L = np.concatenate(blocks_l)
    R = np.concatenate(blocks_r)
    stats = {"voices_unsupported": synth.unsupported}
    if loops_wanted and len(loop_marks) >= 2:
        # Stream = first pass up to the loop point, then the second pass's
        # loop body: that body ends exactly where it restarts (state after a
        # rewind), so the console loop seam carries the right release tails.
        lp = first_loop_point if first_loop_point is not None else 0
        a, b = loop_marks[0] * RATE // 1000, loop_marks[1] * RATE // 1000
        lp_s = lp * RATE // 1000
        # The body's last samples lead into a rewind, as do the samples just
        # before the first rewind (a), which flow into the body's start.
        # Crossfading toward the latter makes the console loop seam continuous.
        k = min(SEAM, b - a, a)
        w = np.linspace(0.0, 1.0, k, dtype=np.float32)
        for X in (L, R):
            X[b - k:b] = X[b - k:b] * (1 - w) + X[a - k:a] * w
        L = np.concatenate([L[:lp_s], L[a:b]])
        R = np.concatenate([R[:lp_s], R[a:b]])
        loop_start, loop_end = lp_s, lp_s + (b - a)
    else:
        loop_start = loop_end = None
    L, R = L * MASTER, R * MASTER
    peak = float(max(np.abs(L).max(initial=0), np.abs(R).max(initial=0)))
    stats["peak_before_limit"] = round(peak, 4)
    st = np.stack([L, R])
    if peak > 0.98:
        st = np.tanh(st / 0.98) * 0.98         # soft limiter, reported
    pcm = np.clip(np.round(st * 32767), -32768, 32767).astype(np.int32)
    return pcm, loop_start, loop_end, stats


# -------------------------------------------------------------------- encoder

def choose_coefs(x):
    """Eight predictor pairs per channel: least-squares fits clustered (k-means)."""
    n = (len(x) - 2) // 14
    if n < 8:
        return [(0, 0), (2048, 0), (0, 0), (0, 0), (0, 0), (0, 0), (0, 0), (0, 0)]
    xs = x[: n * 14 + 2].astype(np.float64)
    y = xs[2:].reshape(n, 14)
    h1 = xs[1:-1].reshape(n, 14)
    h2 = xs[:-2].reshape(n, 14)
    a11, a12, a22 = (h1 * h1).sum(1), (h1 * h2).sum(1), (h2 * h2).sum(1)
    b1, b2 = (y * h1).sum(1), (y * h2).sum(1)
    det = a11 * a22 - a12 * a12
    ok = (det > 1e-3) & (a11 > 0)
    c1 = np.where(ok, (b1 * a22 - b2 * a12) / np.where(ok, det, 1), 0)
    c2 = np.where(ok, (a11 * b2 - a12 * b1) / np.where(ok, det, 1), 0)
    pts = np.stack([c1, c2], 1)
    pts = pts[(np.abs(pts) < 3.9).all(1) & ok]
    seeds = np.array([[0, 0], [1, 0], [1.5, -0.5], [1.75, -0.8], [1.9, -0.92], [1.95, -0.97], [0.5, 0], [1.2, -0.3]])
    cent = seeds.copy()
    if len(pts) >= 8:
        for _ in range(25):
            dist = ((pts[:, None, :] - cent[None]) ** 2).sum(2)
            lab = dist.argmin(1)
            for k in range(8):
                m = pts[lab == k]
                if len(m):
                    cent[k] = m.mean(0)
    cent[0] = (0, 0)
    out = []
    for c in cent:
        out.append((int(max(-32768, min(32767, round(c[0] * 2048)))),
                    int(max(-32768, min(32767, round(c[1] * 2048))))))
    return out


def encode_chunks(x, coefs, starts, lengths):
    """Vectorised GC DSP-ADPCM encoder; each chunk starts from stored history.

    Returns (data bytes per chunk, (ps, hist1, hist2) per chunk). Chunks are
    independent so they encode in parallel; history is the source PCM before
    the chunk, which the decoder is told explicitly (ndspAdpcmData)."""
    C = len(starts)
    frames = max((n + 13) // 14 for n in lengths)
    X = np.zeros((C, frames * 14), dtype=np.int64)
    for i, (s, n) in enumerate(zip(starts, lengths)):
        X[i, :n] = x[s:s + n]
    h1 = np.array([int(x[s - 1]) if s >= 1 else 0 for s in starts], dtype=np.int64)
    h2 = np.array([int(x[s - 2]) if s >= 2 else 0 for s in starts], dtype=np.int64)
    hist = (h1.copy(), h2.copy())
    c1 = np.array([c[0] for c in coefs], dtype=np.int64)[None, :, None]
    c2 = np.array([c[1] for c in coefs], dtype=np.int64)[None, :, None]
    scales = np.arange(12, dtype=np.int64)[None, None, :]
    out = np.zeros((C, frames, 8), dtype=np.uint8)
    for f in range(frames):
        S = X[:, f * 14:(f + 1) * 14]
        y1 = np.broadcast_to(h1[:, None, None], (C, 8, 12)).copy()
        y2 = np.broadcast_to(h2[:, None, None], (C, 8, 12)).copy()
        err = np.zeros((C, 8, 12), dtype=np.int64)
        nibs = np.zeros((C, 8, 12, 14), dtype=np.int64)
        for n in range(14):
            pred = c1 * y1 + c2 * y2
            res = (S[:, n][:, None, None] << 11) - pred
            step = np.int64(2048) << scales
            q = np.floor_divide(res + (step >> 1), step)
            q = np.clip(q, -8, 7)
            dec = ((q << scales) << 11) + pred + 1024 >> 11
            dec = np.clip(dec, -32768, 32767)
            e = dec - S[:, n][:, None, None]
            err += e * e
            nibs[..., n] = q
            y2, y1 = y1, dec
        flat = err.reshape(C, 96).argmin(1)
        p, sc = flat // 12, flat % 12
        rows = np.arange(C)
        sel = nibs[rows, p, sc] & 15
        h1 = y1[rows, p, sc]
        h2 = y2[rows, p, sc]
        out[:, f, 0] = (p << 4 | sc).astype(np.uint8)
        out[:, f, 1:] = (sel[:, 0::2] << 4 | sel[:, 1::2]).astype(np.uint8)
    data = []
    for i, n in enumerate(lengths):
        data.append(out[i, :(n + 13) // 14].tobytes())
    return data, hist


def plan_chunks(total, loop_start):
    """Chunk boundaries: every CHUNK_SAMPLES, plus one at the loop start."""
    cuts = set(range(0, total, CHUNK_SAMPLES))
    if loop_start:
        cuts.add(loop_start)
        cuts.update(range(loop_start, total, CHUNK_SAMPLES))
    cuts = sorted(c for c in cuts if c < total)
    starts = cuts
    lengths = [b - a for a, b in zip(cuts, cuts[1:] + [total])]
    return starts, lengths


def encode_stream(pcm, loop_start, loop_end):
    total = pcm.shape[1]
    if loop_start is not None:
        total = loop_end
    total -= total % 14 if loop_start is None else 0
    if loop_start is not None:
        # keep the loop body a whole number of frames so every chunk starts
        # on an ADPCM frame boundary.
        loop_start -= loop_start % 14
        body = loop_end - loop_start
        body -= body % 14
        total = loop_start + body
        loop_end = total
    starts, lengths = plan_chunks(total, loop_start)
    for s in starts:
        if s % 14:
            raise ValueError("Chunk start is not frame aligned")
    channels = []
    for c in range(2):
        x = pcm[c, :total]
        coefs = choose_coefs(x)
        data, (h1, h2) = encode_chunks(x, coefs, starts, lengths)
        channels.append((coefs, data, h1, h2))
    return total, loop_start, starts, lengths, channels


def write_stream(path, total, loop_start, starts, lengths, channels):
    """C3MU v1: header, per-channel coefficients, chunk table, chunk data.

    header  : magic, version, rate, channels, total samples, loop chunk
              (0xFFFFFFFF none), chunk count, max chunk bytes per channel
    coefs   : 2 x 16 s16
    table   : per chunk u32 offset, u32 samples, u32 bytes per channel,
              then per channel u16 ps, s16 hist1, s16 hist2, u16 pad
    data    : per chunk left bytes then right bytes
    All little-endian (ARM)."""
    count = len(starts)
    loop_chunk = starts.index(loop_start) if loop_start is not None else 0xFFFFFFFF
    max_bytes = max(len(d) for _, data, _, _ in channels for d in data)
    head = struct.pack("<4sIIIIIII", MAGIC, VERSION, RATE, 2, total, loop_chunk, count, max_bytes)
    coefs = b"".join(struct.pack("<16h", *[v for pair in ch[0] for v in pair]) for ch in channels)
    table_size = count * (12 + 2 * 8)
    offset = len(head) + len(coefs) + table_size
    table = bytearray()
    body = bytearray()
    for i in range(count):
        left, right = channels[0][1][i], channels[1][1][i]
        if len(left) != len(right):
            raise ValueError("Channel chunk sizes differ")
        table += struct.pack("<III", offset + len(body), lengths[i], len(left))
        for ch in channels:
            ps = ch[1][i][0]
            table += struct.pack("<HhhH", ps, int(ch[2][i]), int(ch[3][i]), 0)
        body += left + right
    path.write_bytes(head + coefs + bytes(table) + bytes(body))


# ---------------------------------------------------------------- top level

def sound_table(rel, raw_rel):
    rows = {}
    for i in range(SOUND_COUNT):
        fl, group, rv, wi, wid, _u6, _w = struct.unpack_from(">BBBBHHI", rel, SOUND_TABLE + i * 12)
        if not fl & 0x80:
            rows[i] = {"wave": wi, "song": wid, "reverb": rv, "group": group}
    waves = []
    for i in range(WAVE_COUNT):
        fid = be32(raw_rel, WAVE_TABLE + i * 8)
        waves.append(fid)
    return rows, waves


def admit(path, label):
    path = Path(path)
    if path.is_symlink():
        raise ValueError("Symbolic links are not accepted: " + label)
    return path.read_bytes()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("disc", type=Path, help="extracted GC6E01 disc root (sys/, files/)")
    ap.add_argument("output", type=Path, help="new private directory for the music pack")
    ap.add_argument("--wav", action="store_true", help="also write the dry PCM as WAV (review aid)")
    ap.add_argument("--sounds", default="all",
                    help="comma-separated sound IDs (hex or decimal) to render, or 'all'")
    args = ap.parse_args(argv)
    disc = verify_disc(args.disc)
    out = args.output.resolve()
    root = Path(__file__).resolve().parents[1]
    if (out == root or root in out.parents) and (root / "private") not in out.parents:
        raise ValueError("Music output in the repository must be under ignored private/")
    out.mkdir(parents=True, exist_ok=True)
    common_fsys = admit(disc / "files/common.fsys", "common.fsys")
    raw_rel = member(common_fsys, COMMON_MEMBER)
    if hashlib.sha1(raw_rel).hexdigest() != COMMON_SHA1:
        raise ValueError("common REL differs from the admitted source")
    rel = rel_data(raw_rel, 125)
    bgm = admit(disc / "files/bgm_archive.fsys", "bgm_archive.fsys")
    dol = admit(disc / "sys/main.dol", "main.dol")
    banks, samp_digests = {}, {}
    for group, (proj, pool, sdir, samp_name) in BANKS.items():
        samp = admit(disc / "files/sound" / samp_name, samp_name)
        samp_digests[samp_name] = hashlib.sha256(samp).hexdigest()
        banks[group] = Bank(member(common_fsys, proj), member(common_fsys, pool), member(common_fsys, sdir), samp, dol, group)
    rows, waves = sound_table(rel, raw_rel)
    if args.sounds == "all":
        wanted = sorted(rows)
    else:
        wanted = sorted(int(t, 0) for t in args.sounds.split(","))
    report = {"format": "C3MU v1 DSP-ADPCM stereo %d Hz, %d-frame chunks" % (RATE, CHUNK_FRAMES),
              "source": {"common_rel_sha1": COMMON_SHA1,
                         "bgm_archive_sha256": hashlib.sha256(bgm).hexdigest(),
                         "samp_sha256": samp_digests},
              "fixed_call_sites": {"0x%03X" % k: v for k, v in FIXED_SOUNDS.items()},
              "approximations": [
                  "Linear interpolation replaces the DSP polyphase resampler.",
                  "Dry mix: reverb/chorus aux sends (snd table reverb, CC91/93) are not rendered.",
                  "Voice control (pitch, vibrato, volume) updates per 5 ms block; ADSR per 1 ms.",
                  "Voice stealing/priority and age counters are ignored (no voice limit).",
                  "Vibrato is a sine at the macro period; pitch-wheel range fixed at 2 semitones.",
                  "Looped songs: first pass to the loop point, then one rendered loop body.",
                  "Fixed 0.8 master gain; a soft limiter applies only where the mix exceeds full scale.",
                  "Loop seam: last 8 ms of the loop body crossfade into the pre-rewind audio."],
              "sounds": {}, "songs": {}}
    rendered = {}
    index = []
    for sid in wanted:
        row = rows.get(sid)
        if row is None:
            raise ValueError("Sound %#x is not a music row" % sid)
        wi = row["wave"]
        fid = waves[wi] if wi < len(waves) else 0
        if not fid:
            index.append((sid, 0xFFFF, 0))
            report["sounds"]["0x%03X" % sid] = {"wave": wi, "song": row["song"], "file": None}
            continue
        bank = banks.get(row["group"])
        if bank is None:
            raise ValueError("Sound %#x uses unknown song group %d" % (sid, row["group"]))
        key = (fid, row["song"], row["group"])
        if key not in rendered:
            data = member(bgm, fid)
            song = SongData(data)
            setup = bank.setups.get(row["song"])
            loops = any(be16(data, song.tracks[t] + 12 * j + 8) == 0xFFFE
                        for t in range(64) if song.tracks[t]
                        for j in range(_entries(data, song.tracks[t])))
            pcm, ls, le, stats = render_song(bank, song, setup, loops)
            if pcm.shape[1] < CHUNK_SAMPLES // 8 or not np.abs(pcm).max(initial=0):
                index.append((sid, 0xFFFF, wi))   # silent row (null_bgm): stops the slot
                report["sounds"]["0x%03X" % sid] = {"wave": wi, "song": row["song"], "file": None, "silent": True}
                continue
            total, ls, starts, lengths, channels = encode_stream(pcm, ls, le)
            name = "w%02d_s%02d.c3m" % (wi, row["song"])
            if args.wav:
                import wave
                with wave.open(str(out / (name[:-4] + ".wav")), "wb") as w:
                    w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
                    w.writeframes(pcm[:, :total].T.astype("<i2").tobytes())
            write_stream(out / name, total, ls, starts, lengths, channels)
            rendered[key] = (len(rendered), name)
            report["songs"][name] = {"fsys_id": "%08x" % fid, "song_id": row["song"], "samples": total,
                                     "seconds": round(total / RATE, 2), "loop_start": ls,
                                     "bytes": (out / name).stat().st_size, **stats}
            print("%s fsys=%08x song=%d %.1fs loop=%s peak=%.3f %s" % (
                name, fid, row["song"], total / RATE, ls, stats["peak_before_limit"],
                stats["voices_unsupported"] or ""), file=sys.stderr)
        index.append((sid, rendered[key][0], wi))
        report["sounds"]["0x%03X" % sid] = {"wave": wi, "song": row["song"], "file": "w%02d_s%02d.c3m" % (wi, row["song"])}
    # index.bin: count, then (sound id u16, stream file index u16 (0xFFFF = silence), wave u16, pad)
    names = [name for _, name in sorted(rendered.values())]
    blob = struct.pack("<4sII", b"C3MI", 1, len(index))
    for sid, fi, wi in index:
        blob += struct.pack("<HHHH", sid, fi, wi, 0)
    blob += struct.pack("<I", len(names))
    for n in names:
        blob += struct.pack("<16s", n.encode())
    (out / "index.bin").write_bytes(blob)
    (out / "music.json").write_text(json.dumps(report, indent=1) + "\n", newline="\n")
    print("wrote %d streams for %d sounds to %s" % (len(names), len(index), out), file=sys.stderr)


def _entries(data, base):
    n = 0
    while n < 4096:
        p = be16(data, base + n * 12 + 8)
        n += 1
        if p in (0xFFFF, 0xFFFE):
            return n
    raise ValueError("Track exceeds entry bound")


if __name__ == "__main__":
    main()
