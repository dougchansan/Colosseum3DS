#include "music_player.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS 2u
#define BUFFERS 6u
#define BUFFER_BYTES 4096u          /* one 512-frame DSP-ADPCM chunk per channel */
#define CHANNEL_BASE 8u              /* movie uses 0, battle cues 4..7 */
#define TICK_NS 8000000LL
#define MAX_CHUNKS 65536u
#define INDEX_MAX 4096u

typedef struct Chunk {
    uint32_t offset, samples, bytes;
    uint16_t ps[2];
    int16_t hist1[2], hist2[2];
} Chunk;

typedef struct Request {
    int pending;                     /* 0 none, 1 play, 2 stop */
    uint32_t sound, volume, fade_ms;
} Request;

typedef struct Slot {
    int state;                       /* 0 idle, 1 streaming, 2 draining, 3 fading out */
    uint32_t sound, file_index;
    FILE *file;
    Chunk *chunks;
    uint32_t count, loop, next, rate;
    uint16_t coefs[2][16];
    ndspWaveBuf wave[BUFFERS][2];
    ndspAdpcmData adpcm[BUFFERS][2];
    float volume, fade, fade_target, fade_step;
    int paused, applied_paused;
    float applied_gain;
    Request request;
    uint32_t underruns, chunks_read;
} Slot;

typedef struct Index {
    uint16_t sound, file;
} Index;

static struct {
    int open, ndsp, mono, stop_thread;
    char root[256];
    Index *index;
    uint32_t index_count, file_count;
    char (*names)[17];
    uint8_t *linear;
    Slot slots[SLOTS];
    float master, master_target, master_step;
    LightLock lock;
    Thread thread;
    char last_error[128];
} music;

static void set_error(char *error, size_t capacity, const char *text)
{
    if (error && capacity) snprintf(error, capacity, "%s", text);
}

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int find_file(uint32_t sound)
{
    for (uint32_t i = 0; i < music.index_count; ++i)
        if (music.index[i].sound == sound) return music.index[i].file == 0xFFFFu ? -2 : music.index[i].file;
    return -1;
}

static unsigned channel(unsigned slot, unsigned c) { return CHANNEL_BASE + slot * 2u + c; }

static void release_slot(unsigned s)
{
    Slot *slot = &music.slots[s];
    for (unsigned c = 0; c < 2; ++c) {
        ndspChnWaveBufClear(channel(s, c));
        ndspChnSetPaused(channel(s, c), false);
    }
    if (slot->file) fclose(slot->file);
    free(slot->chunks);
    slot->file = NULL;
    slot->chunks = NULL;
    slot->state = 0;
    slot->sound = 0;
    slot->count = 0;
}

static int read_chunk(unsigned s, unsigned b)
{
    Slot *slot = &music.slots[s];
    const Chunk *chunk = &slot->chunks[slot->next];
    uint8_t *base = music.linear + (s * BUFFERS + b) * 2u * BUFFER_BYTES;
    if (fseek(slot->file, (long)chunk->offset, SEEK_SET) ||
        fread(base, 1, chunk->bytes, slot->file) != chunk->bytes ||
        fread(base + BUFFER_BYTES, 1, chunk->bytes, slot->file) != chunk->bytes)
        return 0;
    for (unsigned c = 0; c < 2; ++c) {
        ndspWaveBuf *wave = &slot->wave[b][c];
        ndspAdpcmData *adpcm = &slot->adpcm[b][c];
        memset(wave, 0, sizeof(*wave));
        adpcm->index = chunk->ps[c];
        adpcm->history0 = chunk->hist1[c];
        adpcm->history1 = chunk->hist2[c];
        wave->data_adpcm = base + c * BUFFER_BYTES;
        wave->adpcm_data = adpcm;
        wave->nsamples = chunk->samples;
        wave->looping = false;
        DSP_FlushDataCache(wave->data_adpcm, chunk->bytes);
    }
    ++slot->chunks_read;
    if (++slot->next >= slot->count) {
        if (slot->loop < slot->count) slot->next = slot->loop;
        else slot->state = 2;
    }
    return 1;
}

static void queue_free_buffers(unsigned s)
{
    Slot *slot = &music.slots[s];
    unsigned queued = 0;
    for (unsigned b = 0; b < BUFFERS; ++b) {
        int status_l = slot->wave[b][0].status, status_r = slot->wave[b][1].status;
        int free_l = status_l == NDSP_WBUF_FREE || status_l == NDSP_WBUF_DONE;
        int free_r = status_r == NDSP_WBUF_FREE || status_r == NDSP_WBUF_DONE;
        if (!free_l || !free_r) { ++queued; continue; }
        if (slot->state != 1 && slot->state != 3) continue;
        if (slot->state == 3 && slot->fade <= 0.0f) continue;
        if (!read_chunk(s, b)) {
            snprintf(music.last_error, sizeof music.last_error, "music read failed sound=%lu chunk=%lu",
                     (unsigned long)slot->sound, (unsigned long)slot->next);
            slot->state = 2;
            continue;
        }
        ndspChnWaveBufAdd(channel(s, 0), &slot->wave[b][0]);
        ndspChnWaveBufAdd(channel(s, 1), &slot->wave[b][1]);
        ++queued;
        if (slot->state == 2) break;
    }
    if (slot->state == 1 && queued == 0) ++slot->underruns;
}

static int open_stream(unsigned s, uint32_t sound, int file_index)
{
    Slot *slot = &music.slots[s];
    char path[320];
    uint8_t head[96];
    snprintf(path, sizeof path, "%s/music/%s", music.root, music.names[file_index]);
    FILE *file = fopen(path, "rb");
    if (!file) { snprintf(music.last_error, sizeof music.last_error, "missing %s", music.names[file_index]); return 0; }
    if (fread(head, 1, sizeof head, file) != sizeof head || memcmp(head, "C3MU", 4) || le32(head + 4) != 1 ||
        le32(head + 12) != 2 || le32(head + 28) > BUFFER_BYTES || !le32(head + 24) || le32(head + 24) > MAX_CHUNKS) {
        fclose(file);
        snprintf(music.last_error, sizeof music.last_error, "invalid stream %s", music.names[file_index]);
        return 0;
    }
    uint32_t count = le32(head + 24);
    uint8_t *table = malloc(count * 28u);
    Chunk *chunks = malloc(count * sizeof(Chunk));
    if (!table || !chunks || fread(table, 28, count, file) != count) {
        free(table); free(chunks); fclose(file);
        snprintf(music.last_error, sizeof music.last_error, "stream table failed %s", music.names[file_index]);
        return 0;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *row = table + i * 28u;
        chunks[i].offset = le32(row);
        chunks[i].samples = le32(row + 4);
        chunks[i].bytes = le32(row + 8);
        for (unsigned c = 0; c < 2; ++c) {
            chunks[i].ps[c] = le16(row + 12 + c * 8);
            chunks[i].hist1[c] = (int16_t)le16(row + 14 + c * 8);
            chunks[i].hist2[c] = (int16_t)le16(row + 16 + c * 8);
        }
        if (chunks[i].bytes > BUFFER_BYTES || chunks[i].bytes != (chunks[i].samples + 13u) / 14u * 8u) {
            free(table); free(chunks); fclose(file);
            snprintf(music.last_error, sizeof music.last_error, "invalid chunk %s", music.names[file_index]);
            return 0;
        }
    }
    free(table);
    release_slot(s);
    slot->file = file;
    slot->chunks = chunks;
    slot->count = count;
    slot->loop = le32(head + 20);
    slot->rate = le32(head + 8);
    slot->next = 0;
    slot->sound = sound;
    slot->file_index = (uint32_t)file_index;
    slot->chunks_read = slot->underruns = 0;
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned k = 0; k < 16; ++k) slot->coefs[c][k] = le16(head + 32 + c * 32 + k * 2);
    memset(slot->wave, 0, sizeof slot->wave);
    for (unsigned c = 0; c < 2; ++c) {
        unsigned ch = channel(s, c);
        ndspChnReset(ch);
        ndspChnSetPaused(ch, true);
        ndspChnSetInterp(ch, NDSP_INTERP_POLYPHASE);
        ndspChnSetRate(ch, (float)slot->rate);
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_ADPCM);
        ndspChnSetAdpcmCoefs(ch, slot->coefs[c]);
    }
    slot->applied_gain = -1.0f;
    slot->applied_paused = 1;
    slot->state = 1;
    return 1;
}

static void apply_mix(unsigned s)
{
    Slot *slot = &music.slots[s];
    float gain = slot->volume * slot->fade * music.master;
    if (gain == slot->applied_gain) return;
    for (unsigned c = 0; c < 2; ++c) {
        float mix[12] = {0};
        if (music.mono) mix[0] = mix[1] = gain * 0.5f;
        else mix[c] = gain;
        ndspChnSetMix(channel(s, c), mix);
    }
    slot->applied_gain = gain;
}

static float step_toward(float value, float target, float step)
{
    if (value < target) return value + step >= target ? target : value + step;
    if (value > target) return value - step <= target ? target : value - step;
    return value;
}

static float fade_step(uint32_t fade_ms)
{
    return fade_ms ? (float)(TICK_NS / 1000000LL) / (float)fade_ms : 1.0f;
}

static void service(void)
{
    LightLock_Lock(&music.lock);
    music.master = step_toward(music.master, music.master_target, music.master_step);
    for (unsigned s = 0; s < SLOTS; ++s) {
        Slot *slot = &music.slots[s];
        Request request = slot->request;
        slot->request.pending = 0;
        float volume = (request.volume >= 127u ? 127u : request.volume) / 127.0f;
        if (request.pending == 1) {
            if (slot->state && slot->state != 3 && slot->sound == request.sound) {
                slot->volume = volume;           /* already loaded: keep it running */
            } else {
                int file = find_file(request.sound);
                if (file >= 0 && open_stream(s, request.sound, file)) {
                    slot->volume = volume;
                    slot->fade = request.fade_ms ? 0.0f : 1.0f;
                    slot->fade_target = 1.0f;
                    slot->fade_step = fade_step(request.fade_ms);
                } else if (file == -2) {
                    release_slot(s);             /* the silent "null BGM" rows */
                }
            }
        } else if (request.pending == 2 && slot->state) {
            slot->state = 3;
            slot->fade_target = 0.0f;
            slot->fade_step = fade_step(request.fade_ms);
        }
        if (!slot->state) continue;
        slot->fade = step_toward(slot->fade, slot->fade_target, slot->fade_step);
        if (slot->state == 3 && slot->fade <= 0.0f) { release_slot(s); continue; }
        if (slot->paused != slot->applied_paused && slot->chunks_read) {
            ndspChnSetPaused(channel(s, 0), slot->paused);
            ndspChnSetPaused(channel(s, 1), slot->paused);
            slot->applied_paused = slot->paused;
        }
        apply_mix(s);
        LightLock_Unlock(&music.lock);
        queue_free_buffers(s);           /* SD reads happen outside the lock */
        LightLock_Lock(&music.lock);
        if (slot->state == 2) {
            int busy = 0;
            for (unsigned b = 0; b < BUFFERS; ++b)
                for (unsigned c = 0; c < 2; ++c)
                    busy |= slot->wave[b][c].status == NDSP_WBUF_QUEUED || slot->wave[b][c].status == NDSP_WBUF_PLAYING;
            if (!busy) release_slot(s);
        }
    }
    LightLock_Unlock(&music.lock);
}

static void worker(void *arg)
{
    (void)arg;
    while (!music.stop_thread) {
        service();
        svcSleepThread(TICK_NS);
    }
}

static int load_index(char *error, size_t capacity)
{
    char path[320];
    snprintf(path, sizeof path, "%s/music/index.bin", music.root);
    FILE *file = fopen(path, "rb");
    uint8_t head[12], row[8], word[4];
    if (!file) { set_error(error, capacity, "No music pack (music/index.bin)."); return 0; }
    if (fread(head, 1, 12, file) != 12 || memcmp(head, "C3MI", 4) || le32(head + 4) != 1 || le32(head + 8) > INDEX_MAX) {
        fclose(file); set_error(error, capacity, "Invalid music index."); return 0;
    }
    music.index_count = le32(head + 8);
    music.index = calloc(music.index_count ? music.index_count : 1, sizeof(Index));
    if (!music.index) { fclose(file); set_error(error, capacity, "Music index allocation failed."); return 0; }
    for (uint32_t i = 0; i < music.index_count; ++i) {
        if (fread(row, 1, 8, file) != 8) { fclose(file); set_error(error, capacity, "Truncated music index."); return 0; }
        music.index[i].sound = le16(row);
        music.index[i].file = le16(row + 2);
    }
    if (fread(word, 1, 4, file) != 4 || le32(word) > INDEX_MAX) { fclose(file); set_error(error, capacity, "Invalid music file list."); return 0; }
    music.file_count = le32(word);
    music.names = calloc(music.file_count ? music.file_count : 1, sizeof(*music.names));
    if (!music.names) { fclose(file); set_error(error, capacity, "Music name allocation failed."); return 0; }
    for (uint32_t i = 0; i < music.file_count; ++i) {
        if (fread(music.names[i], 1, 16, file) != 16) { fclose(file); set_error(error, capacity, "Truncated music file list."); return 0; }
        music.names[i][16] = 0;
        if (strchr(music.names[i], '/') || strchr(music.names[i], '\\')) { fclose(file); set_error(error, capacity, "Invalid music file name."); return 0; }
    }
    fclose(file);
    for (uint32_t i = 0; i < music.index_count; ++i)
        if (music.index[i].file != 0xFFFFu && music.index[i].file >= music.file_count) {
            set_error(error, capacity, "Music index names a missing stream."); return 0;
        }
    return 1;
}

int c3_music_open(const char *root, char *error, size_t capacity)
{
    s32 priority = 0x30;
    if (music.open) return 1;
    memset(&music, 0, sizeof music);
    LightLock_Init(&music.lock);
    music.master = music.master_target = 1.0f;
    music.master_step = 1.0f;
    snprintf(music.root, sizeof music.root, "%s", root ? root : "");
    if (!load_index(error, capacity)) goto fail;
    music.linear = linearAlloc(SLOTS * BUFFERS * 2u * BUFFER_BYTES);
    if (!music.linear) { set_error(error, capacity, "Music buffer allocation failed."); goto fail; }
    if (R_FAILED(ndspInit())) { set_error(error, capacity, "NDSP is unavailable (dspfirm.cdc missing?)."); goto fail; }
    music.ndsp = 1;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    music.thread = threadCreate(worker, NULL, 16 * 1024, priority > 0x18 ? priority - 1 : priority, -2, false);
    if (!music.thread) { set_error(error, capacity, "Music thread creation failed."); goto fail; }
    music.open = 1;
    return 1;
fail:
    if (music.ndsp) ndspExit();
    if (music.linear) linearFree(music.linear);
    free(music.index);
    free(music.names);
    memset(&music, 0, sizeof music);
    return 0;
}

void c3_music_close(void)
{
    if (!music.open) return;
    music.stop_thread = 1;
    threadJoin(music.thread, U64_MAX);
    threadFree(music.thread);
    for (unsigned s = 0; s < SLOTS; ++s) release_slot(s);
    ndspExit();
    linearFree(music.linear);
    free(music.index);
    free(music.names);
    memset(&music, 0, sizeof music);
}

int c3_music_play(int slot, uint32_t sound, uint32_t volume, uint32_t fade_ms)
{
    if (!music.open || slot < 0 || slot >= (int)SLOTS || find_file(sound) == -1) return 0;
    LightLock_Lock(&music.lock);
    music.slots[slot].request = (Request){1, sound, volume, fade_ms};
    music.slots[slot].paused = 0;
    LightLock_Unlock(&music.lock);
    return 1;
}

void c3_music_stop_slot(int slot, uint32_t fade_ms)
{
    if (!music.open || slot < 0 || slot >= (int)SLOTS) return;
    LightLock_Lock(&music.lock);
    music.slots[slot].request = (Request){2, 0, 0, fade_ms};
    LightLock_Unlock(&music.lock);
}

void c3_music_stop(uint32_t sound, uint32_t fade_ms)
{
    if (!music.open) return;
    LightLock_Lock(&music.lock);
    for (unsigned s = 0; s < SLOTS; ++s) {
        Slot *slot = &music.slots[s];
        int pending = slot->request.pending == 1 && slot->request.sound == sound;
        if (pending || (slot->state && slot->sound == sound)) slot->request = (Request){2, 0, 0, fade_ms};
    }
    LightLock_Unlock(&music.lock);
}

void c3_music_master(uint32_t volume, uint32_t fade_ms)
{
    if (!music.open) return;
    LightLock_Lock(&music.lock);
    music.master_target = (volume >= 127u ? 127u : volume) / 127.0f;
    music.master_step = fade_ms ? fade_step(fade_ms) : 1.0f;
    LightLock_Unlock(&music.lock);
}

uint32_t c3_music_current(int slot_index)
{
    uint32_t sound = 0;
    if (!music.open || slot_index < 0 || (unsigned)slot_index >= SLOTS) return 0;
    LightLock_Lock(&music.lock);
    const Slot *slot = &music.slots[slot_index];
    if (slot->request.pending == 1) sound = slot->request.sound;
    else if (slot->state && slot->state != 3) sound = slot->sound;
    LightLock_Unlock(&music.lock);
    return sound;
}

int c3_music_status(uint32_t sound)
{
    int result = 0;
    if (!music.open) return 0;
    LightLock_Lock(&music.lock);
    for (unsigned s = 0; s < SLOTS; ++s) {
        const Slot *slot = &music.slots[s];
        if ((slot->request.pending == 1 && slot->request.sound == sound) ||
            (slot->state && slot->state != 3 && slot->sound == sound)) result = 2;
    }
    LightLock_Unlock(&music.lock);
    return result;
}

void c3_music_pause_slot(int slot, int paused)
{
    if (!music.open || slot < 0 || slot >= (int)SLOTS) return;
    LightLock_Lock(&music.lock);
    music.slots[slot].paused = !!paused;
    LightLock_Unlock(&music.lock);
}

void c3_music_set_mono(int mono)
{
    if (!music.open) return;
    LightLock_Lock(&music.lock);
    music.mono = !!mono;
    for (unsigned s = 0; s < SLOTS; ++s) music.slots[s].applied_gain = -1.0f;
    LightLock_Unlock(&music.lock);
}

void c3_music_trace(FILE *trace, const char *phase)
{
    if (!trace) return;
    if (!music.open) { fprintf(trace, "music phase=%s open=0\n", phase ? phase : ""); return; }
    LightLock_Lock(&music.lock);
    for (unsigned s = 0; s < SLOTS; ++s) {
        const Slot *slot = &music.slots[s];
        fprintf(trace, "music phase=%s slot=%u state=%d sound=%lu stream=%s chunk=%lu/%lu read=%lu underruns=%lu gain=%.2f master=%.2f paused=%d pos=%lu%s%s\n",
                phase ? phase : "", s, slot->state, (unsigned long)slot->sound,
                slot->state ? music.names[slot->file_index] : "-", (unsigned long)slot->next, (unsigned long)slot->count,
                (unsigned long)slot->chunks_read, (unsigned long)slot->underruns, (double)slot->applied_gain,
                (double)music.master, slot->paused, (unsigned long)ndspChnGetSamplePos(channel(s, 0)),
                music.last_error[0] ? " error=" : "", music.last_error);
    }
    LightLock_Unlock(&music.lock);
    fflush(trace);
}
