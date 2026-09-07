#include "packet-handler.h"
#include "online-defs.h"
#include "protocol177.h"
#include "diag.h"

#ifdef RSC_DIAG
// -DRSC_DIAG only: last logged raw region-players position, packets still to trace after a death or
// world-info, and the other-player / npc hit counters
static int diag_last_raw_x = -1;
static int diag_last_raw_y = -1;
static int diag_trace_packets = 0;
static int diag_region_packets = 0;
static int diag_other_hits = 0;
static int diag_npc_hits = 0;
#endif

#ifndef REVISION_177
// map the 14 server equipment slots to the 11-slot paperdoll: helm/body/skirt
// overlay the plate slots, then slots 8..13 shift down 3
static int orsc_equip_collapse_slot(int wield_pos) {
    switch (wield_pos) {
    case 5:
        return 0; // SLOT_MEDIUM_HELMET -> helmet
    case 6:
        return 1; // SLOT_CHAIN_BODY    -> body
    case 7:
        return 2; // SLOT_SKIRT         -> legs
    default:
        return wield_pos > 7 ? wield_pos - 3 : wield_pos;
    }
}

// read one newline(0x0A)-terminated string, advancing *offset past the
// terminator. truncates to out_max-1, always NUL-terminates
static void orsc_read_string(int8_t *data, size_t *offset, size_t size,
                             char *out, int out_max) {
    int length = 0;

    while (*offset < size) {
        int byte = get_unsigned_byte(data, *offset, size);
        (*offset)++;

        if (byte == 10) {
            break;
        }

        if (length < out_max - 1) {
            out[length++] = (char)byte;
        }
    }

    out[length] = '\0';
}

// colour code that precedes a player's name when custom rank display (config 39) is on
const char *orsc_staff_prefix(mudclient *mud, int group_id) {
    if (!mud->orsc.want_custom_rank_display) {
        return "";
    }

    switch (group_id) {
    case 0:  return "@dcy@"; // Owner
    case 1:  return "@gre@"; // Admin
    case 2:  return "@blu@"; // Super Moderator
    case 3:  return "@bl1@"; // Moderator
    case 5:  return "@red@"; // Developer
    case 7:  return "@eve@"; // Event
    // 8 Player Moderator, 9 Tester, 10 User: no colour
    default: return "";
    }
}

// trade/duel item list: u8 count, then per item u16 id, [u8 noted iff the world
// accepts notes], i32 amount. entries past TRADE_ITEMS_MAX are parsed and dropped
static int orsc_read_transaction_list(int8_t *data, int size, int offset,
                                      int notes, int *out_count, int *ids,
                                      int *amounts, uint8_t *noted) {
    int wire_count = get_unsigned_byte(data, offset++, size);

    *out_count = wire_count > TRADE_ITEMS_MAX ? TRADE_ITEMS_MAX : wire_count;

    for (int i = 0; i < wire_count; i++) {
        int id = get_unsigned_short(data, offset, size);
        offset += 2;

        int is_noted = 0;

        if (notes) {
            is_noted = get_unsigned_byte(data, offset++, size) == 1;
        }

        int amount = get_unsigned_int(data, offset, size);
        offset += 4;

        if (i < *out_count) {
            ids[i] = id;
            amounts[i] = amount;
            noted[i] = (uint8_t)is_noted;
        }
    }

    return offset;
}

// custom SEND_UPDATE_PLAYERS: shares opcode 204 but diverges. plain newline username/chat, worn items are shorts
// header count includes hp updates; types 7/8/9 are custom-only; type 5 carries a clan/group/icon trailer
static void orsc_handle_player_update(mudclient *mud, int8_t *data, int size) {
    int count = get_unsigned_short(data, 1, size);
    int offset = 3;

    for (int i = 0; i < count && offset < size; i++) {
        int player_index = get_unsigned_short(data, offset, size);
        offset += 2;

        GameCharacter *player = player_index < PLAYERS_SERVER_MAX
                                    ? mud->player_server[player_index]
                                    : NULL;

        int update_type = get_unsigned_byte(data, offset++, size);

        if (update_type == 0) {
            // action bubble with an item in it
            int item_id = get_unsigned_short(data, offset, size);
            offset += 2;

            if (item_id >= game_data.item_count) {
                item_id = IRON_MACE_ID;
            }

            if (player != NULL) {
                player->bubble_timeout = 150;
                player->bubble_item = item_id;
            }
        } else if (update_type == 1 || update_type == 6 || update_type == 7) {
            // 1 = public chat, 6 = quest chat, 7 = muted/tutorial chat
            int crown = 0;
            int muted = 0;
            int on_tutorial = 0;

            if (update_type != 6) {
                crown = get_unsigned_int(data, offset, size);
                offset += 4;

                if (update_type == 7) {
                    muted = get_unsigned_byte(data, offset++, size) > 0;
                    on_tutorial = get_unsigned_byte(data, offset++, size) > 0;
                }
            }

            char message[255] = {0};
            size_t message_offset = (size_t)offset;

            orsc_read_string(data, &message_offset, (size_t)size, message,
                             (int)sizeof(message));

            offset = (int)message_offset;

            // empty type 7 is dropped
            if (update_type == 7 && message[0] == '\0') {
                continue;
            }

            if (player == NULL) {
                continue;
            }

            int ignored = 0;

            for (int j = 0; j < mud->ignore_list_count; j++) {
                if (mud->ignore_list[j] == player->encoded_username) {
                    ignored = 1;
                    break;
                }
            }

            if (ignored) {
                continue;
            }

            player->message_timeout = 150;
            strcpy(player->message, message);

            // prefix order: [MUTED], [TUTORIAL], clan tag, name; quest chat (6)
            // shows only for the local player and carries no crown
            if (update_type == 6) {
                if (player != mud->local_player) {
                    continue;
                }

                const char *staff = orsc_staff_prefix(mud, player->group_id);
                char formatted[320];

                if (player->clan_tag[0] != '\0') {
                    snprintf(formatted, sizeof(formatted),
                             "@whi@[@cla@%s@whi@]@whi@ %s%s: %s",
                             player->clan_tag, staff, player->name, message);
                } else {
                    snprintf(formatted, sizeof(formatted), "%s%s: %s", staff,
                             player->name, message);
                }

                mudclient_show_message(mud, formatted, MESSAGE_TYPE_QUEST);
            } else {
                /* 24 bytes of markup wrap the tag. Sized off the field, not off
                 * their "clanTag.length() > 5" rule (InterfaceOptionHandler:372)
                 * -- that is the server's own validation, and nothing on this
                 * path re-checks it, so a 15-char tag would otherwise truncate
                 * the closing colour code and mis-colour the rest of the line. */
                char clan_prefix[sizeof(player->clan_tag) + 24] = {0};

                if (player->clan_tag[0] != '\0') {
                    snprintf(clan_prefix, sizeof(clan_prefix),
                             "@whi@[@cla@%s@whi@]@yel@ ", player->clan_tag);
                }

                char formatted[360];

                snprintf(formatted, sizeof(formatted), "%s%s%s%s%s: %s",
                         muted ? "@whi@[MUTED]@yel@ " : "",
                         on_tutorial ? "@whi@[TUTORIAL]@yel@ " : "", clan_prefix,
                         orsc_staff_prefix(mud, player->group_id), player->name,
                         message);

                mud->orsc_pending_crown = crown;
                mudclient_show_message(mud, formatted, MESSAGE_TYPE_CHAT);
            }
        } else if (update_type == 2) {
            // combat damage and hp
            int damage = get_unsigned_byte(data, offset++, size);
            int current = get_unsigned_byte(data, offset++, size);
            int max = get_unsigned_byte(data, offset++, size);

#ifdef RSC_DIAG
            if (player == NULL) {
                DIAG("orsc hit for UNKNOWN player idx=%d dmg=%d hp=%d/%d",
                     player_index, damage, current, max);
            } else if (player == mud->local_player) {
                DIAG("orsc hit LOCAL idx=%d dmg=%d hp=%d/%d", player_index,
                     damage, current, max);
            } else {
                diag_other_hits++;
            }
#endif

            if (player != NULL) {
                player->damage_taken = damage;
                player->current_hits = current;
                player->max_hits = max;
                player->combat_timer = 200;

                if (player == mud->local_player) {
                    mud->player_skill_current[SKILL_HITS] = current;
                    mud->player_skill_base[SKILL_HITS] = max;
                    mud->show_dialog_welcome = 0;
                    mud->show_dialog_server_message = 0;
                }
            }
        } else if (update_type == 3 || update_type == 4) {
            // incoming projectile: 3 from an npc, 4 from a player
            int projectile_sprite = get_unsigned_short(data, offset, size);
            offset += 2;

            int shooter_index = get_unsigned_short(data, offset, size);
            offset += 2;

            int limit = update_type == 3 ? NPCS_SERVER_MAX : PLAYERS_SERVER_MAX;

            if (shooter_index >= limit) {
                continue;
            }

            if (player != NULL) {
                player->incoming_projectile_sprite = projectile_sprite;
                player->projectile_range = PROJECTILE_RANGE_MAX;

                if (update_type == 3) {
                    player->attacking_npc_server_index = shooter_index;
                    player->attacking_player_server_index = -1;
                } else {
                    player->attacking_player_server_index = shooter_index;
                    player->attacking_npc_server_index = -1;
                }
            }
        } else if (update_type == 5) {
            // appearance and identity; sized to player->name, so a long name
            // truncates at out_max-1 instead of overrunning the 13-byte field
            char username[MAX_USER_LENGTH + 1] = {0};
            size_t name_offset = (size_t)offset;

            orsc_read_string(data, &name_offset, (size_t)size, username,
                             (int)sizeof(username));

            offset = (int)name_offset;

            int worn_count = get_unsigned_byte(data, offset++, size);

            int worn[ANIMATION_COUNT] = {0};

            for (int j = 0; j < worn_count; j++) {
                int sprite = get_unsigned_short(data, offset, size);
                offset += 2;

                if (j < ANIMATION_COUNT) {
                    worn[j] = sprite;
                }
            }

            int hair = get_unsigned_byte(data, offset++, size);
            int top = get_unsigned_byte(data, offset++, size);
            int bottom = get_unsigned_byte(data, offset++, size);
            int skin = get_unsigned_byte(data, offset++, size);
            int level = get_unsigned_byte(data, offset++, size);
            int skull = get_unsigned_byte(data, offset++, size);

            char clan_tag[16] = {0};

            if (get_unsigned_byte(data, offset++, size) == 1) {
                size_t tag_offset = (size_t)offset;

                orsc_read_string(data, &tag_offset, (size_t)size, clan_tag,
                                 (int)sizeof(clan_tag));

                offset = (int)tag_offset;
            }

            int invisible = get_unsigned_byte(data, offset++, size);
            int invulnerable = get_unsigned_byte(data, offset++, size);
            int group_id = get_unsigned_byte(data, offset++, size);

            int icon = get_unsigned_int(data, offset, size);
            offset += 4;

            if (player == NULL) {
                continue;
            }

            strcpy(player->name, username);
            player->encoded_username = encode_username(username);

            for (int j = 0; j < ANIMATION_COUNT; j++) {
                player->animations[j] = j < worn_count ? worn[j] : 0;
            }

            // clamp against the real palettes
            player->hair_colour = hair < PLAYER_HAIR_COLOUR_COUNT ? hair : 0;
            player->top_colour = top < PLAYER_TOP_BOTTOM_COLOUR_COUNT ? top : 0;

            player->bottom_colour =
                bottom < PLAYER_TOP_BOTTOM_COLOUR_COUNT ? bottom : 0;

            player->skin_colour = skin < PLAYER_SKIN_COLOUR_COUNT ? skin : 0;
            player->level = level;
            player->skull_visible = skull;

            strcpy(player->clan_tag, clan_tag);
            player->is_invisible = invisible > 0;
            player->is_invulnerable = invulnerable > 0;
            player->group_id = group_id;
            player->icon = icon;
        } else if (update_type == 8) {
            // heal: mirror of type 2, custom only
            int heal = get_unsigned_byte(data, offset++, size);
            int current = get_unsigned_byte(data, offset++, size);
            int max = get_unsigned_byte(data, offset++, size);

            if (player != NULL) {
                player->heal_taken = heal;
                player->current_hits = current;
                player->max_hits = max;
                player->heal_timer = 200;

                if (player == mud->local_player) {
                    mud->player_skill_current[SKILL_HITS] = current;
                    mud->player_skill_base[SKILL_HITS] = max;
                    mud->show_dialog_welcome = 0;
                    mud->show_dialog_server_message = 0;
                }
            }
        } else if (update_type == 9) {
            // hp only, no damage number; authentic folds this into type 2
            int current = get_unsigned_byte(data, offset++, size);
            int max = get_unsigned_byte(data, offset++, size);

            if (player != NULL) {
                player->current_hits = current;
                player->max_hits = max;

                if (player == mud->local_player) {
                    mud->player_skill_current[SKILL_HITS] = current;
                    mud->player_skill_base[SKILL_HITS] = max;
                    mud->show_dialog_welcome = 0;
                    mud->show_dialog_server_message = 0;
                }
            }
        } else {
            // unknown type has no length; bail
            return;
        }
    }
}

// custom SEND_FRIEND_UPDATE: the friend list is built from a stream of these, one per friend, an unknown name appends
// layout: currentName, formerName, u8 onlineStatus, [worldName when online]; onlineStatus bit 0 = rename, bit 2 = online
static void orsc_handle_friend_update(mudclient *mud, int8_t *data, int size) {
    size_t offset = 1;

    char current_name[MAX_USER_LENGTH + 1] = {0};
    char former_name[MAX_USER_LENGTH + 1] = {0};

    orsc_read_string(data, &offset, (size_t)size, current_name,
                     (int)sizeof(current_name));

    orsc_read_string(data, &offset, (size_t)size, former_name,
                     (int)sizeof(former_name));

    int online_status = get_unsigned_byte(data, offset++, size);
    int is_rename = (online_status & 1) != 0;
    int is_online = (online_status & 4) != 0;

    if (is_online) {
        // world name, not a number; read to keep framing then drop
        char world_name[32] = {0};

        orsc_read_string(data, &offset, (size_t)size, world_name,
                         (int)sizeof(world_name));
    }

    int64_t current_encoded = encode_username(current_name);
    int64_t former_encoded = encode_username(former_name);

    // world column is a name that can't map to a number; collapse to the
    // "online, same world" sentinel
    int world = is_online ? MUD_FRIEND_ONLINE(mud) : 0;

    int64_t match = is_rename ? former_encoded : current_encoded;

    for (int i = 0; i < mud->friend_list_count; i++) {
        if (mud->friend_list[i] != match) {
            continue;
        }

        if (mud->friend_list_online[i] == 0 && is_online) {
            char formatted[MAX_USER_LENGTH + 20] = {0};

            sprintf(formatted, "@pri@%s has logged in", current_name);
            mudclient_show_server_message(mud, formatted);
        }

        if (mud->friend_list_online[i] != 0 && !is_online) {
            char formatted[MAX_USER_LENGTH + 21] = {0};

            sprintf(formatted, "@pri@%s has logged out", current_name);
            mudclient_show_server_message(mud, formatted);
        }

        mud->friend_list[i] = current_encoded;
        mud->friend_list_online[i] = world;

        mudclient_sort_friends(mud);
        return;
    }

    // unmatched: a rename for an unknown name must not append
    if (is_rename) {
        return;
    }

    if (mud->friend_list_count >= (SOCIAL_LIST_MAX * 2)) {
        return;
    }

    mud->friend_list[mud->friend_list_count] = current_encoded;
    mud->friend_list_online[mud->friend_list_count] = world;
    mud->friend_list_count++;

    mudclient_sort_friends(mud);
}

// custom SEND_UPDATE_NPC: shared opcode, different wire. npc chat is a newline-terminated string; types 3-7 custom-only
// type 1's message is consumed but applied only when the npc is known; type 7 sets bubble_item without arming the timeout
static void orsc_handle_npc_update(mudclient *mud, int8_t *data, int size) {
    int count = get_unsigned_short(data, 1, size);
    int offset = 3;

    for (int i = 0; i < count && offset < size; i++) {
        int npc_index = get_unsigned_short(data, offset, size);
        offset += 2;

        GameCharacter *npc =
            npc_index < NPCS_SERVER_MAX ? mud->npcs_server[npc_index] : NULL;

        int update_type = get_unsigned_byte(data, offset++, size);

        if (update_type == 1) {
            // npc chat
            int chat_recipient = get_unsigned_short(data, offset, size);
            offset += 2;

            char message[255] = {0};
            size_t message_offset = (size_t)offset;

            orsc_read_string(data, &message_offset, (size_t)size, message,
                             (int)sizeof(message));

            offset = (int)message_offset;

            if (npc == NULL) {
                continue;
            }

            npc->message_timeout = 150;
            strcpy(npc->message, message);

            // only the addressed player sees it in the log, as quest chat
            if (mud->local_player != NULL &&
                mud->local_player->server_index == chat_recipient) {
                char formatted[320];

                snprintf(formatted, sizeof(formatted), "@yel@%s: %s",
                         game_data.npcs[npc->npc_id].name, message);

                mudclient_show_message(mud, formatted, MESSAGE_TYPE_QUEST);
            }
        } else if (update_type == 2) {
            // damage and hp
            int damage = get_unsigned_byte(data, offset++, size);
            int current = get_unsigned_byte(data, offset++, size);
            int max = get_unsigned_byte(data, offset++, size);

            if (npc != NULL) {
                npc->damage_taken = damage;
                npc->current_hits = current;
                npc->max_hits = max;
                npc->combat_timer = 200;
            }
        } else if (update_type == 3 || update_type == 4) {
            // incoming projectile: 3 from an npc, 4 from a player
            int projectile_sprite = get_unsigned_short(data, offset, size);
            offset += 2;

            int shooter_index = get_unsigned_short(data, offset, size);
            offset += 2;

            int limit = update_type == 3 ? NPCS_SERVER_MAX : PLAYERS_SERVER_MAX;

            if (shooter_index >= limit) {
                continue;
            }

            if (npc != NULL) {
                npc->incoming_projectile_sprite = projectile_sprite;
                npc->projectile_range = PROJECTILE_RANGE_MAX;

                if (update_type == 3) {
                    npc->attacking_npc_server_index = shooter_index;
                    npc->attacking_player_server_index = -1;
                } else {
                    npc->attacking_player_server_index = shooter_index;
                    npc->attacking_npc_server_index = -1;
                }
            }
        } else if (update_type == 5) {
            // skull
            int skull = get_unsigned_byte(data, offset++, size);

            if (npc != NULL) {
                npc->skull_visible = skull;
            }
        } else if (update_type == 6) {
            // wield
            int wield = get_unsigned_byte(data, offset++, size);
            int wield2 = get_unsigned_byte(data, offset++, size);

            if (npc != NULL) {
                npc->wield = wield;
                npc->wield2 = wield2;
            }
        } else if (update_type == 7) {
            // action bubble
            int item_id = get_unsigned_short(data, offset, size);
            offset += 2;

            if (item_id >= game_data.item_count) {
                item_id = IRON_MACE_ID;
            }

            if (npc != NULL) {
                // no bubble_timeout
                npc->bubble_item = item_id;
            }
        } else {
            return;
        }
    }
}
#endif

void mudclient_update_ground_item_models(mudclient *mud) {
    for (int i = 0; i < GROUND_ITEMS_MAX; i++) {
        if (mud->ground_items[i].model == NULL) {
            continue;
        }

        scene_remove_model(mud->scene, mud->ground_items[i].model);

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
        game_model_destroy(mud->ground_items[i].model);
#endif

        free(mud->ground_items[i].model);

        mud->ground_items[i].model = NULL;
    }

    if (!mud->options->ground_item_models) {
        return;
    }

    for (int i = 0; i < mud->ground_item_count; i++) {
        int item_id = mud->ground_items[i].id;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        GameModel *original_model = mud->item_models[item_id];

        if (original_model == NULL) {
            continue;
        }

        GameModel *model = game_model_copy(original_model);
#else
        int sprite_id = game_data.items[item_id].sprite;
        GameModel *original_model = mud->item_models[sprite_id];

        if (original_model == NULL) {
            continue;
        }

        GameModel *model = game_model_copy(original_model);

        int mask_colour = game_data.items[item_id].mask;

        if (mask_colour != 0) {
            game_model_mask_faces(model, model->face_fill_back, mask_colour);
            game_model_mask_faces(model, model->face_fill_front, mask_colour);
        }
#endif

        model->key = i + GROUND_ITEM_FACE_TAG;

        int area_x = mud->ground_items[i].x;
        int area_y = mud->ground_items[i].y;
        int model_x = ((area_x + area_x + 1) * MAGIC_LOC) / 2;
        int model_y = ((area_y + area_y + 1) * MAGIC_LOC) / 2;

        game_model_translate(
            model, model_x,
            -(world_get_elevation(mud->world, model_x, model_y) +
              mud->ground_items[i].z) -
                10,
            model_y);

        game_model_set_light(model, 1, 48, 48, -50, -10, -50);

        scene_add_model(mud->scene, model);

        mud->ground_items[i].model = model;
    }

#ifdef RENDER_3DS_GL
    if (mud->ground_item_count > 0) {
        GameModel *ground_item_model[mud->ground_item_count];

        for (int i = 0; i < mud->ground_item_count; i++) {
            ground_item_model[i] = mud->ground_items[i].model;
        }

        game_model_gl_buffer_models(&mud->scene->gl_item_buffers,
                                    &mud->scene->gl_item_buffer_length,
                                    ground_item_model, mud->ground_item_count,
                                    0, 0);
    }
#endif
}

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void mudclient_gl_update_wall_models(mudclient *mud) {
    int vbo_offset = 0;
    int ebo_offset = 0;

#if defined(__vita__) && defined(RENDER_GL)
    // plan the door buffer's two-segment EBO layout: segment A = noclip-safe
    // door faces, segment B = alpha-cutout (gates/bars)
    int noclip_total = 0;

    for (int i = 0; i < mud->wall_object_count; i++) {
        GameModel *game_model = mud->wall_objects[i].model;

        game_model->gl_ebo_length = 6;
        noclip_total += game_model_gl_classify(game_model, NULL);
    }

    int a_cursor = 0;
    int b_cursor = noclip_total;

    for (int i = 0; i < mud->wall_object_count; i++) {
        GameModel *game_model = mud->wall_objects[i].model;

        game_model->gl_buffer = mud->scene->gl_wall_buffers[0];

        int noclip_length = game_model_gl_classify(game_model, NULL);

        game_model->gl_vbo_offset = vbo_offset;
        game_model->gl_ebo_offset = a_cursor;
        game_model->gl_noclip_ebo_length = noclip_length;
        game_model->gl_clip_ebo_offset = b_cursor;
        game_model->gl_clip_ebo_length = 6 - noclip_length;

        a_cursor += noclip_length;
        b_cursor += 6 - noclip_length;

        game_model_gl_buffer_arrays(game_model, &vbo_offset, &ebo_offset);

        vbo_offset += 4;
        ebo_offset += 6;
    }
#else
    for (int i = 0; i < mud->wall_object_count; i++) {
        GameModel *game_model = mud->wall_objects[i].model;

        game_model->gl_buffer = mud->scene->gl_wall_buffers[0];
        game_model->gl_ebo_length = 6;

        game_model->gl_vbo_offset = vbo_offset;
        game_model->gl_ebo_offset = ebo_offset;

        game_model_gl_buffer_arrays(game_model, &vbo_offset, &ebo_offset);

        vbo_offset += 4;
        ebo_offset += 6;
    }
#endif

#ifdef RENDER_GL
    if (mud->wall_object_count > 0) {
        vertex_buffer_gl_flush(mud->scene->gl_wall_buffers[0], 0);
    }
#endif
}
#endif


#ifndef REVISION_177
// SEND_SERVER_CONFIGS (19): the 90 per-world entries a custom world pushes right
// after login; the SP/LAN server sends the same packet
static void packet_handler_server_configs(mudclient *mud, int8_t *data,
                                          int size) {
    // 90 entries; positions 1,2,42,45,87,88 are newline-terminated strings, the rest single bytes
    // walk them in order and capture the per-world flags
    int off = 1; // skip the opcode byte
    for (int pos = 1; pos <= 90 && off < size; pos++) {
        if (pos == 1 || pos == 2 || pos == 42 || pos == 45 ||
            pos == 87 || pos == 88) {
            // position 1 is the world's display name, used for the welcome box title
            if (pos == 1) {
                int name_len = 0;
                while (off + name_len < size &&
                       get_unsigned_byte(data, off + name_len, size) !=
                           10 &&
                       name_len <
                           (int)sizeof(mud->orsc.server_name) - 1) {
                    mud->orsc.server_name[name_len] =
                        (char)get_unsigned_byte(data, off + name_len,
                                                size);
                    name_len++;
                }
                mud->orsc.server_name[name_len] = '\0';
            }
            while (off < size && get_unsigned_byte(data, off, size) != 10) {
                off++;
            }
            off++; // step past the newline terminator
        } else {
            int v = get_unsigned_byte(data, off++, size);
            switch (pos) {
            case 4:  mud->orsc.spawn_auction_npcs = v; break;
            case 6:  mud->orsc.floating_nametags = v; break;
            case 7:  mud->orsc.want_clans = v; break;
            case 8:  mud->orsc.want_kill_feed = v; break;
            case 12: mud->orsc.batch_progression = v; break;
            case 13: mud->orsc.side_menu = v; break;
            case 18: mud->orsc.experience_counter_toggle = v; break;
            case 19: mud->orsc.experience_drops_toggle = v; break;
            case 25: mud->orsc.want_skill_menus = v; break;
            case 26: mud->orsc.want_quest_menus = v; break;
            case 28: mud->orsc.want_keyboard_shortcuts = v; break;
            case 32: mud->orsc.want_cert_deposit = v; break;
            case 34: mud->orsc.want_drop_x = v; break;
            case 35: mud->orsc.want_exp_info = v; break;
            case 41: mud->orsc.want_fixed_overhead_chat = v; break;
            case 67: mud->orsc.want_leftclick_webs = v; break;
            case 27: mud->orsc.want_elixirs = v; break;
            case 29: mud->orsc.want_custom_banks = v; break;
            case 30: mud->orsc.want_bank_pins = v; break;
            case 31: mud->orsc.want_bank_notes = v; break;
            case 39: mud->orsc.want_custom_rank_display = v; break;
            case 40: mud->orsc.right_click_bank = v; break;
            case 51: mud->orsc.want_fatigue = v; break;
            case 57: mud->orsc.want_quest_started_indicator = v; break;
            case 52: mud->orsc.custom_sprites = v; break;
            case 60: mud->orsc.want_runecraft = v; break;
            case 61: mud->orsc.custom_landscape = v; break;
            case 62: mud->orsc.want_equipment_tab = v; break;
            case 63: mud->orsc.want_bank_presets = v; break;
            case 64: mud->orsc.want_parties = v; break;
            case 71: mud->orsc.character_creation_mode = v; break;
            case 72: mud->orsc.skilling_exp_rate = v; break;
            case 73: mud->orsc.want_harvesting = v; break;
            case 76: mud->orsc.right_click_trade = v; break;
            case 77: mud->orsc.features_sleep = v; break;
            case 79: mud->orsc.want_cert_as_notes = v; break;
            case 80: mud->orsc.want_openpk_points = v; break;
            case 90: mud->orsc.want_nature_rune_protection = v; break;
            default: break;
            }
        }
    }

    // resolve the config-dependent npc commands (shopOption/bankerOption token rows)
    game_data_resolve_online_npc_commands(mud->orsc.right_click_trade,
                                          mud->orsc.right_click_bank,
                                          mud->orsc.spawn_auction_npcs);

    // turn the custom landscape overlay on if config 61 says this world uses it
    if (mud->world != NULL) {
        mud->world->apply_custom_landscape = mud->orsc.custom_landscape;
    }

}
#endif
void mudclient_packet_tick(mudclient *mud) {
    uint64_t timestamp = get_ticks();

#ifdef __vita__
    // socket syscalls run only on the first packet tick of each rendered frame;
    // catch-up ticks still parse packets already in the buffer
    int do_io = !mud->gl_net_io_this_frame;
    mud->gl_net_io_this_frame = 1;

    if (!mud->packet_stream->singleplayer) {
        mud->packet_stream->recv_skip = !do_io;

        if (do_io) {
            // drain any bytes the non-blocking socket refused earlier
            packet_stream_send_pump(mud->packet_stream);
        }
    }
#endif

    if (packet_stream_has_packet(mud->packet_stream)) {
        mud->packet_last_read = timestamp;
    }

    if (timestamp - mud->packet_last_read > 5000) {
        mud->packet_last_read = timestamp;
        packet_stream_new_packet(mud->packet_stream, CLIENT_PING);
        packet_stream_send_packet(mud->packet_stream);
    }

    if (packet_stream_write_packet(mud->packet_stream, 20) < 0) {
        mudclient_lost_connection(mud);
        return;
    }

    int size =
        packet_stream_read_packet(mud->packet_stream, mud->incoming_packet);

    if (size <= 0) {
        return;
    }

    int8_t *data = mud->incoming_packet;
    ServerOpcode opcode = data[0] & 0xff;

#ifndef NO_ISAAC
    if (mud->packet_stream->isaac_ready) {
        opcode = (opcode - isaac_next(&mud->packet_stream->isaac_in)) & 0xff;
    }
#endif

#ifndef REVISION_177
    // revision-177 world: map the wire opcode to its canonical 204 value
    if (mud->packet_stream->protocol177) {
        int mapped = protocol177_server_opcode(opcode);

        if (mapped < 0) {
            return; // packet with no 204 equivalent: ignore
        }

        opcode = mapped;
    }

    // capture the translated opcode so utility.c's over-read warnings can name the packet
    rsc_debug_last_opcode = opcode;

    // custom world reuses the canonical-204 opcode numbers for shared packets plus ~27 custom-only opcodes
    // SEND_SERVER_CONFIGS (19) is custom-only and pushed after login, so skip it here; the rest fall through to the switch default
    if (mud->packet_stream->protocol_custom && opcode == 19) {
        packet_handler_server_configs(mud, data, size);
        return;
    }
#endif

    switch (opcode) {
#ifndef REVISION_177
    case SERVER_SERVER_CONFIGS:
        // the SP/LAN server pushes the same 90 entries as a custom world
        if (MUD_SP_WIRE(mud)) {
            packet_handler_server_configs(mud, data, size);
        }
        break;
#endif
    case SERVER_WORLD_INFO:
        if (mud->local_player_server_index >= PLAYERS_SERVER_MAX) {
            return;
        }
        mud->loading_area = 1;
        mud->local_player_server_index = get_unsigned_short(data, 1, size);
        mud->plane_width = get_unsigned_short(data, 3, size);
        mud->plane_height = get_unsigned_short(data, 5, size);
        mud->plane_index = get_unsigned_short(data, 7, size);
        mud->plane_multiplier = get_unsigned_short(data, 9, size);
        mud->plane_height -= mud->plane_index * mud->plane_multiplier;
#ifdef RSC_DIAG
        DIAG("world_info idx=%d plane_w=%d plane_h=%d plane_idx=%d mult=%d "
             "(region %d,%d death_timer=%d)",
             mud->local_player_server_index, mud->plane_width,
             mud->plane_height, mud->plane_index, mud->plane_multiplier,
             mud->region_x, mud->region_y, mud->death_screen_timeout);
        diag_trace_packets = 8;
#endif
        break;
    case SERVER_REGION_PLAYERS: {
        mud->known_player_count = mud->player_count;

        memcpy(mud->known_players, mud->players,
               mud->known_player_count * sizeof(GameCharacter *));

        int offset = 8;

        mud->local_region_x = get_bit_mask(data, offset, size, 11);
        offset += 11;

        mud->local_region_y = get_bit_mask(data, offset, size, 13);
        offset += 13;

        int sprite = get_bit_mask(data, offset, size, 4);
        offset += 4;

#ifdef RSC_DIAG
        // logs the raw wire position before the region window is applied: on a jump over 2 tiles, for a few
        // packets after a death or world-info, during the death screen, and every 64th packet
        {
            int raw_x = mud->local_region_x;
            int raw_y = mud->local_region_y;
            int jump = diag_last_raw_x >= 0 &&
                       (abs(raw_x - diag_last_raw_x) > 2 ||
                        abs(raw_y - diag_last_raw_y) > 2);
            int abs_y = raw_y + mud->plane_height;
            int abs_x = raw_x + mud->plane_width;

            diag_region_packets++;

            if (jump || diag_trace_packets > 0 ||
                mud->death_screen_timeout != 0 ||
                (diag_region_packets % 64) == 0) {
                DIAG("region_players raw=%d,%d abs=%d,%d plane_h=%d "
                     "region=%d,%d sprite=%d death_timer=%d loading=%d%s "
                     "(other_hits=%d npc_hits=%d)",
                     raw_x, raw_y, abs_x, abs_y, mud->plane_height,
                     mud->region_x, mud->region_y, sprite,
                     mud->death_screen_timeout, mud->loading_area,
                     jump ? " JUMP" : "", diag_other_hits, diag_npc_hits);
                if (diag_trace_packets > 0) {
                    diag_trace_packets--;
                }
            }

            diag_last_raw_x = raw_x;
            diag_last_raw_y = raw_y;
        }
#endif

        int has_loaded_region = mudclient_load_next_region(
            mud, mud->local_region_x, mud->local_region_y);

        mud->local_region_x -= mud->region_x;
        mud->local_region_y -= mud->region_y;

#ifdef RSC_DIAG
        if (has_loaded_region) {
            DIAG("region_players loaded region -> local=%d,%d region=%d,%d",
                 mud->local_region_x, mud->local_region_y, mud->region_x,
                 mud->region_y);
        }
#endif

        // start a background build of the neighbour region the player is approaching
        mudclient_region_proximity_check(mud);

        int player_x = mud->local_region_x * MAGIC_LOC + 64;
        int player_y = mud->local_region_y * MAGIC_LOC + 64;

        if (has_loaded_region) {
            // hard-reset the walk only when client and server positions disagree;
            // a prebuilt crossing keeps its rebased movement
            int dx = mud->local_player->current_x - player_x;
            int dy = mud->local_player->current_y - player_y;

            if (dx < 0) {
                dx = -dx;
            }
            if (dy < 0) {
                dy = -dy;
            }

            if (dx > 3 * MAGIC_LOC || dy > 3 * MAGIC_LOC) {
                mud->local_player->waypoint_current = 0;
                mud->local_player->moving_step = 0;

                mud->local_player->current_x =
                    mud->local_player->waypoints_x[0] = player_x;

                mud->local_player->current_y =
                    mud->local_player->waypoints_y[0] = player_y;

                // snap the camera anchor with the player on a hard reset so they
                // move together; a prebuilt crossing skips this branch
                mud->camera_auto_rotate_player_x = player_x;
                mud->camera_auto_rotate_player_y = player_y;
            }
        }

        mud->player_count = 0;

        mud->local_player = mudclient_add_player(
            mud, mud->local_player_server_index, player_x, player_y, sprite);

        int length = get_bit_mask(data, offset, size, 8);
        offset += 8;

        for (int i = 0; i < length; i++) {
            GameCharacter *player = mud->known_players[i + 1];
            int has_updated = get_bit_mask(data, offset, size, 1);

            offset++;

            if (has_updated != 0) {
                int update_type = get_bit_mask(data, offset, size, 1);
                offset++;

                if (update_type == 0) {
                    int sprite = get_bit_mask(data, offset, size, 3);
                    offset += 3;

                    // known_players[i+1] can be NULL; consume the bits to stay aligned but skip the deref
                    if (player != NULL) {
                        int waypoint_current = player->waypoint_current;
                        int player_x = player->waypoints_x[waypoint_current];
                        int player_y = player->waypoints_y[waypoint_current];

                        if (sprite == 2 || sprite == 1 || sprite == 3) {
                            player_x += MAGIC_LOC;
                        }

                        if (sprite == 6 || sprite == 5 || sprite == 7) {
                            player_x -= MAGIC_LOC;
                        }

                        if (sprite == 4 || sprite == 3 || sprite == 5) {
                            player_y += MAGIC_LOC;
                        }

                        if (sprite == 0 || sprite == 1 || sprite == 7) {
                            player_y -= MAGIC_LOC;
                        }

                        player->next_animation = sprite;

                        player->waypoint_current = waypoint_current =
                            (waypoint_current + 1) % 10;

                        player->waypoints_x[waypoint_current] = player_x;
                        player->waypoints_y[waypoint_current] = player_y;
                    }
                } else {
                    int sprite = get_bit_mask(data, offset, size, 4);

                    if ((sprite & 12) == 12) {
                        offset += 2;
                        continue;
                    }

                    int next_animation = get_bit_mask(data, offset, size, 4);
                    offset += 4;

                    if (player != NULL) {
                        player->next_animation = next_animation;
                    }
                }
            }

            if (player != NULL) {
                mud->players[mud->player_count++] = player;
            }
        }

        int player_count = 0;

        while (offset + 24 < size * 8) {
            int server_index = get_bit_mask(data, offset, size, 11);
            offset += 11;

            if (server_index >= PLAYERS_SERVER_MAX) {
                return;
            }

            // custom new-player record is a different shape: index 11, offX/offY 6 bits each, sprite 4, no is-known bit = 27 bits
            // authentic uses 5-bit offsets + an is-known bit = 26; sign-correct at 31/64
            int offset_bits = 5;
            int has_known_bit = 1;

#ifndef REVISION_177
            if (mud->protocol_custom) {
                offset_bits = 6;
                has_known_bit = 0;
            }
#endif

            int sign_limit = (1 << (offset_bits - 1)) - 1; // 15 or 31
            int sign_span = 1 << offset_bits;              // 32 or 64

            int area_x = get_bit_mask(data, offset, size, offset_bits);
            offset += offset_bits;

            if (area_x > sign_limit) {
                area_x -= sign_span;
            }

            int area_y = get_bit_mask(data, offset, size, offset_bits);
            offset += offset_bits;

            if (area_y > sign_limit) {
                area_y -= sign_span;
            }

            int sprite = get_bit_mask(data, offset, size, 4);
            offset += 4;

            int is_player_known = 0;

            if (has_known_bit) {
                is_player_known = get_bit_mask(data, offset, size, 1);
                offset++;
            }

            int x = (mud->local_region_x + area_x) * MAGIC_LOC + 64;
            int y = (mud->local_region_y + area_y) * MAGIC_LOC + 64;

            mudclient_add_player(mud, server_index, x, y, sprite);


            // player_server_indexes[] is PLAYERS_MAX (500); clamp the store so a
            // crowded region can't write past the array
            if (is_player_known == 0 && has_known_bit &&
                player_count < PLAYERS_MAX) {
                mud->player_server_indexes[player_count++] = server_index;
            }
        }

        // opcode 163 (known-players request) is never sent on a custom world;
        // custom pushes appearances unprompted via SEND_UPDATE_PLAYERS (234)
        int want_known_players = player_count > 0;

#ifndef REVISION_177
        if (mud->protocol_custom) {
            want_known_players = 0;
        }
#endif

        if (want_known_players) {
            packet_stream_new_packet(mud->packet_stream, CLIENT_KNOWN_PLAYERS);
            packet_stream_put_short(mud->packet_stream, player_count);

            for (int i = 0; i < player_count; i++) {
                GameCharacter *player =
                    mud->player_server[mud->player_server_indexes[i]];

                packet_stream_put_short(mud->packet_stream,
                                        player->server_index);

                packet_stream_put_short(mud->packet_stream, player->server_id);
            }

            packet_stream_send_packet(mud->packet_stream);
        }
        break;
    }
    case SERVER_REGION_PLAYER_UPDATE: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            orsc_handle_player_update(mud, data, size);
            break;
        }
#endif

        int length = get_unsigned_short(data, 1, size);
        int offset = 3;

        for (int i = 0; i < length; i++) {
            int player_index = get_unsigned_short(data, offset, size);
            offset += 2;

            GameCharacter *player = mud->player_server[player_index];

            int update_type = get_unsigned_byte(data, offset++, size);

            if (update_type == 0) {
                /* action bubble with an item in it */
                int item_id = get_unsigned_short(data, offset, size);

                if (item_id >= game_data.item_count) {
                    item_id = IRON_MACE_ID;
                }

                offset += 2;

                if (player != NULL) {
                    player->bubble_timeout = 150;
                    player->bubble_item = item_id;
                }
            } else if (update_type == 1) {
                /* chat */
                int message_length = get_unsigned_byte(data, offset++, size);

                if (player != NULL && message_length <= (size - offset)) {
                    char *message =
                        chat_message_decode(data, offset, message_length);

                    /*if (mud->options->word_filter) {
                        message = word_filter_filter(message);
                    }*/

                    int ignored = 0;

                    for (int j = 0; j < mud->ignore_list_count; j++) {
                        if (mud->ignore_list[j] == player->encoded_username) {
                            ignored = 1;
                            break;
                        }
                    }

                    if (!ignored) {
                        player->message_timeout = 150;
                        strcpy(player->message, message);

                        char formatted_message[strlen(player->name) +
                                               strlen(player->message) + 3];

                        sprintf(formatted_message, "%s: %s", player->name,
                                player->message);

                        mudclient_show_message(mud, formatted_message,
                                               MESSAGE_TYPE_CHAT);
                    }
                }

                offset += message_length;
            } else if (update_type == 2) {
                /* combat damage and hp */
                int damage = get_unsigned_byte(data, offset++, size);
                int current = get_unsigned_byte(data, offset++, size);
                int max = get_unsigned_byte(data, offset++, size);

#ifdef RSC_DIAG
                if (player == NULL) {
                    DIAG("hit for UNKNOWN player idx=%d dmg=%d hp=%d/%d "
                         "(local idx=%d)",
                         player_index, damage, current, max,
                         mud->local_player_server_index);
                } else if (player == mud->local_player) {
                    DIAG("hit LOCAL idx=%d dmg=%d hp=%d/%d (skill hits %d/%d)",
                         player_index, damage, current, max,
                         mud->player_skill_current[SKILL_HITS],
                         mud->player_skill_base[SKILL_HITS]);
                } else {
                    diag_other_hits++;
                }
#endif

                if (player != NULL) {
                    player->damage_taken = damage;
                    player->current_hits = current;
                    player->max_hits = max;
                    player->combat_timer = 200;

                    if (player == mud->local_player) {
                        mud->player_skill_current[SKILL_HITS] = current;
                        mud->player_skill_base[SKILL_HITS] = max;
                        mud->show_dialog_welcome = 0;
                        mud->show_dialog_server_message = 0;
                    }
                }
            } else if (update_type == 3) {
                /* new incoming projectile to npc */
                int projectile_sprite = get_unsigned_short(data, offset, size);
                offset += 2;

                int npc_index = get_unsigned_short(data, offset, size);
                offset += 2;

                if (npc_index >= NPCS_SERVER_MAX) {
                    return;
                }

                if (player != NULL) {
                    player->incoming_projectile_sprite = projectile_sprite;
                    player->attacking_npc_server_index = npc_index;
                    player->attacking_player_server_index = -1;
                    player->projectile_range = PROJECTILE_RANGE_MAX;
                }
            } else if (update_type == 4) {
                /* new incoming projectile from player */
                int projectile_sprite = get_unsigned_short(data, offset, size);
                offset += 2;

                int opponent_index = get_unsigned_short(data, offset, size);
                offset += 2;

                if (opponent_index >= PLAYERS_SERVER_MAX) {
                    return;
                }

                if (player != NULL) {
                    player->incoming_projectile_sprite = projectile_sprite;
                    player->attacking_player_server_index = opponent_index;
                    player->attacking_npc_server_index = -1;
                    player->projectile_range = PROJECTILE_RANGE_MAX;
                }
            } else if (update_type == 5) {
                /* player appearance update */
                if (player != NULL) {
                    player->server_id = get_unsigned_short(data, offset, size);
                    offset += 2;

                    player->encoded_username =
                        get_unsigned_long(data, offset, size);
                    offset += 8;

                    decode_username(player->encoded_username, player->name);

                    int equipped_count = get_unsigned_byte(data, offset, size);
                    offset++;

                    // consume all equipped_count bytes for framing but store only
                    // into animations[ANIMATION_COUNT] (12); clamp so count > 12 can't overflow
                    for (int j = 0; j < equipped_count; j++) {
                        int anim = get_unsigned_byte(data, offset++, size);
                        if (j < ANIMATION_COUNT) {
                            player->animations[j] = anim;
                        }
                    }

                    for (int j = equipped_count; j < ANIMATION_COUNT; j++) {
                        player->animations[j] = 0;
                    }

                    player->hair_colour =
                        get_unsigned_byte(data, offset++, size);

                    player->top_colour =
                        get_unsigned_byte(data, offset++, size);

                    player->bottom_colour =
                        get_unsigned_byte(data, offset++, size);

                    player->skin_colour =
                        get_unsigned_byte(data, offset++, size);

                    player->level = get_unsigned_byte(data, offset++, size);

                    player->skull_visible =
                        get_unsigned_byte(data, offset++, size);

                } else {
                    offset += 14;

                    int unused = get_unsigned_byte(data, offset, size);
                    offset += unused + 1;
                }
            } else if (update_type == 6) {
                /* public chat */
                int message_length = get_unsigned_byte(data, offset++, size);

                if (player != NULL && message_length <= (size - offset)) {
                    char *message =
                        chat_message_decode(data, offset, message_length);

                    player->message_timeout = 150;
                    strcpy(player->message, message);

                    if (player == mud->local_player) {
                        char formatted_message[strlen(player->name) +
                                               strlen(player->message) + 3];

                        sprintf(formatted_message, "%s: %s", player->name,
                                player->message);

                        mudclient_show_message(mud, formatted_message,
                                               MESSAGE_TYPE_QUEST);
                    }
                }

                offset += message_length;
            }
        }
        break;
    }
    case SERVER_REGION_OBJECTS: {
        // this packet mutates the scenery object set the bake worker reads;
        // finish or drop any in-flight bake first
#if defined(RENDER_GL) && (defined(__vita__) || defined(__linux__))
        mudclient_gl_bake_cancel(mud);
#endif

#ifdef RENDER_GL
        int objects_before = mud->object_count;
        int objects_removed = 0;
        int objects_added = 0;
#endif

        for (int offset = 1; offset < size;) {
            if (get_unsigned_byte(data, offset, size) == 255) {
                /* remove the object */
                int index = 0;
                int l_x = (mud->local_region_x +
                           get_signed_byte(data, offset + 1, size)) /
                          8;
                int l_y = (mud->local_region_y +
                           get_signed_byte(data, offset + 2, size)) /
                          8;

                offset += 3;

                for (int i = 0; i < mud->object_count; i++) {
                    int o_x = (mud->objects[i].x / 8) - l_x;
                    int o_y = (mud->objects[i].y / 8) - l_y;

                    if (o_x != 0 || o_y != 0) {
                        if (i != index) {
                            mud->objects[index] = mud->objects[i];
                            mud->objects[index].model->key = index;
                        }

                        index++;
                    } else {
                        scene_remove_model(mud->scene, mud->objects[i].model);

                        world_remove_object(mud->world, mud->objects[i].id,
                                            mud->objects[i].x,
                                            mud->objects[i].y);

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
                        game_model_destroy(mud->objects[i].model);
#endif

                        free(mud->objects[i].model);
                        mud->objects[i].model = NULL;

#ifdef RENDER_GL
                        objects_removed++;
#endif
                    }
                }

                mud->object_count = index;
            } else {
                int object_id = get_unsigned_short(data, offset, size);
                offset += 2;

                int area_x =
                    mud->local_region_x + get_signed_byte(data, offset++, size);
                int area_y =
                    mud->local_region_y + get_signed_byte(data, offset++, size);

                int wire_direction = -1;

#ifndef REVISION_177
                if (mud->protocol_custom) {
                    // custom SEND_SCENERY_HANDLER appends a per-object direction byte that authentic 203/204 doesn't send
                    // register it into the tile grid before the 60000 check
                    wire_direction = get_signed_byte(data, offset++, size);
                    world_set_tile_direction(mud->world, area_x, area_y,
                                             wire_direction);
                }
#endif

                int object_index = 0;

                for (int i = 0; i < mud->object_count; i++) {
                    if (mud->objects[i].x != area_x ||
                        mud->objects[i].y != area_y) {
                        if (i != object_index) {
                            mud->objects[object_index].model =
                                mud->objects[i].model;

                            mud->objects[object_index].model->key =
                                object_index;
                            mud->objects[object_index].x = mud->objects[i].x;
                            mud->objects[object_index].y = mud->objects[i].y;
                            mud->objects[object_index].id = mud->objects[i].id;

                            mud->objects[object_index].direction =
                                mud->objects[i].direction;
                        }

                        object_index++;
                    } else {
                        scene_remove_model(mud->scene, mud->objects[i].model);

                        world_remove_object(mud->world, mud->objects[i].x,
                                            mud->objects[i].y,
                                            mud->objects[i].id);

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
                        game_model_destroy(mud->objects[i].model);
#endif

                        free(mud->objects[i].model);
                        mud->objects[i].model = NULL;
                    }
                }

                mud->object_count = object_index;

                if (object_id != 60000) {
                    if (object_id >= game_data.object_count) {
                        object_id = ODD_WELL_ID;
                    }
                    if (mud->object_count >= OBJECTS_MAX) {
                        return;
                    }

                    // on custom the wire direction is the tile direction, so this reads back what was stored
                    int tile_direction =
                        wire_direction >= 0
                            ? wire_direction
                            : world_get_tile_direction(mud->world, area_x,
                                                       area_y);

                    int width = 0;
                    int height = 0;

                    if (tile_direction == DIR_NORTH ||
                        tile_direction == DIR_SOUTH) {
                        width = game_data.objects[object_id].width;
                        height = game_data.objects[object_id].height;
                    } else {
                        height = game_data.objects[object_id].width;
                        width = game_data.objects[object_id].height;
                    }

                    int model_x = ((area_x + area_x + width) * MAGIC_LOC) / 2;
                    int model_y = ((area_y + area_y + height) * MAGIC_LOC) / 2;
                    int model_index = game_data.objects[object_id].model_index;

                    GameModel *model =
                        game_model_copy(mud->game_models[model_index]);

                    scene_add_model(mud->scene, model);

                    model->key = mud->object_count;

                    game_model_rotate(model, 0, tile_direction * 32, 0);

                    game_model_translate(
                        model, model_x,
                        -world_get_elevation(mud->world, model_x, model_y),
                        model_y);

                    game_model_set_light(model, 1, 48, 48, -50, -10, -50);

                    world_register_object(mud->world, area_x, area_y,
                                          object_id);

                    if (object_id == WINDMILL_SAILS_ID) {
                        game_model_translate(model, 0, -480, 0);
                    }

                    mud->objects[mud->object_count].x = area_x;
                    mud->objects[mud->object_count].y = area_y;
                    mud->objects[mud->object_count].id = object_id;
                    mud->objects[mud->object_count].direction = tile_direction;
                    mud->objects[mud->object_count++].model = model;

#ifdef RENDER_GL
                    objects_added++;
#endif
                }
            }
        }

#ifdef RENDER_3DS_GL
        if (mud->object_count > 0) {
            int object_count = mud->object_count + ANIMATED_MODELS_LENGTH + 8;
            GameModel *object_model[object_count];

            for (int i = 0; i < mud->object_count; i++) {
                object_model[i] = mud->objects[i].model;
            }

            int first_animated_index = 0;

            for (int i = 0; i < ANIMATED_MODELS_LENGTH; i++) {
                int name_length = strlen(animated_models[i]);
                char model_name[name_length + 1];

                strcpy(model_name, animated_models[i]);

                int model_index = game_data_get_model_index(model_name);

                object_model[mud->object_count + i] =
                    mud->game_models[model_index];

                if (model_name[name_length - 1] == '2') {
                    model_name[name_length - 1] = '1';

                    object_model[mud->object_count + ANIMATED_MODELS_LENGTH +
                                 first_animated_index] =
                        mud->game_models[game_data_get_model_index(model_name)];

                    first_animated_index++;
                }
            }

            game_model_gl_buffer_models(
                &mud->scene->gl_game_model_buffers,
                &mud->scene->gl_game_model_buffer_length, object_model,
                object_count, 0, 0);
        }
#endif

#ifdef RENDER_GL
        // re-bake and re-buffer only when the packet actually changed the object set
        if (objects_added > 0 || objects_removed > 0 ||
            mud->object_count != objects_before) {
            // defer the scenery bake and terrain lighting refresh to the next
            // draw_game so it runs once on the final object set
            mud->gl_region_bake_pending = 1;
#ifdef RSC_DIAG
            DIAG("region_objects +%d -%d count %d->%d local=%d,%d region=%d,%d",
                 objects_added, objects_removed, objects_before,
                 mud->object_count, mud->local_region_x, mud->local_region_y,
                 mud->region_x, mud->region_y);
            mudclient_diag_object_audit(mud, "after-objects-packet");
#endif
        }
#elif defined(RENDER_3DS_GL)
        world_gl_update_terrain_buffers(mud->world);
#endif

        break;
    }
    case SERVER_REGION_NPCS: {
        mud->known_npc_count = mud->npc_count;
        mud->npc_count = 0;

        memcpy(mud->known_npcs, mud->npcs,
               mud->known_npc_count * sizeof(GameCharacter *));

        int offset = 8;

        int length = get_bit_mask(data, offset, size, 8);
        offset += 8;

        for (int i = 0; i < length; i++) {
            GameCharacter *npc = mud->known_npcs[i];
            int has_updated = get_bit_mask(data, offset++, size, 1);

            if (has_updated != 0) {
                int has_moved = get_bit_mask(data, offset++, size, 1);

                if (has_moved == 0) {
                    int sprite = get_bit_mask(data, offset, size, 3);
                    offset += 3;

                    // known_npcs[i] can be NULL; consume the bits but skip the deref
                    if (npc != NULL) {
                        int waypoint_current = npc->waypoint_current;
                        int npc_x = npc->waypoints_x[waypoint_current];
                        int npc_y = npc->waypoints_y[waypoint_current];

                        if (sprite == 2 || sprite == 1 || sprite == 3) {
                            npc_x += MAGIC_LOC;
                        }

                        if (sprite == 6 || sprite == 5 || sprite == 7) {
                            npc_x -= MAGIC_LOC;
                        }

                        if (sprite == 4 || sprite == 3 || sprite == 5) {
                            npc_y += MAGIC_LOC;
                        }

                        if (sprite == 0 || sprite == 1 || sprite == 7) {
                            npc_y -= MAGIC_LOC;
                        }

                        npc->next_animation = sprite;

                        npc->waypoint_current = waypoint_current =
                            (waypoint_current + 1) % 10;

                        npc->waypoints_x[waypoint_current] = npc_x;
                        npc->waypoints_y[waypoint_current] = npc_y;
                    }
                } else {
                    int sprite = get_bit_mask(data, offset, size, 4);

                    if ((sprite & 12) == 12) {
                        offset += 2;
                        continue;
                    }

                    int next_animation = get_bit_mask(data, offset, size, 4);
                    offset += 4;

                    if (npc != NULL) {
                        npc->next_animation = next_animation;
                    }
                }
            }

            if (npc != NULL) {
                mud->npcs[mud->npc_count++] = npc;
            }
        }

        /* adding new NPCS */
        while (offset + 34 < size * 8) {
            int server_index = get_bit_mask(data, offset, size, 12);
            offset += 12;

            if (server_index >= NPCS_SERVER_MAX) {
                return;
            }

            // custom new-NPC offsets are 6 bits (authentic 5), sign at 31/64;
            // index (12) and npc id (10) don't vary
            int npc_offset_bits = 5;

#ifndef REVISION_177
            if (mud->protocol_custom) {
                npc_offset_bits = 6;
            }
#endif

            int npc_sign_limit = (1 << (npc_offset_bits - 1)) - 1;
            int npc_sign_span = 1 << npc_offset_bits;

            int area_x = get_bit_mask(data, offset, size, npc_offset_bits);
            offset += npc_offset_bits;

            if (area_x > npc_sign_limit) {
                area_x -= npc_sign_span;
            }

            int area_y = get_bit_mask(data, offset, size, npc_offset_bits);
            offset += npc_offset_bits;

            if (area_y > npc_sign_limit) {
                area_y -= npc_sign_span;
            }

            int sprite = get_bit_mask(data, offset, size, 4);
            offset += 4;

            int x = (mud->local_region_x + area_x) * MAGIC_LOC + 64;
            int y = (mud->local_region_y + area_y) * MAGIC_LOC + 64;

            int npc_id = get_bit_mask(data, offset, size, 10);
            offset += 10;

            if (npc_id >= game_data.npc_count) {
                npc_id = SHIFTY_MAN_ID;
            }

            mudclient_add_npc(mud, server_index, x, y, sprite, npc_id);
        }
        break;
    }
    case SERVER_REGION_NPC_UPDATE: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            orsc_handle_npc_update(mud, data, size);
            break;
        }
#endif

        int length = get_unsigned_short(data, 1, size);

        int offset = 3;

        for (int i = 0; i < length; i++) {
            int server_index = get_unsigned_short(data, offset, size);
            offset += 2;

            if (server_index >= NPCS_SERVER_MAX) {
                return;
            }

            GameCharacter *npc = mud->npcs_server[server_index];
            if (npc == NULL) {
                return;
            }

            int update_type = get_unsigned_byte(data, offset++, size);

            if (update_type == 1) {
                int target_index = get_unsigned_short(data, offset, size);
                offset += 2;

                int encoded_length = get_unsigned_byte(data, offset++, size);

                if (npc != NULL && encoded_length <= (size - offset)) {
                    char *message =
                        chat_message_decode(data, offset, encoded_length);

                    npc->message_timeout = 150;
                    strcpy(npc->message, message);

                    if (target_index == mud->local_player->server_index) {
                        char *npc_name = game_data.npcs[npc->npc_id].name;

                        char formatted_message[strlen(message) +
                                               strlen(npc_name) + 8];

                        sprintf(formatted_message, "@yel@%s: %s", npc_name,
                                message);

                        mudclient_show_message(mud, formatted_message,
                                               MESSAGE_TYPE_QUEST);
                    }
                }

                offset += encoded_length;
            } else if (update_type == 2) {
                int damage_taken = get_unsigned_byte(data, offset++, size);
                int current_health = get_unsigned_byte(data, offset++, size);
                int max_health = get_unsigned_byte(data, offset++, size);

#ifdef RSC_DIAG
                diag_npc_hits++;
                // the first 200 npc hits are logged, the rest only counted
                if (npc != NULL && mud->local_player != NULL &&
                    diag_npc_hits <= 200) {
                    DIAG("npc hit idx=%d id=%d dmg=%d hp=%d/%d "
                         "(local hp %d/%d, local opponent-tile %d,%d)",
                         server_index, npc->npc_id, damage_taken,
                         current_health, max_health,
                         mud->player_skill_current[SKILL_HITS],
                         mud->player_skill_base[SKILL_HITS], npc->current_x,
                         npc->current_y);
                }
#endif

                if (npc != NULL) {
                    npc->damage_taken = damage_taken;
                    npc->current_hits = current_health;
                    npc->max_hits = max_health;
                    npc->combat_timer = 200;
                }
            }
        }
        break;
    }
    case SERVER_REGION_ENTITY_UPDATE: {
        int length = (size - 1) / 4;

        for (int i = 0; i < length; i++) {
            int delta_x = (mud->local_region_x +
                           get_signed_short(data, 1 + i * 4, size)) /
                          8;

            int delta_y = (mud->local_region_y +
                           get_signed_short(data, 3 + i * 4, size)) /
                          8;

            int entity_count = 0;

            for (int j = 0; j < mud->ground_item_count; j++) {
                int x = (mud->ground_items[j].x / 8) - delta_x;
                int y = (mud->ground_items[j].y / 8) - delta_y;

                if (x != 0 || y != 0) {
                    if (j != entity_count) {
                        mud->ground_items[entity_count].x =
                            mud->ground_items[j].x;

                        mud->ground_items[entity_count].y =
                            mud->ground_items[j].y;

                        mud->ground_items[entity_count].id =
                            mud->ground_items[j].id;

                        mud->ground_items[entity_count].z =
                            mud->ground_items[j].z;
                    }

                    entity_count++;
                }
            }

            mud->ground_item_count = entity_count;
            entity_count = 0;

            for (int j = 0; j < mud->object_count; j++) {
                int x = (mud->objects[j].x / 8) - delta_x;
                int y = (mud->objects[j].y / 8) - delta_y;

                if (x != 0 || y != 0) {
                    if (j != entity_count) {
                        mud->objects[entity_count] = mud->objects[j];
                        mud->objects[entity_count].model->key = entity_count;
                    }

                    entity_count++;
                } else {
                    scene_remove_model(mud->scene, mud->objects[j].model);

                    world_remove_object(mud->world, mud->objects[j].x,
                                        mud->objects[j].y, mud->objects[j].id);

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
                    game_model_destroy(mud->objects[j].model);
#endif
                    free(mud->objects[j].model);
                    mud->objects[j].model = NULL;
                }
            }

            mud->object_count = entity_count;
            entity_count = 0;

            for (int j = 0; j < mud->wall_object_count; j++) {
                int x = (mud->wall_objects[j].x / 8) - delta_x;
                int y = (mud->wall_objects[j].y / 8) - delta_y;

                if (x != 0 || y != 0) {
                    if (j != entity_count) {
                        mud->wall_objects[entity_count] = mud->wall_objects[j];
                        mud->wall_objects[entity_count].model->key =
                            entity_count + 10000;
                    }

                    entity_count++;
                } else {
                    scene_remove_model(mud->scene, mud->wall_objects[j].model);

                    world_remove_wall_object(mud->world, mud->wall_objects[j].x,
                                             mud->wall_objects[j].y,
                                             mud->wall_objects[j].direction,
                                             mud->wall_objects[j].id);

                    game_model_destroy(mud->wall_objects[j].model);
                    free(mud->wall_objects[j].model);
                    mud->wall_objects[j].model = NULL;
                }
            }

            mud->wall_object_count = entity_count;
        }

#if defined(RENDER_GL) && (defined(__vita__) || defined(__linux__))
        // defer and coalesce the GPU rebuild to once per frame
        mud->gl_wall_update_pending = 1;
        mud->gl_ground_item_update_pending = 1;
#else
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        mudclient_gl_update_wall_models(mud);
#endif
        mudclient_update_ground_item_models(mud);
#endif
        break;
    }
    case SERVER_REGION_WALL_OBJECTS: {
        for (int offset = 1; offset < size;) {
            if (get_unsigned_byte(data, offset, size) == 255) {
                /* remove the bound */
                int index = 0;
                int l_x = (mud->local_region_x +
                           get_signed_byte(data, offset + 1, size)) /
                          8;
                int l_y = (mud->local_region_y +
                           get_signed_byte(data, offset + 2, size)) /
                          8;

                offset += 3;

                for (int i = 0; i < mud->wall_object_count; i++) {
                    int s_x = (mud->wall_objects[i].x / 8) - l_x;
                    int s_y = (mud->wall_objects[i].y / 8) - l_y;

                    if (s_x != 0 || s_y != 0) {
                        if (i != index) {
                            mud->wall_objects[index] = mud->wall_objects[i];
                            mud->wall_objects[index].model->key = index + 10000;
                        }

                        index++;
                    } else {
                        scene_remove_model(mud->scene,
                                           mud->wall_objects[i].model);

                        world_remove_wall_object(mud->world,
                                                 mud->wall_objects[i].x,
                                                 mud->wall_objects[i].y,
                                                 mud->wall_objects[i].direction,
                                                 mud->wall_objects[i].id);

                        game_model_destroy(mud->wall_objects[i].model);
                        free(mud->wall_objects[i].model);
                        mud->wall_objects[i].model = NULL;
                    }
                }

                mud->wall_object_count = index;
            } else {
                int id = get_unsigned_short(data, offset, size);
                offset += 2;

                int l_x =
                    mud->local_region_x + get_signed_byte(data, offset++, size);
                int l_y =
                    mud->local_region_y + get_signed_byte(data, offset++, size);
                int direction = get_signed_byte(data, offset++, size);
                int count = 0;

                for (int i = 0; i < mud->wall_object_count; i++) {
                    if (mud->wall_objects[i].x != l_x ||
                        mud->wall_objects[i].y != l_y ||
                        mud->wall_objects[i].direction != direction) {
                        if (i != count) {
                            mud->wall_objects[count] = mud->wall_objects[i];
                            mud->wall_objects[count].model->key = count + 10000;
                        }

                        count++;
                    } else {
                        scene_remove_model(mud->scene,
                                           mud->wall_objects[i].model);

                        world_remove_wall_object(mud->world,
                                                 mud->wall_objects[i].x,
                                                 mud->wall_objects[i].y,
                                                 mud->wall_objects[i].direction,
                                                 mud->wall_objects[i].id);

                        game_model_destroy(mud->wall_objects[i].model);
                        free(mud->wall_objects[i].model);
                        mud->wall_objects[i].model = NULL;
                    }
                }

                mud->wall_object_count = count;

                // boundary removal sentinel differs by protocol: authentic
                // (204/177) = 65535, custom = 60000
                int wall_remove_id = 65535;
#ifndef REVISION_177
                if (mud->protocol_custom) {
                    wall_remove_id = 60000;
                }
#endif

                if (id != wall_remove_id) {
                    if (id >= game_data.wall_object_count) {
                        id = ODD_LOOKING_WALL_ID;
                    }
                    if (mud->wall_object_count >= WALL_OBJECTS_MAX) {
                        return;
                    }

                    world_register_wall_object(mud->world, l_x, l_y, direction,
                                               id);

                    GameModel *model = mudclient_create_wall_object(
                        mud, l_x, l_y, direction, id, mud->wall_object_count);

                    mud->wall_objects[mud->wall_object_count].model = model;
                    mud->wall_objects[mud->wall_object_count].x = l_x;
                    mud->wall_objects[mud->wall_object_count].y = l_y;
                    mud->wall_objects[mud->wall_object_count].id = id;

                    mud->wall_objects[mud->wall_object_count++].direction =
                        direction;
                }
            }
        }

#if defined(RENDER_GL) && (defined(__vita__) || defined(__linux__))
        mud->gl_wall_update_pending = 1; // coalesced, drained once/frame
#elif defined(RENDER_GL) || defined(RENDER_3DS_GL)
        mudclient_gl_update_wall_models(mud);
#endif

        break;
    }
    case SERVER_REGION_GROUND_ITEMS: {
        for (int offset = 1; offset < size;) {
            if (get_unsigned_byte(data, offset, size) == 255) {
                /* remove the item */
                int index = 0;

                int l_x = (mud->local_region_x +
                           get_signed_byte(data, offset + 1, size)) /
                          8;

                int l_y = (mud->local_region_y +
                           get_signed_byte(data, offset + 2, size)) /
                          8;

                offset += 3;

#ifndef REVISION_177
                if ((mud->protocol_custom || MUD_SP_WIRE(mud)) &&
                    mud->orsc.want_bank_notes) {
                    // a noted byte trails even the out-of-range cull entry; read and discard it
                    offset++;
                }
#endif

                for (int i = 0; i < mud->ground_item_count; i++) {
                    int g_x = (mud->ground_items[i].x / 8) - l_x;
                    int g_y = (mud->ground_items[i].y / 8) - l_y;

                    if (g_x != 0 || g_y != 0) {
                        if (i != index) {
                            mud->ground_items[index].x = mud->ground_items[i].x;
                            mud->ground_items[index].y = mud->ground_items[i].y;

                            mud->ground_items[index].id =
                                mud->ground_items[i].id;

                            mud->ground_items[index].z = mud->ground_items[i].z;

                            mud->ground_items[index].noted =
                                mud->ground_items[i].noted;
                        }

                        index++;
                    }
                }

                mud->ground_item_count = index;
            } else {
                int item_id = get_unsigned_short(data, offset, size);
                offset += 2;

                int area_x =
                    mud->local_region_x + get_signed_byte(data, offset++, size);
                int area_y =
                    mud->local_region_y + get_signed_byte(data, offset++, size);

                int item_noted = 0;

#ifndef REVISION_177
                if ((mud->protocol_custom || MUD_SP_WIRE(mud)) &&
                    mud->orsc.want_bank_notes) {
                    item_noted = get_unsigned_byte(data, offset++, size) == 1;
                }
#endif

                if ((item_id & 32768) == 0) {
                    if (item_id >= game_data.item_count) {
                        item_id = IRON_MACE_ID;
                    }

                    if (mud->ground_item_count >= GROUND_ITEMS_MAX) {
                        return;
                    }

                    mud->ground_items[mud->ground_item_count].x = area_x;
                    mud->ground_items[mud->ground_item_count].y = area_y;
                    mud->ground_items[mud->ground_item_count].id = item_id;
                    mud->ground_items[mud->ground_item_count].z = 0;
                    mud->ground_items[mud->ground_item_count].noted = item_noted;

                    for (int i = 0; i < mud->object_count; i++) {
                        if (mud->objects[i].x != area_x ||
                            mud->objects[i].y != area_y) {
                            continue;
                        }

                        mud->ground_items[mud->ground_item_count].z =
                            game_data.objects[mud->objects[i].id].elevation;

                        break;
                    }

                    mud->ground_item_count++;
                } else {
                    item_id &= 32767;

                    int index = 0;

                    for (int i = 0; i < mud->ground_item_count; i++) {
                        if (mud->ground_items[i].x != area_x ||
                            mud->ground_items[i].y != area_y ||
                            mud->ground_items[i].id != item_id) {

                            if (i != index) {
                                mud->ground_items[index].x =
                                    mud->ground_items[i].x;

                                mud->ground_items[index].y =
                                    mud->ground_items[i].y;

                                mud->ground_items[index].id =
                                    mud->ground_items[i].id;

                                mud->ground_items[index].z =
                                    mud->ground_items[i].z;

                                mud->ground_items[index].noted =
                                    mud->ground_items[i].noted;
                            }

                            index++;
                        } else {
                            item_id = -123;
                        }
                    }

                    mud->ground_item_count = index;
                }
            }
        }

#if defined(RENDER_GL) && (defined(__vita__) || defined(__linux__))
        mud->gl_ground_item_update_pending = 1; // coalesced, drained once/frame
#else
        mudclient_update_ground_item_models(mud);
#endif

        break;
    }
    case SERVER_MESSAGE: {
        char message[size];
#ifndef REVISION_177
        if (mud->packet_stream->protocol_custom) {
            // custom rich chat (SERVER_MESSAGE): i32 crown, u8 type, u8 flags, msg (newline-terminated)
            // if flags&1 the sender name is written twice, if flags&2 a colour string; render as "sender: msg"
            // i32 crown: packed rank icon (high byte = crownIndex + 1, low 24 bits = recolour mask)
            int crown = (int)get_unsigned_int(data, 1, size);
            int off = 1 + 4;
            /* #28: their MessageType rsID (GAME 0, QUEST 3, CHAT 4,
             * GLOBAL_CHAT 8, CLAN_CHAT 9). Was discarded, so a sender-less
             * QUEST message routed to the plain server-message line instead of
             * the quest line. */
            int msg_type = get_unsigned_byte(data, off++, size);
            int flags = get_unsigned_byte(data, off++, size);
            char msg[512] = {0};
            int mi = 0;
            while (off < size && (data[off] & 0xff) != 10) {
                if (mi < (int)sizeof(msg) - 1) {
                    msg[mi++] = (char)data[off];
                }
                off++;
            }
            off++; // past the message newline
            char sender[64] = {0};
            if (flags & 1) {
                int si = 0;
                while (off < size && (data[off] & 0xff) != 10) {
                    if (si < (int)sizeof(sender) - 1) {
                        sender[si++] = (char)data[off];
                    }
                    off++;
                }
                off++; // past 1st sender \n
                while (off < size && (data[off] & 0xff) != 10) {
                    off++; // sender name written a second time, skip it
                }
                off++;
            }
            // flags & 2 colour string is consumed but ignored for display
            if (sender[0] != '\0') {
                // a sender means a CHAT/GLOBAL_CHAT/CLAN_CHAT type; route it to
                // the chat tab with the crown before the name
                snprintf(message, size, "%s: %s", sender, msg);
                mud->orsc_pending_crown = crown;
                mudclient_show_message(mud, message, MESSAGE_TYPE_CHAT);
            } else if (msg_type == 3) {
                // QUEST: route to the quest line, not the plain server message
                snprintf(message, size, "%s", msg);
                mudclient_show_message(mud, message, MESSAGE_TYPE_QUEST);
            } else {
                snprintf(message, size, "%s", msg);
                mudclient_show_server_message(mud, message);
            }
            break;
        }
#endif
        memcpy(message, data + 1, size - 1);
        message[size - 1] = '\0';
        mudclient_show_server_message(mud, message);
        break;
    }
#ifndef REVISION_177
    case SERVER_COMBAT_STYLE: {
        if (!mud->protocol_custom) {
            break;
        }
        // custom fightmode sync (u8: 0=controlled, 1=aggressive, 2=accurate,
        // 3=defensive); sets mud->combat_style, custom-only opcode
        mud->combat_style = get_unsigned_byte(data, 1, size);
        break;
    }
    case SERVER_EQUIPMENT: {
        if (!mud->protocol_custom) {
            break;
        }
        // custom equipment tab: u8 equipmentCount, then per worn item { u8 wieldPos, u16 catalogId, i32 amount iff stackable }
        // amount is present only when > 0; store collapsed into the 11-slot paperdoll
        for (int i = 0; i < 11; i++) {
            mud->equipped_item_id[i] = 0;
            mud->equipped_item_amount[i] = 0;
        }
        size_t offset = 1;
        int count = get_unsigned_byte(data, offset++, size);
        for (int i = 0; i < count; i++) {
            int wield_pos = get_unsigned_byte(data, offset++, size);
            int item_id = get_unsigned_short(data, offset, size);
            offset += 2;
            int amount = 1;
            // stackable == 0 means "stacks" (config85 polarity)
            if (item_id >= 0 && item_id < game_data.item_count &&
                game_data.items[item_id].stackable == 0) {
                amount = get_unsigned_int(data, offset, size);
                offset += 4;
            }
            int slot = orsc_equip_collapse_slot(wield_pos);
            if (slot >= 0 && slot < 11) {
                // the paperdoll indexes game_data.items[] by this id; clamp an
                // out-of-range id so it can't read past the table
                mud->equipped_item_id[slot] =
                    item_id >= 0 && item_id < game_data.item_count
                        ? item_id
                        : IRON_MACE_ID;
                mud->equipped_item_amount[slot] = amount;
            }
        }
        break;
    }
    case SERVER_EQUIPMENT_UPDATE: {
        if (!mud->protocol_custom) {
            break;
        }
        // single-slot delta: u8 wieldPos, u16 catalogId (0xFFFF clears the slot), i32 amount iff stackable
        size_t offset = 1;
        int wield_pos = get_unsigned_byte(data, offset++, size);
        int item_id = get_unsigned_short(data, offset, size);
        offset += 2;
        int slot = orsc_equip_collapse_slot(wield_pos);
        if (slot < 0 || slot >= 11) {
            break;
        }
        if (item_id == 0xFFFF) {
            mud->equipped_item_id[slot] = 0;
            mud->equipped_item_amount[slot] = 0;
        } else {
            int amount = 1;
            if (item_id >= 0 && item_id < game_data.item_count &&
                game_data.items[item_id].stackable == 0) {
                amount = get_unsigned_int(data, offset, size);
            }
            mud->equipped_item_id[slot] =
                item_id >= 0 && item_id < game_data.item_count ? item_id
                                                               : IRON_MACE_ID;
            mud->equipped_item_amount[slot] = amount;
        }
        break;
    }
    case SERVER_NPC_KILLS: {
        // the SP/co-op wire sends the same 3x i32 payload for its HUD
        if (!mud->protocol_custom && !MUD_SP_WIRE(mud)) {
            break;
        }
        // 3x i32: lifetime total, most-recent npc id, kills of that npc
        mud->orsc_npc_kills_total = get_unsigned_int(data, 1, size);
        mud->orsc_npc_kills_recent_id = get_unsigned_int(data, 5, size);
        mud->orsc_npc_kills_recent_count = get_unsigned_int(data, 9, size);
        mud->orsc_npc_kills_seen = 1;
        break;
    }
    case SERVER_ELIXIR: {
        // opcode 54 is gated on config S_WANT_EXPERIENCE_ELIXIRS and dropped when off
        if (!mud->protocol_custom || !mud->orsc.want_elixirs) {
            break;
        }
        // store getShort() * 32; the countdown and display assume that scale
        mud->orsc_elixir_timer = get_unsigned_short(data, 1, size) * 32;
        break;
    }
    case SERVER_EXPERIENCE_TOGGLE: {
        if (!mud->protocol_custom) {
            break;
        }
        mud->orsc_experience_frozen = get_unsigned_byte(data, 1, size);
        break;
    }
    case SERVER_EXPSHARED: {
        if (!mud->protocol_custom) {
            break;
        }
        mud->orsc_exp_shared = get_unsigned_short(data, 1, size);
        break;
    }
    case SERVER_OPENPK_POINTS: {
        if (!mud->protocol_custom) {
            break;
        }
        mud->orsc_openpk_points =
            ((int64_t)get_unsigned_int(data, 1, size) << 32) |
            (uint32_t)get_unsigned_int(data, 5, size);
        break;
    }
    case SERVER_BANK_PIN: {
        // the SP/co-op wire sends the same 135 payload for the bank PIN
        if (!mud->protocol_custom && !MUD_SP_WIRE(mud)) {
            break;
        }
        // u8 isOpen: 1 = show the PIN pad, 0 = hide it; input resets on every transition
        mud->show_dialog_bank_pin = get_unsigned_byte(data, 1, size) != 0;
        mud->bank_pin_length = 0;
        mud->bank_pin_input[0] = '\0';
        break;
    }
    case SERVER_PROGRESS_BAR: {
        if (!mud->protocol_custom) {
            break;
        }
        // u8 interfaceId: 1 = show (u16 delay ms/item, u8 totalBatch), 2 = hide, 3 = update (u8 itemsCompleted)
        switch (get_unsigned_byte(data, 1, size)) {
        case 1:
            mud->orsc_progress_delay = get_unsigned_short(data, 2, size);
            mud->orsc_progress_total = get_unsigned_byte(data, 4, size);
            mud->orsc_progress_current = 0;
            mud->orsc_progress_visible = 1;
            break;
        case 2:
            mud->orsc_progress_visible = 0;
            break;
        case 3:
            mud->orsc_progress_current = get_unsigned_byte(data, 2, size);
            break;
        }
        break;
    }
    case SERVER_CLAN: {
        // the SP/co-op wire can send the same 112 payload if the server runs a clan plugin
        if (!mud->protocol_custom && !MUD_SP_WIRE(mud)) {
            break;
        }
        // opcode 112 multiplexes SEND_CLAN / SEND_CLAN_SETTINGS / SEND_CLAN_LIST; leading byte discriminates:
        // 0 = roster snapshot, 1 = left/removed (clear), 2 = invite popup, 3 = settings, 4 = browse list
        size_t offset = 1;

        switch (get_unsigned_byte(data, offset++, size)) {
        case 0: {
            orsc_read_string(data, &offset, size, mud->orsc_clan_name,
                             sizeof(mud->orsc_clan_name));
            orsc_read_string(data, &offset, size, mud->orsc_clan_tag,
                             sizeof(mud->orsc_clan_tag));
            orsc_read_string(data, &offset, size, mud->orsc_clan_leader,
                             sizeof(mud->orsc_clan_leader));

            mud->orsc_clan_is_leader = get_unsigned_byte(data, offset++, size);

            int clan_size = get_unsigned_byte(data, offset++, size);
            int stored = 0;

            for (int i = 0; i < clan_size && offset < size; i++) {
                char member_name[33] = {0};

                orsc_read_string(data, &offset, size, member_name,
                                 sizeof(member_name));

                int rank = get_unsigned_byte(data, offset++, size);
                int online = get_unsigned_byte(data, offset++, size);

                if (stored < ORSC_CLAN_MEMBERS_MAX) {
                    strcpy(mud->orsc_clan_member_names[stored], member_name);
                    mud->orsc_clan_member_ranks[stored] = rank;
                    mud->orsc_clan_member_online[stored] = online;
                    stored++;
                }
            }

            mud->orsc_clan_size = stored;
            mud->orsc_clan_in = 1;
            break;
        }
        case 1:
            mud->orsc_clan_in = 0;
            mud->orsc_clan_size = 0;
            mud->orsc_clan_is_leader = 0;
            mud->orsc_clan_name[0] = '\0';
            mud->orsc_clan_tag[0] = '\0';
            mud->orsc_clan_leader[0] = '\0';
            break;
        case 2: {
            char inviter[33] = {0};
            char clan_name[33] = {0};

            orsc_read_string(data, &offset, size, inviter, sizeof(inviter));
            orsc_read_string(data, &offset, size, clan_name,
                             sizeof(clan_name));

            // clan invite text from ClanInterface.initializeInvite
            snprintf(mud->orsc_clan_invite_top,
                     sizeof(mud->orsc_clan_invite_top),
                     "@yel@%s @whi@has sent you a clan invitation:", inviter);
            snprintf(mud->orsc_clan_invite_bottom,
                     sizeof(mud->orsc_clan_invite_bottom), "%s", clan_name);

            mud->confirm_text_top = mud->orsc_clan_invite_top;
            mud->confirm_text_bottom = mud->orsc_clan_invite_bottom;
            mud->confirm_type = CONFIRM_CLAN_INVITE;
            mud->show_dialog_confirm = 1;
            break;
        }
        case 3:
            for (int i = 0; i < 5 && offset < size; i++) {
                mud->orsc_clan_settings[i] =
                    get_unsigned_byte(data, offset++, size);
            }
            break;
        case 4: {
            // clan browse list: u16 count x {clanID u16, name string, tag string,
            // members u8, canJoin u8, clanPoints i32, clanRank u16}
            int browse_count = get_unsigned_short(data, offset, size);
            offset += 2;

            int stored = 0;

            for (int i = 0; i < browse_count && offset < size; i++) {
                offset += 2; // clanID (join goes by name)

                char browse_name[33] = {0};
                char browse_tag[9] = {0};

                orsc_read_string(data, &offset, size, browse_name,
                                 sizeof(browse_name));
                orsc_read_string(data, &offset, size, browse_tag,
                                 sizeof(browse_tag));

                int members = get_unsigned_byte(data, offset++, size);
                int can_join = get_unsigned_byte(data, offset++, size);
                int points = get_unsigned_int(data, offset, size);
                offset += 4;
                offset += 2; // clanRank

                if (stored < ORSC_CLAN_BROWSE_MAX) {
                    strcpy(mud->orsc_clan_browse_names[stored], browse_name);
                    strcpy(mud->orsc_clan_browse_tags[stored], browse_tag);
                    mud->orsc_clan_browse_members[stored] = members;
                    mud->orsc_clan_browse_can_join[stored] = can_join;
                    mud->orsc_clan_browse_points[stored] = points;
                    stored++;
                }
            }

            mud->orsc_clan_browse_count = stored;
            break;
        }
        default:
            break;
        }
        break;
    }
    case SERVER_PARTY: {
        // the SP/co-op wire sends the same party payload
        if (!mud->protocol_custom && !MUD_SP_WIRE(mud)) {
            break;
        }
        // u8 actionId: 0 = snapshot (leaderName, isLeader u8, partySize u8, then per member: name + 11 status bytes + i64 shareExp2)
        // 1 = left (clear), 2 = invite popup (inviter + party name)
        size_t offset = 1;

        switch (get_unsigned_byte(data, offset++, size)) {
        case 0: {
            orsc_read_string(data, &offset, size, mud->orsc_party_leader,
                             sizeof(mud->orsc_party_leader));

            mud->orsc_party_is_leader = get_unsigned_byte(data, offset++, size);

            int party_size = get_unsigned_byte(data, offset++, size);
            int stored = 0;

            for (int i = 0; i < party_size && offset < size; i++) {
                char member_name[33] = {0};

                orsc_read_string(data, &offset, size, member_name,
                                 sizeof(member_name));

                // 11 status bytes; the list and the party HUD render these
                int rank = get_unsigned_byte(data, offset + 0, size);
                int online = get_unsigned_byte(data, offset + 1, size);
                int cur_hits = get_unsigned_byte(data, offset + 2, size);
                int max_hits = get_unsigned_byte(data, offset + 3, size);
                int combat = get_unsigned_byte(data, offset + 4, size);
                int skulled = get_unsigned_byte(data, offset + 5, size);
                int in_combat = get_unsigned_byte(data, offset + 9, size);

                offset += 11; // rank..shareExp
                offset += 8;  // i64 shareExp2

                if (stored < ORSC_PARTY_MEMBERS_MAX) {
                    // recent-damage flash: a member whose hp dropped since the
                    // previous snapshot flashes the HUD bar for ~500 frames
                    for (int prev = 0; prev < mud->orsc_party_size; prev++) {
                        if (strcmp(mud->orsc_party_member_names[prev],
                                   member_name) == 0) {
                            if (cur_hits <
                                mud->orsc_party_member_cur_hits[prev]) {
                                mud->orsc_party_member_flash[stored] = 500;
                            } else if (prev != stored) {
                                mud->orsc_party_member_flash[stored] =
                                    mud->orsc_party_member_flash[prev];
                            }
                            break;
                        }
                    }

                    strcpy(mud->orsc_party_member_names[stored], member_name);
                    mud->orsc_party_member_online[stored] = online;
                    mud->orsc_party_member_cur_hits[stored] = cur_hits;
                    mud->orsc_party_member_max_hits[stored] = max_hits;
                    mud->orsc_party_member_combat[stored] = combat;
                    mud->orsc_party_member_rank[stored] = rank;
                    mud->orsc_party_member_skull[stored] = skulled;
                    mud->orsc_party_member_in_combat[stored] = in_combat;
                    stored++;
                }
            }

            mud->orsc_party_size = stored;
            mud->orsc_party_in = 1;
            break;
        }
        case 1:
            mud->orsc_party_in = 0;
            mud->orsc_party_size = 0;
            mud->orsc_party_is_leader = 0;
            mud->orsc_party_leader[0] = '\0';
            break;
        case 2: {
            char inviter[33] = {0};
            char party_name[33] = {0};

            orsc_read_string(data, &offset, size, inviter, sizeof(inviter));
            orsc_read_string(data, &offset, size, party_name,
                             sizeof(party_name));

            // party invite text from PartyInterface.initializeInvite
            snprintf(mud->orsc_party_invite_top,
                     sizeof(mud->orsc_party_invite_top),
                     "@yel@%s @whi@has sent you a party invitation:", inviter);
            snprintf(mud->orsc_party_invite_bottom,
                     sizeof(mud->orsc_party_invite_bottom), "%s", party_name);

            mud->confirm_text_top = mud->orsc_party_invite_top;
            mud->confirm_text_bottom = mud->orsc_party_invite_bottom;
            mud->confirm_type = CONFIRM_PARTY_INVITE;
            mud->show_dialog_confirm = 1;
            break;
        }
        case 3:
            // party settings: 3 settings + 2 allowed
            for (int i = 0; i < 5 && offset < size; i++) {
                mud->orsc_party_settings[i] =
                    get_unsigned_byte(data, offset++, size);
            }
            break;
        case 4: {
            // SEND_PARTY_LIST: u16 total, then per party {partyId u16, size u8,
            // allowsSearchedJoin u8, points i32, index u16}
            int browse_count = get_unsigned_short(data, offset, size);
            offset += 2;

            int stored = 0;

            for (int i = 0; i < browse_count && offset < size; i++) {
                int party_id = get_unsigned_short(data, offset, size);
                offset += 2;
                int members = get_unsigned_byte(data, offset++, size);
                int can_join = get_unsigned_byte(data, offset++, size);
                int points = get_unsigned_int(data, offset, size);
                offset += 4;
                offset += 2; // list index

                if (stored < ORSC_PARTY_BROWSE_MAX) {
                    mud->orsc_party_browse_ids[stored] = party_id;
                    mud->orsc_party_browse_members[stored] = members;
                    mud->orsc_party_browse_can_join[stored] = can_join;
                    mud->orsc_party_browse_points[stored] = points;
                    stored++;
                }
            }

            mud->orsc_party_browse_count = stored;
            break;
        }
        default:
            // case 4 (party browse list) unrendered; framing discards
            break;
        }
        break;
    }
    case SERVER_BLACK_HOLE: {
        if (!mud->protocol_custom) {
            break;
        }
        // u8 boolean (inside black hole); only visual effect is a taller chat backing box
        mud->orsc_black_hole = get_unsigned_byte(data, 1, size) != 0;
        break;
    }
    case SERVER_POINTS_TO_GP: {
        if (!mud->protocol_custom) {
            break;
        }
        // no payload; opens the points->GP exchange (OpenPK world)
        mud->orsc_show_points_to_gp = 1;
        break;
    }
    case SERVER_FISHING_TRAWLER: {
        if (!mud->protocol_custom) {
            break;
        }
        // reads one leading byte where the server writes two, matching the OpenRSC client (fishingTrawlerUpdate reads one byte,
        // always sees interfaceId 6, does nothing). generator writes interfaceId then actionId: 0 show, 1 variables, 2 hide
        size_t offset = 1;

        switch (get_unsigned_byte(data, offset++, size)) {
        case 0:
            mud->orsc_trawler_visible = 1;
            break;
        case 1:
            mud->orsc_trawler_water = get_unsigned_short(data, offset, size);
            mud->orsc_trawler_fish =
                get_unsigned_short(data, offset + 2, size);
            mud->orsc_trawler_minutes =
                get_unsigned_byte(data, offset + 4, size);
            mud->orsc_trawler_net_ripped =
                get_unsigned_byte(data, offset + 5, size) == 1;
            break;
        case 2:
            mud->orsc_trawler_visible = 0;
            break;
        }
        break;
    }
    case SERVER_IGNORE_RENAME: {
        if (!mud->protocol_custom) {
            break;
        }
        // wire: 4 strings (arg0, replace, arg1, find) + a rename byte; replace/find default to arg0 when empty
        // on a rename, relocate the ignore entry from the old name to the new, keyed on base37 hashes
        size_t offset = 1;
        char arg0[MAX_USER_LENGTH + 1] = {0};
        char replace[MAX_USER_LENGTH + 1] = {0};
        char arg1[MAX_USER_LENGTH + 1] = {0};
        char find[MAX_USER_LENGTH + 1] = {0};

        orsc_read_string(data, &offset, (size_t)size, arg0, sizeof(arg0));
        orsc_read_string(data, &offset, (size_t)size, replace, sizeof(replace));
        orsc_read_string(data, &offset, (size_t)size, arg1, sizeof(arg1));
        orsc_read_string(data, &offset, (size_t)size, find, sizeof(find));

        int rename = get_unsigned_byte(data, offset, size) == 1;

        if (replace[0] == '\0') {
            strcpy(replace, arg0);
        }
        if (find[0] == '\0') {
            strcpy(find, arg0);
        }

        int64_t find_hash = encode_username(find);
        int64_t replace_hash = encode_username(replace);

        for (int i = 0; i < mud->ignore_list_count; i++) {
            if (rename) {
                if (mud->ignore_list[i] == find_hash) {
                    mud->ignore_list[i] = replace_hash;
                    break;
                }
            } else if (mud->ignore_list[i] == replace_hash) {
                break; // already present
            }
        }
        break;
    }
    case SERVER_KILL_ANNOUNCEMENT: {
        if (!mud->protocol_custom || !mud->orsc.want_kill_feed) {
            break;
        }
        // victim string, attacker string, killType i32 (killing weapon's item id;
        // -1 ranged, -2 magic); queued newest-first, max 10
        char victim[33] = {0};
        char attacker[33] = {0};
        size_t offset = 1;

        orsc_read_string(data, &offset, size, victim, sizeof(victim));
        orsc_read_string(data, &offset, size, attacker, sizeof(attacker));

        int kill_type = (int)get_unsigned_int(data, offset, size);

        if (mud->orsc_kill_feed_count < ORSC_KILL_FEED_MAX) {
            mud->orsc_kill_feed_count++;
        }

        for (int i = mud->orsc_kill_feed_count - 1; i > 0; i--) {
            mud->orsc_kill_feed[i] = mud->orsc_kill_feed[i - 1];
        }

        strcpy(mud->orsc_kill_feed[0].killer, attacker);
        strcpy(mud->orsc_kill_feed[0].victim, victim);
        mud->orsc_kill_feed[0].kill_type = kill_type;
        mud->orsc_kill_feed[0].ticks_left = ORSC_KILL_FEED_TTL;
        break;
    }
    case SERVER_IRONMAN: {
        if (!mud->protocol_custom) {
            break;
        }
        // u8 interfaceId, u8 actionId: 0 = set mode + restriction (2 more bytes),
        // 1 = show the selection interface, 2 = hide
        size_t offset = 1;

        offset++; // interfaceId

        switch (get_unsigned_byte(data, offset++, size)) {
        case 0:
            mud->orsc_ironman_type = get_unsigned_byte(data, offset++, size);
            mud->orsc_ironman_restriction =
                get_unsigned_byte(data, offset++, size);
            break;
        case 1:
            mud->show_dialog_ironman = 1;
            break;
        case 2:
            mud->show_dialog_ironman = 0;
            break;
        }
        break;
    }
    case SERVER_ONLINE_LIST: {
        if (!mud->protocol_custom) {
            break;
        }

        // u16 count; per player: name string, crown i32, location string
        size_t offset = 1;
        int online_count = get_unsigned_short(data, offset, size);
        offset += 2;

        mud->orsc_online_count = 0;

        for (int i = 0; i < online_count && offset < (size_t)size; i++) {
            char online_name[MAX_USER_LENGTH + 1] = {0};
            char location[ORSC_ONLINE_LOCATION_MAX] = {0};

            orsc_read_string(data, &offset, size, online_name,
                             (int)sizeof(online_name));

            int crown = get_unsigned_int(data, offset, size);
            offset += 4;

            orsc_read_string(data, &offset, size, location,
                             (int)sizeof(location));

            // keep consuming the wire past the cap so the stream stays aligned; only the store is bounded
            if (mud->orsc_online_count >= ORSC_ONLINE_LIST_MAX) {
                continue;
            }

            int slot = mud->orsc_online_count++;

            strcpy(mud->orsc_online_name[slot], online_name);
            strcpy(mud->orsc_online_location[slot], location);
            mud->orsc_online_crown[slot] = crown;
        }

        mud->online_list_scroll = 0;
        mud->online_list_menu_entry = -1; // their reset() clears the view
        mud->show_dialog_online_list = 1;
        break;
    }
    case SERVER_ON_TUTORIAL: {
        if (!mud->protocol_custom) {
            break;
        }
        mud->orsc_on_tutorial = get_unsigned_byte(data, 1, size);
        break;
    }
    case SERVER_AUCTION: {
        // the SP/co-op wire can send the same 132 payload if the server runs an auction house
        if (!mud->protocol_custom && !MUD_SP_WIRE(mud)) {
            break;
        }
        // u8 packetType: 0 = reset the list, 1 = u16 count x {auctionID i32, itemID i32, amount i32, price i32, isMine u8, hoursLeft u8}
        // isMine 1 = local player (no seller string), else seller string; type 1 appends, it is not a whole-list snapshot
        size_t offset = 1;

        switch (get_unsigned_byte(data, offset++, size)) {
        case 0:
            mud->orsc_auction_count = 0;
            mud->orsc_auction_overflow = 0;
            mud->orsc_auction_selected = -1;
            break;
        case 1: {
            int auction_count = get_unsigned_short(data, offset, size);
            offset += 2;

            int stored = mud->orsc_auction_count; // append, per above
            int chunk_start = stored;

            for (int i = 0; i < auction_count && offset < size; i++) {
                int auction_id = get_unsigned_int(data, offset, size);
                int item_id = get_unsigned_int(data, offset + 4, size);
                int amount = get_unsigned_int(data, offset + 8, size);
                int price = get_unsigned_int(data, offset + 12, size);
                offset += 16;

                int is_mine = get_unsigned_byte(data, offset++, size) == 1;

                char seller[33] = {0};

                if (!is_mine) {
                    orsc_read_string(data, &offset, size, seller,
                                     sizeof(seller));
                } else {
                    // the seller string is omitted from the wire for your own
                    // rows, so use the local player's displayName
                    snprintf(seller, sizeof(seller), "%s",
                             mud->local_player != NULL ? mud->local_player->name
                                                       : "");
                }

                int hours_left = get_unsigned_byte(data, offset++, size);

                if (stored < ORSC_AUCTION_MAX) {
                    mud->orsc_auction_ids[stored] = auction_id;
                    mud->orsc_auction_item_ids[stored] = item_id;
                    mud->orsc_auction_amounts[stored] = amount;
                    mud->orsc_auction_prices[stored] = price;
                    mud->orsc_auction_mine[stored] = is_mine;
                    strcpy(mud->orsc_auction_sellers[stored], seller);
                    mud->orsc_auction_hours[stored] = hours_left;
                    stored++;
                }
            }

            mud->orsc_auction_count = stored;
            // accumulate: each chunk can spill past ORSC_AUCTION_MAX separately
            mud->orsc_auction_overflow += auction_count - (stored - chunk_start);
            mud->orsc_auction_visible = 1;
            mud->orsc_auction_sell_mode = 0;
            break;
        }
        }
        break;
    }
    case SERVER_UNLOCKED_APPEARANCES: {
        if (!mud->protocol_custom) {
            break;
        }
        // six i32 counts (hairStyles, bodyTypes, skinColours, hairColours, topColours, bottomColours), then bit access:
        // a hairStyles-wide mask, a bodyTypes-wide mask, then one bit per skin colour; only the skin bits are kept
        size_t offset = 1;

        int unlocked_hair_styles = get_unsigned_int(data, offset, size);
        int unlocked_body_types = get_unsigned_int(data, offset + 4, size);
        int unlocked_skin_colours = get_unsigned_int(data, offset + 8, size);
        // validate all six counts against the 256 cap and abort if any exceeds
        // it; the trailing three are read and discarded
        int unlocked_hair_colours = get_unsigned_int(data, offset + 12, size);
        int unlocked_top_colours = get_unsigned_int(data, offset + 16, size);
        int unlocked_bottom_colours = get_unsigned_int(data, offset + 20, size);
        offset += 24; // all six counts

        // sanity guard: anything past a byte means a newer client
        if (unlocked_hair_styles > 256 || unlocked_body_types > 256 ||
            unlocked_skin_colours > 256 || unlocked_hair_colours > 256 ||
            unlocked_top_colours > 256 || unlocked_bottom_colours > 256) {
            break;
        }

        // bit cursor over the remaining bytes
        size_t bit = offset * 8;
        bit += (size_t)unlocked_hair_styles; // skipped mask
        bit += (size_t)unlocked_body_types;  // skipped mask

        for (int i = 0; i < unlocked_skin_colours; i++) {
            size_t byte_index = bit >> 3;

            if (byte_index >= size) {
                break;
            }

            int set = (get_unsigned_byte(data, byte_index, size) >>
                       (7 - (bit & 7))) &
                      1;

            if (i < PLAYER_SKIN_COLOUR_COUNT) {
                mud->orsc_unlocked_skin[i] = (int8_t)set;
            }

            bit++;
        }

        mud->orsc_unlocked_skin_known = 1;
        break;
    }
#endif
    case SERVER_INVENTORY_ITEMS: {
        int offset = 1;

        mud->inventory_items_count = get_unsigned_byte(data, offset++, size);

        if (mud->inventory_items_count > INVENTORY_ITEMS_MAX) {
            mud->inventory_items_count = INVENTORY_ITEMS_MAX;
        }

        for (int i = 0; i < mud->inventory_items_count; i++) {
#ifndef REVISION_177
            if (mud->protocol_custom) {
                // custom inventory, different layout from authentic-204: u16 catalogId (no packed equip bit), then separate u8
                // wielded and u8 noted, then i32 amount written only when amount > 0. stackable == 0 means "stacks" (config85 polarity)
                int id = get_unsigned_short(data, offset, size);
                offset += 2;

                if (id >= game_data.item_count) {
                    id = IRON_MACE_ID;
                }

                mud->inventory_equipped[i] =
                    get_unsigned_byte(data, offset++, size);

                int noted = get_unsigned_byte(data, offset++, size);

                mud->inventory_item_id[i] = id;
                mud->inventory_item_noted[i] = noted;

                if (noted || game_data.items[id].stackable == 0) {
                    mud->inventory_item_stack_count[i] =
                        get_unsigned_int(data, offset, size);
                    offset += 4;
                } else {
                    mud->inventory_item_stack_count[i] = 1;
                }

                continue;
            }
#endif

            int id_equip = get_unsigned_short(data, offset, size);
            offset += 2;

            int id = id_equip & 32767;
            if (id >= game_data.item_count) {
                id = IRON_MACE_ID;
            }

            int equipped = id_equip / 32768;
            int noted = 0;

#ifndef REVISION_177
            // SP/LAN wire: a u8 noted follows the id when the world runs notes
            if (MUD_SP_WIRE(mud) && mud->orsc.want_bank_notes) {
                noted = get_unsigned_byte(data, offset++, size) == 1;
            }
#endif

            mud->inventory_item_id[i] = id;
            mud->inventory_equipped[i] = equipped;
            mud->inventory_item_noted[i] = noted;

            if (noted || game_data.items[id].stackable == 0) {
                mud->inventory_item_stack_count[i] =
                    get_stack_int(data, offset, size);

                if (mud->inventory_item_stack_count[i] >= 128) {
                    offset += 4;
                } else {
                    offset++;
                }
            } else {
                mud->inventory_item_stack_count[i] = 1;
            }
        }
        break;
    }
    case SERVER_INVENTORY_ITEM_UPDATE: {
        int offset = 1;
        int stack = 1;

        int index = get_unsigned_byte(data, offset++, size);

        if (index >= INVENTORY_ITEMS_MAX) {
            return;
        }

        int id_equip = get_unsigned_short(data, offset, size);
        offset += 2;

        int id = id_equip & 32767;

        if (id >= game_data.item_count) {
            id = IRON_MACE_ID;
        }

        int equipped = id_equip / 32768;

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom single-slot update packs the wield bit into the id (catalogID + (wielded ? 32768 : 0)),
            // adds a u8 noted, and an i32 amount present only when > 0
            int noted = get_unsigned_byte(data, offset++, size);

            mud->inventory_item_noted[index] = noted;

            if (noted || game_data.items[id].stackable == 0) {
                stack = get_unsigned_int(data, offset, size);
                offset += 4;
            }
        } else
#endif
        {
            int noted = 0;

#ifndef REVISION_177
            // SP/LAN wire: u8 noted after the id when the world runs notes
            if (MUD_SP_WIRE(mud) && mud->orsc.want_bank_notes) {
                noted = get_unsigned_byte(data, offset++, size) == 1;
            }
#endif
            mud->inventory_item_noted[index] = noted;

            if (noted || game_data.items[id & 32767].stackable == 0) {
                stack = get_stack_int(data, offset, size);

                if (stack >= 128) {
                    offset += 4;
                } else {
                    offset++;
                }
            }
        }

        mud->inventory_item_id[index] = id;
        mud->inventory_equipped[index] = equipped;
        mud->inventory_item_stack_count[index] = stack;

        if (index >= mud->inventory_items_count) {
            mud->inventory_items_count = index + 1;
        }
        break;
    }
    case SERVER_INVENTORY_ITEM_REMOVE: {
        int index = get_unsigned_byte(data, 1, size);
        if (index >= INVENTORY_ITEMS_MAX) {
            return;
        }

        mud->inventory_items_count--;

        for (int i = index; i < mud->inventory_items_count; i++) {
            mud->inventory_item_id[i] = mud->inventory_item_id[i + 1];

            mud->inventory_item_stack_count[i] =
                mud->inventory_item_stack_count[i + 1];

            mud->inventory_equipped[i] = mud->inventory_equipped[i + 1];

            // the noted flag is a parallel array; it must shift with the
            // id/count/equipped or items below a removed slot inherit a stale flag
            mud->inventory_item_noted[i] = mud->inventory_item_noted[i + 1];
        }
        break;
    }
    case SERVER_PLAYER_STAT_LIST: {
        int offset = 1;

        // packet layout: opcode + N(current) + N(base) + N*4(experience) + 1(questPoints); N is derived as (payload-1)/6
        // 18 = authentic, 19 = OpenRSC custom (+Runecraft); clamp to array capacity
        int derived = (size - 1) / 6;

        if (derived < 0) {
            derived = 0;
        }

        if (derived > PLAYER_SKILL_MAX) {
            derived = PLAYER_SKILL_MAX;
        }

        mud->player_skill_count = derived;

        for (int i = 0; i < derived; i++) {
            mud->player_skill_current[i] =
                get_unsigned_byte(data, offset++, size);
        }

        for (int i = 0; i < derived; i++) {
            mud->player_skill_base[i] = get_unsigned_byte(data, offset++, size);
        }

        for (int i = 0; i < derived; i++) {
            mud->player_experience[i] = get_unsigned_int(data, offset, size);
            offset += 4;
        }

        mud->player_quest_points = get_unsigned_byte(data, offset++, size);
#ifdef RSC_DIAG
        DIAG("stat_list skills=%d hits %d/%d", derived,
             mud->player_skill_current[SKILL_HITS],
             mud->player_skill_base[SKILL_HITS]);
#endif
        break;
    }
    case SERVER_PLAYER_STAT_EQUIPMENT_BONUS: {
        for (int i = 0; i < PLAYER_STAT_EQUIPMENT_COUNT; i++) {
            mud->player_stat_equipment[i] =
                get_unsigned_byte(data, 1 + i, size);
        }

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom SEND_EQUIPMENT_STATS repeats all five bonuses as i32 after the bytes; the ints overwrite the bytes and are
            // authoritative (the byte copy saturates at 255 and can't go negative)
            for (int i = 0; i < PLAYER_STAT_EQUIPMENT_COUNT; i++) {
                mud->player_stat_equipment[i] =
                    get_unsigned_int(data, 1 + PLAYER_STAT_EQUIPMENT_COUNT +
                                               (i * 4),
                                     size);
            }
        }
#endif

        break;
    }
    case SERVER_PLAYER_STAT_EXPERIENCE_UPDATE: {
        int skill_index = get_unsigned_byte(data, 1, size);
        if (skill_index >= PLAYER_SKILL_MAX) {
            return;
        }

        int old_experience = mud->player_experience[skill_index];
        mud->player_experience[skill_index] = get_unsigned_int(data, 2, size);

        // session gained / xp-per-hour tracking for the on-screen xp counter
        {
            int gained = mud->player_experience[skill_index] - old_experience;

            if (gained > 0) {
                if (mud->player_experience_gained[skill_index] == 0) {
                    mud->xp_gain_start_ms[skill_index] = get_ticks();
                }

                if (mud->xp_gained_total == 0) {
                    mud->xp_gained_total_start_ms = get_ticks();
                }

                mud->player_experience_gained[skill_index] += gained;
                mud->xp_gained_total += gained;
                mud->xp_last_gain_skill = skill_index;
            }
        }

        mudclient_drop_experience(mud, skill_index,
                                  mud->player_experience[skill_index] -
                                      old_experience);
        break;
    }
    case SERVER_PLAYER_STAT_UPDATE: {
        int offset = 1;
        int skill_index = get_unsigned_byte(data, offset++, size);
        if (skill_index >= PLAYER_SKILL_MAX) {
            return;
        }

        mud->player_skill_current[skill_index] =
            get_unsigned_byte(data, offset++, size);

        mud->player_skill_base[skill_index] =
            get_unsigned_byte(data, offset++, size);

        mud->player_experience[skill_index] =
            get_unsigned_int(data, offset, size);
#ifdef RSC_DIAG
        if (skill_index == SKILL_HITS) {
            DIAG("stat_update hits %d/%d",
                 mud->player_skill_current[SKILL_HITS],
                 mud->player_skill_base[SKILL_HITS]);
        }
#endif
        break;
    }
    case SERVER_PLAYER_STAT_FATIGUE: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom SEND_FATIGUE writes two shorts where authentic writes one: first fatigue/(MAX/100) as a 0-100 percentage,
            // then fatigue/(MAX/750); take the second since stat_fatigue is in 750ths
            mud->stat_fatigue = get_unsigned_short(data, 3, size);
            break;
        }
#endif

        mud->stat_fatigue = get_unsigned_short(data, 1, size);
        break;
    }
#ifndef REVISION_177
    case SERVER_BANK_PRESET: {
        // custom SEND_BANK_PRESET (150): one preset slot. u16 slotIndex; 30 inventory entries (u8 0xFF skip, or u16 id + u8 noted + [i32 amount iff stackable||noted])
        // 14 equipment entries (u8 0xFF, or u16 id + [i32 amount iff stackable], no noted byte); 14 wire slots collapse onto 11: 5->0, 6->1, 7->2, >7 -= 3
        if (!mud->protocol_custom) {
            break;
        }

        int slot = get_unsigned_short(data, 1, size);
        int offset = 3;

        if (slot < 0 || slot >= ORSC_PRESET_COUNT) {
            break;
        }

        for (int i = 0; i < INVENTORY_ITEMS_MAX; i++) {
            mud->orsc_preset_inventory_id[slot][i] = -1;
            mud->orsc_preset_inventory_amount[slot][i] = 0;
            mud->orsc_preset_inventory_noted[slot][i] = 0;
        }

        for (int i = 0; i < 11; i++) {
            mud->orsc_preset_equipment_id[slot][i] = -1;
            mud->orsc_preset_equipment_amount[slot][i] = 0;
        }

        for (int i = 0; i < INVENTORY_ITEMS_MAX; i++) {
            if (get_unsigned_byte(data, offset, size) == 0xff) {
                offset++;
                continue;
            }

            int item_id = get_unsigned_short(data, offset, size);
            offset += 2;

            int noted = get_unsigned_byte(data, offset++, size) == 1;
            int amount = 1;

            if (game_data_item_stacks(item_id, noted)) {
                amount = get_unsigned_int(data, offset, size);
                offset += 4;
            }

            mud->orsc_preset_inventory_id[slot][i] = item_id;
            mud->orsc_preset_inventory_amount[slot][i] = amount;
            mud->orsc_preset_inventory_noted[slot][i] = (uint8_t)noted;
        }

        for (int i = 0; i < ORSC_PRESET_EQUIP_WIRE_COUNT; i++) {
            if (get_unsigned_byte(data, offset, size) == 0xff) {
                offset++;
                continue;
            }

            int item_id = get_unsigned_short(data, offset, size);
            offset += 2;

            int amount = 1;

            // equipment has no noted byte, so the test is the plain one
            if (game_data_item_stacks(item_id, 0)) {
                amount = get_unsigned_int(data, offset, size);
                offset += 4;
            }

            int equip_slot = orsc_equip_collapse_slot(i);

            if (equip_slot < 0 || equip_slot >= 11) {
                continue;
            }

            mud->orsc_preset_equipment_id[slot][equip_slot] = item_id;
            mud->orsc_preset_equipment_amount[slot][equip_slot] = amount;
        }

        mud->orsc_preset_known[slot] = 1;
        break;
    }
#endif
    case SERVER_PLAYER_QUEST_LIST: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom SEND_QUESTS: u8 updateType. type 0 (full list): u8 count, then per quest i32 id, i32 stage, String name
            // type 1 (one quest): i32 id, i32 stage. server-named/numbered; stage: < 0 complete, > 0 started, 0 not started
            size_t offset = 1;
            int update_type = get_unsigned_byte(data, offset++, size);

            if (update_type != 1) {
                int count = get_unsigned_byte(data, offset++, size);

                mud->orsc_quest_count = 0;

                for (int i = 0; i < count; i++) {
                    int quest_id = get_unsigned_int(data, offset, size);
                    offset += 4;

                    int stage = get_unsigned_int(data, offset, size);
                    offset += 4;

                    char name[ORSC_QUEST_NAME_MAX] = {0};

                    orsc_read_string(data, &offset, (size_t)size, name,
                                     (int)sizeof(name));

                    // parse every entry to stay framed; store what fits
                    if (mud->orsc_quest_count >= ORSC_QUEST_MAX) {
                        continue;
                    }

                    int slot = mud->orsc_quest_count++;

                    mud->orsc_quest_id[slot] = quest_id;
                    mud->orsc_quest_stage[slot] = stage;
                    strcpy(mud->orsc_quest_name[slot], name);
                }

                break;
            }

            int quest_id = get_unsigned_int(data, offset, size);
            offset += 4;

            int stage = get_unsigned_int(data, offset, size);

            for (int i = 0; i < mud->orsc_quest_count; i++) {
                if (mud->orsc_quest_id[i] == quest_id) {
                    mud->orsc_quest_stage[i] = stage;
                    break;
                }
            }

            break;
        }
#endif

        // packet carries only the server's quest count, but quests_length includes
        // the 2 client-appended custom quests; clamp to what the packet holds
        int quests_sent = size - 1;
        if (quests_sent > quests_length) {
            quests_sent = quests_length;
        }
        for (int i = 0; i < quests_sent; i++) {
            mud->quest_complete[i] = get_unsigned_byte(data, i + 1, size);
        }
        break;
    }
    case SERVER_FRIEND_LIST: {
        mud->friend_list_count = get_unsigned_byte(data, 1, size);
        if (mud->friend_list_count > (SOCIAL_LIST_MAX * 2)) {
            mud->friend_list_count = SOCIAL_LIST_MAX * 2;
        }

        for (int i = 0; i < mud->friend_list_count; i++) {
            mud->friend_list[i] = get_unsigned_long(data, 2 + i * 9, size);
            mud->friend_list_online[i] =
                get_unsigned_byte(data, 10 + i * 9, size);
        }

        mudclient_sort_friends(mud);
        break;
    }
    case SERVER_FRIEND_STATUS_CHANGE: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            orsc_handle_friend_update(mud, data, size);
            break;
        }
#endif

        int64_t encoded_username = get_unsigned_long(data, 1, size);
        int world = get_unsigned_byte(data, 9, size);

        for (int i = 0; i < mud->friend_list_count; i++) {
            if (mud->friend_list[i] == encoded_username) {
                if (mud->friend_list_online[i] == 0 && world != 0) {
                    char username[USERNAME_LENGTH + 1] = {0};
                    decode_username(mud->friend_list[i], username);

                    char formatted[USERNAME_LENGTH + 20] = {0};
                    sprintf(formatted, "@pri@%s has logged in", username);

                    mudclient_show_server_message(mud, formatted);
                }

                if (mud->friend_list_online[i] != 0 && world == 0) {
                    char username[USERNAME_LENGTH + 1] = {0};
                    decode_username(mud->friend_list[i], username);

                    char formatted[USERNAME_LENGTH + 21] = {0};
                    sprintf(formatted, "@pri@%s has logged out", username);

                    mudclient_show_server_message(mud, formatted);
                }

                mud->friend_list_online[i] = world;
                mudclient_sort_friends(mud);

                return;
            }
        }

        if (mud->friend_list_count >= (SOCIAL_LIST_MAX * 2)) {
            return;
        }

        mud->friend_list[mud->friend_list_count] = encoded_username;
        mud->friend_list_online[mud->friend_list_count] = world;

        mud->friend_list_count++;

        mudclient_sort_friends(mud);
        break;
    }
#ifndef REVISION_177
    // custom-only SEND_PM (87): the server's echo of a PM the local player just sent; a rejected send echoes nothing
    // payload: recipient name string, then the body via writeRSCString
    case 87: {
        if (!mud->protocol_custom) {
            break;
        }

        size_t offset = 1;
        char to_username[MAX_USER_LENGTH + 1] = {0};

        orsc_read_string(data, &offset, (size_t)size, to_username,
                         (int)sizeof(to_username));

        int body_offset = (int)offset;
        int char_count = orsc_smart_length_get(data, size, &body_offset);

        char message[ORSC_HUFFMAN_MAX_CHARS + 1] = {0};

        orsc_huffman_decode((const uint8_t *)(data + body_offset),
                            size - body_offset, char_count, message,
                            (int)sizeof(message));

        char formatted_message[MAX_USER_LENGTH + sizeof(message) + 32];

        // StringUtil PRIVATE_SEND format; a send to the global friend comes back addressed to "Global$"
        int is_global = (strncasecmp(to_username, "global$", 7) == 0);

        if (is_global) {
            sprintf(formatted_message, "@pri@You tell [everyone]: %s", message);
        } else {
            sprintf(formatted_message, "@pri@You tell %s: %s", to_username,
                    message);
        }

        mudclient_show_message(mud, formatted_message, MESSAGE_TYPE_PRIVATE);
        break;
    }
#endif

    case SERVER_FRIEND_MESSAGE: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom: String playerName, String formerName, i32 iconSprite, then message body via writeRSCString
            // authentic: long hash, i32 totalSentMessages, RSC-compressed body
            size_t offset = 1;

            char from_username[MAX_USER_LENGTH + 1] = {0};
            char former_name[MAX_USER_LENGTH + 1] = {0};

            orsc_read_string(data, &offset, (size_t)size, from_username,
                             (int)sizeof(from_username));

            orsc_read_string(data, &offset, (size_t)size, former_name,
                             (int)sizeof(former_name));

            int crown = get_unsigned_int(data, offset, size);
            offset += 4;

            // writeRSCString: smart character count, then Huffman bytes
            int body_offset = (int)offset;
            int char_count = orsc_smart_length_get(data, size, &body_offset);

            char message[ORSC_HUFFMAN_MAX_CHARS + 1] = {0};

            orsc_huffman_decode((const uint8_t *)(data + body_offset),
                                size - body_offset, char_count, message,
                                (int)sizeof(message));

            char formatted_message[MAX_USER_LENGTH + sizeof(message) + 32];

            // StringUtil PRIVATE_RECIEVE format. a global-chat message arrives as
            // a PM whose sender is "Global$<name>"; strip the 7-char prefix and say "tells [everyone]"
            int is_global = (strncasecmp(from_username, "global$", 7) == 0);

            if (is_global) {
                sprintf(formatted_message, "@pri@%s tells [everyone]: %s",
                        from_username + 7, message);
            } else {
                sprintf(formatted_message, "@pri@%s tells you: %s",
                        from_username, message);
            }

            mud->orsc_pending_crown = crown;
            mudclient_show_message(mud, formatted_message,
                                   MESSAGE_TYPE_PRIVATE);
            break;
        }
#endif

        size_t offset = 1;
        int64_t from = get_unsigned_long(data, offset, size);
        offset += 8;

#ifndef REVISION_177
        // message number here, ignored for now (absent in the 177 dialect)
        if (!mud->packet_stream->protocol177) {
            offset += 4;
        }
#endif

        char from_username[USERNAME_LENGTH + 1];
        decode_username(from, from_username);

        char *message = chat_message_decode(data, offset, size - offset);
        char formatted_message[USERNAME_LENGTH + strlen(message) + 18];

        sprintf(formatted_message, "@pri@%s: tells you %s", from_username,
                message);

        mudclient_show_server_message(mud, formatted_message);
        break;
    }
    case SERVER_IGNORE_LIST: {
        mud->ignore_list_count = get_unsigned_byte(data, 1, size);
        if (mud->ignore_list_count > SOCIAL_LIST_MAX) {
            mud->ignore_list_count = SOCIAL_LIST_MAX;
        }

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom writes four strings per entry, not one base37 long: name, name, formerName, formerName
            // only the first of each pair is meaningful, consume the duplicates; names fold to base37
            size_t offset = 2;

            for (int i = 0; i < mud->ignore_list_count; i++) {
                char name[MAX_USER_LENGTH + 1] = {0};
                char scratch[MAX_USER_LENGTH + 1] = {0};

                orsc_read_string(data, &offset, (size_t)size, name,
                                 (int)sizeof(name));

                // the three redundant copies
                for (int j = 0; j < 3; j++) {
                    orsc_read_string(data, &offset, (size_t)size, scratch,
                                     (int)sizeof(scratch));
                }

                mud->ignore_list[i] = encode_username(name);
            }

            break;
        }
#endif

        for (int i = 0; i < mud->ignore_list_count; i++) {
            mud->ignore_list[i] = get_unsigned_long(data, 2 + i * 8, size);
        }
        break;
    }
    case SERVER_CLOSE_CONNECTION: {
        mudclient_close_connection(mud);
        break;
    }
    case SERVER_SOUND: {
        char sound_name[size + 1];
        memset(sound_name, '\0', size + 1);

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom writeString()s the sound name (\n-terminated); authentic
            // writeNonTerminatedString()s it, ended by the packet boundary
            size_t offset = 1;

            orsc_read_string(data, &offset, (size_t)size, sound_name, size + 1);
            mudclient_play_sound(mud, sound_name);
            break;
        }
#endif

        strncpy(sound_name, (char *)data + 1, size - 1);
        mudclient_play_sound(mud, sound_name);
        break;
    }
    case SERVER_APPEARANCE: {
        mud->show_appearance_change = 1;
        break;
    }
    case SERVER_SLEEP_OPEN: {
        if (!mud->is_sleeping) {
            mud->fatigue_sleeping = mud->stat_fatigue;
        }

        mud->is_sleeping = 1;

        memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
        memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH + 1);

        mud->sleeping_status_text = NULL;

        // custom worlds send the captcha as a PNG file, not the authentic
        // run-length rows; decode by the PNG signature
        if (size > 9 && (uint8_t)data[1] == 0x89 && data[2] == 'P' &&
            data[3] == 'N' && data[4] == 'G') {
            if (!surface_read_sleep_png(mud->surface, mud->sprite_texture + 1,
                                        data + 1, size - 1)) {
                mud_error("sleep: could not decode the PNG captcha (%d bytes)\n",
                          size - 1);
                // the "tap here to get a different one" link still works
                mud->sleeping_status_text = "Can't show the word - get another";
            }
        } else {
            surface_read_sleep_word(mud->surface, mud->sprite_texture + 1,
                                    data);
        }
        break;
    }
    case SERVER_SLEEP_CLOSE: {
        mud->is_sleeping = 0;
        break;
    }
    case SERVER_SLEEP_INCORRECT: {
        mud->sleeping_status_text = "Incorrect - Please wait...";
        break;
    }
    case SERVER_PLAYER_STAT_FATIGUE_ASLEEP: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            // same two-short fork as SERVER_PLAYER_STAT_FATIGUE (114): custom writes a 0-100 percentage short then the 750-scale short
            // fatigue_sleeping is on the 750 scale, take the second short
            mud->fatigue_sleeping = get_unsigned_short(data, 3, size);
            break;
        }
#endif
        mud->fatigue_sleeping = get_unsigned_short(data, 1, size);
        break;
    }
    case SERVER_OPTION_LIST: {
        int count = get_unsigned_byte(data, 1, size);

        // clamp count as an index into option_menu_entry; the custom protocol
        // sends more than 5 entries (crafting 6, MagicalPoolCustom 13)
        if (count > OPTION_MENU_MAX) {
            count = OPTION_MENU_MAX;
        }

        mud->show_option_menu = 1;
        mud->option_menu_count = count;

        int offset = 2;

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom writes each option as a \n-terminated string; authentic length-prefixes it (u8 len + non-terminated text)
            size_t entry_offset = (size_t)offset;

            for (int i = 0; i < count; i++) {
                orsc_read_string(data, &entry_offset, (size_t)size,
                                 mud->option_menu_entry[i],
                                 (int)sizeof(mud->option_menu_entry[i]));
            }

            break;
        }
#endif

        for (int i = 0; i < count; i++) {
            int entry_length = get_unsigned_byte(data, offset++, size);
            if (entry_length > (size - offset)) {
                break;
            }
            strncpy(mud->option_menu_entry[i], (char *)data + offset,
                    entry_length);

            mud->option_menu_entry[i][entry_length] = '\0';

            offset += entry_length;
        }
        break;
    }
    case SERVER_OPTION_LIST_CLOSE: {
        mud->show_option_menu = 0;
        break;
    }
    case SERVER_WELCOME: {
#ifdef REVISION_177
        mud->welcome_days_ago = get_unsigned_int(data, 1, size);
        mud->welcome_recovery_set_days = get_unsigned_int(data, 5, size);
        mud->welcome_last_ip = get_unsigned_int(data, 9, size);
#else
        if (mud->protocol_custom) {
            // custom: String lastIp, u16 daysSinceLogin, u16 daysUntilRecovery activation (14 - daysSinceChange, or 0); no unread field
            // authentic packs the IP as 4 raw bytes and the recovery figure as a 200/201-sentinel byte
            size_t offset = 1;
            char ip[46] = {0};

            orsc_read_string(data, &offset, (size_t)size, ip, (int)sizeof(ip));

            mud->welcome_days_ago = get_unsigned_short(data, offset, size);
            offset += 2;

            mud->welcome_recovery_set_days =
                get_unsigned_short(data, offset, size);

            mud->welcome_unread_messages = 0;

            free(mud->welcome_last_ip_string);
            mud->welcome_last_ip_string = strdup(ip);

            // ui/welcome.c gates the "from:" line on welcome_last_ip != 0; custom
            // IP arrives already formatted (can be IPv6), so pass the string and use the int only as a non-zero marker
            mud->welcome_last_ip = ip[0] != '\0' ? 1 : 0;

            mud->show_dialog_welcome = 1;
            mud->welcome_screen_already_shown = 1;
            break;
        }

        if (mud->packet_stream->protocol177) {
            // 177 dialect layout (no unread-messages field)
            mud->welcome_days_ago = get_unsigned_int(data, 1, size);
            mud->welcome_recovery_set_days = get_unsigned_int(data, 5, size);
            mud->welcome_last_ip = get_unsigned_int(data, 9, size);
            mud->welcome_unread_messages = 0;
        } else {
            mud->welcome_last_ip = get_unsigned_int(data, 1, size);
            mud->welcome_days_ago = get_unsigned_short(data, 5, size);
            mud->welcome_recovery_set_days = get_unsigned_byte(data, 7, size);
            mud->welcome_unread_messages = get_unsigned_short(data, 8, size);
        }
#endif

        mud->show_dialog_welcome = 1;
        mud->welcome_screen_already_shown = 1;

        free(mud->welcome_last_ip_string);
        mud->welcome_last_ip_string = NULL;
        break;
    }
    case SERVER_SERVER_MESSAGE:
    case SERVER_SERVER_MESSAGE_ONTOP: {
        if (size >= 1 && (size_t)(size - 1) < sizeof(mud->server_message)) {
            memcpy(mud->server_message, (char *)data + 1, size - 1);
            mud->server_message[size - 1] = '\0';
#ifndef REVISION_177
            // custom SEND_BOX/SEND_BOX2 build the body with writeString, appending
            // a trailing 0x0A: strip it. authentic has no terminator, so gated on custom
            if (mud->protocol_custom && size >= 2 &&
                mud->server_message[size - 2] == '\n') {
                mud->server_message[size - 2] = '\0';
            }
#endif
            mud->show_dialog_server_message = 1;
            mud->server_message_box_top = opcode == SERVER_SERVER_MESSAGE_ONTOP;
            strcpy(mud->server_message_next, "");
            mud->server_message_page = 0;
        }
        break;
    }
    case SERVER_BANK_OPEN: {
        mud->show_dialog_bank = 1;

        if (mud->options->bank_menus) {
            mud->show_right_click_menu = 0;
        }

        // bank renumbered under a held reorder pick: drop it rather than commit
        // against the old slot numbering
        mud->bank_organize_slot = -1;

        // TODO toggle
        // mud->bank_selected_item_slot = -1;
        // mud->bank_selected_item = -2;
        // mud->bank_scroll_row = 0;

        int offset = 1;

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom bank: different layout from authentic-204. the two counts are u16 and every entry carries a plain i32 amount
            // rows past BANK_ITEMS_MAX are parsed and dropped, not skipped, so the offset stays aligned
            int stored_size = get_unsigned_short(data, offset, size);
            offset += 2;

            mud->bank_items_max = get_unsigned_short(data, offset, size);
            offset += 2;

            if (mud->bank_items_max > BANK_ITEMS_MAX) {
                mud->bank_items_max = BANK_ITEMS_MAX;
            }

            int stored = 0;

            for (int i = 0; i < stored_size; i++) {
                int bank_id = get_unsigned_short(data, offset, size);
                offset += 2;

                int bank_amount = get_unsigned_int(data, offset, size);
                offset += 4;

                if (stored < BANK_ITEMS_MAX) {
                    // the bank grid indexes game_data.items[] by this id; clamp an
                    // out-of-range id like the inventory handler
                    mud->new_bank_items[stored] =
                        bank_id < game_data.item_count ? bank_id : IRON_MACE_ID;
                    mud->new_bank_items_count[stored] = bank_amount;
                    stored++;
                }
            }

            mud->new_bank_item_count = stored;
        } else {
#endif
        mud->new_bank_item_count = get_unsigned_byte(data, offset++, size);

        if (mud->new_bank_item_count > BANK_ITEMS_MAX) {
            mud->new_bank_item_count = BANK_ITEMS_MAX;
        }

        mud->bank_items_max = get_unsigned_byte(data, offset++, size);

        if (mud->bank_items_max > BANK_ITEMS_MAX) {
            mud->bank_items_max = BANK_ITEMS_MAX;
        }

        for (int i = 0; i < mud->new_bank_item_count; i++) {
            int bank_id = get_unsigned_short(data, offset, size);
            mud->new_bank_items[i] =
                bank_id < game_data.item_count ? bank_id : IRON_MACE_ID;
            offset += 2;

            mud->new_bank_items_count[i] = get_stack_int(data, offset, size);

            if (mud->new_bank_items_count[i] >= 128) {
                offset += 4;
            } else {
                offset++;
            }
        }
#ifndef REVISION_177
        }
#endif

        mudclient_update_bank_items(mud);

        if (mud->options->bank_search) {
            memset(mud->input_pm_current, '\0', INPUT_PM_LENGTH + 1);
            memset(mud->input_pm_final, '\0', INPUT_PM_LENGTH + 1);
        }

        memset(mud->input_digits_current, '\0', INPUT_DIGITS_LENGTH + 1);
        mud->input_digits_final = 0;
        break;
    }
    case SERVER_BANK_CLOSE: {
        mud->show_dialog_bank = 0;
        mud->bank_organize_slot = -1; // don't hold a pick across a close
#ifndef REVISION_177
        // preset editor is a bank sub-interface: tear it down with the bank
        mud->show_dialog_bank_preset = 0;
#endif
        break;
    }
    case SERVER_BANK_UPDATE: {
        // a slot changed under a held reorder pick; a withdraw shifts every slot above it down by one
        mud->bank_organize_slot = -1;

        int offset = 1;
        int item_index = get_unsigned_byte(data, offset++, size);
        int item = get_unsigned_short(data, offset, size);
        offset += 2;

        int item_count;

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom: a plain i32 amount, never the variable stack-int (SEND_BANK_UPDATE: u8 slot, u16 catalogID, i32 amount)
            item_count = get_unsigned_int(data, offset, size);
            offset += 4;
        } else {
#endif
        item_count = get_stack_int(data, offset, size);

        if (item_count >= 128) {
            offset += 4;
        } else {
            offset++;
        }
#ifndef REVISION_177
        }
#endif

        if (item_count == 0) {
            mud->new_bank_item_count--;

            for (int i = item_index; i < mud->new_bank_item_count; i++) {
                mud->new_bank_items[i] = mud->new_bank_items[i + 1];
                mud->new_bank_items_count[i] = mud->new_bank_items_count[i + 1];
            }
        } else {
            mud->new_bank_items[item_index] =
                item < game_data.item_count ? item : IRON_MACE_ID;
            mud->new_bank_items_count[item_index] = item_count;

            if (item_index >= mud->new_bank_item_count) {
                mud->new_bank_item_count = item_index + 1;
            }
        }

        mudclient_update_bank_items(mud);
        break;
    }
    case SERVER_SHOP_OPEN: {
        mud->show_dialog_shop = 1;

        int offset = 1;
        int new_item_count = get_unsigned_byte(data, offset++, size);

        if (new_item_count > 40) {
            /* TODO: use some kind of constant to determine this (also below) */
            /* shop.h columns * rows = 40 */
            return;
        }

        int is_general = get_unsigned_byte(data, offset++, size);

        mud->shop_sell_price_mod = get_unsigned_byte(data, offset++, size);
        mud->shop_buy_price_mod = get_unsigned_byte(data, offset++, size);

        int stock_sensitivity = 0;

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom SEND_SHOP_OPEN diverges from authentic twice: one extra header byte, stockSensitivity; and the per-item third
            // field is a raw u16 baseAmount rather than authentic's signed byte holding a precomputed (baseAmount - amount) delta
            stock_sensitivity = get_unsigned_byte(data, offset++, size);
        }
#endif

        for (int i = 0; i < 40; i++) {
            mud->shop_items[i] = -1;
        }

        for (int i = 0; i < new_item_count; i++) {
            int shop_id = get_unsigned_short(data, offset, size);
            mud->shop_items[i] =
                shop_id < game_data.item_count ? shop_id : IRON_MACE_ID;
            offset += 2;

            mud->shop_items_count[i] = get_unsigned_short(data, offset, size);
            offset += 2;

#ifndef REVISION_177
            if (mud->protocol_custom) {
                int base_amount = get_unsigned_short(data, offset, size);
                offset += 2;

                // fold the custom cost formula into authentic units: delta = clamp(stockSensitivity * (baseAmount - amount), -100, 100),
                // cost = basePrice * max(priceMod + delta, 10) / 100. store the folded delta so ui/shop.c stays identical across protocols
                int delta =
                    stock_sensitivity * (base_amount - mud->shop_items_count[i]);

                if (delta > 100) {
                    delta = 100;
                } else if (delta < -100) {
                    delta = -100;
                }

                mud->shop_items_price[i] = delta;
                continue;
            }
#endif

            mud->shop_items_price[i] = get_signed_byte(data, offset++, size);
        }

        if (is_general == 1) {
            int shop_index = 39;

            for (int i = 0; i < mud->inventory_items_count; i++) {
                if (shop_index < new_item_count) {
                    break;
                }

                int unsellable = 0;

                for (int j = 0; j < 40; j++) {
                    if (mud->shop_items[j] != mud->inventory_item_id[i]) {
                        continue;
                    }

                    unsellable = 1;
                    break;
                }

                if (mud->inventory_item_id[i] == COINS_ID) {
                    unsellable = 1;
                }

                if (!unsellable) {
                    mud->shop_items[shop_index] =
                        mud->inventory_item_id[i] & 32767;

                    mud->shop_items_count[shop_index] = 0;
                    mud->shop_items_price[shop_index] = 0;
                    shop_index--;
                }
            }
        }

        if (mud->shop_selected_item_index >= 0 &&
            mud->shop_selected_item_index < 40 &&
            mud->shop_items[mud->shop_selected_item_index] !=
                mud->shop_selected_item_type) {
            mud->shop_selected_item_index = -1;
            mud->shop_selected_item_type = -2;
        }
        break;
    }
    case SERVER_SHOP_CLOSE: {
        mud->show_dialog_shop = 0;
        break;
    }
    case SERVER_TRADE_OPEN:
    case SERVER_DUEL_OPEN: {
        int player_index = get_unsigned_short(data, 1, size);

        if (player_index >= PLAYERS_SERVER_MAX) {
            return;
        }

        if (mud->player_server[player_index] != NULL) {
            strcpy(mud->transaction_recipient_name,
                   mud->player_server[player_index]->name);
        }

        int is_trade = opcode == SERVER_TRADE_OPEN;

        mud->show_dialog_trade = is_trade;

        mud->transaction_recipient_accepted = 0;
        mud->transaction_accepted = 0;
        mud->transaction_item_count = 0;
        mud->transaction_recipient_item_count = 0;

        if (!is_trade) {
            mud->show_dialog_duel = 1;
            mud->duel_option_retreat = 0;
            mud->duel_option_magic = 0;
            mud->duel_option_prayer = 0;
            mud->duel_option_weapons = 0;
        }
        break;
    }
    case SERVER_TRADE_CLOSE:
    case SERVER_DUEL_CLOSE: {
        mud->show_dialog_trade = 0;
        mud->show_dialog_trade_confirm = 0;

        mud->show_dialog_duel = 0;
        mud->show_dialog_duel_confirm = 0;

        mud->show_dialog_offer_x = 0;
        break;
    }
    case SERVER_TRADE_ITEMS:
    case SERVER_DUEL_ITEMS: {
#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom SEND_TRADE_OTHER_ITEMS / SEND_DUEL_OPPONENTS_ITEMS differ from authentic twice: a per-item noted byte sits
            // between the id and the amount, present only when the world accepts notes; and TRADE echoes your own offer as a second list
            int offset = 1;
            int notes = mud->orsc.want_bank_notes;

            offset = orsc_read_transaction_list(
                data, size, offset, notes,
                &mud->transaction_recipient_item_count,
                mud->transaction_recipient_items,
                mud->transaction_recipient_items_count,
                mud->transaction_recipient_items_noted);

            if (opcode == SERVER_TRADE_ITEMS) {
                orsc_read_transaction_list(data, size, offset, notes,
                                           &mud->transaction_item_count,
                                           mud->transaction_items,
                                           mud->transaction_items_count,
                                           mud->transaction_items_noted);
            }

            mud->transaction_recipient_accepted = 0;
            mud->transaction_accepted = 0;
            break;
        }
#endif

        mud->transaction_recipient_item_count =
            get_unsigned_byte(data, 1, size);
        if (mud->transaction_recipient_item_count > TRADE_ITEMS_MAX) {
            mud->transaction_recipient_item_count = TRADE_ITEMS_MAX;
        }

        int offset = 2;

        for (int i = 0; i < mud->transaction_recipient_item_count; i++) {
            mud->transaction_recipient_items[i] =
                get_unsigned_short(data, offset, size);

            offset += 2;

#ifndef REVISION_177
            // SP/LAN wire: u8 noted between id and amount when the world runs notes
            mud->transaction_recipient_items_noted[i] = 0;

            if (MUD_SP_WIRE(mud) && mud->orsc.want_bank_notes) {
                mud->transaction_recipient_items_noted[i] =
                    get_unsigned_byte(data, offset++, size) == 1;
            }
#endif

            mud->transaction_recipient_items_count[i] =
                get_unsigned_int(data, offset, size);

            offset += 4;
        }

        mud->transaction_recipient_accepted = 0;
        mud->transaction_accepted = 0;
        break;
    }
    case SERVER_TRADE_RECIPIENT_ACCEPTED:
    case SERVER_DUEL_RECIPIENT_ACCEPTED: {
        mud->transaction_recipient_accepted = get_unsigned_byte(data, 1, size);
        break;
    }
    case SERVER_TRADE_ACCEPTED:
    case SERVER_DUEL_ACCEPTED: {
        mud->transaction_accepted = get_unsigned_byte(data, 1, size);
        break;
    }
    case SERVER_TRADE_CONFIRM_OPEN:
    case SERVER_DUEL_CONFIRM_OPEN: {
        mud->show_dialog_offer_x = 0;

        if (opcode == SERVER_TRADE_CONFIRM_OPEN) {
            mud->show_dialog_trade_confirm = 1;
        } else {
            mud->show_dialog_duel_confirm = 1;
        }

        mud->transaction_confirm_accepted = 0;
        mud->show_dialog_trade = 0;
        mud->show_dialog_duel = 0;

        int offset = 1;

#ifndef REVISION_177
        if (mud->protocol_custom) {
            // custom SEND_TRADE_OPEN_CONFIRM / SEND_DUEL_CONFIRMWINDOW diverge from authentic twice: the opponent's name is a
            // \n-terminated string rather than an 8-byte base37 hash; and both item lists carry the per-item noted byte
            size_t name_offset = (size_t)offset;

            orsc_read_string(
                data, &name_offset, (size_t)size,
                mud->transaction_recipient_confirm_name_str,
                (int)sizeof(mud->transaction_recipient_confirm_name_str));

            offset = (int)name_offset;

            int notes = mud->orsc.want_bank_notes;

            offset = orsc_read_transaction_list(
                data, size, offset, notes,
                &mud->transaction_recipient_confirm_item_count,
                mud->transaction_recipient_confirm_items,
                mud->transaction_recipient_confirm_items_count,
                mud->transaction_recipient_confirm_items_noted);

            orsc_read_transaction_list(
                data, size, offset, notes,
                &mud->transaction_confirm_item_count,
                mud->transaction_confirm_items,
                mud->transaction_confirm_items_count,
                mud->transaction_confirm_items_noted);

            break;
        }
#endif

        mud->transaction_recipient_confirm_name =
            get_unsigned_long(data, offset, size);

        offset += 8;

        mud->transaction_recipient_confirm_item_count =
            get_unsigned_byte(data, offset++, size);

        // clamp this list to TRADE_ITEMS_MAX; the transaction_recipient_confirm_items arrays hold 14
        if (mud->transaction_recipient_confirm_item_count > TRADE_ITEMS_MAX) {
            mud->transaction_recipient_confirm_item_count = TRADE_ITEMS_MAX;
        }

        for (int i = 0; i < mud->transaction_recipient_confirm_item_count;
             i++) {
            mud->transaction_recipient_confirm_items[i] =
                get_unsigned_short(data, offset, size);

            offset += 2;

#ifndef REVISION_177
            mud->transaction_recipient_confirm_items_noted[i] = 0;

            if (MUD_SP_WIRE(mud) && mud->orsc.want_bank_notes) {
                mud->transaction_recipient_confirm_items_noted[i] =
                    get_unsigned_byte(data, offset++, size) == 1;
            }
#endif

            mud->transaction_recipient_confirm_items_count[i] =
                get_unsigned_int(data, offset, size);

            offset += 4;
        }

        mud->transaction_confirm_item_count =
            get_unsigned_byte(data, offset++, size);
        if (mud->transaction_confirm_item_count > TRADE_ITEMS_MAX) {
            mud->transaction_confirm_item_count = TRADE_ITEMS_MAX;
        }

        for (int i = 0; i < mud->transaction_confirm_item_count; i++) {
            mud->transaction_confirm_items[i] =
                get_unsigned_short(data, offset, size);

            offset += 2;

#ifndef REVISION_177
            mud->transaction_confirm_items_noted[i] = 0;

            if (MUD_SP_WIRE(mud) && mud->orsc.want_bank_notes) {
                mud->transaction_confirm_items_noted[i] =
                    get_unsigned_byte(data, offset++, size) == 1;
            }
#endif

            mud->transaction_confirm_items_count[i] =
                get_unsigned_int(data, offset, size);

            offset += 4;
        }
        break;
    }
    case SERVER_DUEL_SETTINGS: {
        mud->duel_option_retreat = get_unsigned_byte(data, 1, size);
        mud->duel_option_magic = get_unsigned_byte(data, 2, size);
        mud->duel_option_prayer = get_unsigned_byte(data, 3, size);
        mud->duel_option_weapons = get_unsigned_byte(data, 4, size);

        mud->transaction_recipient_accepted = 0;
        mud->transaction_accepted = 0;
        break;
    }
    case SERVER_PRAYER_STATUS: {
        size--;
        if (size > PRAYER_COUNT) {
            size = PRAYER_COUNT;
        }
        for (int i = 0; i < size; i++) {
            int on = data[i + 1] == 1;

            if (!mud->prayer_on[i] && on) {
                mudclient_play_sound(mud, "prayeron");
            }

            if (mud->prayer_on[i] && !on) {
                mudclient_play_sound(mud, "prayeroff");
            }

            mud->prayer_on[i] = on;
        }
        break;
    }
    case SERVER_MAGIC_BUBBLE: {
        if (mud->magic_bubble_count < MAGIC_BUBBLE_MAX) {
            int type = get_unsigned_byte(data, 1, size);
            int x = get_signed_byte(data, 2, size) + mud->local_region_x;
            int y = get_signed_byte(data, 3, size) + mud->local_region_y;

            mud->magic_bubbles[mud->magic_bubble_count].type = type;
            mud->magic_bubbles[mud->magic_bubble_count].time = 0;
            mud->magic_bubbles[mud->magic_bubble_count].x = x;
            mud->magic_bubbles[mud->magic_bubble_count].y = y;

            mud->magic_bubble_count++;
        }
        break;
    }
    case SERVER_PLAYER_DIED: {
        mud->death_screen_timeout = 250;
#ifdef RSC_DIAG
        DIAG("player_died at local=%d,%d abs=%d,%d plane_idx=%d "
             "region=%d,%d wild_depth=%d hp=%d/%d",
             mud->local_region_x, mud->local_region_y,
             mud->local_region_x + mud->plane_width + mud->region_x,
             mud->local_region_y + mud->plane_height + mud->region_y,
             mud->plane_index, mud->region_x, mud->region_y,
             mudclient_get_wilderness_depth(mud),
             mud->player_skill_current[SKILL_HITS],
             mud->player_skill_base[SKILL_HITS]);
        diag_trace_packets = 12;
#endif
        break;
    }
    case SERVER_LOGOUT_DENY: {
        mud->logout_timeout = 0;

        mudclient_show_message(mud,
                               "@cya@Sorry, you can't logout at the moment",
                               MESSAGE_TYPE_GAME);
        break;
    }
    case SERVER_GAME_SETTINGS: {
        mud->settings_camera_auto = get_unsigned_byte(data, 1, size);
        mud->settings_mouse_button_one = get_unsigned_byte(data, 2, size);
        mud->settings_sound_disabled = get_unsigned_byte(data, 3, size);

#ifndef REVISION_177
        // custom worlds append 34 more bytes to these 3, read sequentially. take the one rendered and ignore the rest
        // nametag = customOptions[22] = payload byte 25 (getCombatStyle is customOptions[0] at payload byte 3, 3 + 22 = 25)
        if (mud->protocol_custom && size > 26) {
            // per-player show flags; default show if the packet is short
            mud->orsc_show_side_menu = get_unsigned_byte(data, 21, size) == 1;
            mud->orsc_show_kill_feed = get_unsigned_byte(data, 22, size) == 1;
            mud->orsc_name_clan_tag_overlay =
                get_unsigned_byte(data, 26, size) == 1;

            // the two kill-counter HUD lines (payload bytes 29 and 35), each its
            // own account opt-in defaulting off
            if (size > 35) {
                mud->orsc_show_npc_kc = get_unsigned_byte(data, 29, size) == 1;
                mud->orsc_show_recent_npc_kc =
                    get_unsigned_byte(data, 35, size) == 1;
            }

            // account xp-counter state (C_EXPERIENCE_COUNTER, payload byte 24): 0 never, 1 recent, 2 always
            mud->orsc_xp_counter_synced = get_unsigned_byte(data, 24, size);
        }
#endif
        break;
    }
    case SERVER_PRIVACY_SETTINGS: {
        mud->settings_block_chat = get_unsigned_byte(data, 1, size);
        mud->settings_block_private = get_unsigned_byte(data, 2, size);
        mud->settings_block_trade = get_unsigned_byte(data, 3, size);
        mud->settings_block_duel = get_unsigned_byte(data, 4, size);
        break;
    }
#ifndef REVISION_177
    case SERVER_SYSTEM_UPDATE: {
        mud->system_update = get_unsigned_short(data, 1, size) * 32;
        break;
    }
#endif
    default:
        // an opcode with no handler
        break;
    }
}
