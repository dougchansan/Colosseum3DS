#include "colosseum3ds/wzx_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW 16u
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static int fail(char *e, size_t n, const char *s) { if (e && n) snprintf(e, n, "%s", s); return 0; }

int c3_wzx_index_decode(C3WzxIndex *x, const void *data, size_t size, char *error, size_t cap)
{
    const uint8_t *b = data;
    if (!x || !b || size < 32 || memcmp(b, "C3WI", 4) || le32(b + 4) != 1) return fail(error, cap, "Invalid effect index header.");
    C3WzxIndex v; memset(&v, 0, sizeof v);
    v.rows = le32(b + 8); v.rows_at = le32(b + 12); v.names_at = le32(b + 16);
    if (v.rows > 65536 || v.rows_at < 32 || v.rows_at > size || (size - v.rows_at) / ROW < v.rows || v.names_at > size)
        return fail(error, cap, "Effect index tables out of bounds.");
    for (uint32_t i = 0; i < v.rows; ++i) {
        uint64_t name = (uint64_t)v.names_at + le32(b + v.rows_at + ROW * i + 8); /* relative to names_at */
        if (name >= size || !memchr(b + name, 0, size - name)) return fail(error, cap, "Effect index name out of bounds.");
        const char *s = (const char *)b + name;
        if (!s[0] || strchr(s, '/') || strchr(s, '\\') || strstr(s, "..")) return fail(error, cap, "Unsafe effect pack name.");
    }
    v.bytes = (uint8_t *)data; v.size = size; *x = v;
    return 1;
}

int c3_wzx_index_load(C3WzxIndex *x, const char *path, char *error, size_t cap)
{
    if (!x || !path) return fail(error, cap, "Invalid effect index arguments.");
    memset(x, 0, sizeof *x);
    FILE *f = fopen(path, "rb"); if (!f) return fail(error, cap, "No effect index.");
    uint8_t *data = NULL; long n = -1;
    if (!fseek(f, 0, SEEK_END)) n = ftell(f);
    if (n > 0 && n <= 4 * 1024 * 1024 && !fseek(f, 0, SEEK_SET) && (data = malloc((size_t)n)) && fread(data, 1, (size_t)n, f) == (size_t)n) {
        fclose(f);
        if (c3_wzx_index_decode(x, data, (size_t)n, error, cap)) return 1;
        free(data); return 0;
    }
    fclose(f); free(data);
    return fail(error, cap, "Unreadable effect index.");
}

void c3_wzx_index_free(C3WzxIndex *x) { if (x) { free(x->bytes); memset(x, 0, sizeof *x); } }

static const char *find(const C3WzxIndex *x, unsigned category, unsigned kind, unsigned id, unsigned pkx, unsigned variant, int any_pkx)
{
    if (!x || !x->bytes) return NULL;
    for (uint32_t i = 0; i < x->rows; ++i) {
        const uint8_t *r = x->bytes + x->rows_at + ROW * i;
        if (r[0] != category || le16(r + 2) != id || (kind && r[1] != kind) || le16(r + 6) != variant) continue;
        if (!any_pkx && le16(r + 4) != pkx) continue;
        return (const char *)x->bytes + x->names_at + le32(r + 8);
    }
    return NULL;
}

const char *c3_wzx_index_move(const C3WzxIndex *x, uint16_t move, unsigned kind, uint16_t pkx, uint16_t variant)
{
    const char *s = NULL;
    if (variant) s = find(x, C3_WZX_CAT_VARIANT, kind, move, 0xFFFF, variant, 1);
    if (!s && pkx != 0xFFFF) s = find(x, C3_WZX_CAT_OVERRIDE, kind, move, pkx, 0, 0);
    if (!s) s = find(x, C3_WZX_CAT_MOVE, kind, move, 0xFFFF, 0, 1);
    return s;
}

const char *c3_wzx_index_special(const C3WzxIndex *x, uint16_t special)
{
    return find(x, C3_WZX_CAT_SPECIAL, 0, special, 0xFFFF, 0, 1);
}
