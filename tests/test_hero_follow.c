#include "colosseum3ds/hero_follow.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-3f)

int main(void)
{
    const float scale = 0.1f, spacing = 12.0f * scale, dt = 1.0f / 60.0f;
    C3Follow f;
    C3FollowStep s;
    float leader[3] = {0, 0, 0}, out[3];
    c3_follow_reset(&f);
    /* Records: the first always, then only more than 12 units apart. */
    c3_follow_record(&f, leader, scale);
    CHECK(f.count == 1 && c3_follow_record_at(&f, 0, out) && NEAR(out[0], 0));
    leader[0] = 1.0f; c3_follow_record(&f, leader, scale);
    CHECK(f.count == 1);
    leader[0] = 1.3f; c3_follow_record(&f, leader, scale);
    CHECK(f.count == 2 && c3_follow_record_at(&f, 0, out) && NEAR(out[0], 1.3f) && c3_follow_record_at(&f, 1, out) && NEAR(out[0], 0));
    for (int i = 0; i < 30; ++i) { leader[0] += 1.3f; c3_follow_record(&f, leader, scale); }
    CHECK(f.count == C3_FOLLOW_HISTORY && c3_follow_record_at(&f, 0, out) && NEAR(out[0], leader[0]) && !c3_follow_record_at(&f, 20, out));

    /* No record: straight at the leader, one unit per frame at most. */
    c3_follow_reset(&f);
    float pos[3] = {0, 0, 0}, far_leader[3] = {0, 0, 5.0f};
    c3_follow_step(&f, far_leader, pos, spacing, scale, dt, &s);
    CHECK(NEAR(s.move[0], 0) && NEAR(s.move[1], 0.1f) && NEAR(s.speed, 1.0f) && s.facing[1] > 0);
    /* Just outside the spacing: closes the remaining gap only. */
    float close[3] = {0, 0, spacing + 0.05f};
    c3_follow_step(&f, close, pos, spacing, scale, dt, &s);
    CHECK(NEAR(s.move[1], 0.05f) && NEAR(s.speed, 0.5f));
    /* Between 9 and 12 units: stays. Inside 9: backs away. */
    float between[3] = {0, 0, 1.0f};
    c3_follow_step(&f, between, pos, spacing, scale, dt, &s);
    CHECK(NEAR(s.move[0], 0) && NEAR(s.move[1], 0) && NEAR(s.speed, 0));
    float inside[3] = {0, 0, 0.5f};
    c3_follow_step(&f, inside, pos, spacing, scale, dt, &s);
    CHECK(s.move[1] < 0 && NEAR(s.move[1], -0.5f * 0.1f / 0.9f));

    /* Leader went round a corner: the follower heads for the newest record
     * when the leader is farther than it. */
    c3_follow_reset(&f);
    float corner[3] = {0, 0, 3.0f}, turned[3] = {3.0f, 0, 3.0f};
    c3_follow_record(&f, corner, scale);
    c3_follow_step(&f, turned, pos, spacing, scale, dt, &s);
    CHECK(NEAR(s.move[0], 0) && NEAR(s.move[1], 0.1f));
    /* Close to the record and inside the leader's spacing circle on the way,
     * the step stops on the circle. */
    float near_pos[3] = {2.0f, 0, 2.95f}, rec[3] = {2.5f, 0, 3.0f}, lead2[3] = {3.0f, 0, 3.0f};
    c3_follow_reset(&f); c3_follow_record(&f, rec, scale);
    c3_follow_step(&f, lead2, near_pos, spacing, scale, dt, &s);
    CHECK(hypotf(s.move[0], s.move[1]) <= 0.1f + 1e-4f);

    /* Catch-up: more than 100 units away for 300 frames. */
    c3_follow_reset(&f);
    float lost[3] = {0, 0, 20.0f};
    int fired = 0;
    for (int i = 0; i < 299; ++i) fired |= c3_follow_far(&f, lost, pos, scale, dt);
    CHECK(!fired && c3_follow_far(&f, lost, pos, scale, dt * 2));
    c3_follow_caught_up(&f);
    CHECK(f.far_seconds == 0 && !c3_follow_far(&f, close, pos, scale, dt));
    /* fn_801887D8: travel against walk and run steps. */
    CHECK(NEAR(c3_follow_amount(0, 0.45f, 0.65f), 0) && NEAR(c3_follow_amount(0.225f, 0.45f, 0.65f), 0.5f));
    CHECK(NEAR(c3_follow_amount(0.45f, 0.45f, 0.65f), 1) && NEAR(c3_follow_amount(0.55f, 0.45f, 0.65f), 1.5f));
    CHECK(NEAR(c3_follow_amount(0.9f, 0.45f, 0.65f), 2) && NEAR(c3_follow_amount(3, 0.45f, 0.65f), 2));
    /* Dominant blend clip with hysteresis; backing away stays idle. */
    CHECK(c3_follow_clip(0.22f, C3_FOLLOW_CLIP_IDLE) == C3_FOLLOW_CLIP_IDLE);
    CHECK(c3_follow_clip(0.26f, C3_FOLLOW_CLIP_IDLE) == C3_FOLLOW_CLIP_WALK);
    CHECK(c3_follow_clip(0.18f, C3_FOLLOW_CLIP_WALK) == C3_FOLLOW_CLIP_WALK);
    CHECK(c3_follow_clip(0.1f, C3_FOLLOW_CLIP_WALK) == C3_FOLLOW_CLIP_IDLE);
    CHECK(c3_follow_clip(1.52f, C3_FOLLOW_CLIP_WALK) == C3_FOLLOW_CLIP_WALK);
    CHECK(c3_follow_clip(1.6f, C3_FOLLOW_CLIP_WALK) == C3_FOLLOW_CLIP_RUN);
    CHECK(c3_follow_clip(1.48f, C3_FOLLOW_CLIP_RUN) == C3_FOLLOW_CLIP_RUN);
    CHECK(c3_follow_clip(-1, C3_FOLLOW_CLIP_WALK) == C3_FOLLOW_CLIP_IDLE);
    /* Phase carry: 0.25 of a 1 s walk is 0.25 of a 0.8 s run. */
    CHECK(NEAR(c3_follow_phase(1.25f, 1.0f, 0.8f), 0.2f) && c3_follow_phase(0.3f, 0, 1) == 0);
    /* In-place turn: only outside +-75 degrees, to the range's edge. */
    const float deg = 0.017453292f;
    float goal;
    CHECK(!c3_follow_idle_turn(0, 60 * deg, 75 * deg, &goal));
    CHECK(c3_follow_idle_turn(0, 100 * deg, 75 * deg, &goal) && NEAR(goal, 25 * deg));
    CHECK(c3_follow_idle_turn(0, -120 * deg, 75 * deg, &goal) && NEAR(goal, -45 * deg));
    CHECK(!c3_follow_idle_turn(25 * deg, 100.05f * deg, 75 * deg, &goal));

    /* A walk at the console's ~17 fps (jittered frame times): east, a right
     * angle north, a stop, back towards the follower, a stop. The follower's
     * clip changes only where the motion does, its animation clock never
     * restarts, and no frame moves it more than its one-unit-per-frame
     * budget (no snap or teleport). */
    {
        const float walk = 0.045f, run = 0.065f; /* port units per 60 Hz frame */
        const float times[3] = {1.0f / 17.0f, 1.0f / 18.0f, 1.0f / 15.5f};
        float lead[3] = {0, 0, 0}, me[3] = {-spacing, 0, 0}, clock = 0, t = 0; /* behind him (partner_place_behind) */
        unsigned clip = C3_FOLLOW_CLIP_IDLE, changes = 0, frames = 0, idle_frames = 0;
        float yaw = 0;
        c3_follow_reset(&f);
        c3_follow_record(&f, lead, scale);
        for (; t < 9.0f; ++frames) {
            float dt = times[frames % 3], vx = 0, vz = 0;
            if (t < 2.0f) vx = 1;            /* east */
            else if (t < 4.0f) vz = 1;       /* north */
            else if (t < 5.0f) ;             /* stop */
            else if (t < 5.6f) vz = -1;      /* back towards her */
            lead[0] += vx * walk * dt * 60; lead[2] += vz * walk * dt * 60;
            c3_follow_advance(&f, lead, me, spacing, scale, dt, &s);
            float moved = hypotf(s.move[0], s.move[1]), fr = dt * 60, amount = 0;
            CHECK(moved <= fr * scale + 1e-4f);
            me[0] += s.move[0]; me[2] += s.move[1];
            if (moved > 0) {
                float speed = fminf(moved / scale / fr, 1.0f);
                amount = c3_follow_amount(speed, walk / scale, run / scale);
                if (s.move[0] * s.facing[0] + s.move[1] * s.facing[1] < 0) amount = -amount;
                if (s.facing[0] != 0 || s.facing[1] != 0) {
                    /* fn_80184A90: nine degrees a tick at the turn speed. */
                    float to = atan2f(s.facing[0], s.facing[1]), d = fmodf(to - yaw + 9.42477796f, 6.2831853f) - 3.14159265f;
                    float limit = 0.15707964f * speed * fr;
                    CHECK(fabsf(d) < 3.1416f);
                    yaw += fabsf(d) <= limit ? d : (d > 0 ? limit : -limit);
                }
            }
            if (getenv("C3_FOLLOW_TRACE"))
                printf("follow_frame t=%.3f moved=%.4f amount=%.3f clip=%u clock=%.3f gap=%.3f\n", t, moved, amount, clip,
                       clock, hypotf(lead[0] - me[0], lead[2] - me[2]));
            unsigned next = c3_follow_clip(amount, clip);
            if (next != clip) {
                float before = clock;
                clock = c3_follow_phase(clock, 1.0f, next == C3_FOLLOW_CLIP_RUN ? 0.7f : next == C3_FOLLOW_CLIP_WALK ? 1.0f : 2.0f);
                CHECK(clock >= 0 && (before == 0 || clock > 0));
                clip = next; ++changes;
            }
            clock += dt;
            idle_frames += clip == C3_FOLLOW_CLIP_IDLE;
            t += dt;
            /* Steady stretches hold one clip: walking mid-leg, idle when stopped. */
            if ((t > 0.8f && t < 1.9f) || (t > 2.6f && t < 3.9f)) CHECK(clip == C3_FOLLOW_CLIP_WALK);
            if (t > 4.6f && t < 5.0f) CHECK(clip == C3_FOLLOW_CLIP_IDLE);
            if (t > 7.0f) CHECK(clip == C3_FOLLOW_CLIP_IDLE);
        }
        float gap = hypotf(lead[0] - me[0], lead[2] - me[2]);
        printf("follow walk: frames=%u clip_changes=%u idle_frames=%u gap=%.3f yaw=%.3f\n", frames, changes, idle_frames, gap, yaw);
        CHECK(changes <= 6 && gap > 0.85f && gap < spacing + 0.05f);
    }
    printf("hero follow: %u checks passed\n", checks);
    return 0;
}
