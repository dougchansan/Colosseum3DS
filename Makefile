CC ?= cc
CPPFLAGS += -Iinclude -Icompat/include -Ivendor/colosseum/include
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
CORE = src/core.c src/data.c src/decomp_rng.c src/decomp_collision.c src/decomp_pokemon.c
HEADERS = $(wildcard include/colosseum3ds/*.h) compat/include/dolphin/types.h
HEADERS += vendor/colosseum/include/game/gs_colsys.h vendor/colosseum/include/game/pokemon_evolution.h
UPSTREAM = vendor/colosseum/src/hsd/random.c vendor/colosseum/src/game/GScolsys2Util_exact_8010C77C.c vendor/colosseum/src/game/pokemon_range_8012795C.c

.PHONY: all test clean 3ds verify-source
all: build/test_core build/test_data build/test_pokemon build/test_model

build:
	mkdir -p $@

build/test_core: tests/test_core.c $(CORE) $(HEADERS) $(UPSTREAM) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_core.c $(CORE) $(LDFLAGS) -lm -o $@

build/test_data: tests/test_data.c src/data.c include/colosseum3ds/data.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_data.c src/data.c $(LDFLAGS) -o $@

build/test_pokemon: tests/test_pokemon.c src/decomp_pokemon.c $(HEADERS) $(UPSTREAM) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_pokemon.c src/decomp_pokemon.c $(LDFLAGS) -o $@

build/test_model: tests/test_model.c src/model.c include/colosseum3ds/model.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_model.c src/model.c $(LDFLAGS) -lm -o $@

test: all
	./build/test_core
	./build/test_data
	./build/test_pokemon
	./build/test_model
	python3 -m unittest discover -s tests -p 'test_*.py'

verify-source:
	python3 tools/verify_source.py

3ds:
	$(MAKE) -f Makefile.3ds

clean:
	rm -rf build build-3ds
