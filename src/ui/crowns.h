#ifndef _H_CROWNS
#define _H_CROWNS

#include "../mudclient.h"

// number of rank crowns defined by the protocol (5)
#define ORSC_CROWN_COUNT 5

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
// registers crown sprite geometry in the shared custom atlas
void surface_setup_custom_crown_sprites(Surface *surface, int sprite_item_base);
#endif

// draws a rank crown from a packed int (high byte = index+1, low 24 bits =
// recolour mask); returns x advance for trailing text
int mudclient_draw_crown(mudclient *mud, int x, int y, int packed);

// x advance a crown would take without drawing, for alignment
int mudclient_crown_width(mudclient *mud, int packed);

#endif
