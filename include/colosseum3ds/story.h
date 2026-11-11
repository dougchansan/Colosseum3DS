#ifndef C3_STORY_H
#define C3_STORY_H

#include <stddef.h>
#include <stdint.h>

/* A bounded, host-endian-neutral executor for original GSVM banks. Game
 * native calls are yielded to the scene, never silently treated as success. */
#define C3_STORY_TASKS 16u
#define C3_STORY_STACK 64u
#define C3_STORY_ARGS 8u
#define C3_STORY_ERROR 192u
#define C3_STORY_MESSAGE 4096u

typedef struct C3Story { void *impl; } C3Story;
typedef enum C3StoryStatus {
    C3_STORY_FAILED = -1,
    C3_STORY_RUNNING = 0,
    C3_STORY_WAITING = 1,
    C3_STORY_FINISHED = 2
} C3StoryStatus;
typedef enum C3StoryCallKind {
    C3_STORY_NATIVE = 0,
    C3_STORY_WAIT_FRAMES = 1,
    C3_STORY_PRINT = 2
} C3StoryCallKind;
typedef struct C3StoryCall {
    uint32_t script_id, pc, native, argc;
    uint32_t args[C3_STORY_ARGS];
    C3StoryCallKind kind;
} C3StoryCall;

/* All constructors are transactional; zero-initialize before first use.
 * Every non-NULL error buffer must have C3_STORY_ERROR bytes.
 * A C3S1 pack contains original field/common GSVM and message bytes.
 * A raw bank can be admitted separately for synthetic/independent probes. */
int c3_story_load(C3Story *story, const char *path, char *error);
int c3_story_decode(C3Story *story, const void *data, size_t size,
                    char *error);
int c3_story_add_bank(C3Story *story, const void *data, size_t size,
                      char *error);
void c3_story_free(C3Story *story);

int c3_story_start(C3Story *story, uint32_t script_id, const uint32_t *args,
                  uint32_t argc, uint16_t *key, char *error);
C3StoryStatus c3_story_step(C3Story *story, uint16_t key, uint32_t budget,
                           C3StoryCall *call, char *error);
int c3_story_resume(C3Story *story, uint16_t key, uint32_t result);
int c3_story_stop(C3Story *story, uint16_t key);
int c3_story_active(const C3Story *story, uint16_t key);
uint32_t c3_story_result(const C3Story *story, uint16_t key);
/* Read/write VM address operands through checked handles, never ARM/host
 * pointer casts. Values remain raw IEEE754/int32 words. */
int c3_story_read_value(const C3Story *story, uint32_t address, uint32_t *value);
int c3_story_write_value(C3Story *story, uint32_t address, uint32_t value);

/* Original text with source newlines and page breaks (\f). This console
 * presentation retains controls 0/2/3 and dynamic player/partner/NPC names;
 * unsupported controls fail instead of returning a truncated conversation. */
int c3_story_message(const C3Story *story, uint32_t id,
                     const char *hero, const char *partner, const char *speaker,
                     char *output, size_t capacity, char *error);
/* The same decoder with a caller-supplied source for value controls: the
 * control byte is the msgctrl variable it prints (2B hero, 2C partner, 2D
 * item name, 2F number, 59 speaker ...). A NULL return rejects the message
 * with that control named, as an unsupported control does. */
typedef const char *(*C3StoryTextSource)(void *context, uint8_t control);
int c3_story_message_with(const C3Story *story, uint32_t id,
                          C3StoryTextSource source, void *context,
                          char *output, size_t capacity, char *error);
uint32_t c3_story_floor(const C3Story *story);

#endif
