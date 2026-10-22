CC ?= cc
CFLAGS ?= -O2 -g
.PHONY: all probe
all: harness
harness: harness.c include/libretro.h include/EGL/egl.h include/GL/gl.h
	$(CC) -std=gnu11 $(CFLAGS) -Wall -Wextra -Wno-format-truncation -Iinclude $< -o $@ -ldl -l:libEGL.so.1 -l:libGL.so.1

# Optional native 3DS filesystem/input probe. This is not a game-save codec.
probe: sd_probe.3dsx
sd_probe.elf: sd_probe.c
	@test -n "$(DEVKITPRO)" || (echo 'Set DEVKITPRO to build the optional probe' >&2; exit 1)
	$(DEVKITPRO)/devkitARM/bin/arm-none-eabi-gcc -g -O2 -Wall -Wextra -std=gnu11 -mword-relocations -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -D__3DS__ -I$(DEVKITPRO)/libctru/include -L$(DEVKITPRO)/libctru/lib -specs=3dsx.specs $< -lctru -lm -o $@
sd_probe.3dsx: sd_probe.elf
	$(DEVKITPRO)/tools/bin/3dsxtool $< $@
