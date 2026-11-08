/* Portable control lowering of pinned 51bfe431 fight_action_range_801F0898.c.
 * Source pins and the executable source oracle are maintained by
 * tools/prepare_battle_action.py. No combat effects or GSVM continuation. */
#include "colosseum3ds/battle_action.h"

_Static_assert(sizeof(C3BattleAction) == 0x30, "original action record size");

uint16_t c3_battle_action_kind(const C3BattleAction *a)
{ return a ? (uint16_t)((uint16_t)a->bytes[0] << 8 | a->bytes[1]) : 0; }

void c3_battle_action_set_kind(C3BattleAction *a, uint16_t kind)
{ if (a) { a->bytes[0] = (uint8_t)(kind >> 8); a->bytes[1] = (uint8_t)kind; } }

uint32_t c3_battle_action_word(const C3BattleAction *a, unsigned off)
{
    if (!a || off < 4 || off > 44 || off % 4) return 0;
    return (uint32_t)a->bytes[off] << 24 | (uint32_t)a->bytes[off+1] << 16 |
           (uint32_t)a->bytes[off+2] << 8 | a->bytes[off+3];
}

void c3_battle_action_set_word(C3BattleAction *a, unsigned off, uint32_t value)
{
    if (!a || off < 4 || off > 44 || off % 4) return;
    a->bytes[off] = (uint8_t)(value >> 24); a->bytes[off+1] = (uint8_t)(value >> 16);
    a->bytes[off+2] = (uint8_t)(value >> 8); a->bytes[off+3] = (uint8_t)value;
}

void c3_battle_action_init(C3BattleAction *a)
{
    if (!a) return;
    c3_battle_action_set_kind(a, 0);
    for (unsigned off = 4; off < C3_ACTION_BYTES; off += 4)
        c3_battle_action_set_word(a, off, off == C3_ACTION_FIFO_INDEX ? UINT32_MAX : 0);
}

void c3_battle_action_queue_init(C3BattleActionQueue *q)
{
    if (!q) return;
    q->write_index = q->read_index = 0;
    for (unsigned i = 0; i < C3_ACTION_SLOTS; ++i) c3_battle_action_init(&q->slots[i]);
}

static int valid_queue(const C3BattleActionQueue *q)
{ return q && q->write_index < C3_ACTION_SLOTS && q->read_index < C3_ACTION_SLOTS; }

static int overlaps_queue(const C3BattleActionQueue *q, const C3BattleAction *a)
{
    uintptr_t p = (uintptr_t)a, base = (uintptr_t)q;
    return p >= base ? p-base < sizeof *q : base-p < sizeof *a;
}

size_t c3_battle_action_queue_count(const C3BattleActionQueue *q)
{ return valid_queue(q) ? (q->write_index + C3_ACTION_SLOTS - q->read_index) % C3_ACTION_SLOTS : 0; }

static C3BattleActionResult result(C3BattleActionStatus status, uint32_t value, size_t count)
{ C3BattleActionResult r = {status, value, count}; return r; }

static int valid_runtime(const C3BattleActionRuntime *r)
{
    return r && (!r->data_count || r->data) && (!r->kind_count || r->kinds) &&
           (!r->binding_count || r->bindings);
}

/* Distinct status prevents unknown addresses from becoming a successful
 * no-op. Known kind + callback ref zero is the source's legitimate NULL. */
static C3BattleActionStatus resolve(const C3BattleActionRuntime *r,
    const C3BattleAction *a, int display, const C3BattleActionBinding **binding)
{
    uint32_t ref = c3_battle_action_word(a, C3_ACTION_DATA);
    const C3BattleActionData *data = NULL;
    const C3BattleActionKind *kind = NULL;
    *binding = NULL;
    for (size_t i = 0; i < r->data_count; ++i)
        if (r->data[i].ref == ref && ref) {
            if (data) return C3_ACTION_AMBIGUOUS_REFERENCE;
            data = &r->data[i];
        }
    if (!data) return C3_ACTION_UNKNOWN_DATA;
    for (size_t i = 0; i < r->kind_count; ++i)
        if (r->kinds[i].kind == data->kind) {
            if (kind) return C3_ACTION_AMBIGUOUS_REFERENCE;
            kind = &r->kinds[i];
        }
    if (!kind) return C3_ACTION_UNKNOWN_KIND;
    ref = display ? kind->display_ref : kind->flow_ref;
    if (!ref) return C3_ACTION_OK;
    for (size_t i = 0; i < r->binding_count; ++i)
        if (r->bindings[i].ref == ref) {
            if (*binding) return C3_ACTION_AMBIGUOUS_REFERENCE;
            *binding = &r->bindings[i];
        }
    return *binding && (*binding)->call ? C3_ACTION_OK : C3_ACTION_UNIMPLEMENTED;
}

static int valid_action(const C3BattleAction *a)
{ return a && c3_battle_action_kind(a) && c3_battle_action_word(a, C3_ACTION_DATA); }

C3BattleActionResult c3_battle_action_flow(C3BattleActionQueue *q,
    C3BattleAction *a, const C3BattleActionRuntime *r)
{
    const C3BattleActionBinding *binding = NULL;
    C3BattleActionStatus status;
    uint32_t value = 0, index;
    if (!valid_queue(q) || !a || overlaps_queue(q, a) || !valid_runtime(r))
        return result(C3_ACTION_BAD_ARGUMENT, 0, 0);
    /* Source full-FIFO check precedes callback evaluation. */
    index = q->write_index;
    if ((index + 1) % C3_ACTION_SLOTS == q->read_index) return result(C3_ACTION_OK, 2, 0);
    if (valid_action(a)) {
        status = resolve(r, a, 0, &binding);
        if (status != C3_ACTION_OK) return result(status, 0, 0);
    }
    q->slots[index] = *a;
    q->write_index = (index + 1) % C3_ACTION_SLOTS;
    c3_battle_action_set_word(a, C3_ACTION_FIFO_INDEX, index);
    c3_battle_action_set_word(&q->slots[index], C3_ACTION_FIFO_INDEX, index);
    if (valid_action(a)) value = binding ? binding->call(binding->user, a) : 1;
    if ((uint8_t)value == 1) q->slots[index] = *a;
    else if (q->write_index != q->read_index)
        q->read_index = (q->read_index + 1) % C3_ACTION_SLOTS;
    return result(C3_ACTION_OK, value, binding ? 1 : 0);
}

C3BattleActionResult c3_battle_action_create_flow(C3BattleActionQueue *q,
    C3BattleAction *a, uint32_t moto, uint32_t actor, uint16_t kind,
    uint32_t buff, const C3BattleActionRuntime *r)
{
    const C3BattleActionData *found = NULL;
    C3BattleAction candidate;
    C3BattleActionResult out;
    if (!valid_queue(q) || !a || overlaps_queue(q, a) || !valid_runtime(r))
        return result(C3_ACTION_BAD_ARGUMENT, 0, 0);
    /* The pointer table is bounded here; the source walks to a kind-zero sentinel. */
    if (!r->data_count || r->data[r->data_count-1].kind != 0)
        return result(C3_ACTION_BAD_ARGUMENT, 0, 0);
    candidate = *a;
    c3_battle_action_init(&candidate);
    for (size_t i = 0; i < r->data_count && r->data[i].kind; ++i)
        if (kind && kind == r->data[i].kind && buff == r->data[i].buff) {
            found = &r->data[i]; break;
        }
    if (!found || !found->ref) { *a = candidate; return result(C3_ACTION_OK, 4, 0); }
    c3_battle_action_set_kind(&candidate, kind);
    c3_battle_action_set_word(&candidate, C3_ACTION_BUFF, buff);
    c3_battle_action_set_word(&candidate, C3_ACTION_DATA, found->ref);
    c3_battle_action_set_word(&candidate, C3_ACTION_ACTOR, actor);
    c3_battle_action_set_word(&candidate, C3_ACTION_BUFF_ID, buff);
    c3_battle_action_set_word(&candidate, C3_ACTION_MOTO, moto);
    out = c3_battle_action_flow(q, &candidate, r);
    if (out.status == C3_ACTION_OK) *a = candidate;
    return out;
}

C3BattleActionResult c3_battle_action_display(C3BattleActionQueue *q,
    const C3BattleActionRuntime *r, size_t budget)
{
    size_t count = 0;
    uint32_t value = 3;
    if (!valid_queue(q) || !valid_runtime(r)) return result(C3_ACTION_BAD_ARGUMENT, 0, 0);
    while (q->write_index != q->read_index) {
        const C3BattleActionBinding *binding = NULL;
        C3BattleAction action = q->slots[q->read_index];
        C3BattleActionStatus status;
        if (count == budget) return result(C3_ACTION_BUDGET, value, count);
        if (valid_action(&action)) {
            status = resolve(r, &action, 1, &binding);
            if (status != C3_ACTION_OK) return result(status, 0, count);
        }
        q->read_index = (q->read_index + 1) % C3_ACTION_SLOTS;
        value = valid_action(&action) ? (binding ? binding->call(binding->user, &action) : 1) : 0;
        ++count;
        if ((uint8_t)value != 1) return result(C3_ACTION_OK, value, count);
    }
    return result(C3_ACTION_OK, 3, count);
}
