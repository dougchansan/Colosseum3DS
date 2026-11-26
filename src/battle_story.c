#include "colosseum3ds/battle_story.h"
#include "colosseum3ds/inventory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *e, const char *m) { if (e) snprintf(e, C3_GB_ERROR_SIZE, "%s", m); return 0; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

int c3_battle_story_prepare(const C3GameState *state, C3GeneralBattle *battle)
{
    if (!state || !battle) return 0;
    /* No Snag Machine when a CPU trainer fights on side 0 (encounter 525). */
    battle->snag = battle->info.cpu_side0 ? 0 : 1;
    /* The hero's name for the fight text's control 0x13 (the prize line). */
    {
        C3GameSummary summary;
        if (!battle->link && !battle->info.cpu_side0 && c3_game_state_summary(state, &summary))
            snprintf(battle->side_name[0], sizeof battle->side_name[0], "%.15s", summary.name);
    }
    memset(battle->balls, 0, sizeof battle->balls);
    for (unsigned slot = 0; slot < 16; ++slot) {
        C3GameItem item;
        if (!c3_game_state_item(state, 2, slot, &item)) return 0;
        if (item.id >= 1 && item.id <= 12) battle->balls[item.id] = (uint16_t)(battle->balls[item.id] + item.quantity);
    }
    /* The partner's name (fight text control 0x2C) and flag 0x9A0 for the
     * opposing Shadow Pokemon's appearance, then that check for the Pokemon
     * the battle opened with (fn_8022E6F0 in fightActionFlowKaisiPost). */
    {
        C3GameSummary summary; uint32_t named = 0;
        if (!battle->link && c3_game_state_summary(state, &summary))
            snprintf(battle->partner_name, sizeof battle->partner_name, "%.15s", summary.partner);
        battle->shadow_named = c3_game_state_get_flag(state, 0x9a0u, &named) && named;
    }
    c3_general_battle_appear_checks(battle);
    return 1;
}

int c3_battle_story_prepare_bag(const C3GameState *state, const C3PdaAssets *assets, C3GeneralBattle *battle)
{
    static const unsigned pockets[2] = { 0, 4 }; /* Items, Berries */
    C3GameSummary summary;
    if (!state || !battle) return 0;
    if (!c3_game_state_summary(state, &summary)) memset(&summary, 0, sizeof summary);
    battle->bag_count = 0;
    memset(battle->bag, 0, sizeof battle->bag);
    for (unsigned k = 0; k < 2; ++k)
        for (unsigned slot = 0; slot < c3_inventory_capacity(pockets[k]); ++slot) {
            C3GameItem item;
            if (!c3_game_state_item(state, pockets[k], slot, &item)) return 0;
            if (!item.id || !item.quantity || battle->bag_count >= C3_GB_BAG) continue;
            C3GeneralBagItem *e = &battle->bag[battle->bag_count++];
            size_t n = 0, pn = 0;
            const uint8_t *row = assets ? c3_pda_entry(assets, 7, item.id, &n) : NULL;
            e->item = item.id; e->count = item.quantity; e->pocket = (uint8_t)pockets[k];
            snprintf(e->name, sizeof e->name, "ITEM %u", (unsigned)item.id);
            if (!row || n != 40) continue;
            /* ItemDataBios: +4 itemEffectParam, +0x10 name, +0x20 battleUseFunc. */
            e->use = c3_general_bag_use_class(rd32(row + 0x20));
            const uint8_t *param = row[4] ? c3_pda_entry(assets, 14, row[4], &pn) : NULL;
            if (param && pn == 16) memcpy(e->param, param, 16);
            char name[64];
            if (c3_pda_text(assets, rd32(row + 0x10), summary.name, summary.partner, name, sizeof name) && name[0])
                snprintf(e->name, sizeof e->name, "%.15s", name);
        }
    return 1;
}

/* Snagged Shadow Pokemon into the party or PC box, the snag flag and the
 * balls thrown out of the bag. The heart gauge comes from the save's Shadow
 * runtime table (savedata section 15 at body+0x1B014, row id*0x18, +0x4A6:
 * fn_801EEEB8), the pack's Shadow table when that is empty. */
static int apply_snags(C3GameState *state, const C3GeneralBattle *battle, const C3BattlePack *pack,
                       const C3GeneralRewards *rw, unsigned stored[C3_GB_PARTY], char *error)
{
    for (unsigned ball = 1; ball <= 12; ++ball)
        if (rw->balls_used[ball] && !c3_game_state_take_item(state, 2, ball, rw->balls_used[ball]))
            return fail(error, "The thrown balls are not in the bag.");
    /* Bag items used in the turns (heroItemDecItemDataId as each ran). */
    for (unsigned k = 0; k < battle->bag_count && k < C3_GB_BAG; ++k)
        if (battle->bag[k].used && !c3_game_state_take_item(state, battle->bag[k].pocket, battle->bag[k].item, battle->bag[k].used))
            return fail(error, "The used items are not in the bag.");
    if (!rw->caught_count) return 1;
    size_t bytes = 0; const uint8_t *body = c3_game_state_body(state, &bytes);
    C3GameSummary summary; uint8_t first[C3_GB_POKEMON_SIZE];
    if (!body || bytes < 0x1B014u + 0x4A8u + 0x61u * 0x18u || !c3_game_state_summary(state, &summary) ||
        !c3_game_state_pokemon(state, 0, first)) return fail(error, "The save cannot take a snagged Pokemon.");
    C3SnagOwner owner; memset(&owner, 0, sizeof owner);
    memcpy(owner.name, body + 0x70, sizeof owner.name);
    owner.trainer_id = rd32(body + 0x70 + 0x2c);
    memcpy(owner.attest, first + 8, 4);
    owner.floor = (uint16_t)summary.floor;
    for (unsigned k = 0; k < rw->caught_count; ++k) {
        unsigned index = rw->caught[k]; const C3GeneralMon *m = &battle->party[1][index]; C3ShadowInfo sh;
        if (!c3_battle_pack_shadow(pack, m->shadow, &sh)) return fail(error, "Unknown Shadow Pokemon.");
        uint16_t gauge = rd16(body + 0x1B014u + 0x4A6u + m->shadow * 0x18u);
        if (!gauge) gauge = sh.gauge;
        uint8_t record[C3_GB_POKEMON_SIZE], restored[C3_GB_POKEMON_SIZE];
        if (!c3_general_snag_record(pack, battle, index, &owner, gauge, battle->rng.seed ^ (0x5a6u + index), record))
            return fail(error, "The snagged Pokemon could not be created.");
        /* pcboxAddPokemon's pokemonAllKaihuku: full HP, no condition, full PP. */
        memcpy(restored, record, sizeof restored);
        memcpy(restored + 0x8a, restored + 0x8c, 2);
        memset(restored + 0x64, 0, 7); memset(restored + 0x6c, 0, 6);
        for (unsigned j = 0; j < 4; ++j) {
            C3GeneralMoveInfo mv; uint16_t id = rd16(restored + 0x78 + 4 * j);
            if (id && c3_battle_pack_move(pack, id, &mv)) restored[0x7a + 4 * j] = mv.pp;
        }
        unsigned where = 0;
        if (!c3_game_state_receive_pokemon(state, record, restored, &where))
            return fail(error, "The party and PC boxes are full.");
        if (sh.snag_flag && !c3_game_state_set_flag(state, sh.snag_flag, 1)) return fail(error, "Snag flag rejected.");
        if (stored) stored[k] = where;
    }
    return 1;
}

int c3_battle_story_apply(C3FieldStory *story, C3GameState *state,
                          const C3GeneralBattle *battle, const C3BattlePack *pack,
                          int (*restore_party)(void *context),
                          C3GeneralRewards *rewards, char *error)
{
    uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE];
    C3GeneralRewards rw;
    C3GameSummary summary;
    uint32_t encounter;
    if (!story || !state || !battle || !pack || battle->outcome == C3_GB_RUNNING)
        return fail(error, "No finished battle to apply.");
    if (!c3_field_story_battle_pending(story, &encounter) || encounter != battle->encounter)
        return fail(error, "The story is not waiting for this battle.");
    for (unsigned i = 0; i < C3_GB_PARTY; ++i)
        if (!c3_game_state_pokemon(state, i, party[i])) return fail(error, "Party read failed.");
    if (!c3_general_battle_writeback(battle, pack, party, &rw)) return fail(error, "Party writeback refused.");
    for (unsigned i = 0; i < battle->party_count[0] && !battle->info.cpu_side0; ++i) {
        unsigned slot = battle->party[0][i].party_slot;
        if (!c3_game_state_set_pokemon(state, slot, party[slot])) return fail(error, "Party record rejected.");
    }
    if (rw.written && !apply_snags(state, battle, pack, &rw, rw.stored, error)) return 0;
    if (!c3_game_state_summary(state, &summary)) return fail(error, "Save summary failed.");
    if (rw.result_code == 2 && rw.prize) {
        uint32_t room = summary.money < 9999999u ? 9999999u - summary.money : 0;
        if (!c3_game_state_add_money(state, rw.prize < room ? rw.prize : room)) return fail(error, "Prize money rejected.");
    }
    /* scriptAddPremium (fn_8021E754 with okanePool): the colosseum
     * challenge's pool, flag 0A9E, paid out by its script (native BB, then
     * scriptAddPokedoru) and cleared with native BC. */
    if (rw.premium) {
        uint32_t pool;
        if (!c3_game_state_get_flag(state, C3_PREMIUM_FLAG, &pool) ||
            !c3_game_state_set_flag(state, C3_PREMIUM_FLAG, pool + rw.premium)) return fail(error, "Premium pool rejected.");
    }
    if (rw.wipeout) {
        if (!c3_game_state_add_money(state, -(int64_t)(summary.money / 2u))) return fail(error, "Money loss rejected.");
        if (restore_party && restore_party(state) < 0) return fail(error, "Party restore failed.");
        if (!c3_field_story_battle_whiteout(story, error)) return 0;
    } else if (!c3_field_story_battle_result(story, rw.result_code, error)) return 0;
    if (rewards) *rewards = rw;
    return 1;
}

int c3_battle_story_autoplay(C3FieldStory *story, C3GameState *state,
                             const C3BattlePack *pack, uint32_t encounter, uint32_t seed,
                             int (*restore_party)(void *context),
                             C3GeneralRewards *rewards, unsigned *turns, char *error)
{
    uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE];
    C3GeneralBattle *b = malloc(sizeof *b);
    int ok = 0;
    unsigned count = 0;
    if (!b) return fail(error, "Battle allocation failed.");
    for (unsigned i = 0; i < C3_GB_PARTY; ++i)
        if (!c3_game_state_pokemon(state, i, party[i])) { fail(error, "Party read failed."); goto done; }
    if (!c3_general_battle_begin(b, pack, encounter, party, seed, error)) goto done;
    if (!c3_battle_story_prepare(state, b)) { fail(error, "Bag read failed."); goto done; }
    while (b->outcome == C3_GB_RUNNING) {
        C3GeneralCommand c[2];
        memset(c, 0, sizeof c);
        for (unsigned a = 0; a < b->active_count; ++a)
            if (b->active[0][a] != 255 && b->party[0][b->active[0][a]].hp &&
                !c3_general_battle_choose(b, pack, a, &c[a])) { fail(error, "No legal command."); goto done; }
        if (!c3_general_battle_turn(b, pack, c, error)) goto done;
        ++count;
    }
    ok = c3_battle_story_apply(story, state, b, pack, restore_party, rewards, error);
done:
    if (turns) *turns = count;
    free(b);
    return ok;
}

/* GSmsgGetGSchar from the P*DA's common messages (plain GSchar only). */
static int gift_message(void *context, uint32_t id, uint8_t text[0x16])
{
    size_t size = 0; const uint8_t *d = context ? c3_pda_entry(context, 1, id, &size) : NULL;
    if (!d || size < 2) return 0;
    memset(text, 0, 0x16);
    for (size_t i = 0; i + 1 < size && i < 0x14; i += 2) {
        uint16_t c = rd16(d + i);
        if (!c) break;
        if (c == 0xffff) return 0;
        text[i] = d[i]; text[i + 1] = d[i + 1];
    }
    return 1;
}

int c3_battle_story_gift(C3GameState *state, const C3BattlePack *pack, const C3PdaAssets *assets,
                         unsigned kind, uint16_t *species, unsigned *where)
{
    size_t bytes = 0; const uint8_t *body = c3_game_state_body(state, &bytes);
    uint8_t record[C3_GB_POKEMON_SIZE], slot[C3_GB_POKEMON_SIZE];
    C3GiftHero hero; C3GiftInfo info;
    if (!body || bytes < 0x70u + 0xA81u || !pack) return -3;
    /* Hero (save body +0x70): name +0, trainer ID +0x2C, sex +0xA80; the
     * gamedata attest is the body's first word. */
    memcpy(hero.name, body + 0x70, sizeof hero.name);
    hero.trainer_id = rd32(body + 0x70 + 0x2c);
    hero.sex = body[0x70 + 0xa80];
    memcpy(hero.attest, body, 4);
    if (!c3_battle_pack_gift_record(pack, kind, &hero, gift_message, (void *)assets,
                                    c3_game_state_random16, state, record, &info)) return -3;
    if (info.memo && c3_game_state_memo_add(state, record) < 0) return -3;
    if (!info.box) {
        unsigned free_slot = 6;
        for (unsigned i = 0; i < 6 && free_slot == 6; ++i)
            if (c3_game_state_pokemon(state, i, slot) && !rd16(slot)) free_slot = i;
        if (free_slot == 6) return -2;
    }
    unsigned stored = 0;
    if (!c3_game_state_receive_pokemon(state, record, record, &stored)) return -2;
    if (species) *species = info.species;
    if (where) *where = stored;
    return stored < 6 ? (int)stored : -1;
}
