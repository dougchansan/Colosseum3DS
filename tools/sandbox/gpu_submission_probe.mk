# Run from tools/sandbox. Requires the same pinned SDK as the game.
ifndef DEVKITPRO
$(error Set DEVKITPRO to the devkitPro directory)
endif
REPO := $(abspath ../..)
BUILD ?= gpu-probe-build
C3_BUILD_ID ?= $(shell git -C $(REPO) rev-parse --short=12 HEAD)
CC := $(DEVKITPRO)/devkitARM/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -O2 -g -Wall -Wextra -Werror -MMD -MP -std=gnu11 -mword-relocations $(ARCH) \
 -D__3DS__ -DC3_GPU_DIAGNOSTICS -DC3_BUILD_ID=\"$(C3_BUILD_ID)\" -I$(REPO)/include \
 -I$(REPO)/platform/3ds -I$(DEVKITPRO)/libctru/include -I$(BUILD)
OBJS := $(BUILD)/probe.o $(BUILD)/title.o $(BUILD)/platform.o $(BUILD)/recovery.o $(BUILD)/renderer.o $(BUILD)/shader.o
all: $(BUILD)/gpu-probe.3dsx
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
$(BUILD)/probe.o: gpu_submission_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/title.o: $(REPO)/src/title.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/platform.o: $(REPO)/platform/3ds/n3ds_platform.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/recovery.o: $(REPO)/platform/3ds/render_recovery.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/renderer.o: $(REPO)/platform/3ds/title_renderer.c $(BUILD)/title_shader_shbin.h
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/gpu-probe.elf: $(OBJS)
	$(CC) $(ARCH) -specs=3dsx.specs $^ -L$(DEVKITPRO)/libctru/lib -lcitro3d -lctru -lm -o $@
$(BUILD)/gpu-probe.3dsx: $(BUILD)/gpu-probe.elf
	$(DEVKITPRO)/tools/bin/3dsxtool $< $@
.SECONDARY:
-include $(OBJS:.o=.d)
