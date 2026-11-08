# Native geometry probes, using the same shared renderer as the application.
ifndef DEVKITPRO
$(error Set DEVKITPRO to the devkitPro directory)
endif
PROBE ?= tv_texture_probe
BUILD ?= scene-probe-build
REPO := $(abspath ../..)
CC := $(DEVKITPRO)/devkitARM/bin/arm-none-eabi-gcc
ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS := -O2 -g -Wall -Wextra -Werror -std=gnu11 -mword-relocations $(ARCH) -D__3DS__ \
 -I$(REPO)/include -I$(REPO)/platform/3ds -I$(DEVKITPRO)/libctru/include -I$(BUILD)
OBJECTS := $(BUILD)/probe.o $(BUILD)/renderer.o $(BUILD)/title.o $(BUILD)/area.o $(BUILD)/field.o $(BUILD)/area_camera.o $(BUILD)/area_lights.o $(BUILD)/shader.o
all: $(BUILD)/$(PROBE).3dsx
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
$(BUILD)/probe.o: $(PROBE).c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/renderer.o: $(REPO)/platform/3ds/title_renderer.c $(BUILD)/title_shader_shbin.h
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: $(REPO)/src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/$(PROBE).elf: $(OBJECTS)
	$(CC) $(ARCH) -specs=3dsx.specs $^ -L$(DEVKITPRO)/libctru/lib -lcitro3d -lctru -lm -o $@
$(BUILD)/$(PROBE).3dsx: $(BUILD)/$(PROBE).elf
	$(DEVKITPRO)/tools/bin/3dsxtool $< $@
.SECONDARY:
