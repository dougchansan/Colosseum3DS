#ifndef C3_AGENT_LINK_H
#define C3_AGENT_LINK_H
/* Private gameplay-harness link (C3_AGENT_HARNESS builds only): the state
 * feed of colosseum3ds/agent_feed.h sent as UDP datagrams to the PC bridge.
 * Target and rate come from the build (C3_AGENT_HOST, C3_AGENT_PORT) or from
 * an optional sdmc:/3ds/Colosseum3DS/agent.cfg (host=, port=, hz=, off). */
#ifdef C3_AGENT_HARNESS
#include "colosseum3ds/agent_feed.h"
#include "n3ds_platform.h"

/* Called by c3_n3ds_poll once per frame: counts the frame, records the input
 * the game sees, and sends the feed when its interval has elapsed. */
void c3_agent_link_poll(const C3N3dsInput *input);
/* A scene loop claims the current frame for its mode. A frame no loop claims
 * is reported as mode 0 (unknown); a new mode drops the old scene's data. */
void c3_agent_link_scene(uint8_t mode);
/* Nonzero when the next poll will send: refresh scene data only then. */
int c3_agent_link_due(void);
C3AgentFeed *c3_agent_link_feed(void);
/* A battle loop claims the frame and, when due, records the battle summary. */
struct C3GeneralBattle;
void c3_agent_link_battle(const struct C3GeneralBattle *battle, uint8_t ui_page);
void c3_agent_link_close(void);
#endif
#endif
