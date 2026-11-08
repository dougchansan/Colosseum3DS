# Actual private map/Wes visual probe. No game state or save service is linked.
# From tools/sandbox: make -f field_probe.mk DEVKITPRO=/absolute/devkitpro
ifndef DEVKITPRO
$(error Set DEVKITPRO to the devkitPro directory)
endif
REPO := $(abspath ../..)
BUILD ?= field-probe-build
CC := $(DEVKITPRO)/devkitARM/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -O2 -g -Wall -Wextra -Werror -MMD -MP -std=gnu11 -mword-relocations $(ARCH) -D__3DS__ \
          -I$(REPO)/include -I$(REPO)/platform/3ds -I$(DEVKITPRO)/libctru/include -I$(BUILD)
OBJS := $(BUILD)/probe.o $(BUILD)/field.o $(BUILD)/title.o $(BUILD)/platform.o $(BUILD)/renderer.o $(BUILD)/shader.o
.PHONY: all
all: $(BUILD)/field-probe.3dsx
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
$(BUILD)/probe.o: field_probe.c $(REPO)/include/colosseum3ds/field.h $(REPO)/platform/3ds/stage_renderer.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/field.o: $(REPO)/src/field.c $(REPO)/include/colosseum3ds/field.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/title.o: $(REPO)/src/title.c $(REPO)/include/colosseum3ds/title.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/platform.o: $(REPO)/platform/3ds/n3ds_platform.c $(REPO)/platform/3ds/n3ds_platform.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/renderer.o: $(REPO)/platform/3ds/title_renderer.c $(REPO)/platform/3ds/title_renderer.h $(REPO)/platform/3ds/stage_renderer.h $(REPO)/include/colosseum3ds/title.h $(BUILD)/title_shader_shbin.h
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/field-probe.elf: $(OBJS)
	$(CC) $(ARCH) -specs=3dsx.specs $^ -L$(DEVKITPRO)/libctru/lib -lcitro3d -lctru -lm -o $@
$(BUILD)/field-probe.3dsx: $(BUILD)/field-probe.elf
	$(DEVKITPRO)/tools/bin/3dsxtool $< $@
.SECONDARY:
-include $(OBJS:.o=.d)
