#ifndef _H_SKILL_GUIDE
#define _H_SKILL_GUIDE

#include "../mudclient.h"

#ifndef REVISION_177
#include "guide-data.h"

// in-game skill/quest guide windows, opened from the stats tab and the quest list; content from guide-data
int mudclient_guides_visible(mudclient *mud);
void mudclient_skill_guide_open(mudclient *mud, const char *skill_name);
void mudclient_quest_guide_open(mudclient *mud, int quest_id,
                                const char *quest_name, int stage);
void mudclient_draw_guides(mudclient *mud);
#endif

#endif
