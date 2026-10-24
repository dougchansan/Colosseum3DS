build/test_wzx_bite: tests/test_wzx_bite.c src/wzx_bite.c include/colosseum3ds/wzx_bite.h build/save_codec.c | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_bite.c src/wzx_bite.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_wzx_bite
.PHONY: wzx-bite-check
wzx-bite-check: build/test_wzx_bite
	./build/test_wzx_bite
test: wzx-bite-check

# Optional original-asset integration; not an automatic CI original-data test.
build/test_wzx_bite_stage: tests/test_wzx_bite_stage.c platform/3ds/battle_stage.c platform/3ds/battle_stage.h platform/3ds/battle_damage_effect.inc platform/3ds/battle_reflect_effect.inc src/title.c src/battle_presentation.c src/wzx_bite.c src/wzx_damage.c src/wzx_reflect.c src/wzx_confusion_sequence.c src/wzx_confusion.c src/battle_audio.c src/particle.c src/particle_pack.c src/battle_camera.c $(HEADERS) build/save_codec.c | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc $(CFLAGS) tests/test_wzx_bite_stage.c platform/3ds/battle_stage.c src/title.c src/battle_presentation.c src/wzx_bite.c src/wzx_damage.c src/wzx_reflect.c src/wzx_confusion_sequence.c src/wzx_confusion.c src/battle_audio.c src/particle.c src/particle_pack.c src/battle_camera.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/test_wzx_reflect_stage: tests/test_wzx_reflect_stage.c platform/3ds/battle_stage.c platform/3ds/battle_reflect_effect.inc platform/3ds/battle_damage_effect.inc platform/3ds/battle_stage.h src/title.c src/battle_presentation.c src/wzx_bite.c src/wzx_damage.c src/wzx_reflect.c src/wzx_confusion_sequence.c src/wzx_confusion.c src/battle_audio.c src/particle.c src/particle_pack.c src/battle_camera.c $(HEADERS) build/save_codec.c | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc $(CFLAGS) tests/test_wzx_reflect_stage.c platform/3ds/battle_stage.c src/title.c src/battle_presentation.c src/wzx_bite.c src/wzx_damage.c src/wzx_reflect.c src/wzx_confusion_sequence.c src/wzx_confusion.c src/battle_audio.c src/particle.c src/particle_pack.c src/battle_camera.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/test_wzx_confusion_stage: tests/test_wzx_confusion_stage.c platform/3ds/battle_stage.c platform/3ds/battle_reflect_effect.inc platform/3ds/battle_damage_effect.inc platform/3ds/battle_stage.h src/title.c src/battle_presentation.c src/wzx_bite.c src/wzx_damage.c src/wzx_reflect.c src/wzx_confusion_sequence.c src/wzx_confusion.c src/battle_audio.c src/particle.c src/particle_pack.c src/battle_camera.c $(HEADERS) build/save_codec.c | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc $(CFLAGS) tests/test_wzx_confusion_stage.c src/title.c src/battle_presentation.c src/wzx_bite.c src/wzx_damage.c src/wzx_reflect.c src/wzx_confusion_sequence.c src/wzx_confusion.c src/battle_audio.c src/particle.c src/particle_pack.c src/battle_camera.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/test_wzx_confusion_sequence: tests/test_wzx_confusion_sequence.c src/wzx_confusion_sequence.c include/colosseum3ds/wzx_confusion_sequence.h build/save_codec.c | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_confusion_sequence.c src/wzx_confusion_sequence.c build/save_codec.c $(LDFLAGS) -lm -o $@
