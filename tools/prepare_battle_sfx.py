#!/usr/bin/env python3
"""Render original battle sound effects and Pokemon cries to one DSP-ADPCM pack.

The game asks for sounds by GSsnd sound-request id (fn_80166AB8 and its
wrappers, gs_range_801653CC_exact_8016604C.c). A row with flags bit 7 set is
a MusyX FX: fn_8016758C calls sndFXStartEx(row +4 FX id, 0x7F, 0x40, 0) and
sndFXCtrl(handle, 7, 0x7F) (gs_dvd.c). synthFXStart (synth.c) then starts the
FX table's macro on key fx.key | 0x80 at volume 0x7F. The FX tables live in
the five SE banks of rel/common_rel/snd_sample_table.c (common.fsys proj /
pool / sdir members plus files/sound/*.samp).

This exporter plays each requested FX once, offline, with the MusyX macro
synthesiser of tools/prepare_music.py (imported, not copied) plus the FX-only
opcodes below, and writes a mono 32 kHz DSP-ADPCM pack the 3DS streams on
NDSP (platform/3ds/sfx_player.c).

Requested ids:
  * every sound-request id of a kind-5 (sound) node in any wzx_*.fsys member
    (wazaSequence.c; payload +0 se_request) - moves, specials (ball open,
    send-out, faint, status, stat up/down, weather, ...), trainer effects;
  * every species cry: species row +14 (pokemon status 0x61), species table
    common REL data +0x121A04, stride 284;
  * constant ids passed to fn_80166AB8 / fn_80166A50 / fn_80166A28 in the
    decompiled game (menus, battle UI, messages): UI_SOUNDS below.

Extra opcodes (synthmacros.c macHandleActive): 0x01 end, 0x05 loop, 0x08 play
child macro, 0x0E panning, 0x11 stop sample (hwBreak), 0x17 random key,
0x1D/0x1E pitch sweeps (synth.c HandleVoices: +freq/16 Hz per ms, sawtooth
over num*16 ms), 0x28/0x29 trap events (only trap 1, sample end, can fire in
a one-shot render), 0x2A send message (only the app callback form, which is
silent), 0x40 volume select (controller 7 = 0x7F after sndFXCtrl), 0x46/0x47
reverb / surround select (dry mono: no effect).

Approximations are listed in the JSON report. Outputs contain original game
audio: write them under private/ and copy sfx.c3s to the SD card
(3ds/Colosseum3DS/boot/battle3d/sfx.c3s); never commit them.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import wave

import numpy as np

import prepare_music as pm
from prepare_boot_media import verify_disc
from prepare_outskirt import member, rel_data
from prepare_battle_init import COMMON_SHA1

RATE = pm.RATE
MAGIC = b"C3SX"
VERSION = 1
MAX_MS = 12000                          # one-shot render bound
LOOP_RENDER_MS = 6000                   # held/looping sounds: render window
SEAM = 256
CHUNK_SAMPLES = pm.CHUNK_SAMPLES        # 512 DSP-ADPCM frames, 4096 bytes
SPECIES_TABLE = 0x121A04
SPECIES_STRIDE = 284
SPECIES_COUNT = 412
COMMON_MEMBER = pm.COMMON_MEMBER
# rel/common_rel/snd_sample_table.c: group -> (pool, proj, sdir, sample file)
BANKS = {
    6: (0x01AD0000, 0x01AE0000, 0x01B00000, "snd_se.samp"),
    9: (0x01BD0000, 0x01BE0000, 0x01C00000, "snd_se_nakigoe2.samp"),
    5: (0x01B10000, 0x01B20000, 0x01B40000, "snd_se_battle.samp"),
    8: (0x01B50000, 0x01B60000, 0x01B80000, "snd_se_motion.samp"),
    4: (0x01B90000, 0x01BA0000, 0x01BC0000, "snd_se_nakigoe.samp"),
}
# Constant sound ids at fn_80166AB8 / fn_80166A50 / fn_80166A28 call sites in
# vendor/colosseum/src/game (grep, 2026-11-14): menus, battle UI, messages.
UI_SOUNDS = [0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x44, 0x45, 0x46, 0x47, 0x48, 0x54,
             0x3C2, 0x3C3, 0x3C5, 0x3C6, 0x3C7, 0x3CB, 0x3CC, 0x3F9, 0x440, 0x448,
             0x46E, 0x49E, 0x4A1, 0x4C2, 0x4C5, 0x4C6, 0x4C7, 0x4C8, 0x4CB, 0x4D0]


def be16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def be32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def sb32(d, o):
    return struct.unpack_from(">i", d, o)[0]


def align32(x):
    return (x + 0x1F) & ~0x1F


# ------------------------------------------------------------- WZX sound nodes

def fsys_ids(arc):
    count, table = be32(arc, 12), be32(arc, be32(arc, 24))
    return [be32(arc, be32(arc, table + i * 4)) for i in range(count)]


def _effect_end(d, p):
    """Byte after a kind-4 effect payload (effect/tracefx.c fn_801364A8)."""
    t, q = be32(d, p), p + 0xC
    if t == 0:
        cnt, mode = sb32(d, q + 4), sb32(d, q + 8)
        return q + (-4 if mode in (1, 2) else 0) + 0x10 + cnt * 0x10
    if t in (1, 3):
        return align32(q + 0x54) + align32(be32(d, q + 0x4C))
    if t == 2:
        return q + 0x1C
    if t == 4:
        return align32(q + 0x18) + align32(be32(d, q + 0x10))
    if t == 5:
        return align32(q + 0x3C) + align32(be32(d, q + 0x34))
    if t == 6:
        off = -4 if be32(d, q + 0x3C) == 1 else 0
        return align32(q + off + 0x44) + align32(be32(d, q + 0x38))
    if t == 7:
        return align32(q + 0x34) + align32(be32(d, q + 0x2C))
    if t == 8:
        cnt, mode = sb32(d, q + 8), sb32(d, q + 0xC)
        return q + 0x10 + (cnt * 0x10 if mode == 2 else 0)
    if t == 9:
        cnt, mode = sb32(d, q), sb32(d, q + 4)
        return q + (0xC + cnt * 16 if mode == 3 else 8 + cnt * 16 if mode == 2 else 8)
    if t == 10:
        return q + 0x28
    if t == 11:
        size = be32(d, q)
        return align32(q + 0x18) + (align32(size) if size else 0)
    if t == 12:
        off = -4 if be32(d, q + 0x14) == 1 else 0
        return align32(q + off + 0x1C) + align32(be32(d, q + 0x10))
    raise ValueError("effect type %d" % t)


def wzx_sounds(d):
    """Sound-request ids of the kind-5 nodes of one WZX member (wazaSequence.c)."""
    if len(d) < 0x80 or be32(d, 0x68) not in (1, 2):
        return None
    p = 0x70 if be32(d, 0x68) == 2 else 0x6C
    count, ver, cam = be32(d, p + 4), be32(d, p + 0x10), be32(d, p + 0x14)
    if ver == 1:
        nxt = p + 0x10
    elif ver == 2:
        nxt = p + 0x14
    else:
        nxt = ((p + 0x37) & ~0x1F) + (align32(cam) if cam else 0)
    out = []
    for _ in range(max(0, count - 1)):
        nver = be32(d, nxt + 0x68)
        if nver not in (1, 2):
            raise ValueError("node version %d" % nver)
        kind, state = sb32(d, nxt + 4), (sb32(d, nxt + 0x6C) if nver == 2 else 0)
        q = nxt + (0x70 if nver == 2 else 0x6C)
        if kind == 1:
            nxt = q + 0xC + (sb32(d, q + 4) * 8 if sb32(d, q) == 3 else 0)
        elif kind == 2:
            nxt = ((q + 0x43) & ~0x1F) + align32(be32(d, q + 0x1C))
        elif kind == 3:
            nxt = q + (4 if sb32(d, q + 0xC) == 3 else 0) + 0x10
            if state == 0:
                nxt += align32(be32(d, q + 8))
        elif kind == 4:
            nxt = _effect_end(d, q)
        elif kind == 5:
            out.append(be32(d, q))
            nxt = q + (-4 if sb32(d, q + 4) in (1, 2) else 0) + 0xC
        elif kind == 6:
            nxt = q + 8
        else:
            raise ValueError("node kind %d" % kind)
    return out


def collect_wzx(disc):
    ids, members, errors = {}, 0, []
    for path in sorted((disc / "files").glob("wzx_*.fsys")):
        arc = pm.admit(path, path.name)
        for rid in fsys_ids(arc):
            try:
                found = wzx_sounds(member(arc, rid))
            except ValueError as e:
                errors.append("%s:%08x %s" % (path.name, rid, e))
                continue
            if found is None:
                continue
            members += 1
            for se in found:
                ids.setdefault(se, "%s:%08x" % (path.name, rid))
    return ids, members, errors


# ------------------------------------------------------------------ FX banks

class SfxBank(pm.Bank):
    """An SE bank: FX group (type 1) instead of a song group."""

    def _group(self, proj, group):
        if be16(proj, 4) != group or be16(proj, 6) != 1 or be32(proj, be32(proj, 0)) != 0xFFFFFFFF:
            raise ValueError("Expected exactly one MusyX FX group %d" % group)
        self.normal, self.drum, self.setups = [], [], {}
        t = be32(proj, 28)
        count = be16(proj, t)
        if not 1 <= count <= 2048:
            raise ValueError("Invalid FX table")
        self.fx = {}
        for i in range(count):
            fid, mac, maxv, prio, vol, pan, key, vg = struct.unpack_from(">HHBBBBBB", proj, t + 4 + i * 10)
            self.fx[fid] = dict(macro=mac, max_voices=maxv, priority=prio, volume=vol, pan=pan, key=key, vgroup=vg)


class FxChannel(pm.Channel):
    def __init__(self):
        super().__init__()
        self.cc[7] = 127                     # sndFXCtrl(handle, 7, 0x7F)


class FxSeq:
    """What prepare_music.Synth reads from a sequencer, for an FX voice."""

    def __init__(self):
        self.chan = [FxChannel()]
        self.bpm = 120 << 10                 # only tick-based times use it (none in these banks)


class FxSynth(pm.Synth):
    def __init__(self, bank, seed):
        super().__init__(bank, 1)
        self.seq = FxSeq()
        self.ext = {}
        self.next_pc = None
        self.rng = (seed * 2654435761 + 12345) & 0xFFFFFFFF
        self.notes_used = {}

    def note(self, name):
        self.notes_used[name] = self.notes_used.get(name, 0) + 1

    def rand(self):
        self.rng = (self.rng * 1103515245 + 12345) & 0x7FFFFFFF   # fixed seed: reproducible
        return self.rng >> 8

    def state(self, v):
        s = self.ext.get(id(v))
        if s is None:                         # keeps v alive, so id(v) stays unique
            s = self.ext[id(v)] = {"voice": v, "loop": 0, "sweep": [None, None], "volsel": None, "trap": {}}
        return s

    def run_macro(self, v):
        if self.next_pc is not None:          # child started by 0x08 at a step offset
            v.pc, self.next_pc = self.next_pc, None
        super().run_macro(v)

    def extra_op(self, v, op, a, b):
        st = self.state(v)
        if op == 0x01:                        # stop: as 0x00
            v.active = False
            return True
        if op == 0x05:                        # mcmdLoop
            if st["loop"] == 0:
                n = b >> 16
                if (a >> 16) & 1:
                    n = self.rand() % max(1, n)
                    self.note("random_loop_count")
                st["loop"] = n
                if n == 0xFFFF:
                    self.note("infinite_loop")
                else:
                    st["loop"] += 1
            if st["loop"] != 0xFFFF:
                st["loop"] -= 1
                if st["loop"] == 0:
                    return False
            if (a >> 8) & 1 and v.keyoff:
                st["loop"] = 0
            elif (a >> 24) & 1 and not v.playing:
                st["loop"] = 0
            else:
                v.pc = b & 0xFFFF
            return False
        if op == 0x08:                        # mcmdPlayMacro
            key = max(0, min(127, v.org_note + pm.s8((a >> 8) & 0xFF)))
            self.next_pc = b & 0xFFFF
            out = []
            self._start_obj((a >> 16) & 0xFFFF, v.ch, key, (v.volume >> 16) & 0xFF, (v.pan >> 16) & 0xFF,
                            v.track, out, 0)
            self.next_pc = None
            if not out:
                self.note("missing_child_macro")
            return False
        if op == 0x0E:                        # panning: mono render
            v.pan = ((a >> 8) & 0xFF) << 16
            return False
        if op == 0x11:                        # hwBreak: cut the sample now
            v.playing = False
            return False
        if op == 0x17:                        # mcmdRandomKey
            if not (b >> 8) & 0xFF:
                k1, k2 = sorted(((a >> 8) & 0xFF, (a >> 24) & 0xFF))
            else:
                k1 = max(0, min(127, v.note - ((a >> 8) & 0xFF)))
                k2 = max(0, min(127, v.note + ((a >> 24) & 0xFF)))
            detune = (self.rand() % 201) - 100 if b & 0xFF else pm.s8((a >> 16) & 0xFF)
            v.note = (k1 + self.rand() % (k2 - k1 + 1)) & 0x7F
            v.detune = detune
            self.note("random_key")
            return self.do_wait(v, 4, 0)
        if op in (0x1D, 0x1E):                # mcmdPitchSweep
            num = (a >> 8) & 0xFF
            freq = struct.unpack(">h", struct.pack(">H", (a >> 16) & 0xFFFF))[0]
            st["sweep"][op - 0x1D] = (self.ms, num, freq) if num else None
            return self.do_wait(v, 0, b)
        if op == 0x28:                        # trap event: macro, step
            mac = self.bank.macros.get((a >> 16) & 0xFFFF)
            if mac is not None:
                st["trap"][(a >> 8) & 0xFF] = (mac, b & 0xFFFF)
                if (a >> 8) & 0xFF != 1:
                    self.note("trap_event_%d_never_fires" % ((a >> 8) & 0xFF))
            return False
        if op == 0x29:
            st["trap"].pop((a >> 8) & 0xFF, None)
            return False
        if op == 0x2A:                        # send message
            if (a >> 8) & 0xFF or (a >> 16) & 0xFFFF != 0xFFFF:
                self.note("message_to_voice_ignored")
            return False                      # 0xFFFF: game callback, silent
        if op == 0x40:                        # mcmdVolumeSelect
            ctrl, comb = (a >> 8) & 0xFF, (b & 0xFF)
            scale = struct.unpack(">h", struct.pack(">H", (a >> 16) & 0xFFFF))[0] / 100.0
            scale += pm.s8((b >> 16) & 0xFF) / 25600.0 * (1 if scale >= 0 else -1)
            if st["volsel"] is None:
                comb, st["volsel"] = 0, []
            if comb == 0:
                st["volsel"] = []
            st["volsel"].append((ctrl, scale, comb))
            if ctrl != 7:
                self.note("volume_select_ctrl_%d" % ctrl)
            return False
        if op in (0x46, 0x47):                # reverb / surround sends: dry mono
            return False
        return None

    def pitch_ratio(self, v, rate, root):
        r = super().pitch_ratio(v, rate, root)
        st = self.ext.get(id(v))
        if st:
            hz = 0.0
            for sw in st["sweep"]:
                if sw:
                    t0, num, freq = sw
                    hz += freq / 16.0 * ((self.ms - t0) % (num * 16))
            r = max(0.0, r + hz / RATE)
        return r

    def gains(self, v):
        st = self.ext.get(id(v))
        cc = self.seq.chan[v.ch].cc
        f = 1.0
        if st and st["volsel"]:
            f = 0.0
            for ctrl, scale, comb in st["volsel"]:
                x = cc.get(ctrl, 0) / 127.0 * scale if ctrl < 0x80 else 0.0
                f = x if comb == 0 else (f + x if comb == 1 else f * x)
            f = max(0.0, min(1.0, f))
        saved = cc[7]
        cc[7] = 127 * f
        try:
            return super().gains(v)
        finally:
            cc[7] = saved

    def render_block(self, out_l, out_r, env_ms):
        before = {id(v): v for v in self.voices if v.playing}
        super().render_block(out_l, out_r, env_ms)
        for v in before.values():             # trap 1: hardware voice ended
            st = self.ext.get(id(v))
            if not v.playing and not v.dead and st and 1 in st["trap"]:
                mac, step = st["trap"].pop(1)
                v.macro, v.pc, v.wait, v.active = mac, step, 0, True
                if v not in self.voices:
                    self.voices.append(v)
                self.run_macro(v)


def render_fx(bank, fx, seed):
    """One FX, mono float32 at RATE; returns (pcm, held, stats)."""
    syn = FxSynth(bank, seed)
    syn.ms = 0
    started = []
    syn._start_obj(fx["macro"], 0, fx["key"] & 0x7F, 0x7F, 0x40, 0, started, 0)
    if not started:
        raise ValueError("FX macro %#x does not start a voice" % fx["macro"])
    blocks, ms, held = [], 0, False
    while True:
        for sub in range(pm.BLOCK_MS):
            syn.ms = ms + sub
            syn.wake_waits()
        l = np.zeros(pm.BLOCK, dtype=np.float32)
        r = np.zeros(pm.BLOCK, dtype=np.float32)
        syn.ms = ms
        syn.render_block(l, r, pm.BLOCK_MS)
        blocks.append((l + r) * 0.5)
        ms += pm.BLOCK_MS
        if not syn.voices:
            break
        if not any(v.playing for v in syn.voices) and all(v.wait == float("inf") for v in syn.voices if v.active):
            break                             # silent and waiting for a key-off that never comes
        if ms >= LOOP_RENDER_MS and any(v.playing and v.sample[3] is not None for v in syn.voices):
            held = True                       # a looped sample still sounds: held until stopped
            break
        if ms >= MAX_MS:
            held = True
            break
    pcm = np.concatenate(blocks)
    nz = np.nonzero(np.abs(pcm) > 1e-5)[0]
    pcm = pcm[:nz[-1] + 1] if len(nz) else pcm[:0]
    stats = {"unsupported": dict(syn.unsupported), "notes": dict(syn.notes_used)}
    return pcm, held, stats


def make_loop(pcm):
    """Held sound: loop the end of the render with a crossfaded seam.

    The body is a whole number of the render's repeat period (autocorrelation
    of the last two seconds, 10 ms .. 1 s), up to one second, so a macro that
    retriggers a sample on a fixed beat keeps its beat across the seam."""
    total = len(pcm)
    body = min(RATE, total // 2)
    tail = pcm[-2 * RATE:].astype(np.float64)
    if len(tail) >= RATE:
        spec = np.fft.rfft(tail, 2 * len(tail))
        ac = np.fft.irfft(spec * np.conj(spec))[:RATE + 1]
        lo = RATE // 100
        if ac[0] > 0:
            lag = lo + int(np.argmax(ac[lo:RATE + 1]))
            if ac[lag] > 0.5 * ac[0]:
                body = max(lag, (RATE // lag) * lag)
    body -= body % 14
    ls = total - body
    ls -= ls % 14
    k = min(SEAM, ls)
    w = np.linspace(0.0, 1.0, k, dtype=np.float32)
    pcm = pcm.copy()
    end = ls + body
    pcm[end - k:end] = pcm[end - k:end] * (1 - w) + pcm[ls - k:ls] * w
    return pcm[:end], ls


def encode_mono(x, loop_start):
    """DSP-ADPCM via prepare_music's encoder; chunked so NDSP can stream it."""
    total = len(x)
    total -= total % 14 if loop_start is None else 0
    if total < 14:
        raise ValueError("too short")
    starts, lengths = pm.plan_chunks(total, loop_start)
    coefs = pm.choose_coefs(x[:total])
    data, (h1, h2) = pm.encode_chunks(x[:total], coefs, starts, lengths)
    return total, coefs, starts, lengths, data, h1, h2


def decode_entry(blob, row):
    """Decode one pack row back to int16 (verification aid)."""
    sid, off, samples, loop_chunk, rate, ch, fmt, count, nbytes = row
    coef = struct.unpack_from("<16h", blob, off)
    table = off + 32
    data = table + count * 16
    out = []
    for i in range(count):
        o, n, ps, h1, h2, _ = struct.unpack_from("<IIHhhH", blob, table + i * 16)
        frames = (n + 13) // 14
        raw = blob[data + o:data + o + frames * 8]
        hist1, hist2 = h1, h2
        for f in range(frames):
            fps = raw[f * 8]
            c1, c2 = coef[(fps >> 4) * 2], coef[(fps >> 4) * 2 + 1]
            for k in range(14):
                byte = raw[f * 8 + 1 + k // 2]
                nib = (byte >> 4) if k % 2 == 0 else byte & 15
                if nib >= 8:
                    nib -= 16
                v = ((nib << (fps & 15)) << 11) + c1 * hist1 + c2 * hist2 + 1024 >> 11
                v = max(-32768, min(32767, v))
                hist2, hist1 = hist1, v
                out.append(v)
        del out[len(out) - (frames * 14 - n):]
    return np.array(out, dtype=np.int16), rate


def read_pack(blob):
    magic, version, count, rows_at, data_at = struct.unpack_from("<4sIIII", blob, 0)
    if magic != MAGIC or version != VERSION:
        raise ValueError("not a C3SX v1 pack")
    rows = []
    for i in range(count):
        sid, off, samples, loop_chunk, rate, ch, fmt, cnt, nbytes, _fx, _g, _fl = struct.unpack_from(
            "<IIIIHBBIIHBB", blob, rows_at + i * 32)
        rows.append((sid, off, samples, loop_chunk, rate, ch, fmt, cnt, nbytes))
    return rows


def write_wav(path, pcm16, rate):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm16.astype("<i2").tobytes())


def parse_ids(text):
    return [int(t, 0) for t in text.split(",") if t.strip()]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--disc", type=Path, help="extracted GC6E01 disc root (sys/, files/)")
    ap.add_argument("--common-rel", type=Path, help="common_rel.rel (default: common.fsys member)")
    ap.add_argument("--output", type=Path, help="sfx.c3s to write (under private/)")
    ap.add_argument("--wav", type=Path, help="directory for review WAVs of every converted id")
    ap.add_argument("--ids", help="comma-separated sound ids instead of the full set")
    ap.add_argument("--verify", type=Path, help="decode an existing pack's --ids to --wav and print stats")
    args = ap.parse_args(argv)
    if args.verify:
        blob = args.verify.read_bytes()
        rows = {r[0]: r for r in read_pack(blob)}
        for sid in parse_ids(args.ids or ""):
            if sid not in rows:
                print("0x%03X absent" % sid)
                continue
            pcm, rate = decode_entry(blob, rows[sid])
            f = pcm.astype(np.float64) / 32768.0
            print("0x%03X %.3fs peak=%.3f rms=%.4f loop_chunk=%s" % (
                sid, len(pcm) / rate, np.abs(f).max(initial=0), float(np.sqrt((f * f).mean())) if len(f) else 0,
                rows[sid][3] if rows[sid][3] != 0xFFFFFFFF else "-"))
            if args.wav:
                args.wav.mkdir(parents=True, exist_ok=True)
                write_wav(args.wav / ("decoded_%03x.wav" % sid), pcm, rate)
        return
    if not args.disc or not args.output:
        ap.error("--disc and --output are required (unless --verify)")
    disc = verify_disc(args.disc)
    out = args.output.resolve()
    root = Path(__file__).resolve().parents[1]
    if (out == root or root in out.parents) and (root / "private") not in out.parents:
        raise ValueError("SFX output in the repository must be under ignored private/")
    common_fsys = pm.admit(disc / "files/common.fsys", "common.fsys")
    raw_rel = pm.admit(args.common_rel, "common_rel.rel") if args.common_rel else member(common_fsys, COMMON_MEMBER)
    if hashlib.sha1(raw_rel).hexdigest() != COMMON_SHA1:
        raise ValueError("common REL differs from the admitted source")
    rel = rel_data(raw_rel, 125)
    dol = pm.admit(disc / "sys/main.dol", "main.dol")
    banks, digests = {}, {}
    for group, (pool, proj, sdir, samp_name) in BANKS.items():
        samp = pm.admit(disc / "files/sound" / samp_name, samp_name)
        digests[samp_name] = hashlib.sha256(samp).hexdigest()
        banks[group] = SfxBank(member(common_fsys, proj), member(common_fsys, pool),
                               member(common_fsys, sdir), samp, dol, group)
    fx_bank = {}
    for group, bank in banks.items():
        for fid in bank.fx:
            if fid in fx_bank:
                raise ValueError("FX id %#x in two banks" % fid)
            fx_bank[fid] = group

    # what to render, and why
    reasons = {}
    wzx_ids, wzx_members, wzx_errors = collect_wzx(disc)
    for sid, where in wzx_ids.items():
        reasons.setdefault(sid, []).append("wzx " + where)
    for sp in range(SPECIES_COUNT):
        cry = be16(rel, SPECIES_TABLE + sp * SPECIES_STRIDE + 14)
        if cry:
            reasons.setdefault(cry, []).append("cry species %d" % sp)
    for sid in UI_SOUNDS:
        reasons.setdefault(sid, []).append("decomp call site")
    wanted = sorted(reasons) if not args.ids else parse_ids(args.ids)

    report = {"format": "C3SX v1 mono DSP-ADPCM %d Hz, %d-sample chunks" % (RATE, CHUNK_SAMPLES),
              "source": {"common_rel_sha1": COMMON_SHA1, "samp_sha256": digests},
              "wzx": {"members_with_nodes": wzx_members, "distinct_ids": len(wzx_ids), "parse_errors": wzx_errors},
              "approximations": [
                  "Rendered with prepare_music's synthesiser: linear interpolation, ADSR per 1 ms, pitch/volume per 5 ms.",
                  "Dry mono: pan, surround and reverb/aux sends are not rendered; L/R are averaged (centre pan = original level per speaker).",
                  "FX voices start at volume 0x7F with controller 7 = 0x7F (fn_8016758C); volume groups / master faders are runtime.",
                  "Random key, detune and loop counts use a fixed per-id seed (one deterministic take).",
                  "Pitch sweep adds freq/16 Hz per ms (synth.c, deltaTime 256 per ms) as a sawtooth over num*16 ms, sampled per 5 ms block.",
                  "Trap events other than 1 (sample end) and messages between voices never fire in a one-shot render.",
                  "Held sounds (a looped sample still sounding after %d ms, or not ended by %d ms) loop up to the last second of the render (a whole number of its repeat period) with a crossfaded seam; the game stops them." % (LOOP_RENDER_MS, MAX_MS),
                  "Fixed %.1f gain (as the music pack); soft limiter where a mix still exceeds full scale (peak reported per id)." % pm.MASTER],
              "converted": {}, "failed": {}}

    entries = []
    for sid in wanted:
        why = reasons.get(sid, ["--ids"])
        try:
            if sid >= pm.SOUND_COUNT:
                raise ValueError("not a sound-table row")
            fl, group, _rv, _wi, fid = struct.unpack_from(">BBBBH", rel, pm.SOUND_TABLE + sid * 12)
            if not fl & 0x80:
                raise ValueError("not an FX row (flags %#04x: %s)" % (
                    fl, "streamed/resource sound, fn_8016761C" if fl & 0x20 else "music row"))
            g = fx_bank.get(fid)
            if g is None:
                raise ValueError("FX id %#x is in no SE bank" % fid)
            fx = banks[g].fx[fid]
            pcm, held, stats = render_fx(banks[g], fx, sid)
            if not len(pcm) or float(np.abs(pcm).max(initial=0)) < 1e-4:
                raise ValueError("renders silent (%s)" % (stats["unsupported"] or stats["notes"] or "no audible voice"))
            ls = None
            if held:
                pcm, ls = make_loop(pcm)
            pcm = pcm * pm.MASTER                  # the music pack's fixed headroom
            peak = float(np.abs(pcm).max(initial=0))
            if peak > 0.98:
                pcm = np.tanh(pcm / 0.98) * 0.98
            x = np.clip(np.round(pcm * 32767), -32768, 32767).astype(np.int64)
            total, coefs, starts, lengths, data, h1, h2 = encode_mono(x, ls)
            entries.append((sid, fid, g, total, starts.index(ls) if ls is not None else 0xFFFFFFFF,
                            coefs, starts, lengths, data, h1, h2, 1 if stats["unsupported"] else 0))
            if args.wav:
                args.wav.mkdir(parents=True, exist_ok=True)
                write_wav(args.wav / ("sfx_%03x.wav" % sid), x[:total], RATE)
            info = {"fx": "%#x" % fid, "bank": BANKS[g][3], "seconds": round(total / RATE, 3),
                    "peak": round(peak, 4), "loop_start": ls, "why": why[:3]}
            if stats["unsupported"]:
                info["unsupported_ops"] = stats["unsupported"]
            if stats["notes"]:
                info["notes"] = sorted(stats["notes"])
            report["converted"]["0x%03X" % sid] = info
        except ValueError as e:
            report["failed"]["0x%03X" % sid] = {"reason": str(e), "why": why[:3]}
        if (len(entries) + len(report["failed"])) % 100 == 0:
            print("%d/%d" % (len(entries) + len(report["failed"]), len(wanted)), file=sys.stderr)

    # C3SX v1 (little-endian): header 32, rows 32 each sorted by id, data.
    rows_at = 32
    data_at = rows_at + 32 * len(entries)
    rows, body = bytearray(), bytearray()
    for sid, fid, g, total, loop_chunk, coefs, starts, lengths, data, h1, h2, flags in entries:
        while (data_at + len(body)) % 32:
            body.append(0)
        off = data_at + len(body)
        adpcm = b"".join(data)
        table = bytearray()
        o = 0
        for i, n in enumerate(lengths):
            table += struct.pack("<IIHhhH", o, n, data[i][0], int(h1[i]), int(h2[i]), 0)
            o += len(data[i])
        body += struct.pack("<16h", *[c for pair in coefs for c in pair]) + table + adpcm
        rows += struct.pack("<IIIIHBBIIHBB", sid, off, total, loop_chunk, RATE, 1, 0,
                            len(lengths), len(adpcm), fid, g, flags)
    head = struct.pack("<4sIIIII8x", MAGIC, VERSION, len(entries), rows_at, data_at, data_at + len(body))
    out.parent.mkdir(parents=True, exist_ok=True)
    blob = head + bytes(rows) + bytes(body)
    out.write_bytes(blob)
    reasons_count = {}
    for v in report["failed"].values():
        k = v["reason"].split(" (")[0]
        reasons_count[k] = reasons_count.get(k, 0) + 1
    report["summary"] = {"requested": len(wanted), "converted": len(entries), "failed": len(report["failed"]),
                         "failed_by_reason": reasons_count, "held_looping": sum(1 for e in entries if e[4] != 0xFFFFFFFF),
                         "with_unsupported_ops": sum(1 for e in entries if e[11]), "pack_bytes": len(blob),
                         "pack_sha256": hashlib.sha256(blob).hexdigest()}
    out.with_suffix(".json").write_text(json.dumps(report, indent=1) + "\n", newline="\n")
    print(json.dumps(report["summary"], indent=1), file=sys.stderr)


if __name__ == "__main__":
    main()
