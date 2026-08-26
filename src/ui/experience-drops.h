#ifndef _H_EXPERIENCE_DROPS
#define _H_EXPERIENCE_DROPS

#include "../mudclient.h"

void mudclient_drop_experience(mudclient *mud, int skill_index, int experience);
void mudclient_draw_experience_drops(mudclient *mud);
#ifndef REVISION_177
void mudclient_draw_experience_counter(mudclient *mud);
#endif

#endif
