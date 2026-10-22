# Build source-only graphics probes using the exact native title renderer.
# Run from tools/sandbox: make -f native_probe.mk DEVKITPRO=/path/devkitpro
ifndef DEVKITPRO
$(error Set DEVKITPRO to the devkitPro directory)
endif
REPO := $(abspath ../..)
BUILD := probe-build
CC := $(DEVKITPRO)/devkitARM/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -O2 -g -Wall -Wextra -std=gnu11 -mword-relocations $(ARCH) -D__3DS__ \
          -I$(REPO)/include -I$(REPO)/platform/3ds -I$(DEVKITPRO)/libctru/include -I$(BUILD)
.PHONY: all
all: $(BUILD)/title-corner.3dsx $(BUILD)/title-combiner.3dsx $(BUILD)/stage-material.3dsx $(BUILD)/arrival-material.3dsx $(BUILD)/arrival-fade.3dsx
$(BUILD):
	mkdir -p $@
$(BUILD)/title_shader.shbin: $(REPO)/platform/3ds/title_shader.v.pica | $(BUILD)
	$(DEVKITPRO)/tools/bin/picasso -o $@ $<
$(BUILD)/title_shader_shbin.h: | $(BUILD)
	python3 -c 'from pathlib import Path; Path("$@").write_text("#include <3ds.h>\nextern const u8 title_shader_shbin[];\nextern const u32 title_shader_shbin_size;\n")'
$(BUILD)/shader.S: $(BUILD)/title_shader.shbin
	python3 -c 'from pathlib import Path; Path("$@").write_text(".section .rodata\n.balign 4\n.global title_shader_shbin\ntitle_shader_shbin:\n.incbin \"$(BUILD)/title_shader.shbin\"\n.global title_shader_shbin_size\n.balign 4\ntitle_shader_shbin_size:\n.word .-title_shader_shbin\n")'
$(BUILD)/shader.o: $(BUILD)/shader.S
	$(CC) $(ARCH) -c $< -o $@
$(BUILD)/renderer.o: $(REPO)/platform/3ds/title_renderer.c $(REPO)/platform/3ds/title_renderer.h $(REPO)/platform/3ds/stage_renderer.h $(REPO)/include/colosseum3ds/title.h $(BUILD)/title_shader_shbin.h
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/title-corner.o: title_corner_probe.c $(REPO)/include/colosseum3ds/title.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/title-combiner.o: title_corner_probe.c $(REPO)/include/colosseum3ds/title.h | $(BUILD)
	$(CC) $(CFLAGS) -DC3_CORNER_TEV_TEST -c $< -o $@
$(BUILD)/stage-material.o: stage_material_probe.c $(REPO)/include/colosseum3ds/title.h $(REPO)/platform/3ds/stage_renderer.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/arrival-material.o: arrival_material_probe.c $(REPO)/include/colosseum3ds/title.h $(REPO)/platform/3ds/stage_renderer.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/arrival-fade.o: arrival_material_probe.c $(REPO)/include/colosseum3ds/title.h $(REPO)/platform/3ds/stage_renderer.h | $(BUILD)
	$(CC) $(CFLAGS) -DC3_ARRIVAL_FADE_TEST -c $< -o $@
$(BUILD)/%.elf: $(BUILD)/%.o $(BUILD)/renderer.o $(BUILD)/shader.o
	$(CC) $(ARCH) -specs=3dsx.specs $^ -L$(DEVKITPRO)/libctru/lib -lcitro3d -lctru -lm -o $@
$(BUILD)/%.3dsx: $(BUILD)/%.elf
	$(DEVKITPRO)/tools/bin/3dsxtool $< $@
.SECONDARY:
