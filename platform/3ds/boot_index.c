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
#define SAVED_PATH "sdmc:/3ds/Colosseum3DS/boot_index.c3x"
#define SAVED_TEMP "sdmc:/3ds/Colosseum3DS/boot_index.tmp"
#define SAVED_MAGIC "C3BX"
#define SAVED_VERSION 1u
#define READ_BATCH 64u
#define MAX_GROUPS 1024u

FILE *__real_fopen(const char *path, const char *mode);
int __real_stat(const char *path, struct stat *st);

typedef struct {
    uint32_t name;   /* offset into names */
    uint32_t group;  /* 0: boot/ itself, n: boot/packs/<group n-1>/ */
    uint64_t size;
    uint32_t directory;
} Entry;

/* One listing: boot/ and the groups (full), or a single group. Never
 * changed once published, so lookups read it without a lock. */
typedef struct {
    char *names;
    size_t names_used, names_capacity;
    Entry *entries;
    uint32_t entry_count, entry_capacity;
    uint32_t *slots, slot_mask; /* entry index + 1, 0 = empty */
    uint32_t *group_names;      /* offsets into names */
    uint32_t group_count, packs_files;
} Index;

/* IDLE: not started. BUILDING: listing, nothing to answer from yet.
 * PROVISIONAL: boot/ and the group list are fresh, the groups' contents
 * come from the copy saved by the last full listing, and each group is
 * listed again the first time a lookup needs it. READY: full listing. */
enum { IDLE, BUILDING, PROVISIONAL, READY, FAILED };
static volatile int state = IDLE;
static Index *full, *provisional;
static char **prefixes;                   /* per provisional group, see derive_prefixes */
static Index *volatile verified[MAX_GROUPS]; /* fresh listing of a provisional group */
static LightLock verify_lock;
static volatile int busy, waiters, full_finished;
static uint64_t build_us, provisional_us;
static int saved_state; /* 0 none, 1 used, 2 stale (group list changed or unreadable) */
static unsigned verified_count;
static int traced_state = IDLE;

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

static int add_name(Index *x, const char *s, uint32_t *offset)
{
    size_t n = strlen(s) + 1;
    if (x->names_used + n > x->names_capacity) {
        size_t capacity = x->names_capacity ? x->names_capacity * 2 : 16u * 1024u;
        while (capacity < x->names_used + n) capacity *= 2;
        char *grown = realloc(x->names, capacity);
        if (!grown) return 0;
        x->names = grown; x->names_capacity = capacity;
    }
    memcpy(x->names + x->names_used, s, n);
    *offset = (uint32_t)x->names_used; x->names_used += n;
    return 1;
}

static int add_entry(Index *x, const char *name, uint32_t group, uint64_t size, int directory)
{
    if (x->entry_count == x->entry_capacity) {
        uint32_t capacity = x->entry_capacity ? x->entry_capacity * 2 : 256u;
        Entry *grown = realloc(x->entries, capacity * sizeof *grown);
        if (!grown) return 0;
        x->entries = grown; x->entry_capacity = capacity;
    }
    Entry *e = x->entries + x->entry_count;
    if (!add_name(x, name, &e->name)) return 0;
    e->group = group; e->size = size; e->directory = directory != 0;
    ++x->entry_count;
    if (group) ++x->packs_files;
    return 1;
}

static int add_group(Index *x, const char *name)
{
    uint32_t offset;
    if (x->group_count >= MAX_GROUPS) return 0;
    uint32_t *grown = realloc(x->group_names, (x->group_count + 1u) * sizeof *grown);
    if (!grown) return 0;
    x->group_names = grown;
    if (!add_name(x, name, &offset)) return 0;
    x->group_names[x->group_count++] = offset;
    return 1;
}

static void free_index(Index *x)
{
    if (!x) return;
    free(x->names); free(x->entries); free(x->slots); free(x->group_names); free(x);
}

static int hash_index(Index *x)
{
    uint32_t capacity = 64u;
    while (capacity < x->entry_count * 2u) capacity *= 2u;
    free(x->slots);
    x->slots = calloc(capacity, sizeof *x->slots);
    if (!x->slots) return 0;
    x->slot_mask = capacity - 1u;
    for (uint32_t i = 0; i < x->entry_count; ++i) {
        uint32_t s = hash_name(x->names + x->entries[i].name) & x->slot_mask;
        while (x->slots[s]) s = (s + 1u) & x->slot_mask;
        x->slots[s] = i + 1u;
    }
    return 1;
}

static const Entry *find(const Index *x, const char *name)
{
    if (!x || !x->slots) return NULL;
    const Entry *found = NULL;
    for (uint32_t i = hash_name(name) & x->slot_mask; x->slots[i]; i = (i + 1u) & x->slot_mask) {
        const Entry *e = x->entries + x->slots[i] - 1u;
        /* boot/ wins over a grouped copy of the same name. */
        if (same_name(x->names + e->name, name) && (!found || e->group < found->group)) found = e;
    }
    return found;
}

static int find_group(const Index *x, const char *name, uint32_t *group)
{
    for (uint32_t g = 0; g < x->group_count; ++g)
        if (!strcmp(x->names + x->group_names[g], name)) { *group = g + 1u; return 1; }
    return 0;
}

/* The listing pauses while packs load (c3_boot_index_busy) unless a lookup
 * is waiting for it. */
static void yield_to_loads(void)
{
    while (__atomic_load_n(&busy, __ATOMIC_SEQ_CST) > 0 && !__atomic_load_n(&waiters, __ATOMIC_SEQ_CST))
        svcSleepThread(10000000);
}

/* List one directory of the SD archive into x: its files as entries of
 * `group`, or (groups_only) its subdirectories as groups. */
static int list(Index *x, FS_Archive archive, const char *directory, uint32_t group, int groups_only)
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
            if (groups_only) { if (is_directory) ok = add_group(x, (const char *)name); }
            else ok = add_entry(x, (const char *)name, group, batch[i].fileSize, is_directory);
        }
    }
    free(batch);
    FSDIR_Close(handle);
    return ok;
}

static int list_group(Index *x, FS_Archive archive, const Index *groups, uint32_t g, uint32_t group)
{
    char directory[160];
    snprintf(directory, sizeof directory, PACKS_DIR "/%s", groups->names + groups->group_names[g - 1u]);
    return list(x, archive, directory, group, 0);
}

/* ---- The copy saved by the last full listing (SAVED_PATH) ----
 * "C3BX", version, group count, entry count, name bytes, the group name
 * offsets, the entries {name, group, size lo, size hi, directory}, the
 * names, then an FNV-1a sum of everything before it. */
static uint32_t fnv(uint32_t h, const void *data, size_t bytes)
{
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < bytes; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint8_t *serialize(const Index *x, size_t *bytes)
{
    size_t size = 20u + x->group_count * 4u + x->entry_count * 20u + x->names_used + 4u;
    uint8_t *out = malloc(size), *p = out;
    if (!out) return NULL;
    uint32_t head[5] = {0, SAVED_VERSION, x->group_count, x->entry_count, (uint32_t)x->names_used};
    memcpy(head, SAVED_MAGIC, 4);
    memcpy(p, head, sizeof head); p += sizeof head;
    memcpy(p, x->group_names, x->group_count * 4u); p += x->group_count * 4u;
    for (uint32_t i = 0; i < x->entry_count; ++i) {
        const Entry *e = x->entries + i;
        uint32_t row[5] = {e->name, e->group, (uint32_t)e->size, (uint32_t)(e->size >> 32), e->directory};
        memcpy(p, row, sizeof row); p += sizeof row;
    }
    memcpy(p, x->names, x->names_used); p += x->names_used;
    uint32_t sum = fnv(2166136261u, out, (size_t)(p - out));
    memcpy(p, &sum, 4);
    *bytes = size;
    return out;
}

static Index *load_saved(void)
{
    FILE *file = __real_fopen(SAVED_PATH, "rb");
    if (!file) return NULL;
    uint8_t *data = NULL;
    long size = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (size >= 24 && size < 8 * 1024 * 1024 && fseek(file, 0, SEEK_SET) == 0 && (data = malloc((size_t)size)) != NULL &&
        fread(data, 1, (size_t)size, file) != (size_t)size) { free(data); data = NULL; }
    fclose(file);
    if (!data) return NULL;
    Index *x = NULL;
    uint32_t head[5], sum;
    memcpy(head, data, sizeof head);
    memcpy(&sum, data + size - 4, 4);
    uint64_t expected = 20u + (uint64_t)head[2] * 4u + (uint64_t)head[3] * 20u + head[4] + 4u;
    if (!memcmp(head, SAVED_MAGIC, 4) && head[1] == SAVED_VERSION && head[2] <= MAX_GROUPS && expected == (uint64_t)size &&
        head[4] && data[size - 5] == 0 && fnv(2166136261u, data, (size_t)size - 4u) == sum && (x = calloc(1, sizeof *x)) != NULL) {
        const uint8_t *p = data + 20;
        x->group_count = head[2]; x->entry_count = x->entry_capacity = head[3];
        x->names_used = x->names_capacity = head[4];
        x->group_names = malloc((x->group_count ? x->group_count : 1u) * 4u);
        x->entries = malloc((x->entry_count ? x->entry_count : 1u) * sizeof *x->entries);
        x->names = malloc(x->names_used);
        int ok = x->group_names && x->entries && x->names;
        if (ok) {
            memcpy(x->group_names, p, x->group_count * 4u); p += x->group_count * 4u;
            for (uint32_t i = 0; i < x->entry_count; ++i, p += 20) {
                uint32_t row[5];
                memcpy(row, p, sizeof row);
                x->entries[i] = (Entry){row[0], row[1], (uint64_t)row[2] | (uint64_t)row[3] << 32, row[4] != 0};
                ok &= row[0] < x->names_used && row[1] <= x->group_count;
                if (row[1]) ++x->packs_files;
            }
            memcpy(x->names, p, x->names_used);
            for (uint32_t g = 0; g < x->group_count; ++g) ok &= x->group_names[g] < x->names_used;
        }
        if (!ok) { free_index(x); x = NULL; }
    }
    free(data);
    return x;
}

static int same_listing(const Index *a, const Index *b)
{
    size_t na = 0, nb = 0;
    uint8_t *sa = serialize(a, &na), *sb = serialize(b, &nb);
    int same = sa && sb && na == nb && !memcmp(sa, sb, na);
    free(sa); free(sb);
    return same;
}

static void save(const Index *x)
{
    size_t bytes = 0;
    uint8_t *data = serialize(x, &bytes);
    if (!data) return;
    FILE *file = __real_fopen(SAVED_TEMP, "wb");
    int ok = file && fwrite(data, 1, bytes, file) == bytes;
    if (file && fclose(file) != 0) ok = 0;
    free(data);
    /* Replace the old copy; a copy that fails its checks is ignored. */
    if (ok) { remove(SAVED_PATH); ok = rename(SAVED_TEMP, SAVED_PATH) == 0; }
    if (!ok) remove(SAVED_TEMP);
}

/* The prefix a group's files share, as tools/sd_pack_layout.py groups them:
 * the longest common start that every name continues with '_' or '.'. */
static char *derive_prefix(const Index *x, uint32_t group)
{
    const char *first = NULL;
    size_t common = 0;
    for (uint32_t i = 0; i < x->entry_count; ++i) {
        if (x->entries[i].group != group || x->entries[i].directory) continue;
        const char *name = x->names + x->entries[i].name;
        if (!first) { first = name; common = strlen(name); continue; }
        size_t n = 0;
        while (n < common && name[n] && lower((unsigned char)name[n]) == lower((unsigned char)first[n])) ++n;
        common = n;
    }
    if (!first) return NULL;
    for (;; --common) {
        int boundary = common > 0;
        for (uint32_t i = 0; i < x->entry_count && boundary; ++i) {
            if (x->entries[i].group != group || x->entries[i].directory) continue;
            char c = x->names[x->entries[i].name + common];
            boundary = c == '_' || c == '.';
        }
        if (boundary || !common) break;
    }
    if (!common) return NULL;
    char *prefix = malloc(common + 1);
    if (prefix) { memcpy(prefix, first, common); prefix[common] = 0; }
    return prefix;
}

/* The provisional group that would hold name: the longest group prefix it
 * continues with '_' or '.'. 0 when none does. */
static uint32_t candidate_group(const char *name)
{
    uint32_t best = 0;
    size_t best_length = 0;
    for (uint32_t g = 0; g < provisional->group_count; ++g) {
        const char *prefix = prefixes[g];
        if (!prefix) continue;
        size_t n = strlen(prefix);
        if (n <= best_length) continue;
        size_t i = 0;
        while (i < n && lower((unsigned char)name[i]) == lower((unsigned char)prefix[i])) ++i;
        if (i == n && (name[n] == '_' || name[n] == '.')) { best = g + 1u; best_length = n; }
    }
    return best;
}

/* Fresh listing of provisional group g (listed once per boot). */
static const Index *verify(uint32_t g)
{
    if (!g || g > provisional->group_count) return NULL;
    Index *v = __atomic_load_n(&verified[g - 1u], __ATOMIC_SEQ_CST);
    if (v) return v;
    LightLock_Lock(&verify_lock);
    v = verified[g - 1u];
    if (!v) {
        FS_Archive archive;
        if (R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
            v = calloc(1, sizeof *v);
            if (v && (!list_group(v, archive, provisional, g, g) || !hash_index(v))) { free_index(v); v = NULL; }
            FSUSER_CloseArchive(archive);
        }
        if (v) { __atomic_store_n(&verified[g - 1u], v, __ATOMIC_SEQ_CST); ++verified_count; }
    }
    LightLock_Unlock(&verify_lock);
    return v;
}

/* boot/ and the group list fresh, the groups' contents from the saved copy
 * when it lists exactly the same groups. */
static Index *make_provisional(const Index *fresh)
{
    Index *saved = load_saved();
    if (!saved) return NULL;
    Index *x = calloc(1, sizeof *x);
    uint32_t *map = calloc(saved->group_count + 1u, sizeof *map);
    int ok = x && map && saved->group_count == fresh->group_count;
    for (uint32_t g = 0; ok && g < saved->group_count; ++g) ok = find_group(fresh, saved->names + saved->group_names[g], map + g + 1u);
    for (uint32_t g = 0; ok && g < fresh->group_count; ++g) ok = add_group(x, fresh->names + fresh->group_names[g]);
    for (uint32_t i = 0; ok && i < fresh->entry_count; ++i)
        ok = add_entry(x, fresh->names + fresh->entries[i].name, 0, fresh->entries[i].size, (int)fresh->entries[i].directory);
    for (uint32_t i = 0; ok && i < saved->entry_count; ++i) {
        const Entry *e = saved->entries + i;
        if (e->group) ok = add_entry(x, saved->names + e->name, map[e->group], e->size, (int)e->directory);
    }
    char **derived = ok ? calloc(x->group_count ? x->group_count : 1u, sizeof *derived) : NULL;
    ok = ok && derived && hash_index(x);
    for (uint32_t g = 0; ok && g < x->group_count; ++g) derived[g] = derive_prefix(x, g + 1u);
    free(map);
    free_index(saved);
    if (!ok) { free(derived); free_index(x); saved_state = 2; return NULL; }
    prefixes = derived;
    return x;
}

static void build(void)
{
    uint64_t started = now_us();
    FS_Archive archive;
    Index *x = calloc(1, sizeof *x);
    int ok = x && R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")));
    if (!ok) {
        free_index(x);
        __atomic_store_n(&state, FAILED, __ATOMIC_SEQ_CST);
        __atomic_store_n(&full_finished, 1, __ATOMIC_SEQ_CST);
        return;
    }
    ok = list(x, archive, BOOT_DIR, 0, 0);
    /* No packs/ directory: boot/ alone is the whole index. */
    int grouped = ok && list(x, archive, PACKS_DIR, 0, 1);
    if (grouped && x->group_count) {
        Index *quick = make_provisional(x);
        if (quick) {
            saved_state = 1;
            provisional = quick;
            provisional_us = now_us() - started;
            __atomic_store_n(&state, PROVISIONAL, __ATOMIC_SEQ_CST);
        }
    }
    for (uint32_t g = 1; grouped && ok && g <= x->group_count; ++g) {
        yield_to_loads();
        ok = list_group(x, archive, x, g, g);
    }
    FSUSER_CloseArchive(archive);
    if (ok) ok = hash_index(x);
    if (!ok) {
        free_index(x);
        /* A provisional index keeps answering as it is. */
        if (state != PROVISIONAL) __atomic_store_n(&state, FAILED, __ATOMIC_SEQ_CST);
        __atomic_store_n(&full_finished, 1, __ATOMIC_SEQ_CST);
        return;
    }
    build_us = now_us() - started;
    full = x;
    /* The provisional index stays allocated: a lookup may still read it. */
    __atomic_store_n(&state, READY, __ATOMIC_SEQ_CST);
    __atomic_store_n(&full_finished, 1, __ATOMIC_SEQ_CST);
    if (!x->group_count) { remove(SAVED_PATH); return; }
    /* Rewrite the saved copy only when the listing changed. */
    Index *old = load_saved();
    int stale = !old || !same_listing(old, x);
    free_index(old);
    if (stale) { yield_to_loads(); save(x); }
}

static void build_thread(void *argument) { (void)argument; build(); }

static int claim(void)
{
    int expected = IDLE;
    return __atomic_compare_exchange_n(&state, &expected, BUILDING, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

void c3_boot_index_start(void)
{
    if (!stats_ready) { LightLock_Init(&stats_lock); LightLock_Init(&verify_lock); stats_ready = 1; }
    if (!claim()) return;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    /* Below the scene: it waits on the card, not the CPU. */
    if (!threadCreate(build_thread, NULL, 32 * 1024, priority < 0x3f ? priority + 1 : priority, -2, true)) build();
}

void c3_boot_index_busy(int loading)
{
    __atomic_add_fetch(&busy, loading ? 1 : -1, __ATOMIC_SEQ_CST);
}

/* The state once the full listing has finished (READY; FAILED, or
 * PROVISIONAL when only the full listing failed), building on this thread
 * when nobody started it. */
static int settled(void)
{
    if (claim()) {
        if (!stats_ready) { LightLock_Init(&stats_lock); LightLock_Init(&verify_lock); stats_ready = 1; }
        build();
    }
    __atomic_add_fetch(&waiters, 1, __ATOMIC_SEQ_CST);
    while (!__atomic_load_n(&full_finished, __ATOMIC_SEQ_CST)) svcSleepThread(1000000);
    __atomic_sub_fetch(&waiters, 1, __ATOMIC_SEQ_CST);
    return __atomic_load_n(&state, __ATOMIC_SEQ_CST);
}

/* The state once the provisional or the full index can answer (or the
 * listing failed). */
static int past_building(void)
{
    __atomic_add_fetch(&waiters, 1, __ATOMIC_SEQ_CST);
    int now;
    while ((now = __atomic_load_n(&state, __ATOMIC_SEQ_CST)) == BUILDING) svcSleepThread(1000000);
    __atomic_sub_fetch(&waiters, 1, __ATOMIC_SEQ_CST);
    return now;
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

/* Where a boot name is, answered from an index. */
typedef struct {
    int known;           /* 0: the provisional index cannot tell: wait for the full one */
    const Entry *entry;  /* NULL: missing */
    const Index *groups; /* group names for entry->group */
} Where;

static Where locate(const char *name, int now)
{
    Where w = {1, NULL, NULL};
    if (now == READY) { w.entry = find(full, name); w.groups = full; return w; }
    const Entry *e = find(provisional, name);
    w.groups = provisional;
    if (e && !e->group) { w.entry = e; return w; }
    uint32_t g = e ? e->group : candidate_group(name);
    const Index *fresh = g ? verify(g) : NULL;
    if (!fresh) { w.known = 0; return w; }
    w.entry = find(fresh, name);
    /* Gone from the group the saved copy named: moved, so ask the full list. */
    if (!w.entry && e) w.known = 0;
    return w;
}

static FILE *open_entry(Where w, const char *path, const char *mode, const char *name)
{
    if (!w.entry) { count(name, 0, 1); errno = ENOENT; return NULL; }
    if (!w.entry->group) return timed_fopen(path, mode, name);
    char grouped[192];
    snprintf(grouped, sizeof grouped, PACKS_PREFIX "%s/%s", w.groups->names + w.groups->group_names[w.entry->group - 1u],
             name);
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
        Where w = locate(name, READY);
        return w.entry && w.entry->group ? open_entry(w, path, mode, name) : (errno = ENOENT, NULL);
    }
    if (now == BUILDING) now = past_building();
    if (now == PROVISIONAL) {
        Where w = locate(name, now);
        if (w.known) return open_entry(w, path, mode, name);
        now = settled();
    }
    if (now != READY) return timed_fopen(path, mode, name);
    return open_entry(locate(name, READY), path, mode, name);
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
        return stat_entry(locate(name, READY).entry, st, name);
    }
    if (now == BUILDING) now = past_building();
    if (now == PROVISIONAL) {
        Where w = locate(name, now);
        if (w.known) return stat_entry(w.entry, st, name);
        now = settled();
    }
    if (now != READY) {
        uint64_t started = now_us();
        int result = __real_stat(path, st);
        count(name, now_us() - started, 0);
        return result;
    }
    return stat_entry(locate(name, READY).entry, st, name);
}

uint64_t c3_boot_index_size(const char *path)
{
    const char *name = boot_name(path);
    int now = __atomic_load_n(&state, __ATOMIC_SEQ_CST);
    if (!name || (now != READY && now != PROVISIONAL)) return 0;
    const Entry *e = NULL;
    if (now == READY) e = find(full, name);
    else {
        /* Never lists: a group already listed again, else the saved copy. */
        e = find(provisional, name);
        uint32_t g = e ? e->group : 0;
        const Index *fresh = g ? __atomic_load_n(&verified[g - 1u], __ATOMIC_SEQ_CST) : NULL;
        if (fresh) e = find(fresh, name);
    }
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
    if (!trace || now == traced_state || now == IDLE || now == BUILDING) return;
    traced_state = now;
    static const char *const names[] = {"idle", "building", "provisional", "ready", "failed"};
    static const char *const saved[] = {"none", "used", "stale"};
    const Index *x = now == READY ? full : provisional;
    fprintf(trace, "boot_index status=%s entries=%lu groups=%lu grouped_files=%lu us=%llu provisional_us=%llu saved=%s verified=%u\n",
            names[now], x ? (unsigned long)x->entry_count : 0ul, x ? (unsigned long)x->group_count : 0ul,
            x ? (unsigned long)x->packs_files : 0ul, (unsigned long long)build_us, (unsigned long long)provisional_us,
            saved[saved_state], verified_count);
    fflush(trace);
}
