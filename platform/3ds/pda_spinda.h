#ifndef C3_PDA_SPINDA_H
#define C3_PDA_SPINDA_H
#include "colosseum3ds/title.h"
/* Apply once to owned source pixels before GPU upload. Failures preserve models. */
int c3_pda_spinda_apply(C3Title *models,unsigned count,uint32_t pid,const char *path,char *error,size_t capacity);
#endif
