#ifndef DOLPHIN_TYPES_H
#define DOLPHIN_TYPES_H

/* Native boundary for the explicitly selected upstream translation units.
 * Upstream uses long for 32-bit types. long is 64-bit on an LP64 test host.
 * This header does not make arbitrary GameCube pointer layouts portable. */
#include <stddef.h>
#include <stdint.h>

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float f32;
typedef double f64;
typedef int BOOL;
typedef s64 OSTime;
typedef s32 OSHeapHandle;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

_Static_assert(sizeof(u32) == 4, "GameCube u32 must be four bytes");
_Static_assert(sizeof(f32) == 4, "GameCube f32 must be four bytes");
#endif
