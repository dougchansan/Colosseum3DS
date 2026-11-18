#include "colosseum3ds/hero_follow.h"

#include <math.h>
#include <string.h>

void c3_follow_reset(C3Follow *f) { if (f) memset(f, 0, sizeof *f); }

int c3_follow_record_at(const C3Follow *f, unsigned i, float out[3])
{
    /* getLeaderLog: newest first, ring wrap. */
    if (!f || i >= f->count || i >= C3_FOLLOW_HISTORY) return 0;
    int index = (int)f->head - (int)i - 1;
    if (index < 0) index += (int)C3_FOLLOW_HISTORY;
    memcpy(out, f->history[index], sizeof f->history[index]);
    return 1;
}

void c3_follow_record(C3Follow *f, const float leader[3], float scale)
{
    float last[3];
    if (!f || !leader) return;
    memcpy(f->leader, leader, sizeof f->leader);
    f->has_leader = 1;
    if (c3_follow_record_at(f, 0, last) && hypotf(last[0] - leader[0], last[2] - leader[2]) <= 12.0f * scale) return;
    memcpy(f->history[f->head], leader, sizeof f->history[f->head]);
    if (++f->head >= C3_FOLLOW_HISTORY) f->head = 0;
    if (f->count < C3_FOLLOW_HISTORY) ++f->count;
}

/* fn_8012D39C: where the line start->end meets the circle (center, radius),
 * the crossing nearer to reference. Returns 1 with the point, 0 none. */
static int circle_hit(const float start[3], const float end[3], const float center[3], const float reference[3],
                      float radius, float out[2])
{
    float a = end[2] - start[2], b = start[0] - end[0], c = -(a * start[0] + b * start[2]);
    float n = a * a + b * b;
    if (n < 1e-12f) return 0; /* heroMoveGetLine: no line */
    float scale = 1.0f / n, ox = a * (-c * scale), oz = b * (-c * scale), len = sqrtf(scale);
    float dx = b * len, dz = -a * len, l2 = dx * dx + dz * dz;
    if (l2 < 1e-7f) return 0;
    float rx = center[0] - ox, rz = center[2] - oz, cross = dx * rz - dz * rx;
    float disc = l2 * radius * radius - cross * cross;
    if (disc < -1e-7f) return 0;
    float projection = dx * rx + dz * rz;
    if (disc < 1e-7f) { float t = projection / l2; out[0] = dx * t + ox; out[1] = dz * t + oz; return 1; }
    float root = sqrtf(disc), inv = 1.0f / l2;
    float t0 = inv * (projection - root), t1 = inv * (projection + root);
    float nx = dx * t0 + ox, nz = dz * t0 + oz, fx = dx * t1 + ox, fz = dz * t1 + oz;
    float dn = (reference[0] - nx) * (reference[0] - nx) + (reference[2] - nz) * (reference[2] - nz);
    float df = (reference[0] - fx) * (reference[0] - fx) + (reference[2] - fz) * (reference[2] - fz);
    if (dn < df) { out[0] = nx; out[1] = nz; } else { out[0] = fx; out[1] = fz; }
    return 1;
}

void c3_follow_step(const C3Follow *f, const float leader[3], const float pos[3], float spacing, float scale,
                    float seconds, C3FollowStep *step)
{
    float record[3], frames = seconds * 60.0f * scale; /* one original unit per frame */
    memset(step, 0, sizeof *step);
    if (!f || !leader || !pos || seconds <= 0) return;
    int has = c3_follow_record_at(f, 0, record);
    float dx = leader[0] - pos[0], dz = leader[2] - pos[2], distance = hypotf(dx, dz);
    float record_distance = has ? hypotf(pos[0] - record[0], pos[2] - record[2]) : 0.0f;
    float eps = 0.010000001f * scale, back = 9.0f * scale;
    if (!has || distance <= record_distance || distance <= eps + spacing) {
        float k = 0.0f;
        if (distance > eps + spacing) {
            float amount = distance - spacing;
            if (amount > frames) amount = frames;
            k = amount / distance;
        } else if (distance < back) {
            float amount = back - distance;
            if (amount > frames) amount = frames;
            k = -amount / back;
        }
        step->move[0] = dx * k; step->move[1] = dz * k;
        step->facing[0] = dx; step->facing[1] = dz;
    } else {
        float rx = record[0] - pos[0], rz = record[2] - pos[2], length = hypotf(rx, rz);
        float sx = rx, sz = rz;
        if (length > frames) { sx = rx * frames / length; sz = rz * frames / length; }
        float next[3] = {pos[0] + sx, pos[1], pos[2] + sz}, hit[2];
        if (circle_hit(pos, next, leader, pos, spacing, hit)) {
            float segx = next[0] - pos[0], segz = next[2] - pos[2], l2 = segx * segx + segz * segz, t = 0.0f;
            if (l2 > 0.0f) t = (segx * (hit[0] - pos[0]) + segz * (hit[1] - pos[2])) / l2;
            if (t >= 0.0f && t <= 1.0f) { sx = hit[0] - pos[0]; sz = hit[1] - pos[2]; }
        }
        step->move[0] = sx; step->move[1] = sz;
        step->facing[0] = sx; step->facing[1] = sz;
    }
    /* fn_8012CA84: the animation amount is the step over the frame budget. */
    float length = hypotf(step->move[0], step->move[1]);
    step->speed = frames > 0 ? length / frames : 0.0f;
    if (step->speed > 1.0f) step->speed = 1.0f;
    if (hypotf(step->facing[0], step->facing[1]) <= eps) step->facing[0] = step->facing[1] = 0.0f;
}

void c3_follow_advance(C3Follow *f, const float leader[3], const float position[3], float spacing, float scale,
                       float seconds, C3FollowStep *total)
{
    memset(total, 0, sizeof *total);
    if (!f || !leader || !position || !(seconds > 0) || !(scale > 0)) return;
    float from[3], pos[3];
    memcpy(from, f->has_leader ? f->leader : leader, sizeof from);
    memcpy(pos, position, sizeof pos);
    unsigned n = (unsigned)ceilf(seconds * 60.0f - 0.001f);
    if (n < 1) n = 1;
    if (n > 60) n = 60;
    float dt = seconds / (float)n;
    for (unsigned i = 1; i <= n; ++i) {
        float k = (float)i / (float)n, at[3];
        for (unsigned j = 0; j < 3; ++j) at[j] = from[j] + (leader[j] - from[j]) * k;
        C3FollowStep step;
        c3_follow_step(f, at, pos, spacing, scale, dt, &step);
        pos[0] += step.move[0]; pos[2] += step.move[1];
        total->move[0] += step.move[0]; total->move[1] += step.move[1];
        if (step.facing[0] != 0 || step.facing[1] != 0) { total->facing[0] = step.facing[0]; total->facing[1] = step.facing[1]; }
        c3_follow_record(f, at, scale);
    }
    float budget = seconds * 60.0f * scale;
    total->speed = hypotf(total->move[0], total->move[1]) / budget;
    if (total->speed > 1.0f) total->speed = 1.0f;
}

int c3_follow_far(C3Follow *f, const float leader[3], const float pos[3], float scale, float seconds)
{
    if (!f) return 0;
    if (hypotf(leader[0] - pos[0], leader[2] - pos[2]) < 100.0f * scale) { f->far_seconds = 0; return 0; }
    f->far_seconds += seconds;
    return f->far_seconds >= 5.0f; /* 300 frames */
}

void c3_follow_caught_up(C3Follow *f) { if (f) f->far_seconds = 0; }

float c3_follow_amount(float travel, float walk, float run)
{
    if (!(travel > 0.0f) || !(walk > 0.0f)) return 0.0f;
    if (travel > 1.0f) travel = 1.0f;
    if (run > walk && travel >= run) return 2.0f;
    if (run > walk && travel >= walk) return 1.0f + (travel - walk) / (run - walk);
    return travel >= walk ? 1.0f : travel / walk;
}

unsigned c3_follow_clip(float amount, unsigned current)
{
    const float h = 0.05f;
    float walk = current == C3_FOLLOW_CLIP_IDLE ? 0.2f + h : 0.2f - h;
    float run = current == C3_FOLLOW_CLIP_RUN ? 1.5f - h : 1.5f + h;
    if (!(amount >= walk)) return C3_FOLLOW_CLIP_IDLE;
    return amount >= run ? C3_FOLLOW_CLIP_RUN : C3_FOLLOW_CLIP_WALK;
}

unsigned c3_follow_choose(C3Follow *f, float amount, unsigned current, float seconds)
{
    unsigned next = c3_follow_clip(amount, current);
    if (!f) return next;
    if (seconds > 0) f->clip_held += seconds;
    if (next != current && next != C3_FOLLOW_CLIP_IDLE && current != C3_FOLLOW_CLIP_IDLE &&
        f->clip_held < C3_FOLLOW_GAIT_HOLD) return current;
    if (next != current) f->clip_held = 0;
    return next;
}

float c3_follow_phase(float seconds, float from_length, float to_length)
{
    if (!(from_length > 0.0f) || !(to_length > 0.0f) || !isfinite(seconds) || seconds <= 0.0f) return 0.0f;
    float phase = fmodf(seconds / from_length, 1.0f);
    return phase * to_length;
}

static float wrap_pi(float a)
{
    a = fmodf(a, 6.2831855f);
    if (a < -3.1415927f) a += 6.2831855f;
    else if (a > 3.1415927f) a -= 6.2831855f;
    return a;
}

int c3_follow_idle_turn(float yaw, float to_leader, float limit, float *target)
{
    float diff = wrap_pi(to_leader - yaw), goal;
    if (diff > limit) goal = to_leader - limit;
    else if (diff < -limit) goal = to_leader + limit;
    else return 0;
    if (fabsf(wrap_pi(goal - yaw)) < 0.0017453289f) return 0;
    if (target) *target = goal;
    return 1;
}
