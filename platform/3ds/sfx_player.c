#include "sfx_player.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define VOICES 8u
#define CHANNEL_BASE 12u             /* movie 0, battle cues 4..7, music 8..11 */
#define BUFFERS 3u
#define BUFFER_BYTES 4096u           /* one 512-frame DSP-ADPCM chunk */
#define TICK_NS 4000000LL
#define FADE_TICKS 6u                /* stop fade, ~24 ms */
#define QUEUE 32u
#define MAX_ROWS 8192u
#define MAX_CHUNKS 4096u

typedef struct Row {
    uint32_t sound, offset, samples, loop_chunk, chunk_count, bytes;
    uint16_t rate;
} Row;

typedef struct Chunk {
    uint32_t offset, samples;
    uint16_t ps;
    int16_t hist1, hist2;
} Chunk;

typedef struct Request {
    int kind;                        /* 1 play, 2 stop sound, 3 stop handle, 4 stop all */
    uint32_t value, handle, volume;
} Request;

typedef struct Voice {
    int state;                       /* 0 idle, 1 streaming, 2 draining, 3 fading out */
    uint32_t handle, sound, row, next, count, loop, data;
    Chunk *chunks;
    uint16_t coefs[16];
    ndspWaveBuf wave[BUFFERS];
    ndspAdpcmData adpcm[BUFFERS];
    float volume, applied_gain;
    unsigned fade;
    int applied_paused, started;
} Voice;

static struct {
    int open, stop_thread, paused, hooked;
    unsigned apt_pause_mask;
    aptHookCookie hook;
    FILE *file;
    Row *rows;
    uint32_t row_count, next_handle, age;
    uint8_t *linear;
    Voice voices[VOICES];
    Request queue[QUEUE];
    unsigned queued;
    float master, master_target, master_step;
    LightLock lock;
    Thread thread;
    uint32_t dropped, underruns;
    char last_error[128];
} sfx;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static void set_error(char *error, size_t capacity, const char *text)
{
    if (error && capacity) snprintf(error, capacity, "%s", text);
}

static int find_row(uint32_t sound)
{
    uint32_t lo = 0, hi = sfx.row_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u;
        if (sfx.rows[mid].sound < sound) lo = mid + 1u;
        else hi = mid;
    }
    return lo < sfx.row_count && sfx.rows[lo].sound == sound ? (int)lo : -1;
}

static unsigned channel(unsigned v) { return CHANNEL_BASE + v; }

static void release_voice(unsigned v)
{
    Voice *voice = &sfx.voices[v];
    ndspChnWaveBufClear(channel(v));
    ndspChnSetPaused(channel(v), false);
    free(voice->chunks);
    voice->chunks = NULL;
    voice->state = 0;
    voice->handle = voice->sound = 0;
}

/* Worker only: reads the coefficients and chunk table, programs the channel. */
static int start_voice(unsigned v, int row_index, uint32_t handle, uint32_t volume)
{
    const Row *row = &sfx.rows[row_index];
    Voice *voice = &sfx.voices[v];
    uint8_t *table = malloc(32u + row->chunk_count * 16u);
    Chunk *chunks = malloc(row->chunk_count * sizeof(Chunk));
    if (!table || !chunks || fseek(sfx.file, (long)row->offset, SEEK_SET) ||
        fread(table, 1, 32u + row->chunk_count * 16u, sfx.file) != 32u + row->chunk_count * 16u) {
        free(table); free(chunks);
        snprintf(sfx.last_error, sizeof sfx.last_error, "sfx table read failed sound=%lu", (unsigned long)row->sound);
        return 0;
    }
    for (uint32_t i = 0; i < row->chunk_count; ++i) {
        const uint8_t *c = table + 32u + i * 16u;
        chunks[i].offset = le32(c);
        chunks[i].samples = le32(c + 4);
        chunks[i].ps = le16(c + 8);
        chunks[i].hist1 = (int16_t)le16(c + 10);
        chunks[i].hist2 = (int16_t)le16(c + 12);
        if (!chunks[i].samples || (chunks[i].samples + 13u) / 14u * 8u > BUFFER_BYTES ||
            chunks[i].offset + (chunks[i].samples + 13u) / 14u * 8u > row->bytes) {
            free(table); free(chunks);
            snprintf(sfx.last_error, sizeof sfx.last_error, "invalid sfx chunk sound=%lu", (unsigned long)row->sound);
            return 0;
        }
    }
    release_voice(v);
    for (unsigned k = 0; k < 16; ++k) voice->coefs[k] = le16(table + k * 2u);
    free(table);
    voice->chunks = chunks;
    voice->count = row->chunk_count;
    voice->loop = row->loop_chunk;
    voice->data = row->offset + 32u + row->chunk_count * 16u;
    voice->next = 0;
    voice->row = (uint32_t)row_index;
    voice->sound = row->sound;
    voice->handle = handle;
    voice->volume = (volume >= 127u ? 127u : volume) / 127.0f;
    voice->fade = 0;
    voice->started = 0;
    memset(voice->wave, 0, sizeof voice->wave);
    unsigned ch = channel(v);
    ndspChnReset(ch);
    ndspChnSetPaused(ch, true);
    ndspChnSetInterp(ch, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(ch, (float)row->rate);
    ndspChnSetFormat(ch, NDSP_FORMAT_MONO_ADPCM);
    ndspChnSetAdpcmCoefs(ch, voice->coefs);
    voice->applied_gain = -1.0f;
    voice->applied_paused = 1;
    voice->state = 1;
    return 1;
}

static int read_chunk(unsigned v, unsigned b)
{
    Voice *voice = &sfx.voices[v];
    const Chunk *chunk = &voice->chunks[voice->next];
    uint32_t bytes = (chunk->samples + 13u) / 14u * 8u;
    uint8_t *base = sfx.linear + (v * BUFFERS + b) * BUFFER_BYTES;
    if (fseek(sfx.file, (long)(voice->data + chunk->offset), SEEK_SET) || fread(base, 1, bytes, sfx.file) != bytes)
        return 0;
    ndspWaveBuf *wave = &voice->wave[b];
    ndspAdpcmData *adpcm = &voice->adpcm[b];
    memset(wave, 0, sizeof(*wave));
    adpcm->index = chunk->ps;
    adpcm->history0 = chunk->hist1;
    adpcm->history1 = chunk->hist2;
    wave->data_adpcm = base;
    wave->adpcm_data = adpcm;
    wave->nsamples = chunk->samples;
    wave->looping = false;
    DSP_FlushDataCache(base, bytes);
    if (++voice->next >= voice->count) {
        if (voice->loop < voice->count) voice->next = voice->loop;
        else voice->state = 2;
    }
    return 1;
}

static int voice_busy(unsigned v)
{
    const Voice *voice = &sfx.voices[v];
    for (unsigned b = 0; b < BUFFERS; ++b)
        if (voice->wave[b].status == NDSP_WBUF_QUEUED || voice->wave[b].status == NDSP_WBUF_PLAYING) return 1;
    return 0;
}

static void queue_buffers(unsigned v)
{
    Voice *voice = &sfx.voices[v];
    if (voice->started && !voice_busy(v) && !voice->applied_paused) ++sfx.underruns;
    for (unsigned b = 0; b < BUFFERS && voice->state == 1; ++b) {
        int status = voice->wave[b].status;
        if (status != NDSP_WBUF_FREE && status != NDSP_WBUF_DONE) continue;
        if (!read_chunk(v, b)) {
            snprintf(sfx.last_error, sizeof sfx.last_error, "sfx read failed sound=%lu chunk=%lu",
                     (unsigned long)voice->sound, (unsigned long)voice->next);
            voice->state = 2;
            break;
        }
        ndspChnWaveBufAdd(channel(v), &voice->wave[b]);
        voice->started = 1;
    }
}

static unsigned pick_voice(void)
{
    unsigned best = 0;
    uint32_t oldest = UINT32_MAX;
    for (unsigned v = 0; v < VOICES; ++v) {
        if (!sfx.voices[v].state) return v;
        if (sfx.voices[v].handle < oldest) { oldest = sfx.voices[v].handle; best = v; }
    }
    return best;                     /* all busy: replace the oldest start */
}

static float step_toward(float value, float target, float step)
{
    if (value < target) return value + step >= target ? target : value + step;
    if (value > target) return value - step <= target ? target : value - step;
    return value;
}

static void begin_fade(Voice *voice)
{
    if (voice->state && voice->state != 3) { voice->state = 3; voice->fade = FADE_TICKS; }
}

static void service(void)
{
    Request requests[QUEUE];
    unsigned count;
    LightLock_Lock(&sfx.lock);
    count = sfx.queued;
    memcpy(requests, sfx.queue, count * sizeof(Request));
    sfx.queued = 0;
    sfx.master = step_toward(sfx.master, sfx.master_target, sfx.master_step);
    int paused = sfx.paused || sfx.apt_pause_mask;
    LightLock_Unlock(&sfx.lock);

    for (unsigned i = 0; i < count; ++i) {
        const Request *r = &requests[i];
        if (r->kind == 1) {
            int row = find_row(r->value);
            if (row >= 0) start_voice(pick_voice(), row, r->handle, r->volume);
        } else {
            for (unsigned v = 0; v < VOICES; ++v) {
                Voice *voice = &sfx.voices[v];
                if ((r->kind == 2 && voice->sound == r->value) || (r->kind == 3 && voice->handle == r->handle) ||
                    r->kind == 4)
                    begin_fade(voice);
            }
        }
    }
    for (unsigned v = 0; v < VOICES; ++v) {
        Voice *voice = &sfx.voices[v];
        if (!voice->state) continue;
        float gain = voice->volume * sfx.master;
        if (voice->state == 3) {
            if (!voice->fade) { release_voice(v); continue; }
            gain *= (float)--voice->fade / (float)FADE_TICKS;
        }
        if (gain != voice->applied_gain) {
            float mix[12] = {0};
            mix[0] = mix[1] = gain;  /* mono sound, both front speakers */
            ndspChnSetMix(channel(v), mix);
            voice->applied_gain = gain;
        }
        if (voice->state == 1) queue_buffers(v);
        if (paused != voice->applied_paused && voice->started) {
            ndspChnSetPaused(channel(v), paused);
            voice->applied_paused = paused;
        }
        if (voice->state == 2 && !voice_busy(v)) release_voice(v);
    }
}

static void worker(void *arg)
{
    (void)arg;
    while (!sfx.stop_thread) {
        service();
        svcSleepThread(TICK_NS);
    }
}

static void application_hook(APT_HookType hook, void *user)
{
    (void)user;
    LightLock_Lock(&sfx.lock);
    if (hook == APTHOOK_ONSUSPEND) sfx.apt_pause_mask |= 1u;
    else if (hook == APTHOOK_ONRESTORE) sfx.apt_pause_mask &= ~1u;
    else if (hook == APTHOOK_ONSLEEP) sfx.apt_pause_mask |= 2u;
    else if (hook == APTHOOK_ONWAKEUP) sfx.apt_pause_mask &= ~2u;
    LightLock_Unlock(&sfx.lock);
}

static int load_rows(const char *path, char *error, size_t capacity)
{
    uint8_t head[32];
    sfx.file = fopen(path, "rb");
    if (!sfx.file) { set_error(error, capacity, "No SFX pack (sfx.c3s)."); return 0; }
    if (fread(head, 1, 32, sfx.file) != 32 || memcmp(head, "C3SX", 4) || le32(head + 4) != 1 ||
        le32(head + 8) > MAX_ROWS || le32(head + 12) != 32u) {
        set_error(error, capacity, "Invalid SFX pack.");
        return 0;
    }
    uint32_t count = le32(head + 8), end = le32(head + 20);
    uint8_t *raw = malloc(count ? count * 32u : 1u);
    sfx.rows = calloc(count ? count : 1u, sizeof(Row));
    if (!raw || !sfx.rows || fread(raw, 32, count, sfx.file) != count) {
        free(raw);
        set_error(error, capacity, "SFX index read failed.");
        return 0;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *p = raw + i * 32u;
        Row *row = &sfx.rows[i];
        row->sound = le32(p);
        row->offset = le32(p + 4);
        row->samples = le32(p + 8);
        row->loop_chunk = le32(p + 12);
        row->rate = le16(p + 16);
        row->chunk_count = le32(p + 20);
        row->bytes = le32(p + 24);
        if (p[18] != 1 || p[19] != 0 || !row->chunk_count || row->chunk_count > MAX_CHUNKS || !row->rate ||
            (uint64_t)row->offset + 32u + row->chunk_count * 16u + row->bytes > end ||
            (i && row->sound <= sfx.rows[i - 1].sound) ||
            (row->loop_chunk != 0xFFFFFFFFu && row->loop_chunk >= row->chunk_count)) {
            free(raw);
            set_error(error, capacity, "Invalid SFX row.");
            return 0;
        }
    }
    free(raw);
    sfx.row_count = count;
    return 1;
}

int c3_sfx_open(const char *path, char *error, size_t capacity)
{
    s32 priority = 0x30;
    int ndsp = 0;
    if (sfx.open) return 1;
    memset(&sfx, 0, sizeof sfx);
    LightLock_Init(&sfx.lock);
    sfx.master = sfx.master_target = sfx.master_step = 1.0f;
    if (!path || !load_rows(path, error, capacity)) goto fail;
    sfx.linear = linearAlloc(VOICES * BUFFERS * BUFFER_BYTES);
    if (!sfx.linear) { set_error(error, capacity, "SFX buffer allocation failed."); goto fail; }
    if (R_FAILED(ndspInit())) { set_error(error, capacity, "NDSP is unavailable (dspfirm.cdc missing?)."); goto fail; }
    ndsp = 1;
    for (unsigned v = 0; v < VOICES; ++v) ndspChnReset(channel(v));
    aptHook(&sfx.hook, application_hook, NULL);
    sfx.hooked = 1;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    sfx.thread = threadCreate(worker, NULL, 16 * 1024, priority > 0x18 ? priority - 1 : priority, -2, false);
    if (!sfx.thread) { set_error(error, capacity, "SFX thread creation failed."); goto fail; }
    sfx.open = 1;
    return 1;
fail:
    if (sfx.hooked) aptUnhook(&sfx.hook);
    if (ndsp) ndspExit();
    if (sfx.linear) linearFree(sfx.linear);
    if (sfx.file) fclose(sfx.file);
    free(sfx.rows);
    memset(&sfx, 0, sizeof sfx);
    return 0;
}

void c3_sfx_close(void)
{
    if (!sfx.open) return;
    sfx.stop_thread = 1;
    threadJoin(sfx.thread, U64_MAX);
    threadFree(sfx.thread);
    for (unsigned v = 0; v < VOICES; ++v) release_voice(v);
    aptUnhook(&sfx.hook);
    ndspExit();
    linearFree(sfx.linear);
    fclose(sfx.file);
    free(sfx.rows);
    memset(&sfx, 0, sizeof sfx);
}

int c3_sfx_has(uint32_t sound)
{
    return sfx.open && find_row(sound) >= 0;
}

static int push(Request request)
{
    int ok = 0;
    LightLock_Lock(&sfx.lock);
    if (sfx.queued < QUEUE) { sfx.queue[sfx.queued++] = request; ok = 1; }
    else ++sfx.dropped;
    LightLock_Unlock(&sfx.lock);
    return ok;
}

uint32_t c3_sfx_play(uint32_t sound, uint32_t volume)
{
    uint32_t handle;
    if (!sfx.open || find_row(sound) < 0) return 0;
    LightLock_Lock(&sfx.lock);
    handle = ++sfx.next_handle ? sfx.next_handle : ++sfx.next_handle;
    LightLock_Unlock(&sfx.lock);
    return push((Request){1, sound, handle, volume}) ? handle : 0;
}

void c3_sfx_stop(uint32_t sound)
{
    if (sfx.open) push((Request){2, sound, 0, 0});
}

void c3_sfx_stop_handle(uint32_t handle)
{
    if (sfx.open && handle) push((Request){3, 0, handle, 0});
}

void c3_sfx_stop_all(void)
{
    if (sfx.open) push((Request){4, 0, 0, 0});
}

static int status_where(int by_handle, uint32_t value)
{
    int result = 0;
    if (!sfx.open) return 0;
    LightLock_Lock(&sfx.lock);
    for (unsigned i = 0; i < sfx.queued; ++i)
        if (sfx.queue[i].kind == 1 && (by_handle ? sfx.queue[i].handle : sfx.queue[i].value) == value) result = 1;
    LightLock_Unlock(&sfx.lock);
    for (unsigned v = 0; v < VOICES && !result; ++v) {
        const Voice *voice = &sfx.voices[v];   /* word reads; the worker owns writes */
        if (voice->state && voice->state != 3 && (by_handle ? voice->handle : voice->sound) == value) result = 1;
    }
    return result;
}

int c3_sfx_status(uint32_t sound) { return status_where(0, sound); }
int c3_sfx_handle_status(uint32_t handle) { return handle ? status_where(1, handle) : 0; }

void c3_sfx_master(uint32_t volume, uint32_t fade_ms)
{
    if (!sfx.open) return;
    LightLock_Lock(&sfx.lock);
    sfx.master_target = (volume >= 127u ? 127u : volume) / 127.0f;
    sfx.master_step = fade_ms ? (float)(TICK_NS / 1000000LL) / (float)fade_ms : 1.0f;
    LightLock_Unlock(&sfx.lock);
}

void c3_sfx_pause(int paused)
{
    if (!sfx.open) return;
    LightLock_Lock(&sfx.lock);
    sfx.paused = !!paused;
    LightLock_Unlock(&sfx.lock);
}

void c3_sfx_trace(FILE *trace, const char *phase)
{
    if (!trace) return;
    if (!sfx.open) { fprintf(trace, "sfx phase=%s open=0\n", phase ? phase : ""); return; }
    fprintf(trace, "sfx phase=%s rows=%lu master=%.2f paused=%d apt=%u dropped=%lu underruns=%lu%s%s\n",
            phase ? phase : "", (unsigned long)sfx.row_count, (double)sfx.master, sfx.paused, sfx.apt_pause_mask,
            (unsigned long)sfx.dropped, (unsigned long)sfx.underruns,
            sfx.last_error[0] ? " error=" : "", sfx.last_error);
    for (unsigned v = 0; v < VOICES; ++v) {
        const Voice *voice = &sfx.voices[v];
        if (!voice->state) continue;
        fprintf(trace, "sfx phase=%s voice=%u ch=%u state=%d sound=0x%03lX handle=%lu chunk=%lu/%lu gain=%.2f\n",
                phase ? phase : "", v, channel(v), voice->state, (unsigned long)voice->sound,
                (unsigned long)voice->handle, (unsigned long)voice->next, (unsigned long)voice->count,
                (double)voice->applied_gain);
    }
    fflush(trace);
}
