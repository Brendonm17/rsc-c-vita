#ifndef _H_INVENTORY_TAB
#define _H_INVENTORY_TAB

#include "../mudclient.h"

void mudclient_draw_ui_tab_inventory(mudclient *mud, int no_menus);

// draws and consumes the drop-x amount dialog
int mudclient_handle_drop_x(mudclient *mud);

#endif
