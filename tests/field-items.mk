FIELD_ITEMS_SOURCES = platform/3ds/bag_menu.c src/game_state_items.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c
FIELD_ITEMS_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/field-items-source-files.json'))['files']))"))
build/field_items_original.h: tools/prepare_field_items_source.py config/field-items-source-files.json source-lock.json $(FIELD_ITEMS_UPSTREAM) | build
	python3 tools/prepare_field_items_source.py

build/test_field_items: tests/test_field_items.c $(FIELD_ITEMS_SOURCES) build/field_items_original.h include/colosseum3ds/game_items.h src/game_state_internal.h tests/field_items_fixture.h platform/3ds/bag_menu.h
	$(CC) $(CPPFLAGS) -Ibuild -Isrc -Iplatform/3ds -DC3_BAG_HOST_TEST $(CFLAGS) tests/test_field_items.c $(FIELD_ITEMS_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_field_items
test: test-field-items
.PHONY: test-field-items
test-field-items: build/test_field_items
	./build/test_field_items
