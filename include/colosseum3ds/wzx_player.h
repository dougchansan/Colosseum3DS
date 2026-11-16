#ifndef C3_WZX_PLAYER_H
#define C3_WZX_PLAYER_H

#include <stddef.h>
#include <stdint.h>

/* General player for the original battle effect sequences ("waza
 * sequences", WZX) prepared by tools/prepare_battle_wzx.py as C3WX packs.
 *
 * This is the source scheduling, not the visuals: node start times follow
 * wazaSequenceEntryLink (0x801DC310), the per-tick start/update/stop
 * lifecycle wazaSequenceUpdate (0x801DB988), control nodes their kind-1
 * EntryUpdate case, and the hit/finish times wazaSequenceSysGetWazaTime
 * (0x801DB3F8). Models, particles, effects, sounds and owner actions are
 * started, updated and stopped through a backend (the 3D battle stage).
 * Times are 60 Hz source ticks. Timing depends on the owner: its PKX
 * animation entry's timing marks anchor every unlinked node. */

#define C3_WZX_MAX_NODES 96u
#define C3_WZX_NODE_BYTES 160u
#define C3_WZX_HEADER_BYTES 64u

enum { C3_WZX_CONTROL = 1, C3_WZX_MODEL, C3_WZX_PARTICLE, C3_WZX_EFFECT, C3_WZX_SOUND, C3_WZX_ACTION };
/* wazaSequenceSysGetWazaTime timeType values used by the battle. */
enum { C3_WZX_TIME_START = 0, C3_WZX_TIME_END = 1, C3_WZX_TIME_ATTACK = 3, C3_WZX_TIME_DAMAGE = 5, C3_WZX_TIME_FINISHED = 6 };

typedef struct {
    uint32_t key, kind, attach, postype, link, src, tgt, tidx;
    int32_t timing[16];
    uint32_t flags, part, node_ver, state;
    const uint8_t *payload;   /* original big-endian payload header (may be NULL) */
    uint32_t payload_bytes;
    const char *file;         /* model/particle pack basename, or NULL */
    uint32_t status;          /* nonzero: not converted, skipped */
    uint32_t model_clips, model_bones;
    const uint16_t *joint_map; /* little-endian u16 per source joint (unaligned-safe via accessor) */
    uint32_t joint_map_count;
} C3WzxNode;

typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint32_t node_count, entry, flags, part_sel, seq_version;
    uint32_t nodes_at, strings_at, camera_at, camera_count;
} C3WzxPack;

int c3_wzx_pack_decode(C3WzxPack *pack, const void *bytes, size_t size, char *error, size_t capacity);
int c3_wzx_pack_node(const C3WzxPack *pack, uint32_t index, C3WzxNode *out);
/* Source joint -> pack bone (0xFFFF none). */
unsigned c3_wzx_node_joint(const C3WzxNode *node, uint32_t joint);
/* Camera sample (eye xyz, interest xyz, roll, fov) at a tick, clamped. */
int c3_wzx_pack_camera(const C3WzxPack *pack, uint32_t tick, float out[8]);
/* Big-endian payload words. */
uint32_t c3_wzx_payload_u32(const C3WzxNode *node, uint32_t offset);

/* update: 1 running, 0 done, -1 let the player decide (the node's duration
 * mark timing[tidx-1] on its own timeline, or done at once when tidx is 0). */
typedef struct {
    void *user;
    int (*start)(void *user, uint32_t index, const C3WzxNode *node);  /* 0: skipped */
    int (*update)(void *user, uint32_t index, const C3WzxNode *node, uint32_t ticks, int32_t local);
    void (*stop)(void *user, uint32_t index, const C3WzxNode *node, int forced);
} C3WzxBackend;

typedef struct {
    const C3WzxPack *pack;
    C3WzxBackend backend;
    C3WzxNode nodes[C3_WZX_MAX_NODES];
    int32_t start[C3_WZX_MAX_NODES];
    uint8_t state[C3_WZX_MAX_NODES];   /* 0 waiting, 1 running, 2 done */
    uint8_t order[C3_WZX_MAX_NODES];
    int32_t owner_timing[16];
    int32_t time, motion_delay, end_time;
    int32_t control_left;
    uint8_t active, stopping, kind;
    uint32_t started, skipped;
} C3WzxPlayer;

/* owner_timing: the owner's PKX timing values for the sequence's entry
 * (16 ticks, the 0xD4-byte table row +0x0C). kind: 1 attack, 2 damage,
 * 3/4 special (selects how hit/finish marks are read, as the original's
 * animationMode). Links every node; a negative earliest start shifts the
 * sequence and becomes motion_delay (the owner's motion starts that late). */
int c3_wzx_player_begin(C3WzxPlayer *player, const C3WzxPack *pack, unsigned kind,
                        const int32_t owner_timing[16], const C3WzxBackend *backend, char *error, size_t capacity);
/* Advance by ticks; returns 1 while running, 0 when finished. */
int c3_wzx_player_step(C3WzxPlayer *player, uint32_t ticks);
/* wazaSequenceSysGetWazaTime for this owner: the sequence time of a mark. */
int32_t c3_wzx_player_mark(const C3WzxPlayer *player, unsigned time_type);
int c3_wzx_player_reached(const C3WzxPlayer *player, unsigned time_type);
/* wazaSequenceApplyStop: stop every running node (forced). */
void c3_wzx_player_stop(C3WzxPlayer *player);
#endif
