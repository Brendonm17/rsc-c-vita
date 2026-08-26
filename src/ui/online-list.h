#ifndef _H_ONLINE_LIST
#define _H_ONLINE_LIST

#include "../mudclient.h"

#ifndef REVISION_177

void mudclient_draw_online_list(mudclient *mud);
void mudclient_handle_online_list_input(mudclient *mud);
// the entry under (x, y), or -1; walks the same flow the draw does
int mudclient_online_list_entry_at(mudclient *mud, int x, int y);

#endif
#endif
