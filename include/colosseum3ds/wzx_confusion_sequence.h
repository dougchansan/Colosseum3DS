#ifndef COLOSSEUM3DS_WZX_CONFUSION_SEQUENCE_H
#define COLOSSEUM3DS_WZX_CONFUSION_SEQUENCE_H
#include <stddef.h>
#include <stdint.h>
#define C3_CONFUSION_SEQUENCE_NODES 15u
#define C3_CONFUSION_SEQUENCE_TRACKS 4u
/* Full source entry contract, not a complete native move player. Owner rate .5 is independently pinned in DOL and motion dispatch. Position
 * takes explicitly sampled source joint seconds; it does not double-apply rate. */
typedef struct {
 uint32_t phase,key,kind,start,attachment,position_type,link,source,target;
 uint32_t timing_index,flags,part,resource_key,resource[19];
} C3ConfusionSequenceNode;
typedef struct {
 uint32_t species,part,clip,frames,offset,bytes,entry;
 uint8_t pkx_sha1[20];
} C3ConfusionSequenceTrack;
typedef struct {
 uint8_t *bytes;size_t length;
 uint8_t type12_sha1[20];
 uint8_t particles_sha1[2][20];
 float owner_rate;
 uint32_t owner_start[2],owner_end[2];
 C3ConfusionSequenceNode nodes[C3_CONFUSION_SEQUENCE_NODES];
 C3ConfusionSequenceTrack tracks[C3_CONFUSION_SEQUENCE_TRACKS];
} C3WzxConfusionSequenceProgram;
/* Zero-init program. Decode is transactional; close only after users cancel. */
int c3_wzx_confusion_sequence_decode(C3WzxConfusionSequenceProgram *,const void *,size_t,char *,size_t);
int c3_wzx_confusion_sequence_load(C3WzxConfusionSequenceProgram *,const char *,char *,size_t);
void c3_wzx_confusion_sequence_close(C3WzxConfusionSequenceProgram *);
/* Entry0 idle loops; entries1/8 motion clamp at their authored endpoint.
 * Lookup uses idle body-map[2] source joint while sampling requested motion.
 * Source seconds are supplied by caller after separately qualifying playback
 * rate and transition timing. Failure preserves output. */
int c3_wzx_confusion_sequence_position(const C3WzxConfusionSequenceProgram *,
 unsigned species,unsigned entry,float source_seconds,const float root[3],
 float yaw,float scale,float out[3],char *,size_t);
/* phase0 ownerEnd145, phase1 ownerStart44/end402. Return source-order nodes
 * crossed in [previous,current], include_start includes previous endpoint.
 * Caller must retain exact-once identity and wait for each real resource. */
unsigned c3_wzx_confusion_sequence_crossed(const C3WzxConfusionSequenceProgram *,
 unsigned phase,uint64_t previous_units,uint64_t current_units,int include_start,
 uint8_t out[C3_CONFUSION_SEQUENCE_NODES]);
#endif
