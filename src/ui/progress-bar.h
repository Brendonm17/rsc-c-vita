#ifndef _H_PROGRESS_BAR
#define _H_PROGRESS_BAR

#include "../mudclient.h"

#define PROGRESS_BAR_WIDTH 200
#define PROGRESS_BAR_HEIGHT 16

// interface options sub-op: cancel_batch(6), no payload
#define INTERFACE_OPTION_CANCEL_BATCH 6

void mudclient_draw_progress_bar(mudclient *mud);

#endif
