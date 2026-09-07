#ifndef WORLDLIST_H
#define WORLDLIST_H

#include "../mudclient.h"

void worldlist_new(mudclient *mud);
void worldlist_handle_mouse(mudclient *mud);

#ifdef WITH_SINGLEPLAYER
// draw the world editor popup over the world list when open
void worldlist_draw_editor(mudclient *mud);

// route a typed character to the world screen's focused text field
void worldlist_handle_key(mudclient *mud, int key_code);

// save a bot's appearance edit onto its def and return 1; 0 for a real player change
int worldlist_bots_appearance_accept(mudclient *mud);
// 1 while the appearance panel is editing a bot's look
int worldlist_bots_appearance_active(void);
#endif
#endif
