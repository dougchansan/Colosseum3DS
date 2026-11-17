#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/move_forget.h"
#include "colosseum3ds/pda.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The forget-a-move prompt: the engine asks the bound prompt when a level-up
 * move finds four moves known (battle writeback and _expRecover), the
 * evolutionWazaLearn state machine, and the original message renderer.
 * Optional private argument: a prepared pda.c3pd, whose five original
 * messages must render. */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[256];
#include "battle_pack_fixture.h"

static int answer_slot = -1, asked = 0;
static uint16_t asked_move;
static int prompt(void *context, const C3BattlePack *pack, const uint8_t record[C3_GB_POKEMON_SIZE], uint16_t move)
{
    (void)context; (void)pack; (void)record;
    ++asked; asked_move = move;
    return answer_slot;
}

/* A level-5 HERO with four moves (TACKLE, 3, 4, 5) one experience point short of level 6, where it learns GROWL (2). */
static void four_moves(uint8_t party[6][0x138])
{
    make_party(party, 1, 5, 1, 0); p32(party[0] + 0x5c, 215);
    for (unsigned k = 0; k < 4; ++k) { p16(party[0] + 0x78 + 4 * k, k ? k + 2 : 1); party[0][0x7a + 4 * k] = 5; }
}

/* UTF-16BE messages for the renderer. */
static uint8_t msgs[6][64]; static size_t msg_size[6];
static const uint32_t msg_id[6] = {C3_MF_MSG_ASK_FORGET, C3_MF_MSG_ASK_GIVE_UP, C3_MF_MSG_DID_NOT_LEARN,
                                    C3_MF_MSG_FORGOT, C3_MF_MSG_LEARNED, 0x9998};
/* "{" xx = control xx (hex pair); controls 07 and 09 take the next spec character as its one raw argument byte. */
static void put_msg(unsigned i, const char *spec)
{
    size_t n = 0;
    for (const char *c = spec; *c; ++c) {
        if (*c == '{') {
            unsigned v; sscanf(c + 1, "%2x", &v); msgs[i][n++] = 0xff; msgs[i][n++] = 0xff; msgs[i][n++] = (uint8_t)v; c += 2;
            if ((v == 7 || v == 9) && c[1]) msgs[i][n++] = (uint8_t)*++c;
            continue;
        }
        msgs[i][n++] = 0; msgs[i][n++] = (uint8_t)*c;
    }
    msgs[i][n++] = 0; msgs[i][n++] = 0; msg_size[i] = n;
}
static const uint8_t *lookup(void *context, uint32_t id, size_t *size)
{
    (void)context;
    for (unsigned i = 0; i < 6; ++i) if (msg_id[i] == id) { *size = msg_size[i]; return msgs[i]; }
    return NULL;
}
static const uint8_t *pda_lookup(void *context, uint32_t id, size_t *size)
{ return c3_pda_entry(context, 1, id, size); }

int main(int argc, char **argv)
{
    size_t n; uint8_t *bytes = make_pack(2, 3, &n);
    C3BattlePack pack = {0};
    CHECK(c3_battle_pack_decode(&pack, bytes, n, error));
    uint8_t party[6][0x138], copy[6][0x138];
    static C3GeneralBattle b;
    C3GeneralRewards rw;

    /* No prompt bound: GROWL is skipped (the old behaviour). */
    four_moves(party);
    CHECK(c3_general_battle_begin(&b, &pack, 0, party, 3, error));
    while (b.outcome == C3_GB_RUNNING) {
        C3GeneralCommand c[2] = {{0}};
        for (unsigned a = 0; a < b.active_count; ++a)
            if (b.active[0][a] != 255 && b.party[0][b.active[0][a]].hp) CHECK(c3_general_battle_choose(&b, &pack, a, &c[a]));
        CHECK(c3_general_battle_turn(&b, &pack, c, error));
    }
    if (b.outcome != C3_GB_WON) { fprintf(stderr, "seed lost\n"); return 1; }
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw) && copy[0][0x60] >= 6);
    CHECK(rw.skipped_count[0] == 1 && rw.skipped[0][0] == 2 && !rw.learned_count[0]);

    /* Prompt: forget slot 2 (move 4); GROWL goes there with full PP. */
    c3_general_set_forget_prompt(prompt, NULL); answer_slot = 2; asked = 0;
    memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw));
    CHECK(asked == 1 && asked_move == 2);
    CHECK(rw.learned_count[0] == 1 && rw.learned[0][0] == 2 && rw.forgot[0][0] == 4 && !rw.skipped_count[0]);
    CHECK(g16(copy[0] + 0x80) == 2 && copy[0][0x82] == 40 && copy[0][0x83] == 0 && g16(copy[0] + 0x78) == 1);

    /* Giving up (-1) and an HM slot both leave the moves as they were. */
    answer_slot = -1; memcpy(copy, party, sizeof copy);
    CHECK(c3_general_battle_writeback(&b, &pack, copy, &rw) && rw.skipped_count[0] == 1 && g16(copy[0] + 0x80) == 4);
    {   C3BattlePack k = pack; k.bytes = malloc(pack.size); CHECK(k.bytes); memcpy(k.bytes, pack.bytes, pack.size);
        k.bytes[128 + 4 * 56 + 0x12] = 1;  /* move 4 is an HM */
        CHECK(c3_battle_pack_move_is_hm(&k, 4) && !c3_battle_pack_move_is_hm(&k, 1));
        answer_slot = 2; memcpy(copy, party, sizeof copy);
        CHECK(c3_general_battle_writeback(&b, &k, copy, &rw) && rw.skipped_count[0] == 1 && g16(copy[0] + 0x80) == 4);
        /* The state machine refuses the HM slot and stays on the choice. */
        C3MoveForget f; c3_move_forget_begin(&f, &k, party[0], 2);
        CHECK(c3_move_forget_answer(&f, 1) == C3_MF_CHOOSE);
        CHECK(c3_move_forget_answer(&f, 2) == C3_MF_CHOOSE && f.hm_refused);
        CHECK(c3_move_forget_answer(&f, 1) == C3_MF_FORGOT && !f.hm_refused && f.slot == 1 && f.forgotten == 3);
        free(k.bytes);
    }

    /* _expRecover asks the same prompt: purification's banked experience. */
    {   uint8_t r[0x138]; C3ExpRecovery o;
        four_moves(party); memcpy(r, party[0], sizeof r);
        answer_slot = 0; asked = 0;
        CHECK(c3_battle_pack_recover_exp(&pack, r, 10, &o) && o.level_up && asked == 1);
        CHECK(o.learned_count == 1 && o.learned[0] == 2 && o.forgot[0] == 1 && g16(r + 0x78) == 2);
    }
    c3_general_set_forget_prompt(NULL, NULL);

    /* evolutionWazaLearn's order: no -> give up? no -> forget? yes -> cancel -> give up? yes -> did not learn. */
    {   C3MoveForget f; uint16_t m;
        four_moves(party); c3_move_forget_begin(&f, &pack, party[0], 2);
        CHECK(c3_move_forget_message(&f, &m) == C3_MF_MSG_ASK_FORGET && m == 2);
        CHECK(c3_move_forget_answer(&f, 0) == C3_MF_ASK_GIVE_UP && c3_move_forget_message(&f, &m) == C3_MF_MSG_ASK_GIVE_UP);
        CHECK(c3_move_forget_answer(&f, 0) == C3_MF_ASK_FORGET);
        CHECK(c3_move_forget_answer(&f, 1) == C3_MF_CHOOSE && c3_move_forget_message(&f, &m) == 0);
        CHECK(c3_move_forget_answer(&f, 4) == C3_MF_ASK_GIVE_UP);
        CHECK(c3_move_forget_answer(&f, 1) == C3_MF_DID_NOT_LEARN && c3_move_forget_message(&f, &m) == C3_MF_MSG_DID_NOT_LEARN && m == 2);
        CHECK(c3_move_forget_answer(&f, 0) == C3_MF_DONE && f.slot == -1);
        /* yes -> slot 3 -> forgot (old move) -> learned (new move). */
        c3_move_forget_begin(&f, &pack, party[0], 2);
        CHECK(c3_move_forget_answer(&f, 1) == C3_MF_CHOOSE && c3_move_forget_answer(&f, 3) == C3_MF_FORGOT);
        CHECK(c3_move_forget_message(&f, &m) == C3_MF_MSG_FORGOT && m == 5);
        CHECK(c3_move_forget_answer(&f, 0) == C3_MF_LEARNED && c3_move_forget_message(&f, &m) == C3_MF_MSG_LEARNED && m == 2);
        CHECK(c3_move_forget_answer(&f, 0) == C3_MF_DONE && f.slot == 3);
    }

    /* The renderer: nickname, move; presentation controls and the sound cue print nothing. */
    {   char out[256]; int sound;
        put_msg(0, "{07A{32 wants {39.{03Forget?");
        put_msg(3, "{09x{5D{5E{32 forgot {39.");
        CHECK(c3_move_forget_text(lookup, NULL, C3_MF_MSG_ASK_FORGET, "ZUBAT", "GROWL", out, sizeof out, &sound) && !sound);
        CHECK(!strcmp(out, "ZUBAT wants GROWL." "\n\n" "Forget?"));
        CHECK(c3_move_forget_text(lookup, NULL, C3_MF_MSG_FORGOT, "ZUBAT", "TACKLE", out, sizeof out, &sound) && sound);
        CHECK(!strcmp(out, "ZUBAT forgot TACKLE."));
        put_msg(1, "{40bad");
        CHECK(!c3_move_forget_text(lookup, NULL, C3_MF_MSG_ASK_GIVE_UP, "Z", "G", out, sizeof out, NULL) && !out[0]);
        CHECK(!c3_move_forget_text(lookup, NULL, 0x9999, "Z", "G", out, sizeof out, NULL));
    }

    if (argc > 1) {   /* private: the prepared PDA pack's original messages */
        FILE *fp = fopen(argv[1], "rb"); CHECK(fp);
        fseek(fp, 0, SEEK_END); long len = ftell(fp); fseek(fp, 0, SEEK_SET);
        uint8_t *data = malloc((size_t)len); CHECK(data && fread(data, 1, (size_t)len, fp) == (size_t)len); fclose(fp);
        C3PdaAssets assets; CHECK(c3_pda_assets_open(&assets, data, (size_t)len));
        for (unsigned i = 0; i < 5; ++i) {
            char out[512];
            int sound;
            CHECK(c3_move_forget_text(pda_lookup, &assets, msg_id[i], "ZUBAT", "GROWL", out, sizeof out, &sound) && strlen(out) > 8);
            CHECK(sound == (msg_id[i] == C3_MF_MSG_FORGOT));
            CHECK(strstr(out, "GROWL") || msg_id[i] == C3_MF_MSG_ASK_GIVE_UP || msg_id[i] == C3_MF_MSG_FORGOT);
        }
        free(data);
        puts("move-forget: the five original messages render from the PDA pack");
    } else puts("move-forget: private PDA messages skipped (needs <pda.c3pd>)");
    c3_battle_pack_free(&pack); free(bytes);
    puts("move-forget: prompt hook, HM refusal, _expRecover, flow order and renderer ok");
    return 0;
}
