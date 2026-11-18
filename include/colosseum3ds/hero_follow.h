#ifndef C3_HERO_FOLLOW_H
#define C3_HERO_FOLLOW_H

/* The party follower of hero_move.c (person 0,101: Rui), in port units.
 * moveLeader records the leader's position whenever it is more than 12
 * original units from the last record (a ring of 20). fn_8012D7F0 walks
 * the follower towards the leader while it is nearer than the newest
 * record, else towards that record, stopping on the circle of its spacing
 * (12) around the leader (fn_8012D39C) and backing off inside 9 units. One
 * step is at most one original unit per 60 Hz frame. fn_8012DE94 puts a
 * follower that stayed more than 100 units away for 300 frames on the
 * newest record it fits on (here: the newest with ground). scale converts
 * original units to port units (C3FieldMap.scale). */
#define C3_FOLLOW_HISTORY 20u

typedef struct C3Follow {
    float history[C3_FOLLOW_HISTORY][3];
    unsigned head, count;
    float far_seconds;
    float leader[3]; /* the leader at the last record call */
    int has_leader;
} C3Follow;

typedef struct C3FollowStep {
    float move[2];   /* x/z displacement for this update */
    float facing[2]; /* direction to face (zero: keep) */
    float speed;     /* 0..1, the animation amount (move / frame budget) */
} C3FollowStep;

void c3_follow_reset(C3Follow *follow);
/* moveLeader's record after the leader moved. */
void c3_follow_record(C3Follow *follow, const float leader[3], float scale);
/* The newest record i (0 newest); 0 when there are fewer. */
int c3_follow_record_at(const C3Follow *follow, unsigned i, float out[3]);
/* fn_8012D7F0: the follower's step towards the leader for `seconds`. */
void c3_follow_step(const C3Follow *follow, const float leader[3], const float position[3],
                    float spacing, float scale, float seconds, C3FollowStep *step);
/* fn_8012D7F0 then moveLeader once per 60 Hz frame for `seconds`, the
 * leader moving evenly from its last recorded position to `leader`. The
 * original steps per frame; one large step per slow console frame stops
 * short at each record and lunges after it, so the follower's speed (and
 * its walk/run choice) would pulse. `total` is the summed move, the last
 * facing and the mean speed. The caller resolves collision on the total. */
void c3_follow_advance(C3Follow *follow, const float leader[3], const float position[3],
                       float spacing, float scale, float seconds, C3FollowStep *total);
/* fn_8012DE94's catch-up timer: returns 1 when the follower has been more
 * than 100 units from the leader for 300 frames (the caller then places it
 * on a record and calls c3_follow_caught_up). */
int c3_follow_far(C3Follow *follow, const float leader[3], const float position[3],
                  float scale, float seconds);
void c3_follow_caught_up(C3Follow *follow);

/* fn_8012CA84's animation half, in original units per 60 Hz frame. The
 * follower's travel this frame (capped at one unit) against its PeopleInfo
 * walk and run steps gives the animation amount (fn_801887D8): 0..1 idle to
 * walk, 1..2 walk to run, negated when it backs away from its facing. */
float c3_follow_amount(float travel, float walk, float run);
/* fn_8012C660 blends idle/walk/run by that amount at one constant rate,
 * carrying the frame across a clip change. The port draws one clip: the one
 * with the larger blend weight (walk from 0.2, run from 1.5; idle when
 * backing away, the pack has no back-walk), with a 0.05 hysteresis so a
 * one-frame dip in travel does not flip the clip. Clips: 0 idle, 1 walk,
 * 2 run. */
#define C3_FOLLOW_CLIP_IDLE 0u
#define C3_FOLLOW_CLIP_WALK 1u
#define C3_FOLLOW_CLIP_RUN 2u
unsigned c3_follow_clip(float amount, unsigned current);
/* GSmodelSetAnimFrame after GSmodelSetAnimIndex/SetAnimBlend: the new clip
 * starts at the old clip's phase (time/length, wrapped) times its length. */
float c3_follow_phase(float seconds, float from_length, float to_length);
/* fn_8012CA84 with no step: the follower turns in place only while the
 * leader is outside its neck range (PeopleInfo +0x1C/+0x20, 75 degrees for
 * Wes and Rui), to the yaw that puts the leader on the range's edge, and
 * not for less than 0.1 degree. Returns 1 with that target yaw. */
int c3_follow_idle_turn(float yaw, float to_leader, float limit, float *target);

#endif
