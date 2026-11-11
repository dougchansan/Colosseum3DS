#ifndef C3_BATTLE_START_H
#define C3_BATTLE_START_H
/* Start-of-battle stages of the battle thread (fight_action.c), lowered for
 * the paths Willie's battle reaches; every other path fails the script with
 * a named reason. See docs/original-battle-turns.md, "Start of battle". */
#include "colosseum3ds/battle_handlers.h"

/* fightActionFlowKaisiPost (kind 5), after the send-out (kind 4) created the
 * battlers in battle:
 *   1. fightFloorCreateFightOutPokemonPtrAry: the floor's battler list
 *      (FightFloor +0xA4C4, 8 pointers) in side, trainer, slot order;
 *   2. fightFloorSortFightOutPokemonPtrAry(0, 0): sorted by speed (ties draw
 *      once each);
 *   3. fightSeqInit: per valid battler the one-turn fields 0x102..0x112,
 *      conditions 0x32 / 0x37 / 0x33 erased, 0xED counted down (a rampage,
 *      condition 0x12, and a Substitute, 0x14, are refused); each valid
 *      side's condition 0x4D (category 6) erased; the move cleanup
 *      (0x113..0x11F); the script work bytes 0x80478D78..7F and the current
 *      action cleared; flinch (0x11) erased on battlers in battle;
 *   4. the Shadow appear check (fn_8022E6F0, 0): Shadow Pokemon refused;
 *   5. kind 6, fightActionFlowTenkouInit: field effect 1 (if the floor's
 *      clear-weather condition 0x4E can be written, it is written), then the
 *      weather; a weather's start message (lbl_80378AA0) is refused;
 *   6. in speed order, the ability appear check (fn_8022E410): weather,
 *      Intimidate, Trace, Forecast and Cloud Nine / Air Lock refused;
 *   7. the Intimidate (fn_8022E314(1)) and Trace (fn_8022E1C4) passes, the
 *      end-of-move service;
 *   8. in speed order, the held-item appear check (fn_8022D084): the floor's
 *      appointed item (0x56) for each battler in battle; a held item is
 *      refused;
 *   9. the Shadow appear check (fn_8022E6F0, 1);
 *  10. the met lists, the enemy registry and the enemy arrays;
 *  11. floor 0x5B (first-attack random) from one draw;
 *  12. fn_801DA7AC, which frees the move-effect pool (presentation).
 * sc is the battle's script instance (its work bytes and current action;
 * failures name the refused path). Returns 0 with sc failed when a path is
 * refused or a record is unreadable. */
int c3_battle_start_post(C3BattleHandlers *handlers, C3BattleScript *sc);

/* fightActionFlowKaisiNyuujouPokemon (kind 4) for one side, buff 5 (side 1)
 * then 4 (side 0) in the battle thread:
 *   1. each valid trainer with a model sequence (trainer field 0x4C) sends
 *      out its first battle-ready party members (fightPokemonCheckFightOut),
 *      up to the active count: face and model sequence (presentation), the
 *      battler created from the party member in the same slot
 *      (fightOutPokemonCreate), its ball effect loaded, and, when floor 0x1E
 *      is 1, an opponent's Pokedex and Shadow flags: the seen flag and
 *      Strategy Memo entry are recorded in state->seen (not applied), a
 *      Shadow Pokemon is refused; then its grid entry;
 *   2. the ball throws and appearance effects loaded;
 *   3. per trainer: the appear message variables (0x14..0x17 by slot), the
 *      throw, the trainer variables 0x22 (class prefix), 0x23 and 0x25
 *      (name), the appear message (0x7670.. by count and side), and per
 *      battler its appearance with the floor's attacker set to it (0x36,
 *      restored), its status menu opened and closed;
 *   4. the message closed and the effects freed.
 * A send-out from party slot 2 or later is refused. Returns 0 with sc failed
 * when a path is refused or a record is unreadable. */
int c3_battle_start_send_out(C3BattleHandlers *handlers, C3BattleScript *sc, uint16_t side_kind);
#endif
