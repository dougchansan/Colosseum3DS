# Link play (docs/link-play.md): the session protocol over the loopback transport.
build/test_link_protocol: tests/test_link_protocol.c src/link_protocol.c include/colosseum3ds/link_protocol.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_link_protocol.c src/link_protocol.c $(LDFLAGS) -o $@
all: build/test_link_protocol
.PHONY: link-check
link-check: build/test_link_protocol
	./build/test_link_protocol
test: link-check

# Lockstep link battles and trades (synthetic pack; the private part takes
# <battles.c3x> <newgame.c3g> <save A> <save B>).
LINK_BATTLE_SOURCES = src/link_battle.c src/link_protocol.c src/battle_general.c src/battle_rng.c src/decomp_rng.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c
build/test_link_battle: tests/test_link_battle.c tests/battle_pack_fixture.h $(LINK_BATTLE_SOURCES) include/colosseum3ds/link_battle.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_link_battle.c $(LINK_BATTLE_SOURCES) $(LDFLAGS) -lm -o $@
all: build/test_link_battle
.PHONY: link-battle-check
link-battle-check: build/test_link_battle
	./build/test_link_battle
test: link-battle-check
