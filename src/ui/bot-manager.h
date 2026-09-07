#ifndef _H_BOT_MANAGER
#define _H_BOT_MANAGER

#include "../mudclient.h"

// the world editor's Bot Manager popup: a world's bot roster (bot-defs.json)
// with a per-bot settings panel and the auto-create batch popup

#ifdef WITH_SINGLEPLAYER
void bot_manager_open(mudclient *mud, const char *world_id);
int bot_manager_shown(void);
void bot_manager_draw(mudclient *mud);
// returns 1 when the popup is open and consumed this frame's input
int bot_manager_handle(mudclient *mud);
// returns 1 when the popup took the key (its Name box)
int bot_manager_key(mudclient *mud, int key_code);
#endif

#endif
