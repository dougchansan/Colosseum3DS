#ifndef COLOSSEUM3DS_AREA_CAMERA_H
#define COLOSSEUM3DS_AREA_CAMERA_H
#include "colosseum3ds/title.h"

#define C3_AREA_CAMERA_MAX_FRAMES 4096u
#define C3_AREA_CAMERA_MAX_BYTES (64u+C3_AREA_CAMERA_MAX_FRAMES*44u)
typedef struct {
    float *frames;
    uint32_t frame_count,loop;
    float fps;
    /* Original camera member ID (header word 28). Zero in the first shop
     * pack, which predates the field and means member 00E51800. */
    uint32_t resource;
} C3AreaCamera;

/* Zero-initialize before use. Replacement is transactional. Samples are the
 * original HSD camera curves at the prepared rate, independent of actor time. */
int c3_area_camera_parse(C3AreaCamera *track,const void *data,size_t bytes,char *error,size_t capacity);
int c3_area_camera_load(C3AreaCamera *track,const char *path,char *error,size_t capacity);
int c3_area_camera_sample(const C3AreaCamera *track,float seconds,C3TitleCamera *camera);
void c3_area_camera_close(C3AreaCamera *track);
#endif
