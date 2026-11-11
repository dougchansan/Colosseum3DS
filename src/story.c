#include "colosseum3ds/story.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BANKS 8u
#define MAX_GROUPS 16u
#define MAX_BANK_BYTES (256u * 1024u)
#define MAX_PACK_BYTES (1024u * 1024u)
#define MAX_GLOBALS 1024u

typedef struct Bank {
    uint8_t *data;
    uint32_t size, global_offset, global_count, first_code;
    uint16_t id, function_count;
} Bank;
typedef struct MessageGroup { uint8_t *data; uint32_t size; } MessageGroup;
typedef struct Task {
    uint32_t stack[C3_STORY_STACK];
    uint32_t pc, opcode_pc, frame, count, result, script_id, result_slot;
    uint16_t key;
    uint8_t bank;
    C3StoryStatus status;
    C3StoryCall pending;
    char failure[C3_STORY_ERROR];
} Task;
typedef struct Impl {
    Bank banks[MAX_BANKS];
    MessageGroup groups[MAX_GROUPS];
    Task tasks[C3_STORY_TASKS];
    uint32_t bank_count, group_count, floor;
    uint16_t last_key;
} Impl;

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static void put_be32(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)(x >> 24); p[1] = (uint8_t)(x >> 16);
    p[2] = (uint8_t)(x >> 8); p[3] = (uint8_t)x;
}
static float word_float(uint32_t word) { float f; memcpy(&f, &word, 4); return f; }
static uint32_t float_word(float f) { uint32_t word; memcpy(&word, &f, 4); return word; }
static int fail(char *error, const char *format, ...) {
    if (error) { va_list args; va_start(args, format); vsnprintf(error, C3_STORY_ERROR, format, args); va_end(args); }
    return 0;
}
static int inside(uint32_t offset, uint32_t length, size_t size) {
    return offset <= size && length <= size - offset;
}

void c3_story_free(C3Story *story) {
    Impl *p; uint32_t i;
    if (!story || !(p = story->impl)) return;
    for (i = 0; i < p->bank_count; ++i) free(p->banks[i].data);
    for (i = 0; i < p->group_count; ++i) free(p->groups[i].data);
    free(p); story->impl = NULL;
}

int c3_story_add_bank(C3Story *story, const void *data, size_t size, char *error) {
    const uint8_t *bytes = data; Impl *p; Bank next = {0}; uint32_t i, first, reloc, reloc_count;
    if (!story || !data || size < 28 || size > MAX_BANK_BYTES) return fail(error, "GSVM bank size is invalid");
    next.id = be16(bytes); next.function_count = be16(bytes + 4);
    reloc_count = be16(bytes + 6); reloc = be32(bytes + 12);
    next.global_offset = be32(bytes + 16); first = (uint32_t)size;
    if (!next.id || !next.function_count || next.function_count > 2048 || bytes[10] ||
        !inside(24, 4u * next.function_count, size) || !inside(reloc, 4u * reloc_count, size) ||
        next.global_offset < 24u + 4u * next.function_count || next.global_offset % 4)
        return fail(error, "GSVM bank header is invalid");
    for (i = 0; i < next.function_count; ++i) {
        uint32_t at = be32(bytes + 24 + 4*i);
        /* Retail banks keep null slots (offset zero) for removed functions;
         * they are never valid entry points. */
        if (!at) continue;
        if (at < next.global_offset || at >= size) return fail(error, "GSVM function offset is invalid");
        if (at < first) first = at;
    }
    if ((first - next.global_offset) % 4 || (first - next.global_offset)/4 > MAX_GLOBALS)
        return fail(error, "GSVM global extent is invalid");
    for (i = 0; i < reloc_count; ++i) {
        uint32_t at = be32(bytes + reloc + 4*i);
        if (!inside(at, 4, size) || be32(bytes + at) >= size)
            return fail(error, "GSVM relocation is invalid");
    }
    p = story->impl;
    if (p) {
        if (p->bank_count >= MAX_BANKS) return fail(error, "GSVM bank limit reached");
        for (i = 0; i < p->bank_count; ++i)
            if (p->banks[i].id == next.id) return fail(error, "Duplicate GSVM bank %04x", next.id);
        for (i = 0; i < C3_STORY_TASKS; ++i)
            if (p->tasks[i].key && (p->tasks[i].status == C3_STORY_RUNNING ||
                                    p->tasks[i].status == C3_STORY_WAITING))
                return fail(error, "Cannot add a bank while scripts are active");
    }
    next.data = malloc(size);
    if (!next.data) return fail(error, "GSVM bank allocation failed");
    memcpy(next.data, bytes, size); next.size = (uint32_t)size;
    next.global_count = (first - next.global_offset)/4; next.first_code = first;
    if (!p) {
        p = calloc(1, sizeof(*p));
        if (!p) { free(next.data); return fail(error, "GSVM context allocation failed"); }
        story->impl = p;
    }
    p->banks[p->bank_count++] = next;
    if (error) error[0] = 0;
    return 1;
}

static int group_valid(const uint8_t *b, uint32_t size) {
    uint32_t i, count, previous = 0, start;
    if (size < 16) return 0;
    count = be16(b + 4); start = 16 + count*8;
    if (!count || count > 8192 || start > size) return 0;
    for (i = 0; i < count; ++i) {
        uint32_t key = be32(b + 16 + i*8), off = be32(b + 20 + i*8);
        if ((i && key <= previous) || key > 0xfffff || off < start || !inside(off, 2, size)) return 0;
        previous = key;
    }
    return 1;
}

int c3_story_decode(C3Story *story, const void *data, size_t size, char *error) {
    const uint8_t *b = data; C3Story next = {0}; Impl *p; uint32_t i, nb, ng, offset;
    if (!story || !data || size < 64 || size > MAX_PACK_BYTES || memcmp(b, "C3S1", 4) ||
        le32(b+4) != 1 || le32(b+8) != size) return fail(error, "Invalid C3S1 header");
    nb = le32(b+16); ng = le32(b+20);
    if (!nb || nb > MAX_BANKS || ng > MAX_GROUPS || le32(b+24) != 64 ||
        le32(b+28) != 64 + nb*16 || !inside(64, (nb+ng)*16, size)) return fail(error, "Invalid C3S1 directory");
    for (i = 32; i < 64; ++i) if (b[i]) return fail(error, "C3S1 reserved bytes are nonzero");
    offset = 64 + (nb+ng)*16;
    for (i = 0; i < nb+ng; ++i) {
        const uint8_t *row = b + 64 + i*16;
        uint32_t id = le32(row), at = le32(row+4), length = le32(row+8);
        if (le32(row+12) || at != offset || !inside(at, length, size)) { fail(error, "Invalid C3S1 payload extent"); goto bad; }
        offset += length;
        if (i < nb) {
            if (length < 2 || be16(b+at) != id) { fail(error, "C3S1 bank ID mismatch"); goto bad; }
            if (!c3_story_add_bank(&next, b+at, length, error)) goto bad;
        } else {
            MessageGroup *group;
            if (!group_valid(b+at, length) || id != be16(b+at)) { fail(error, "Invalid C3S1 message group"); goto bad; }
            p = next.impl; group = &p->groups[p->group_count]; group->data = malloc(length);
            if (!group->data) { fail(error, "Message group allocation failed"); goto bad; }
            memcpy(group->data, b+at, length); group->size = length; ++p->group_count;
        }
    }
    if (offset != size) { fail(error, "Trailing C3S1 bytes"); goto bad; }
    p = next.impl; p->floor = le32(b+12);
    c3_story_free(story); *story = next; if (error) error[0] = 0; return 1;
bad:
    c3_story_free(&next); return 0;
}

int c3_story_load(C3Story *story, const char *path, char *error) {
    FILE *f; long length; uint8_t *data; int ok;
    if (!path || !(f = fopen(path, "rb"))) return fail(error, "Cannot open story pack");
    if (fseek(f, 0, SEEK_END) || (length = ftell(f)) < 64 || length > (long)MAX_PACK_BYTES || fseek(f, 0, SEEK_SET)) {
        fclose(f); return fail(error, "Story pack length is invalid");
    }
    data = malloc((size_t)length);
    if (!data) { fclose(f); return fail(error, "Story pack allocation failed"); }
    ok = fread(data, 1, (size_t)length, f) == (size_t)length;
    if (fclose(f)) ok = 0;
    if (ok) ok = c3_story_decode(story, data, (size_t)length, error);
    else fail(error, "Cannot read complete story pack");
    free(data); return ok;
}

static Task *find_task(Impl *p, uint16_t key) {
    uint32_t i;
    if (!p || !key) return NULL;
    for (i = 0; i < C3_STORY_TASKS; ++i) if (p->tasks[i].key == key) return &p->tasks[i];
    return NULL;
}
int c3_story_start(C3Story *story, uint32_t script_id, const uint32_t *args,
                  uint32_t argc, uint16_t *key, char *error) {
    Impl *p = story ? story->impl : NULL; Task *t = NULL; uint32_t i, bank;
    uint16_t candidate;
    if (!p || !key || argc > C3_STORY_ARGS || (argc && !args)) return fail(error, "Invalid GSVM task arguments");
    for (bank = 0; bank < p->bank_count; ++bank) if (p->banks[bank].id == script_id >> 16) break;
    if (bank == p->bank_count || (script_id & 0xffff) >= p->banks[bank].function_count)
        return fail(error, "GSVM script %08x is not loaded", (unsigned)script_id);
    for (i = 0; i < C3_STORY_TASKS; ++i) {
        if (!p->tasks[i].key || p->tasks[i].status == C3_STORY_FINISHED || p->tasks[i].status == C3_STORY_FAILED) { t = &p->tasks[i]; break; }
    }
    if (!t) return fail(error, "GSVM task limit reached");
    candidate = p->last_key;
    do { ++candidate; if (!candidate) ++candidate; } while (find_task(p, candidate));
    p->last_key = candidate; memset(t, 0, sizeof(*t)); t->key = candidate;
    t->bank = (uint8_t)bank; t->script_id = script_id; t->status = C3_STORY_RUNNING;
    t->pc = be32(p->banks[bank].data + 24 + (script_id & 0xffff)*4);
    if (!t->pc) { memset(t, 0, sizeof(*t)); return fail(error, "GSVM script %08x is an original null slot", (unsigned)script_id); }
    t->count = argc + 4;
    for (i = 0; i < argc; ++i) t->stack[i+1] = args[i];
    t->stack[argc+3] = argc;
    *key = candidate; if (error) error[0] = 0; return 1;
}

static int task_fail(Task *t, uint32_t pc, const char *reason) {
    t->status = C3_STORY_FAILED;
    snprintf(t->failure, sizeof(t->failure), "GSVM %08x PC %08x: %s", (unsigned)t->script_id, (unsigned)pc, reason);
    return 0;
}
static int push(Task *t, uint32_t v) {
    if (t->count >= C3_STORY_STACK) return task_fail(t, t->opcode_pc, "stack overflow");
    t->stack[t->count++] = v; return 1;
}
static int pop(Task *t, uint32_t *v) {
    if (!t->count) return task_fail(t, t->opcode_pc, "stack underflow");
    *v = t->stack[--t->count]; return 1;
}
static int slot(Impl *p, Task *t, uint16_t desc, uint32_t index, uint32_t *address) {
    int64_t at = (int32_t)index;
    if (desc & 0x40) {
        at += t->frame;
        if (at < 0 || at >= C3_STORY_STACK) return task_fail(t, t->opcode_pc, "local index out of range");
        *address = 0xa0000000u | (uint32_t)t->key << 6 | (uint32_t)at;
    } else {
        if (at < 0 || at >= p->banks[t->bank].global_count) return task_fail(t, t->opcode_pc, "global index out of range");
        *address = 0xb0000000u | (uint32_t)t->bank << 16 | (uint32_t)at;
    }
    return 1;
}
int c3_story_read_value(const C3Story *story, uint32_t address, uint32_t *value) {
    Impl *p = story ? story->impl : NULL; Task *t; uint32_t b, i;
    if (!p || !value) return 0;
    if ((address & 0xf0000000u) == 0xa0000000u && !(address & 0x0fc00000u)) {
        t = find_task(p, (uint16_t)(address >> 6)); if (!t) return 0;
        *value = t->stack[address & 63]; return 1;
    }
    if ((address & 0xfff80000u) == 0xb0000000u) {
        b = (address >> 16) & 7; i = address & 0xffff;
        if (b >= p->bank_count || i >= p->banks[b].global_count) return 0;
        *value = be32(p->banks[b].data + p->banks[b].global_offset + i*4); return 1;
    }
    return 0;
}
int c3_story_write_value(C3Story *story, uint32_t address, uint32_t value) {
    Impl *p = story ? story->impl : NULL; Task *t; uint32_t b, i, old;
    if (!c3_story_read_value(story, address, &old)) return 0;
    if ((address & 0xf0000000u) == 0xa0000000u) {
        t = find_task(p, (uint16_t)(address >> 6)); t->stack[address & 63] = value;
    } else {
        b = (address >> 16) & 7; i = address & 0xffff;
        put_be32(p->banks[b].data + p->banks[b].global_offset + i*4, value);
    }
    return 1;
}
static int operand(Impl *p, Task *t, uint16_t desc, uint32_t *value) {
    uint32_t index, address; C3Story story = {p};
    if (!desc) return task_fail(t, t->opcode_pc, "undefined operand type");
    if (!pop(t, &index)) return 0;
    if (desc & 0x80) { *value = index; return 1; }
    if (!slot(p, t, desc, index, &address)) return 0;
    if (desc & 0x120) { *value = address; return 1; }
    return c3_story_read_value(&story, address, value);
}
static int cast_int(Task *t, uint32_t word, uint16_t desc, int32_t *out) {
    float f;
    if ((desc & 0x3f) == 2) { *out = (int32_t)word; return 1; }
    f = word_float(word);
    if (!isfinite(f) || (double)f < INT32_MIN || (double)f > INT32_MAX) return task_fail(t, t->opcode_pc, "float-to-int conversion out of range");
    *out = (int32_t)f; return 1;
}
static int binary(Task *t, uint8_t op, uint32_t l, uint16_t ld, uint32_t r, uint16_t rd, uint32_t *out) {
    int li = (ld & 0x3f) == 2, ri = (rd & 0x3f) == 2;
    int32_t ls = (int32_t)l, rs = (int32_t)r;
    float lf = li ? (float)ls : word_float(l), rf = ri ? (float)rs : word_float(r), f = 0;
    if ((!li && !isfinite(lf)) || (!ri && !isfinite(rf))) return task_fail(t, t->opcode_pc, "nonfinite float operand");
    if (op == 0x0d || op == 0x0e) {
        if (!cast_int(t,l,ld,&ls) || !cast_int(t,r,rd,&rs)) return 0;
        *out = op == 0x0d ? (uint32_t)ls & (uint32_t)rs : (uint32_t)ls | (uint32_t)rs; return 1;
    }
    if (op >= 0x19 || op == 0x21 || op == 0x22) {
        switch (op) {
        case 0x19: *out = li && ri ? ls < rs : lf < rf; break;
        case 0x1a: *out = li && ri ? ls <= rs : lf <= rf; break;
        case 0x1b: *out = li && ri ? ls > rs : lf > rf; break;
        case 0x1c: *out = li && ri ? ls >= rs : lf >= rf; break;
        case 0x1d: *out = li && ri ? ls == rs : lf == rf; break;
        case 0x1e: *out = li && ri ? ls != rs : lf != rf; break;
        case 0x21: *out = (li ? ls != 0 : lf != 0) && (ri ? rs != 0 : rf != 0); break;
        case 0x22: *out = (li ? ls != 0 : lf != 0) || (ri ? rs != 0 : rf != 0); break;
        default: return task_fail(t, t->opcode_pc, "invalid binary opcode");
        }
        return 1;
    }
    if (li && ri) {
        switch (op) {
        case 0x14: *out = l*r; break;
        case 0x15: *out = !rs ? 0 : ls == INT32_MIN && rs == -1 ? l : (uint32_t)(ls/rs); break;
        case 0x16: *out = !rs || (ls == INT32_MIN && rs == -1) ? 0 : (uint32_t)(ls%rs); break;
        case 0x17: *out = l+r; break;
        case 0x18: *out = l-r; break;
        default: return task_fail(t,t->opcode_pc,"invalid arithmetic opcode");
        }
        return 1;
    }
    switch (op) {
    case 0x14: f = lf*rf; break;
    case 0x15: f = r ? lf/rf : 0; break;
    /* Preserve fn_800F4818's raw signed-lhs / float-rhs behavior. */
    case 0x16: f = r ? (float)fmod((int32_t)l, word_float(r)) : 0; break;
    case 0x17: f = lf+rf; break;
    case 0x18: f = lf-rf; break;
    default: return task_fail(t,t->opcode_pc,"invalid arithmetic opcode");
    }
    if (!isfinite(f)) return task_fail(t,t->opcode_pc,"nonfinite arithmetic result");
    *out = float_word(f); return 1;
}

C3StoryStatus c3_story_step(C3Story *story, uint16_t key, uint32_t budget, C3StoryCall *call, char *error) {
    Impl *p = story ? story->impl : NULL; Task *t = find_task(p,key); Bank *b; uint32_t step;
    if (!t || !budget) { fail(error, "Invalid GSVM step"); return C3_STORY_FAILED; }
    b = &p->banks[t->bank];
    for (step = 0; step < budget && t->status == C3_STORY_RUNNING; ++step) {
        uint32_t pc = t->pc, a=0, v=0, x=0, y=0, n=0, address=0; uint16_t d=0,e=0; uint8_t op;
        if (!inside(pc,1,b->size) || pc < b->first_code) { task_fail(t,pc,"instruction address out of range"); break; }
        t->opcode_pc=pc;op = b->data[t->pc++];
        switch (op) {
        case 0: break;
        case 1: t->result = t->count ? t->stack[t->count-1] : 0; t->status=C3_STORY_FINISHED; break;
        case 2: push(t,UINT32_MAX); break;
        case 3:
            if (!inside(t->pc,3,b->size)) goto truncated;
            n=be16(b->data+t->pc+1);t->pc+=3;
            if (n>t->count) task_fail(t,pc,"drop underflow"); else t->count-=n;
            break;
        case 4:
            if (!inside(t->pc,8,b->size)) goto truncated;
            a=be32(b->data+t->pc);n=be16(b->data+t->pc+4);v=be16(b->data+t->pc+6);t->pc+=8;
            if (a>=b->function_count || n+v>t->count || t->count+3>C3_STORY_STACK) { task_fail(t,pc,"invalid function call frame");break; }
            push(t,t->pc);push(t,t->frame);push(t,v);t->frame=t->count-(n+v+3);
            t->pc=be32(b->data+24+a*4);break;
        case 5:
            if (!pop(t,&n)||!pop(t,&v)||!pop(t,&a)) break;
            if (n>t->count || v>=C3_STORY_STACK || (a && (a<b->first_code||a>=b->size))) {task_fail(t,pc,"invalid return frame");break;}
            t->frame=v;t->count-=n;t->pc=a;
            if (!a) {t->status=C3_STORY_FINISHED;t->result=t->count?t->stack[t->count-1]:0;}
            break;
        case 6: case 7:
            if (!inside(t->pc,4,b->size)) goto truncated;
            a=be32(b->data+t->pc);t->pc+=4;
            if (op==7 && !pop(t,&v)) break;
            if (op==6 || !v) {
                if (a<b->first_code || a>=b->size) task_fail(t,pc,"branch target out of range");else t->pc=a;
            }
            break;
        case 8:
            if (!inside(t->pc,5,b->size)) goto truncated;
            v=be32(b->data+t->pc+1);t->pc+=5;push(t,v);break;
        case 0x0f: case 0x11: case 0x12: case 0x13:
            if (!inside(t->pc,1,b->size)) goto truncated;
            d=b->data[t->pc++];
            if (!operand(p,t,op==0x0f?d|0x100:d,&v)) break;
            if (op==0x11) v=(d&0x3f)==2 ? !v : float_word(!word_float(v));
            if (op==0x12) v=(d&0x3f)==2 ? 0u-v : float_word(-word_float(v));
            push(t,v);break;
        case 0x10:
            if (!inside(t->pc,3,b->size)) goto truncated;
            d=b->data[t->pc];n=be16(b->data+t->pc+1);t->pc+=3;
            if (!pop(t,&a)) break;
            if (n>C3_STORY_STACK-t->count) {task_fail(t,pc,"struct push overflow");break;}
            for (x=0;x<n && t->status==C3_STORY_RUNNING;++x)
                if (push(t,a+x)&&operand(p,t,d,&v)) push(t,v);
            break;
        case 0x1f:
            if (!inside(t->pc,2,b->size)) goto truncated;
            d=b->data[t->pc++];e=b->data[t->pc++];
            if (!e || e&0x80) {task_fail(t,pc,"invalid assignment destination");break;}
            if (!operand(p,t,d,&v)||!pop(t,&a)||!slot(p,t,e,a,&address)) break;
            if ((d&0x3f)!=(e&0x3f)) {
                int32_t iv;
                if ((d&0x3f)==2) v=float_word((float)(int32_t)v);
                else if (cast_int(t,v,d,&iv)) v=(uint32_t)iv;else break;
            }
            if (!c3_story_write_value(story,address,v)) task_fail(t,pc,"invalid assignment address");else push(t,v);
            break;
        case 0x20: {
            uint32_t source, dest, source_stack=0;
            if (!inside(t->pc,4,b->size)) goto truncated;
            d=b->data[t->pc++];e=b->data[t->pc++];n=be16(b->data+t->pc);t->pc+=2;
            if (!n||n>C3_STORY_STACK||!e||(e&0x80)) {task_fail(t,pc,"invalid struct assignment");break;}
            if (d&0x80) {
                if(n>t->count){task_fail(t,pc,"struct source underflow");break;}
                t->count-=n;source=t->count;source_stack=1;
            } else {
                if(!pop(t,&source))break;
                for(x=0;x<n;++x)if(!slot(p,t,d,source+x,&address))break;
                if(x!=n)break;
            }
            if(!pop(t,&dest)||n>C3_STORY_STACK-t->count) {task_fail(t,pc,"struct destination overflow");break;}
            /* Validate every destination before the first write. */
            for(x=0;x<n;++x)if(!slot(p,t,e,dest+x,&address))break;
            if(x!=n)break;
            /* The original copies forward, including overlapping variable
             * blocks. Read each source after preceding writes, then push the
             * completed destination block (fn_800F2264). */
            for(x=0;x<n;++x){
                if(source_stack)v=t->stack[source+x];
                else{slot(p,t,d,source+x,&address);c3_story_read_value(story,address,&v);}
                slot(p,t,e,dest+x,&address);c3_story_write_value(story,address,v);
            }
            for(x=0;x<n;++x){slot(p,t,e,dest+x,&address);c3_story_read_value(story,address,&v);push(t,v);}
            break;
        }
        case 0x23: case 0x24: case 0x25:
            if (!inside(t->pc,4,b->size)) goto truncated;
            n=be16(b->data+t->pc+2);t->pc+=4;
            if(n>t->count||n>(op==0x25?C3_STORY_ARGS+1:C3_STORY_ARGS)||(op==0x25&&(!n||n==t->count))) {task_fail(t,pc,"invalid native call frame");break;}
            memset(&t->pending,0,sizeof(t->pending));t->pending.script_id=t->script_id;t->pending.pc=pc;
            t->pending.kind=op==0x25?C3_STORY_NATIVE:op==0x24?C3_STORY_WAIT_FRAMES:C3_STORY_PRINT;
            t->pending.native=op==0x25?t->stack[t->count-n]:0;
            t->pending.argc=op==0x25?n-1:n;
            for(x=0;x<t->pending.argc;++x)t->pending.args[x]=t->stack[t->count-n+x+(op==0x25)];
            if(op==0x25&&t->pending.native>=0xf7){task_fail(t,pc,"native index exceeds pinned table");break;}
            t->count-=n;t->result_slot=op==0x25?t->count-1:UINT32_MAX;t->status=C3_STORY_WAITING;break;
        default:
            if(op==0x0d||op==0x0e||(op>=0x14&&op<=0x1e)||op==0x21||op==0x22){
                if(!inside(t->pc,2,b->size))goto truncated;
                d=b->data[t->pc++];e=b->data[t->pc++];
                if(operand(p,t,d,&x)&&operand(p,t,e,&y)&&binary(t,op,y,e,x,d,&v))push(t,v);
            }else task_fail(t,pc,"unsupported opcode");
            break;
        }
        continue;
truncated:
        task_fail(t,pc,"truncated instruction");
    }
    if(t->status==C3_STORY_WAITING&&call)*call=t->pending;
    if(error){if(t->status==C3_STORY_FAILED)snprintf(error,C3_STORY_ERROR,"%s",t->failure);else error[0]=0;}
    return t->status;
}
int c3_story_resume(C3Story *story,uint16_t key,uint32_t result){
    Task*t=find_task(story?story->impl:NULL,key);
    if(!t||t->status!=C3_STORY_WAITING)return 0;
    if(t->result_slot!=UINT32_MAX)t->stack[t->result_slot]=result;
    t->status=C3_STORY_RUNNING;return 1;
}
int c3_story_stop(C3Story *story,uint16_t key){
    Task*t=find_task(story?story->impl:NULL,key);if(!t)return 0;t->status=C3_STORY_FINISHED;t->result=0;return 1;
}
int c3_story_active(const C3Story *story,uint16_t key){
    Task*t=find_task(story?story->impl:NULL,key);return t&&(t->status==C3_STORY_RUNNING||t->status==C3_STORY_WAITING);
}
uint32_t c3_story_result(const C3Story *story,uint16_t key){
    Task*t=find_task(story?story->impl:NULL,key);return t?t->result:0;
}
uint32_t c3_story_floor(const C3Story *story){Impl*p=story?story->impl:NULL;return p?p->floor:0;}

static int append(char *out,size_t capacity,size_t *used,const char*text){
    size_t n=strlen(text);if(n>=capacity-*used)return 0;memcpy(out+*used,text,n);*used+=n;out[*used]=0;return 1;
}
typedef struct { const char *hero, *partner, *speaker; } Names;
static const char *names_source(void *context,uint8_t control){
    const Names *n=context;
    return control==0x2b?(n->hero?n->hero:""):control==0x2c?(n->partner?n->partner:""):
           control==0x59?(n->speaker?n->speaker:""):NULL;
}
int c3_story_message(const C3Story *story,uint32_t id,const char*hero,const char*partner,const char*speaker,
                    char*out,size_t capacity,char*error){
    Names names={hero,partner,speaker};
    return c3_story_message_with(story,id,names_source,&names,out,capacity,error);
}
int c3_story_message_with(const C3Story *story,uint32_t id,C3StoryTextSource source,void *context,
                    char*out,size_t capacity,char*error){
    Impl*p=story?story->impl:NULL;const uint8_t*data=NULL;uint32_t length=0,i,j;size_t used=0;int ruby=0;
    if(!p||!id||!out||!capacity)return fail(error,"Invalid story message request");
    out[0]=0;
    for(i=0;i<p->group_count&&!data;++i){
        MessageGroup*g=&p->groups[i];uint32_t count=be16(g->data+4),start=0,end=g->size;
        if(be16(g->data)!=(id>>20))continue;
        for(j=0;j<count;++j)if(be32(g->data+16+j*8)==(id&0xfffff))start=be32(g->data+20+j*8);
        if(!start)continue;
        for(j=0;j<count;++j){uint32_t off=be32(g->data+20+j*8);if(off>start&&off<end)end=off;}
        data=g->data+start;length=end-start;
    }
    if(!data)return fail(error,"Original message %08x is not loaded",(unsigned)id);
    for(i=0;i+2<=length;){
        uint16_t glyph=be16(data+i);char encoded[5]={0};const char*text=encoded;i+=2;
        if(!glyph){if(error)error[0]=0;return 1;}
        if(glyph==0xffff){
            uint8_t control;if(i>=length)return fail(error,"Truncated message control");control=data[i++];
            switch(control){
            case 0:text="\n";break;
            case 2:break; /* final acknowledgement is owned by the scene */
            case 3:text="\f";break;
            case 4:ruby=0;break;
            case 5:ruby=1;break;
            case 6:ruby=0;break;
            case 7:if(i>=length)return fail(error,"Truncated font control");++i;break;
            case 0x3d:break; /* message sound disabled; no text payload */
            default:
                text=source?source(context,control):NULL;
                if(!text)return fail(error,"Message %08x uses unsupported control %02x",(unsigned)id,control);
                break;
            }
        }else if(!ruby){
            if(glyph==0x2031)text="???"; /* original unknown-name glyph */
            else if(glyph==0xe9)text="e"; /* libctru console font */
            else if(glyph<0x80){if(glyph<32)return fail(error,"Unexpected text control glyph");encoded[0]=(char)glyph;}
            else if(glyph<0x800){encoded[0]=(char)(0xc0|(glyph>>6));encoded[1]=(char)(0x80|(glyph&63));}
            else if(glyph<0xd800||glyph>0xdfff){encoded[0]=(char)(0xe0|(glyph>>12));encoded[1]=(char)(0x80|((glyph>>6)&63));encoded[2]=(char)(0x80|(glyph&63));}
            else return fail(error,"Unsupported surrogate GSchar");
        }
        if(!append(out,capacity,&used,text))return fail(error,"Story message output is too small");
    }
    return fail(error,"Original message is unterminated");
}
