#ifndef C3_AREA_H
#define C3_AREA_H

#include "colosseum3ds/field.h"
#include <stddef.h>
#include <stdint.h>

#define C3_AREA_MAX_ACTORS 32u
#define C3_AREA_MAX_SURFACES 1024u
#define C3_AREA_MAX_LINKS 128u

typedef struct C3AreaActor {
    uint32_t index, resource, people_info, flags, move_script, talk_script, aux2, aux3;
    float position[3], yaw, radius, walk_distance, run_distance;
    uint8_t motions[9], collision_type; /* Original PeopleInfo flags bits2..3. */
    char file[64]; /* Empty means the original actor is not prepared. */
    uint32_t name_index, hear_flag, name_message, talk_start, talk_end;
    /* C3A1 actor word156: reviewed original part1 -> sampled bone+1.
     * Zero means no prepared attachment part. Other part IDs remain closed. */
    uint32_t part1_bone_plus_one;
} C3AreaActor;
/* One row of the original door/elevator table lbl_80478ECC (0x18-byte
 * FloorDoorEntry, floor_event.c): animation numbers of the door model
 * (negative = none), the CCD object its collision uses, and the flags that
 * record it open (open_flag) or gate it (close_flag). */
#define C3_AREA_MAX_DOORS 16u
typedef struct C3AreaDoor {
    uint32_t index;    /* row index the scripts pass to native 60 */
    int8_t open_anim, close_anim, open_end_anim, close_end_anim;
    uint8_t door_type, part_index;
    int16_t collision_id;
    uint16_t open_flag, close_flag;
    uint32_t resource; /* door model resource; zero = no model */
    int8_t alternate_a, alternate_b; /* elevator travel animations */
    uint16_t part_bone_plus_one;     /* sampled bone of model part part_index; 0 = none */
    char file[40];                   /* prepared object pack; empty = not prepared */
} C3AreaDoor;
/* C3D1: the door-table rows of one floor with their object pack names. */
typedef struct C3AreaDoors {
    uint32_t floor, count;
    C3AreaDoor rows[C3_AREA_MAX_DOORS];
} C3AreaDoors;
typedef struct C3AreaLink {
    uint32_t kind, source_id, script, args[4], source_index;
} C3AreaLink;
typedef struct C3Area {
    uint32_t floor, group, actor_count, transition_count, interaction_count, link_count;
    uint32_t load_script, init_script, pre_script, main_script, post_script;
    C3AreaActor *actors;
    C3FieldTriangle *transitions, *interactions;
    C3AreaLink *links;
} C3Area;

/* The C3A1 metadata preserves original actor records, CCD event surfaces,
 * and independently resolved event/link records. Initialize to zero. No
 * embedded path may escape the prepared boot directory. */
int c3_area_parse(C3Area *area, const void *data, size_t bytes, char *error, size_t capacity);
int c3_area_load(C3Area *area, const char *path, char *error, size_t capacity);
void c3_area_close(C3Area *area);
const C3AreaLink *c3_area_link(const C3Area *area, uint32_t kind,
                             uint32_t source_id);
/* Return a contacted original transition surface, in original table order.
 * A stationary player or one moving away cannot trigger a doorway. */
const C3FieldTriangle *c3_area_transition(const C3Area *area,
                                        const float before[3], const float after[3], float radius);
const C3FieldTriangle *c3_area_transition_active(const C3Area *area,const C3FieldMap *map,
                                        const float before[3], const float after[3], float radius);

/* Zero-initialize before use. A failed parse leaves the previous table. */
int c3_area_doors_parse(C3AreaDoors *doors, const void *data, size_t bytes, char *error, size_t capacity);
int c3_area_doors_load(C3AreaDoors *doors, const char *path, char *error, size_t capacity);
/* GScolsys2ThruGetEventID (kind 2 events): source IDs of the event
 * surfaces a sphere of `radius` touches on its way from before to after,
 * approached from their front side. Face, flagged edges and flagged corners
 * are tested as in the original; at most four distinct IDs, in table order
 * per pass. Returns the count. */
unsigned c3_area_touch_events(const C3Area *area,const C3FieldMap *map,const float before[3],
                              const float after[3],float radius,uint16_t ids[4]);

#endif
