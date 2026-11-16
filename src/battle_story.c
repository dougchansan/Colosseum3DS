#include "colosseum3ds/battle_story.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *e, const char *m) { if (e) snprintf(e, C3_GB_ERROR_SIZE, "%s", m); return 0; }

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
    for (unsigned i = 0; i < battle->party_count[0]; ++i) {
        unsigned slot = battle->party[0][i].party_slot;
        if (!c3_game_state_set_pokemon(state, slot, party[slot])) return fail(error, "Party record rejected.");
    }
    if (!c3_game_state_summary(state, &summary)) return fail(error, "Save summary failed.");
    if (rw.result_code == 2 && rw.prize) {
        uint32_t room = summary.money < 9999999u ? 9999999u - summary.money : 0;
        if (!c3_game_state_add_money(state, rw.prize < room ? rw.prize : room)) return fail(error, "Prize money rejected.");
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
