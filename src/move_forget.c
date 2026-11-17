#include "colosseum3ds/move_forget.h"
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] << 8 | p[1]); }

void c3_move_forget_begin(C3MoveForget *f, const C3BattlePack *pack,
                          const uint8_t record[C3_GB_POKEMON_SIZE], uint16_t move)
{
    if (!f) return;
    memset(f, 0, sizeof *f);
    f->step = C3_MF_ASK_FORGET; f->move = move; f->slot = -1;
    for (unsigned k = 0; k < 4 && record; ++k) {
        f->known[k] = be16(record + 0x78 + 4 * k);
        f->hm[k] = (uint8_t)c3_battle_pack_move_is_hm(pack, f->known[k]);
    }
}

C3MoveForgetStep c3_move_forget_answer(C3MoveForget *f, int answer)
{
    if (!f) return C3_MF_DONE;
    switch (f->step) {
    case C3_MF_ASK_FORGET: f->step = answer == 1 ? C3_MF_CHOOSE : C3_MF_ASK_GIVE_UP; break;
    case C3_MF_CHOOSE:
        f->hm_refused = 0;
        if (answer >= 0 && answer < 4 && f->known[answer]) {
            if (f->hm[answer]) { f->hm_refused = 1; break; }
            f->slot = answer; f->forgotten = f->known[answer]; f->step = C3_MF_FORGOT;
        } else if (answer == 4) f->step = C3_MF_ASK_GIVE_UP;
        break;
    case C3_MF_ASK_GIVE_UP: f->step = answer == 1 ? C3_MF_DID_NOT_LEARN : C3_MF_ASK_FORGET; break;
    case C3_MF_FORGOT: f->step = C3_MF_LEARNED; break;
    case C3_MF_LEARNED: f->step = C3_MF_DONE; break;
    case C3_MF_DID_NOT_LEARN: f->slot = -1; f->step = C3_MF_DONE; break;
    case C3_MF_DONE: break;
    }
    return f->step;
}

uint32_t c3_move_forget_message(const C3MoveForget *f, uint16_t *move)
{
    uint16_t m = f ? f->move : 0; uint32_t id = 0;
    if (f) switch (f->step) {
    case C3_MF_ASK_FORGET: id = C3_MF_MSG_ASK_FORGET; break;
    case C3_MF_ASK_GIVE_UP: id = C3_MF_MSG_ASK_GIVE_UP; break;
    case C3_MF_FORGOT: id = C3_MF_MSG_FORGOT; m = f->forgotten; break;
    case C3_MF_LEARNED: id = C3_MF_MSG_LEARNED; break;
    case C3_MF_DID_NOT_LEARN: id = C3_MF_MSG_DID_NOT_LEARN; break;
    default: break;
    }
    if (move) *move = m;
    return id;
}

static int append(char *out, size_t cap, size_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n + 1 > cap) return 0;
    memcpy(out + *used, text, n); *used += n; out[*used] = 0;
    return 1;
}

static int render(C3MoveForgetLookup lookup, void *context, uint32_t id, const char *nickname,
                  const char *move, char *out, size_t cap, size_t *used, int *sound)
{
    size_t size, pos = 0; const uint8_t *d = lookup(context, id, &size);
    if (!d) return 0;
    while (pos + 2 <= size) {
        unsigned code = be16(d + pos); char encoded[2] = {0, 0}; const char *text = encoded;
        pos += 2;
        if (!code) return 1;
        if (code == 0xffff) {
            if (pos >= size) return 0;
            unsigned control = d[pos++];
            switch (control) {
            case 0x00: text = "\n"; break;
            case 0x02: return 1;
            case 0x03: text = "\n\n"; break;
            case 0x07: case 0x09: if (pos >= size) return 0; ++pos; text = ""; break;
            case 0x08: if (pos + 4 > size) return 0; pos += 4; text = ""; break;
            case 0x32: text = nickname ? nickname : ""; break;
            case 0x39: text = move ? move : ""; break;
            case 0x5D: if (sound) *sound = 1; text = ""; break;
            case 0x5E: text = ""; break;
            default: return 0;
            }
        } else if (code >= 32 && code <= 126) encoded[0] = (char)code;
        else switch (code) {
            case 0xe9: text = "e"; break;
            case 0xc9: text = "E"; break;
            case 0x3000: text = " "; break;
            case 0x2026: text = "..."; break;
            case 0x2018: case 0x2019: text = "'"; break;
            case 0x201c: case 0x201d: text = "\""; break;
            case 0xff0d: text = "-"; break;
            case 0x2640: text = "F"; break;
            case 0x2642: text = "M"; break;
            default: return 0;
        }
        if (!append(out, cap, used, text)) return 0;
    }
    return 1;
}

int c3_move_forget_text(C3MoveForgetLookup lookup, void *context, uint32_t id,
                        const char *nickname, const char *move, char *out, size_t capacity, int *sound)
{
    size_t used = 0;
    if (sound) *sound = 0;
    if (!lookup || !out || !capacity) return 0;
    out[0] = 0;
    if (!render(lookup, context, id, nickname, move, out, capacity, &used, sound)) { out[0] = 0; return 0; }
    return 1;
}
