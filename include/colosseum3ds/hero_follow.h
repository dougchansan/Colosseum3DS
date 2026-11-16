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
/* fn_8012DE94's catch-up timer: returns 1 when the follower has been more
 * than 100 units from the leader for 300 frames (the caller then places it
 * on a record and calls c3_follow_caught_up). */
int c3_follow_far(C3Follow *follow, const float leader[3], const float position[3],
                  float scale, float seconds);
void c3_follow_caught_up(C3Follow *follow);

#endif
