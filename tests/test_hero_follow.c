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
    printf("hero follow: %u checks passed\n", checks);
    return 0;
}
