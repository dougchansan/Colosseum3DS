#ifndef C3_PDA_PREVIEW_H
#define C3_PDA_PREVIEW_H
#include "stage_renderer.h"
#include <stdbool.h>
typedef struct C3PdaPreview C3PdaPreview;
/* Caller must drain and close its renderer before open. Root is the models
 * directory (normally sdmc:/3ds/Colosseum3DS/pda/models). */
int c3_pda_preview_open(C3PdaPreview **out,const char *root,uint16_t species,
                      uint32_t pid,uint32_t ot,char *error,size_t capacity);
int c3_pda_preview_draw(C3PdaPreview *,float seconds,C3TitleRenderStats *,char *,size_t);
/* Normalized C-stick axes and elapsed seconds; bounded orbit pitch. */
void c3_pda_preview_control(C3PdaPreview *,float x,float y,float dt);
/* False retains ownership while the GPU is busy; retry after recovery. */
bool c3_pda_preview_close(C3PdaPreview **);
#endif
