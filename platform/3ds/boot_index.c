#include "boot_index.h"
#include <3ds.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BOOT_PREFIX "sdmc:/3ds/Colosseum3DS/boot/"
#define BOOT_DIR "/3ds/Colosseum3DS/boot"
#define PACKS_DIR "/3ds/Colosseum3DS/boot/packs"
#define PACKS_PREFIX "sdmc:/3ds/Colosseum3DS/boot/packs/"
#define READ_BATCH 64u

FILE *__real_fopen(const char *path, const char *mode);
int __real_stat(const char *path, struct stat *st);

typedef struct {
    uint32_t name;   /* offset into names */
    uint32_t group;  /* 0: boot/ itself, n: boot/packs/<group n-1>/ */
    uint64_t size;
    uint8_t directory;
} Entry;

enum { IDLE, BUILDING, READY, FAILED };
static volatile int state = IDLE;
static char *names;
static size_t names_used, names_capacity;
static Entry *entries;
static uint32_t entry_count, entry_capacity, group_count, packs_files;
static uint32_t *slots, slot_mask; /* entry index + 1, 0 = empty */
static uint32_t *group_names;      /* offsets into names */
static uint64_t build_us;
static int traced;

static LightLock stats_lock;
static int stats_ready;
static C3BootFsStats stats;

static uint64_t now_us(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000u); }

static unsigned lower(unsigned c) { return c >= 'A' && c <= 'Z' ? c + 32u : c; }
static uint32_t hash_name(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ lower((unsigned char)*s)) * 16777619u;
    return h;
}
static int same_name(const char *a, const char *b)
{
    for (; *a && lower((unsigned char)*a) == lower((unsigned char)*b); ++a, ++b) {}
    return lower((unsigned char)*a) == lower((unsigned char)*b);
}

static int add_name(const char *s, uint32_t *offset)
{
    size_t n = strlen(s) + 1;
    if (names_used + n > names_capacity) {
        size_t capacity = names_capacity ? names_capacity * 2 : 64u * 1024u;
        while (capacity < names_used + n) capacity *= 2;
        char *grown = realloc(names, capacity);
        if (!grown) return 0;
        names = grown; names_capacity = capacity;
    }
    memcpy(names + names_used, s, n);
    *offset = (uint32_t)names_used; names_used += n;
    return 1;
}

static int add_entry(const char *name, uint32_t group, uint64_t size, int directory)
{
    if (entry_count == entry_capacity) {
        uint32_t capacity = entry_capacity ? entry_capacity * 2 : 1024u;
        Entry *grown = realloc(entries, capacity * sizeof *grown);
        if (!grown) return 0;
        entries = grown; entry_capacity = capacity;
    }
    Entry *e = entries + entry_count;
    if (!add_name(name, &e->name)) return 0;
    e->group = group; e->size = size; e->directory = (uint8_t)(directory != 0);
    ++entry_count;
    return 1;
}

static const Entry *find(const char *name)
{
    if (!slots) return NULL;
    const Entry *found = NULL;
    for (uint32_t i = hash_name(name) & slot_mask; slots[i]; i = (i + 1u) & slot_mask) {
        const Entry *e = entries + slots[i] - 1u;
        /* boot/ wins over a grouped copy of the same name. */
        if (same_name(names + e->name, name) && (!found || e->group < found->group)) found = e;
    }
    return found;
}

/* List one directory of the SD archive; group as add_entry. */
static int list(FS_Archive archive, const char *directory, uint32_t group, int groups_only)
{
    Handle handle;
    if (R_FAILED(FSUSER_OpenDirectory(&handle, archive, fsMakePath(PATH_ASCII, directory)))) return 0;
    FS_DirectoryEntry *batch = malloc(READ_BATCH * sizeof *batch);
    int ok = batch != NULL;
    for (u32 read = 0; ok;) {
        if (R_FAILED(FSDIR_Read(handle, &read, READ_BATCH, batch))) { ok = 0; break; }
        if (!read) break;
        for (u32 i = 0; i < read && ok; ++i) {
            uint8_t name[0x106 * 3 + 1];
            ssize_t units = utf16_to_utf8(name, batch[i].name, sizeof name - 1);
            if (units <= 0 || units >= (ssize_t)sizeof name) continue;
            name[units] = 0;
            int is_directory = (batch[i].attributes & FS_ATTRIBUTE_DIRECTORY) != 0;
            if (groups_only) {
                if (!is_directory) continue;
                uint32_t offset;
                uint32_t *grown = realloc(group_names, (group_count + 1u) * sizeof *grown);
                if (!grown || !add_name((const char *)name, &offset)) { if (grown) group_names = grown; ok = 0; break; }
                group_names = grown; group_names[group_count++] = offset;
            } else {
                ok = add_entry((const char *)name, group, batch[i].fileSize, is_directory);
                if (ok && group) ++packs_files;
            }
        }
    }
    free(batch);
    FSDIR_Close(handle);
    return ok;
}

static int build(void)
{
    uint64_t started = now_us();
    FS_Archive archive;
    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) return 0;
    int ok = list(archive, BOOT_DIR, 0, 0);
    if (ok && list(archive, PACKS_DIR, 0, 1)) {
        char directory[160];
        for (uint32_t g = 0; g < group_count && ok; ++g) {
            snprintf(directory, sizeof directory, PACKS_DIR "/%s", names + group_names[g]);
            ok = list(archive, directory, g + 1u, 0);
        }
    }
    FSUSER_CloseArchive(archive);
    if (!ok) return 0;
    uint32_t capacity = 1024u;
    while (capacity < entry_count * 2u) capacity *= 2u;
    slots = calloc(capacity, sizeof *slots);
    if (!slots) return 0;
    slot_mask = capacity - 1u;
    for (uint32_t i = 0; i < entry_count; ++i) {
        uint32_t s = hash_name(names + entries[i].name) & slot_mask;
        while (slots[s]) s = (s + 1u) & slot_mask;
        slots[s] = i + 1u;
    }
    build_us = now_us() - started;
    return 1;
}

static void finish_build(void)
{
    int ok = build();
    __atomic_store_n(&state, ok ? READY : FAILED, __ATOMIC_SEQ_CST);
}

static void build_thread(void *argument) { (void)argument; finish_build(); }

static int claim(void)
{
    int expected = IDLE;
    return __atomic_compare_exchange_n(&state, &expected, BUILDING, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

void c3_boot_index_start(void)
{
    if (!stats_ready) { LightLock_Init(&stats_lock); stats_ready = 1; }
    if (!claim()) return;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    /* Below the scene: it waits on the card, not the CPU. */
    if (!threadCreate(build_thread, NULL, 16 * 1024, priority < 0x3f ? priority + 1 : priority, -2, true))
        finish_build();
}

/* READY or FAILED, building on this thread when nobody started it. */
static int settled(void)
{
    if (claim()) finish_build();
    while (__atomic_load_n(&state, __ATOMIC_SEQ_CST) == BUILDING) svcSleepThread(1000000);
    return __atomic_load_n(&state, __ATOMIC_SEQ_CST);
}

static const char *boot_name(const char *path)
{
    if (!path || strncmp(path, BOOT_PREFIX, sizeof BOOT_PREFIX - 1)) return NULL;
    const char *name = path + sizeof BOOT_PREFIX - 1;
    return *name && !strchr(name, '/') ? name : NULL;
}

static void count(const char *name, uint64_t elapsed, int answered)
{
    if (!stats_ready) return;
    LightLock_Lock(&stats_lock);
    if (answered) ++stats.answered;
    else {
        ++stats.lookups; stats.lookup_us += elapsed;
        if (elapsed > stats.max_us) { stats.max_us = elapsed; snprintf(stats.max_name, sizeof stats.max_name, "%s", name); }
    }
    LightLock_Unlock(&stats_lock);
}

static FILE *timed_fopen(const char *path, const char *mode, const char *name)
{
    uint64_t started = now_us();
    FILE *file = __real_fopen(path, mode);
    count(name, now_us() - started, 0);
    return file;
}

static FILE *open_entry(const Entry *e, const char *path, const char *mode, const char *name)
{
    if (!e) { count(name, 0, 1); errno = ENOENT; return NULL; }
    if (!e->group) return timed_fopen(path, mode, name);
    char grouped[192];
    snprintf(grouped, sizeof grouped, PACKS_PREFIX "%s/%s", names + group_names[e->group - 1u], names + e->name);
    return timed_fopen(grouped, mode, name);
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    const char *name = boot_name(path);
    if (!name || !mode || mode[0] != 'r') return __real_fopen(path, mode);
    int now = __atomic_load_n(&state, __ATOMIC_SEQ_CST);
    if (now == IDLE) {
        /* Nobody listed the directory yet (boot movies): open as before,
         * and list it only when the name is not where it used to be. */
        FILE *file = timed_fopen(path, mode, name);
        if (file || settled() != READY) return file;
        const Entry *e = find(name);
        return e && e->group ? open_entry(e, path, mode, name) : (errno = ENOENT, NULL);
    }
    if (now == BUILDING) now = settled();
    if (now != READY) return timed_fopen(path, mode, name);
    return open_entry(find(name), path, mode, name);
}

static int stat_entry(const Entry *e, struct stat *st, const char *name)
{
    count(name, 0, 1);
    if (!e) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof *st);
    st->st_mode = e->directory ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_nlink = 1;
    st->st_size = (off_t)e->size;
    return 0;
}

int __wrap_stat(const char *path, struct stat *st)
{
    const char *name = boot_name(path);
    if (!name || !st) return __real_stat(path, st);
    int now = __atomic_load_n(&state, __ATOMIC_SEQ_CST);
    if (now == IDLE) {
        uint64_t started = now_us();
        int result = __real_stat(path, st);
        count(name, now_us() - started, 0);
        if (!result || settled() != READY) return result;
        return stat_entry(find(name), st, name);
    }
    if (now == BUILDING) now = settled();
    if (now != READY) {
        uint64_t started = now_us();
        int result = __real_stat(path, st);
        count(name, now_us() - started, 0);
        return result;
    }
    return stat_entry(find(name), st, name);
}

uint64_t c3_boot_index_size(const char *path)
{
    const char *name = boot_name(path);
    if (!name || __atomic_load_n(&state, __ATOMIC_SEQ_CST) != READY) return 0;
    const Entry *e = find(name);
    return e && !e->directory ? e->size : 0;
}

void c3_boot_fs_stats(C3BootFsStats *out)
{
    if (!out) return;
    if (!stats_ready) { memset(out, 0, sizeof *out); return; }
    LightLock_Lock(&stats_lock);
    *out = stats;
    LightLock_Unlock(&stats_lock);
}

void c3_boot_index_trace(FILE *trace)
{
    int now = __atomic_load_n(&state, __ATOMIC_SEQ_CST);
    if (!trace || traced || (now != READY && now != FAILED)) return;
    traced = 1;
    fprintf(trace, "boot_index status=%s entries=%lu groups=%lu grouped_files=%lu us=%llu\n",
            now == READY ? "ready" : "failed", (unsigned long)entry_count, (unsigned long)group_count,
            (unsigned long)packs_files, (unsigned long long)build_us);
    fflush(trace);
}
