#define _POSIX_C_SOURCE 200809L
#include "route_battles.h"

#include "colosseum3ds/battle_story.h"

#include <stdlib.h>
#include <string.h>

enum { MAX_PLAYED = 256, MAX_GAPS = 32, MAX_SEEDS = 64 };
static C3BattlePack pack;
static int enabled, any_outcome;
static uint32_t played[MAX_PLAYED], gaps[MAX_GAPS];
static unsigned played_count, gap_count, gaps_used;

static void parse_list(const char *text, void (*add)(uint32_t))
{
    while (text && *text) {
        char *end;
        unsigned long v = strtoul(text, &end, 0);
        if (end == text) { ++text; continue; }
        add((uint32_t)v);
        text = end;
    }
}

void route_battles_add_gap(uint32_t encounter) { if (gap_count < MAX_GAPS) gaps[gap_count++] = encounter; }
void route_battles_set_any(int any) { any_outcome = any; }
int route_battles_enabled(void) { return enabled; }
unsigned route_battles_played(void) { return played_count; }
unsigned route_battles_gaps_used(void) { return gaps_used; }

int route_battles_open(char *error, size_t capacity)
{
    const char *path = getenv("C3_ROUTE_BATTLES"), *mode = getenv("C3_ROUTE_BATTLE_MODE");
    if (!path || !*path) return 1;
    char problem[C3_GB_ERROR_SIZE] = "";
    if (!c3_battle_pack_load(&pack, path, problem)) { snprintf(error, capacity, "battles: %s", problem); return 0; }
    enabled = 1;
    any_outcome = mode && !strcmp(mode, "any");
    parse_list(getenv("C3_ROUTE_ENGINE_GAPS"), route_battles_add_gap);
    return 1;
}

int route_battles_was_played(uint32_t encounter)
{
    for (unsigned i = 0; i < played_count; ++i) if (played[i] == encounter) return 1;
    return 0;
}

int route_battles_heal(C3GameState *game)
{
    int n = 0;
    for (unsigned i = 0; i < C3_GB_PARTY; ++i) {
        uint8_t r[C3_GB_POKEMON_SIZE];
        if (!c3_game_state_pokemon(game, i, r) || !(r[0] | r[1])) continue;
        r[0x8a] = r[0x8c]; r[0x8b] = r[0x8d]; memset(r + 0x64, 0, 7); memset(r + 0x6c, 0, 6);
        if (!c3_game_state_set_pokemon(game, i, r)) return -1;
        ++n;
    }
    return n;
}

static C3GameState *heal_target;
static int heal(void *context) { (void)context; return route_battles_heal(heal_target); }

int route_battle_play(C3FieldStory *story, C3GameState *game, uint32_t floor, uint32_t fallback_result,
                      FILE *report, const char *tag, char *error, size_t capacity)
{
    uint32_t encounter;
    if (!c3_field_story_battle_pending(story, &encounter)) return 1;
    if (!tag) tag = "route_battle";
    if (!enabled) {
        fprintf(report, "%s floor=%03lx encounter=%lu result=%lu skipped_as_result=1\n", tag,
                (unsigned long)floor, (unsigned long)encounter, (unsigned long)fallback_result);
        return c3_field_story_battle_result(story, fallback_result, error);
    }
    C3BattleEncounterInfo info;
    memset(&info, 0, sizeof info);
    int known = c3_battle_pack_info(&pack, encounter, &info);
    if (!known || info.reason) {
        const char *why = known ? c3_battle_pack_reason(info.reason) : "not in the pack";
        for (unsigned i = 0; i < gap_count; ++i)
            if (gaps[i] == encounter) {
                ++gaps_used;
                fprintf(report, "%s floor=%03lx encounter=%lu played=0 engine_gap=1 reason=\"%s\" result=%lu\n", tag,
                        (unsigned long)floor, (unsigned long)encounter, why, (unsigned long)fallback_result);
                return c3_field_story_battle_result(story, fallback_result, error);
            }
        snprintf(error, capacity, "battle %lu cannot be played: %s", (unsigned long)encounter, why);
        return 0;
    }
    uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE];
    for (unsigned i = 0; i < C3_GB_PARTY; ++i)
        if (!c3_game_state_pokemon(game, i, party[i])) { snprintf(error, capacity, "party read failed"); return 0; }
    C3GeneralBattle *b = malloc(sizeof *b);
    if (!b) { snprintf(error, capacity, "battle allocation failed"); return 0; }
    char problem[C3_GB_ERROR_SIZE] = "";
    unsigned attempt, turns = 0;
    for (attempt = 0;; ++attempt) {
        if (attempt == MAX_SEEDS) {
            snprintf(error, capacity, "battle %lu: no win in %u seeds", (unsigned long)encounter, MAX_SEEDS); goto fail;
        }
        if (!c3_general_battle_begin(b, &pack, encounter, party, 0x5eed0000u ^ encounter ^ attempt * 0x9e3779b9u, problem) ||
            !c3_battle_story_prepare(game, b)) {
            snprintf(error, capacity, "battle %lu: %s", (unsigned long)encounter, problem[0] ? problem : "bag read failed"); goto fail;
        }
        for (turns = 0; b->outcome == C3_GB_RUNNING; ++turns) {
            C3GeneralCommand c[2];
            memset(c, 0, sizeof c);
            for (unsigned a = 0; a < b->active_count; ++a)
                if (b->active[0][a] != 255 && b->party[0][b->active[0][a]].hp &&
                    !c3_general_battle_choose(b, &pack, a, &c[a])) { snprintf(error, capacity, "no legal command"); goto fail; }
            if (!c3_general_battle_turn(b, &pack, c, problem)) { snprintf(error, capacity, "turn: %s", problem); goto fail; }
        }
        /* Scripted CPU-vs-CPU scenes (525) are watched, not retried by the hero.
         * Their first real outcome must resume the original event, including a loss. */
        if (info.cpu_side0 || any_outcome || b->outcome == C3_GB_WON) break;
        /* A player who loses whites out, is healed at a Pokemon Center
         * and comes back: retry with the healed party. */
        if (route_battles_heal(game) < 0) { snprintf(error, capacity, "party heal failed"); goto fail; }
        for (unsigned i = 0; i < C3_GB_PARTY; ++i)
            if (!c3_game_state_pokemon(game, i, party[i])) { snprintf(error, capacity, "party read failed"); goto fail; }
    }
    C3GeneralRewards rw;
    memset(&rw, 0, sizeof rw);
    heal_target = game;
    if (!c3_battle_story_apply(story, game, b, &pack, heal, &rw, problem)) {
        snprintf(error, capacity, "apply: %s", problem); goto fail;
    }
    unsigned long exp = 0;
    for (unsigned i = 0; i < C3_GB_PARTY; ++i) exp += rw.exp[i];
    fprintf(report, "%s floor=%03lx encounter=%lu played=1 trainer=\"%s %s\" kind=%u party=%u attempts=%u turns=%u "
            "result=%lu prize=%lu exp=%lu snagged=%u wipeout=%d written=%d\n", tag,
            (unsigned long)floor, (unsigned long)encounter, info.title, info.trainer, info.kind, info.party_count,
            attempt + 1, turns, (unsigned long)rw.result_code, (unsigned long)rw.prize, exp, (unsigned)rw.caught_count,
            rw.wipeout, rw.written);
    /* A won hero battle with getOkane ends with the prize line (fight
     * message 0x7546) unless the turn's event log saturated. */
    if (b->outcome == C3_GB_WON && info.get_money && !info.cpu_side0) {
        const char *line = b->event_count ? b->events[b->event_count - 1].text : "";
        if (!strstr(line, " for winning!") && b->event_count < C3_GB_EVENT_LIMIT) {
            snprintf(error, capacity, "battle %lu: no prize line", (unsigned long)encounter); goto fail;
        }
        fprintf(report, "%s_prize encounter=%lu prize=%lu text=\"%s\"\n", tag, (unsigned long)encounter,
                (unsigned long)rw.prize, line);
    }
    if (played_count < MAX_PLAYED) played[played_count++] = encounter;
    free(b);
    return 1;
fail:
    free(b);
    return 0;
}

unsigned route_battles_close(FILE *report)
{
    unsigned missing = 0;
    if (!enabled) return 0;
    const char *expect = getenv("C3_ROUTE_EXPECT_BATTLES");
    while (expect && *expect) {
        char *end;
        unsigned long v = strtoul(expect, &end, 0);
        if (end == expect) { ++expect; continue; }
        if (!route_battles_was_played((uint32_t)v)) {
            fprintf(report, "route_battle_missing encounter=%lu\n", v);
            ++missing;
        }
        expect = end;
    }
    fprintf(report, "route_battles played=%u engine_gaps=%u missing=%u\n", played_count, gaps_used, missing);
    c3_battle_pack_free(&pack);
    enabled = 0;
    return missing;
}
