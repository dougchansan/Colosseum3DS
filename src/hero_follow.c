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

int c3_follow_far(C3Follow *f, const float leader[3], const float pos[3], float scale, float seconds)
{
    if (!f) return 0;
    if (hypotf(leader[0] - pos[0], leader[2] - pos[2]) < 100.0f * scale) { f->far_seconds = 0; return 0; }
    f->far_seconds += seconds;
    return f->far_seconds >= 5.0f; /* 300 frames */
}

void c3_follow_caught_up(C3Follow *f) { if (f) f->far_seconds = 0; }
