#include "colosseum3ds/battle_general_view.h"
#include <stdio.h>
#include <string.h>

static int is_recall(const C3GeneralBattle *b, uint32_t i)
{
    /* The engine logs a voluntary switch as "come back" then "Go" for the
     * same position; only the second brings a Pokemon in. */
    return b->events[i].kind == C3_GB_EVENT_SWITCH && i + 1 < b->event_count &&
           b->events[i + 1].kind == C3_GB_EVENT_SWITCH && b->events[i + 1].actor == b->events[i].actor;
}
static int is_send_out(const C3GeneralBattle *b, uint32_t i)
{
    return b->events[i].kind == C3_GB_EVENT_SWITCH && b->events[i].actor < 4 && !is_recall(b, i);
}

void c3_general_view_restart(C3GeneralView *v, const C3GeneralBattle *b)
{
    if (!v || !b) return;
    v->next = 0;
    if (!v->introduced && b->event_count) memcpy(v->hp, b->events[0].hp, sizeof v->hp);
}

int c3_general_view_send_out(const C3GeneralBattle *b, uint32_t index, unsigned *position, uint16_t *species)
{
    if (!b || index >= b->event_count || !is_send_out(b, index)) return 0;
    if (position) *position = b->events[index].actor;
    if (species) *species = b->events[index].species;
    return 1;
}

unsigned c3_general_view_move_role(const C3BattlePack *pack, uint16_t move)
{
    C3GeneralMoveInfo info;
    if (!pack || !move || !c3_battle_pack_move(pack, move, &info)) return 0;
    if (info.power <= 0) return 2;
    return info.type < 9 ? 1 : 2;
}

uint32_t c3_general_view_batch(C3GeneralView *v, const C3GeneralBattle *b,
                               C3BattlePreviewEvent *out, uint32_t capacity, uint32_t *first)
{
    if (!v || !b || !out) return 0;
    if (capacity > C3_PREVIEW_EVENT_LIMIT) capacity = C3_PREVIEW_EVENT_LIMIT;
    if (first) *first = v->next;
    uint32_t n = 0;
    while (n < capacity && v->next < b->event_count) {
        uint32_t i = v->next;
        const C3GeneralEvent *g = &b->events[i];
        if (n && is_send_out(b, i)) break;
        C3BattlePreviewEvent *e = &out[n];
        memset(e, 0, sizeof *e);
        e->sequence = ++v->sequence;
        e->actor = g->actor < 4 ? g->actor : C3_PREVIEW_NO_SLOT;
        e->target = g->target < 4 ? g->target : C3_PREVIEW_NO_SLOT;
        memcpy(e->hp_before, v->hp, sizeof e->hp_before);
        memcpy(e->hp, v->hp, sizeof e->hp);
        snprintf(e->text, sizeof e->text, "%s", g->text);
        switch (g->kind) {
        case C3_GB_EVENT_MOVE:
            e->kind = C3_PREVIEW_EVENT_MOVE; e->move = g->move;
            v->group = v->sequence; v->last_move = g->move; v->last_actor = e->actor;
            v->last_move_group = v->group;
            break;
        case C3_GB_EVENT_HP:
            e->kind = C3_PREVIEW_EVENT_HP;
            e->target = e->actor;
            /* The move user when this falls inside a move's group and the HP
             * went down; residual damage and healing have no attacker. */
            e->actor = v->last_move_group == v->group && e->target < 4 && g->hp[e->target] < v->hp[e->target] &&
                       v->last_actor != e->target ? v->last_actor : C3_PREVIEW_NO_SLOT;
            memcpy(e->hp, g->hp, sizeof e->hp);
            break;
        case C3_GB_EVENT_FAINT:
            e->kind = C3_PREVIEW_EVENT_FAINT; e->target = e->actor;
            if (e->target < 4) memset(v->stages[e->target], 0, sizeof v->stages[e->target]);
            break;
        case C3_GB_EVENT_SWITCH:
            if (is_recall(b, i)) { e->kind = C3_PREVIEW_EVENT_MESSAGE; break; }
            e->kind = C3_PREVIEW_EVENT_SEND_OUT;
            if (e->actor < 4) {
                e->target = e->actor; e->target_mask = (uint8_t)(1u << e->actor);
                memset(v->stages[e->actor], 0, sizeof v->stages[e->actor]);
                e->hp[e->actor] = g->hp[e->actor];
            }
            break;
        case C3_GB_EVENT_OUTCOME:
            e->kind = C3_PREVIEW_EVENT_OUTCOME;
            e->outcome = b->outcome == C3_GB_WON ? C3_PREVIEW_PLAYER_WON :
                         b->outcome == C3_GB_LOST ? C3_PREVIEW_PLAYER_LOST : C3_PREVIEW_DRAW;
            break;
        default:
            if (!v->introduced && i == 0) { e->kind = C3_PREVIEW_EVENT_INTRO; break; }
            e->kind = C3_PREVIEW_EVENT_MESSAGE;
            if (g->stat >= 0 && g->stat < 7 && e->actor < 4) {
                int next = v->stages[e->actor][g->stat] + g->stat_delta;
                v->stages[e->actor][g->stat] = (int8_t)(next > 6 ? 6 : next < -6 ? -6 : next);
                e->kind = C3_PREVIEW_EVENT_STATUS;
                e->target = e->actor;
                e->actor = v->last_move_group == v->group ? v->last_actor : e->actor;
                e->move = v->last_move;
            }
            break;
        }
        memcpy(e->stages, v->stages, sizeof e->stages);
        e->group = v->group;
        memcpy(v->hp, e->hp, sizeof v->hp);
        if (e->kind == C3_PREVIEW_EVENT_SEND_OUT && e->target < 4) v->hp[e->target] = g->hp[e->target];
        ++v->next; ++n;
    }
    if (n) v->introduced = 1;
    return n;
}
