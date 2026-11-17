#!/bin/bash
# PRIVATE-DATA check: every region's chapter route triggers its story
# battles and plays each one on the general battle engine with the original
# trainer and team (tests/route_battles.h). Nothing is skipped as a win;
# encounter 525 also plays its original CPU-vs-CPU rosters with pack v3.
#
# Build first: make build/test_phenac_events build/test_story_route build/test_story_crawl
# Inputs (all private, never committed):
#   C3_ROUTE_BATTLES   battles.c3x (tools/prepare_battle_encounters.py, pack v3)
#   C3_ROUTE_PDA       pda.c3pd (tools/prepare_pda.py)
#   NEWGAME            directory holding newgame.c3g
#   PHENAC_PACKS PHENAC_SAVES                 test_phenac_events inputs
#   WORLD_PACKS SAVE_3641 AGATE_ROUTE         Agate route (docs/agate-story.md)
#   PYRITE_PACKS PYRITE_START                 tests/routes/pyrite.route
#   CIPHER_PACKS CIPHER_SAVE_3641 CIPHER_SAVE_5007   Shadow Lab, Pyrite, Snagem crawls
#   UNDER_BOOT UNDER_SAVE                     tests/routes/under-subway.route
#   OUT                output directory (default private/story-battles)
# Regions whose inputs are unset are reported as skipped.
set -u
OUT="${OUT:-private/story-battles}"
mkdir -p "$OUT/saves"
: > "$OUT/status"
export C3_ROUTE_BATTLES C3_ROUTE_PDA
: "${C3_ROUTE_BATTLES:?set C3_ROUTE_BATTLES to battles.c3x}"
run() { local name=$1; shift; "$@" > "$OUT/$name.log" 2>&1; local rc=$?; printf '%s %s\n' "$rc" "$name" | tee -a "$OUT/status"; }
skip() { printf 'skip %s (inputs unset)\n' "$1" | tee -a "$OUT/status"; }

# Phenac: test_phenac_events checks each event's encounter list itself.
if [ -n "${PHENAC_PACKS:-}" ] && [ -n "${PHENAC_SAVES:-}" ]; then
  C3_ROUTE_EXPECT_BATTLES=26,15,17,20,21,22,23,24,25,36,16,19,532,533,534 \
    run phenac ./build/test_phenac_events "$NEWGAME" "$PHENAC_PACKS" "$PHENAC_SAVES" "$OUT/phenac.trace"
else skip phenac; fi

# Agate and Mt. Battle: Relic Cave 195-197, Relic Forest 525 and 543, Mt. Battle 544.
if [ -n "${WORLD_PACKS:-}" ] && [ -n "${SAVE_3641:-}" ] && [ -n "${AGATE_ROUTE:-}" ]; then
  C3_ROUTE_EXPECT_BATTLES=195,196,197,525,543,544 \
    run agate ./build/test_story_route "$NEWGAME" "$WORLD_PACKS" "$SAVE_3641" "$AGATE_ROUTE" "$OUT/agate.report" "$OUT/agate.trace"
else skip agate; fi

# Pyrite: colosseum 169-172, 101, 126, 129, Miror B. 164 (expect_battle lines in the route).
if [ -n "${PYRITE_PACKS:-}" ] && [ -n "${PYRITE_START:-}" ]; then
  run pyrite ./build/test_story_route "$NEWGAME" "$PYRITE_PACKS" "$PYRITE_START" tests/routes/pyrite.route "$OUT/pyrite.report" "$OUT/pyrite.trace"
else skip pyrite; fi

# Shadow Pokemon Lab (D1), Pyrite revisit and Snagem Hideout (S2) crawls.
if [ -n "${CIPHER_PACKS:-}" ] && [ -n "${CIPHER_SAVE_3641:-}" ] && [ -n "${CIPHER_SAVE_5007:-}" ]; then
  C3_ROUTE_EXPECT_BATTLES=381,382,383,386,389,397,401,405,398,402,392,200,394,393 \
    run cipher-lab ./build/test_story_crawl "$NEWGAME" "$CIPHER_PACKS" "$CIPHER_SAVE_3641" 41 0 "$OUT/lab.report" "$OUT/saves/lab-after.gci" 900
  run cipher-pyrite ./build/test_story_crawl "$NEWGAME" "$CIPHER_PACKS" "$CIPHER_SAVE_5007" f 0 "$OUT/cipher-pyrite.report" "$OUT/saves/pyrite-after.gci" 400
  C3_CRAWL_FLAGS=a7c=1 C3_ROUTE_EXPECT_BATTLES=475 \
    run cipher-snagem ./build/test_story_crawl "$NEWGAME" "$CIPHER_PACKS" "$OUT/saves/pyrite-after.gci" 88 0 "$OUT/snagem.report" "$OUT/saves/snagem-after.gci" 1500
else skip cipher; fi

# The Under: Venus 328, then the four subway-platform trainers 333, 336, 339, 342.
# Continue through the actual key pickups, both train rides and Pyrite elevator.
if [ -n "${UNDER_BOOT:-}" ] && [ -n "${UNDER_SAVE:-}" ]; then
  C3_ROUTE_EXPECT_BATTLES=328,333,336,339,342 \
    run under ./build/test_story_route "$NEWGAME" "$UNDER_BOOT" "$UNDER_SAVE" \
      tests/routes/under-subway.route "$OUT/under.report" "$OUT/under.trace"
else skip under; fi

grep -h -E "played=1( |$)" "$OUT"/*.report "$OUT"/*.trace 2>/dev/null | wc -l | sed 's/^/battles played: /'
! grep -qv -E '^(0 |skip )' "$OUT/status"
