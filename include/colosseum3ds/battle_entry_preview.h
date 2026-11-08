#ifndef C3_BATTLE_ENTRY_PREVIEW_H
#define C3_BATTLE_ENTRY_PREVIEW_H

#include "colosseum3ds/battle_entry_session.h"
#include "colosseum3ds/battle_preview.h"

/* Start the nonpersistent preview from a finished trainer-entry session: the
 * first two FightPokemon of each side (player side 0, opponent side 1) become
 * the four active mons, and turns continue from the session's RNG. The session
 * must be DONE with both entry actions returning 1. Nothing is written to the
 * session, the request or any save. Returns 1 on success; failure leaves *out
 * unchanged and explains why in error. */
int c3_battle_entry_preview_begin(C3BattlePreview *out, const C3BattleRequest *request,
                                  const C3BattleEntrySession *session,
                                  const C3BattlePreviewTables *tables, char *error);
#endif
