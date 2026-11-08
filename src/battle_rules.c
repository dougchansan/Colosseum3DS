#include "colosseum3ds/battle_rules.h"
#include "dolphin/types.h"
#include <stddef.h>
#include <stdatomic.h>

/* All imported global services belong to this guarded, short-lived context.
 * Handle 1 is the sole floor token; it is never a converted host address. */
static atomic_flag in_use = ATOMIC_FLAG_INIT;
static struct {
    u16 result;
    u8 draw_enabled;
    u8 hero_host;
    int error;
    C3BattleRng rng;
} context;

static u16 c3r_src_fn_801EF634(void) { return context.result; }
static void c3r_src_fn_801EF62C(u16 value) { context.result = value; }
static u32 c3r_src_fightFloorGetStatus(u32 floor, u32 id, u32 field, u16 index)
{
    if (floor != 1 || id || field != 0x23 || index) context.error = 1;
    return context.draw_enabled;
}
static u8 c3r_src_fightFloorIsGcHeroHostSide(u32 floor)
{
    if (floor != 1) context.error = 1;
    return context.hero_host;
}
static u32 c3r_src_fn_801ADCD8(void)
{
    uint16_t value = 0;
    if (!c3_battle_rng_next16(&context.rng, &value)) context.error = 1;
    return value;
}

#include "battle_rules_original.inc"

static int overlaps(const void *first, size_t first_size,
                    const void *second, size_t second_size)
{
    uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
    if (!first_size || !second_size) return 0;
    return a >= b ? a - b < second_size : b - a < first_size;
}

static int acquire(void)
{
    if (atomic_flag_test_and_set_explicit(&in_use, memory_order_acquire)) return 0;
    context.error = 0;
    return 1;
}

static void release(void)
{
    atomic_flag_clear_explicit(&in_use, memory_order_release);
}

int c3_battle_rules_combine_result(uint16_t current, uint16_t requested,
                                 uint8_t draw_enabled, C3BattleRuleResult *out)
{
    C3BattleRuleResult result = {0};
    if (!out || current > 7 || requested > 7 || draw_enabled > 1 || !acquire()) return 0;
    context.result = current;
    context.draw_enabled = draw_enabled;
    result.accepted = c3r_src_fightFloorSetFightResultId(1, requested);
    result.result = context.result;
    int ok = !context.error && result.result <= 7 && result.accepted <= 1;
    release();
    if (ok) *out = result;
    return ok;
}

int c3_battle_rules_hero_win(uint16_t result, uint8_t hero_is_host_side,
                            uint8_t *out)
{
    if (!out || result > 7 || hero_is_host_side > 1 || !acquire()) return 0;
    context.hero_host = hero_is_host_side;
    u32 value = c3r_src_fightFloorIsGcHeroWin(1, result);
    int ok = !context.error && value <= 1;
    release();
    if (ok) *out = (uint8_t)value;
    return ok;
}

int c3_battle_rules_ai_argmax(const int32_t *values, uint16_t count,
                             uint8_t use_random, C3BattleRng *rng, int32_t *out)
{
    C3BattleRngSnapshot snapshot;
    int32_t copied[16] = {0};
    size_t bytes = (size_t)count * sizeof(*values);
    if (!rng || !out || count > 16 || (count && !values) ||
        overlaps(rng, sizeof(*rng), out, sizeof(*out)) ||
        overlaps(values, bytes, out, sizeof(*out)) ||
        overlaps(values, bytes, rng, sizeof(*rng)) || !acquire()) return 0;
    /* Snapshot and restore keep the externally owned stream unchanged even
     * when a source service rejects after it has started executing. */
    int ok = c3_battle_rng_snapshot(rng, &snapshot) &&
             c3_battle_rng_restore(&context.rng, &snapshot);
    for (uint16_t i = 0; i < count; ++i) copied[i] = values[i];
    s32 value = -1;
    if (ok) value = c3r_src_fightTrainerAiGetValueAryMaxBanme(copied, count, use_random);
    ok = ok && !context.error && value >= -1 && value < (s32)count;
    if (ok) {
        ok = c3_battle_rng_snapshot(&context.rng, &snapshot) &&
             c3_battle_rng_restore(rng, &snapshot);
        if (ok) *out = value;
    }
    release();
    return ok;
}
