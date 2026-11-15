/* Kind-4 effect nodes other than type 0 (colour keys), included by
 * battle_general_wzx.inc. Types on disc: 12 distortion, 11 billboard /
 * banner (+DAT), 8 owner shine, 9 owner blur / after-image, 5 leaf clones of
 * a DAT, 1 lightning. A node's DAT (types 5/11) is loaded as
 * r->sequence->node_model[i] (a stage model, slot WZX_EFFECT_SLOT).
 * start: 1 started (0 skipped); update: 1 running, 0 done, -1 the node's
 * duration mark; frame: once per stage step after the nodes ran. */
static int wzx_effect_start(C3BattleGeneralStage *s, WzxRun *r, uint32_t i, const C3WzxNode *n)
{ (void)s; (void)r; (void)i; (void)n; return 1; }
static int wzx_effect_update(C3BattleGeneralStage *s, WzxRun *r, uint32_t i, const C3WzxNode *n, uint32_t ticks, int32_t local)
{ (void)s; (void)r; (void)i; (void)n; (void)ticks; (void)local; return -1; }
static void wzx_effect_stop(C3BattleGeneralStage *s, WzxRun *r, uint32_t i, const C3WzxNode *n, int forced)
{ (void)s; (void)r; (void)i; (void)n; (void)forced; }
static void wzx_effect_frame(C3BattleGeneralStage *s) { (void)s; }
