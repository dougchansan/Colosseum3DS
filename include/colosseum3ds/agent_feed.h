#ifndef COLOSSEUM3DS_AGENT_FEED_H
#define COLOSSEUM3DS_AGENT_FEED_H

/* Developer-only state feed for the private gameplay harness. It is built
 * only with C3_AGENT_HARNESS (Makefile.3ds C3_AGENT_HARNESS=1, default off);
 * a player build carries none of this. One datagram describes the scene the
 * console is running; layout (little-endian, docs in the private harness):
 *
 *   header  40 bytes  "C3AG", version, length, sequence, frame, time, mode,
 *                     section count, held buttons, stick, stylus, and what the
 *                     game saw since the previous datagram (frames with any
 *                     input, buttons held at any point, the strongest stick,
 *                     frames elapsed), so a short press is never invisible
 *   section  4 bytes  type, version, payload length; then the payload
 *
 * Section 1 (field) is the floor, Wes, the partner, the dialogue/menu state
 * and the CCD object enable bits; 2 the placed people; 3 a battle summary;
 * 4 the open dialogue text without markup. */

#include <stddef.h>
#include <stdint.h>

#define C3_AGENT_FEED_VERSION 1u
#define C3_AGENT_HEADER_BYTES 40u
#define C3_AGENT_PACKET_MAX 1400u
#define C3_AGENT_MAX_ACTORS 48u
#define C3_AGENT_TEXT_MAX 200u
#define C3_AGENT_PARTY 6u

enum {
    C3_AGENT_MODE_UNKNOWN = 0, C3_AGENT_MODE_TITLE, C3_AGENT_MODE_LOADING,
    C3_AGENT_MODE_FIELD, C3_AGENT_MODE_DIALOGUE, C3_AGENT_MODE_MENU,
    C3_AGENT_MODE_PDA, C3_AGENT_MODE_BATTLE, C3_AGENT_MODE_WORLDMAP,
    C3_AGENT_MODE_EVENT /* a field script holds control without dialogue */
};
enum { C3_AGENT_SECTION_FIELD = 1, C3_AGENT_SECTION_ACTORS, C3_AGENT_SECTION_BATTLE, C3_AGENT_SECTION_TEXT };

/* Field flags (C3AgentField.flags). */
#define C3_AGENT_FIELD_CONTROL      0x0001u
#define C3_AGENT_FIELD_DIALOGUE     0x0002u
#define C3_AGENT_FIELD_CHOICE       0x0004u
#define C3_AGENT_FIELD_CHOICE_NO    0x0008u
#define C3_AGENT_FIELD_PREPARED     0x0010u
#define C3_AGENT_FIELD_INGAME       0x0020u
#define C3_AGENT_FIELD_FIRST_PERSON 0x0040u
#define C3_AGENT_FIELD_CAMERA       0x0080u
#define C3_AGENT_FIELD_SHOP         0x0100u
#define C3_AGENT_FIELD_NAMING       0x0200u
#define C3_AGENT_FIELD_PC           0x0400u
#define C3_AGENT_FIELD_BLOCKED      0x0800u
#define C3_AGENT_FIELD_ORBIT        0x1000u
#define C3_AGENT_FIELD_PICK         0x2000u
#define C3_AGENT_FIELD_PARTNER      0x4000u
#define C3_AGENT_FIELD_PARTNER_SEEN 0x8000u
/* Actor flags. */
#define C3_AGENT_ACTOR_VISIBLE  0x01u
#define C3_AGENT_ACTOR_TALKABLE 0x02u
#define C3_AGENT_ACTOR_PLAYING  0x04u

typedef struct C3AgentField {
    uint16_t floor, flags, object_count;
    uint8_t phase, pause_page, ingame_page, motion, collisions;
    uint32_t message_id, battle_id, menu_floor;
    char prefix[16];
    float wes[3], wes_yaw, move_yaw, camera_yaw;
    float partner[3], partner_yaw;
    uint16_t partner_clip;
    uint8_t disabled_objects[32];
} C3AgentField;

typedef struct C3AgentActor {
    uint16_t index;
    uint8_t flags;
    float position[3], yaw;
} C3AgentActor;

typedef struct C3AgentMon {
    uint16_t species, hp, max_hp, stats[5], moves[4]; /* stats: Atk Def SpA SpD Spe */
    uint8_t pp[4], level, types[2], status;
} C3AgentMon;

typedef struct C3AgentBattle {
    uint32_t encounter;
    uint16_t turn;
    uint8_t outcome, ui_page, active_count, party_count[2], snag;
    uint8_t active[2][2]; /* party index per position, 255 empty */
    C3AgentMon party[2][C3_AGENT_PARTY];
} C3AgentBattle;

typedef struct C3AgentFeed {
    uint32_t sequence, frame, time_ms;
    uint8_t mode;
    uint16_t held;
    int8_t stick_x, stick_y;
    uint8_t touch_held;
    uint16_t touch_x, touch_y;
    /* Since the previous datagram: frames whose input was not neutral, the
     * buttons held at any point, the stick of largest reach, frames elapsed. */
    uint8_t input_frames;
    uint16_t held_latch, frames_since;
    int8_t latch_stick_x, latch_stick_y;
    int has_field, has_battle;
    C3AgentField field;
    uint32_t actor_count;
    C3AgentActor actors[C3_AGENT_MAX_ACTORS];
    C3AgentBattle battle;
    char text[C3_AGENT_TEXT_MAX + 1u];
} C3AgentFeed;

/* Encode one datagram. Returns its length, or 0 when capacity is too small.
 * Sections are written only for the parts present (has_field, actors while
 * has_field, has_battle, non-empty text). */
size_t c3_agent_feed_encode(const C3AgentFeed *feed, uint8_t *out, size_t capacity);

#endif
