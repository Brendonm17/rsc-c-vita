#ifndef _H_PACKET_HANDLER

#include "chat-message.h"
#include "client-opcodes.h"
#include "mudclient.h"
#include "packet-stream.h"
#include "scene.h"
#include "server-opcodes.h"
#include "ui/message-tabs.h"
#include "utility.h"
#include "world.h"

void mudclient_update_ground_item_models(mudclient *mud);
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void mudclient_gl_update_wall_models(mudclient *mud);
#endif
void mudclient_packet_tick(mudclient *mud);

#ifndef REVISION_177
// the colour code preceding a player's name on a want_custom_rank_display world, or empty
const char *orsc_staff_prefix(mudclient *mud, int group_id);
#endif

#endif
