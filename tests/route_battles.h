#ifndef C3_TESTS_ROUTE_BATTLES_H
#define C3_TESTS_ROUTE_BATTLES_H

#include "colosseum3ds/field_story.h"
#include "colosseum3ds/game_state.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Real story battles for the headless chapter harnesses (test_story_route,
 * test_story_crawl, test_phenac_story, test_phenac_events, test_under_*).
 *
 * With C3_ROUTE_BATTLES=<battles.c3x> (tools/prepare_battle_encounters.py)
 * each waiting native 98/66 request is played as the field plays it
 * (platform/3ds/field_scene.c play_story_battle): the original encounter
 * row, trainer and team from the pack, both sides on the engine's policy,
 * then c3_battle_story_apply (party writeback, EXP, prize, snagging and
 * whiteout) resumes the script with the original result ID. An encounter
 * the pack refuses fails the harness unless it is a declared engine gap.
 * Without the variable the old behaviour stays: the fallback result is
 * reported ("skipped_as_result=1").
 *
 * Environment:
 *   C3_ROUTE_BATTLES=<pack>          enable real battles
 *   C3_ROUTE_BATTLE_MODE=win|any     win (default): a lost battle is replayed
 *                                    with the next seed until the hero wins,
 *                                    as a player retries; any: the first
 *                                    outcome is applied (losses, whiteouts).
 *                                    CPU-vs-CPU scenes always apply the first outcome.
 *   C3_ROUTE_EXPECT_BATTLES=a,b,...  route_battles_close fails unless every
 *                                    listed encounter was played
 *   C3_ROUTE_ENGINE_GAPS=a,b,...     encounters the engine cannot play yet
 *                                    (battle-rules lane): reported, given the
 *                                    fallback result, never silent */
int route_battles_open(char *error, size_t capacity);
int route_battles_enabled(void);
void route_battles_set_any(int any);
void route_battles_add_gap(uint32_t encounter);
/* Handle the waiting battle. Returns 0 with the reason in error. */
int route_battle_play(C3FieldStory *story, C3GameState *game, uint32_t floor, uint32_t fallback_result,
                      FILE *report, const char *tag, char *error, size_t capacity);
int route_battles_was_played(uint32_t encounter);
unsigned route_battles_played(void);
unsigned route_battles_gaps_used(void);
/* pokemonAllKaihuku for the party service: HP to maximum, persistent
 * condition cleared (tests/test_world_story.c). Returns the count. */
int route_battles_heal(C3GameState *game);
/* Checks C3_ROUTE_EXPECT_BATTLES, prints a summary line, frees the pack.
 * Returns the number of expected encounters that were not played. */
unsigned route_battles_close(FILE *report);

#endif
