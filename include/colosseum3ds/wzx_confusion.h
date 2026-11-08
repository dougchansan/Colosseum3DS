#ifndef C3_WZX_CONFUSION_H
#define C3_WZX_CONFUSION_H
#include <stddef.h>
#include <stdint.h>
/* Source type12 CPU path only: real inverse-view quad and indirect texture
 * matrices. Rendering requires the original visible-model capture and GX
 * dependent-texture sampling. No ordinary billboard is a substitute. */
#define C3_CONFUSION_MASK_BYTES (128u*128u)
#define C3_CONFUSION_CAPTURE 1u
#define C3_CONFUSION_INDIRECT 2u
#define C3_CONFUSION_POINT_TRAIL 4u
typedef struct {
    uint32_t phase, start_tick, duration, owner_start, owner_end, part;
    float radius_per_tick, pivot, indirect_base;
} C3ConfusionType12Profile;
typedef struct {
    C3ConfusionType12Profile phase[2];
    uint8_t intensity[C3_CONFUSION_MASK_BYTES]; /* Original I8 mask, top-origin linear pixels. */
} C3WzxConfusionProgram;
typedef struct {
    C3ConfusionType12Profile profile;
    uint64_t units;
    uint32_t frame, owner, target;
    float center[3], radius, indirect;
    uint8_t active, paused;
} C3ConfusionType12;
typedef struct {
    float world[4][3];
    float captured_uv[2][3], indirect_matrix[2][3];
    uint32_t required_capabilities, capture_model_flags;
    int8_t indirect_exponent;
    uint8_t capture_include_particles;
} C3ConfusionType12Frame;
int c3_wzx_confusion_decode(C3WzxConfusionProgram *,const void *,size_t,char *,size_t);
int c3_wzx_confusion_load(C3WzxConfusionProgram *,const char *,char *,size_t);
/* Call exactly when profile.start_tick is crossed, passing the checked original
 * idle body-map[2] position at that moment. Attachment is one-shot (follow0).
 * attack owner=slot1/part62; damage owner=slot2or3/part53. */
int c3_confusion_type12_begin(C3ConfusionType12 *,const C3ConfusionType12Profile *,
                            unsigned owner,unsigned target,uint32_t source_part,
                            const float center[3],char *,size_t);
int c3_confusion_type12_step(C3ConfusionType12 *,uint32_t milliseconds,char *,size_t);
void c3_confusion_type12_pause(C3ConfusionType12 *,int);
void c3_confusion_type12_cancel(C3ConfusionType12 *);
/* Camera right/up are orthonormal inverse-view axes. projected[0/1] are the
 * original quad(-1,-1)/(+1,+1) projected screen coordinates divided640/480.
 * Capture dimensions are the actual shared render target, not guessed UVs. */
int c3_confusion_type12_frame(const C3ConfusionType12 *,const float right[3],const float up[3],
                            const float projected[2][2],unsigned capture_width,unsigned capture_height,
                            C3ConfusionType12Frame *,char *,size_t);
/* Missing native capabilities are an explicit error, never successful no-op. */
int c3_confusion_type12_require(uint32_t available,char *,size_t);
#endif
