#ifndef _H_IRONMAN
#define _H_IRONMAN

#include "../mudclient.h"

#define IRONMAN_DIALOG_WIDTH 260
#define IRONMAN_DIALOG_HEIGHT 272
#define IRONMAN_ROW_HEIGHT 24

// interface options sub-op: ironman_mode(7); byte 2: 0=mode, 1=restriction
#define INTERFACE_OPTION_IRONMAN 7

void mudclient_draw_ironman_interface(mudclient *mud);

#endif
