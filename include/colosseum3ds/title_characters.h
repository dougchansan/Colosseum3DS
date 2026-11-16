#ifndef COLOSSEUM3DS_TITLE_CHARACTERS_H
#define COLOSSEUM3DS_TITLE_CHARACTERS_H

#include <stddef.h>
#include <stdint.h>

#define C3_TITLE_CHARACTER_TEXTURES 17u
#define C3_TITLE_CHARACTER_ITEMS 19u
#define C3_TITLE_CHARACTER_PAGES 4u
#define C3_TITLE_CHARACTER_COLUMNS 6u
#define C3_TITLE_CHARACTER_MAX_BYTES (4u * 1024u * 1024u)

typedef struct {
    const uint8_t *data;
    size_t size;
    void *owned;
} C3TitleCharacters;
typedef struct { unsigned width, height; const uint8_t *pixels; } C3TitleCharacterTexture;
typedef struct { unsigned texture, page, column; int x, y, w, h; } C3TitleCharacterItem;
typedef struct { unsigned page; float offsets[6]; } C3TitleCharacterState;

/* Open borrows memory; load owns its allocation. Close before replacing a pack. */
int c3_title_characters_open(C3TitleCharacters *pack, const void *data, size_t size);
int c3_title_characters_load(C3TitleCharacters *pack, const char *path);
void c3_title_characters_close(C3TitleCharacters *pack);
int c3_title_characters_texture(const C3TitleCharacters *pack, unsigned index, C3TitleCharacterTexture *out);
int c3_title_characters_item(const C3TitleCharacters *pack, unsigned index, C3TitleCharacterItem *out);
/* Initialize page to 3 once at process startup, then enter on each title entry. */
void c3_title_characters_enter(C3TitleCharacterState *state);
void c3_title_characters_tick(C3TitleCharacterState *state, float delta_seconds);
void c3_title_characters_rect(const C3TitleCharacterItem *item, const C3TitleCharacterState *state,
                              float *x, float *y, float *w, float *h);
#endif
