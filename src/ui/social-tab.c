#include "social-tab.h"

#ifndef REVISION_177
void mudclient_orsc_send_clan_action(mudclient *mud, int action,
                                     const char *str1, const char *str2) {
    packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
    packet_stream_put_byte(mud->packet_stream, INTERFACE_OPTION_CLAN);
    packet_stream_put_byte(mud->packet_stream, action);

    // custom clan packet: create reads name+tag, invite/kick read a name
    if (str1 != NULL) {
        packet_stream_put_string_newline(mud->packet_stream, (char *)str1);
    }

    if (str2 != NULL) {
        packet_stream_put_string_newline(mud->packet_stream, (char *)str2);
    }

    packet_stream_send_packet(mud->packet_stream);
}

void mudclient_orsc_send_party_action(mudclient *mud, int action,
                                      const char *str1) {
    packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
    packet_stream_put_byte(mud->packet_stream, INTERFACE_OPTION_PARTY);
    packet_stream_put_byte(mud->packet_stream, action);

    if (str1 != NULL) {
        packet_stream_put_string_newline(mud->packet_stream, (char *)str1);
    }

    // invite reads player + name + tag; name/tag are sent empty
    if (action == PARTY_OPTION_INVITE_PLAYER_OR_MAKE) {
        packet_stream_put_string_newline(mud->packet_stream, "");
        packet_stream_put_string_newline(mud->packet_stream, "");
    }

    packet_stream_send_packet(mud->packet_stream);
}
#endif

void mudclient_sort_friends(mudclient *mud) {
    int flag = 1;

    while (flag) {
        flag = 0;

        for (int i = 0; i < mud->friend_list_count - 1; i++) {
            if ((mud->friend_list_online[i] != MUD_FRIEND_ONLINE(mud) &&
                 mud->friend_list_online[i + 1] == MUD_FRIEND_ONLINE(mud)) ||
                (mud->friend_list_online[i] == 0 &&
                 mud->friend_list_online[i + 1] != 0)) {
                int online_status = mud->friend_list_online[i];
                mud->friend_list_online[i] = mud->friend_list_online[i + 1];
                mud->friend_list_online[i + 1] = online_status;

                int64_t encoded_username = mud->friend_list[i];
                mud->friend_list[i] = mud->friend_list[i + 1];
                mud->friend_list[i + 1] = encoded_username;

                flag = 1;
            }
        }
    }
}

void mudclient_add_friend(mudclient *mud, char *username) {
    int64_t encoded_username = encode_username(username);

#ifndef REVISION_177
    if (mud->protocol_custom) {
        mudclient_send_social_custom(mud, CLIENT_FRIEND_ADD, username);
    } else
#endif
    {
        packet_stream_new_packet(mud->packet_stream, CLIENT_FRIEND_ADD);
        packet_stream_put_long(mud->packet_stream, encoded_username);
        packet_stream_send_packet(mud->packet_stream);
    }

    for (int i = 0; i < mud->friend_list_count; i++) {
        if (mud->friend_list[i] == encoded_username) {
            return;
        }
    }

    if (mud->friend_list_count >= SOCIAL_LIST_MAX) {
        return;
    }

    mud->friend_list[mud->friend_list_count] = encoded_username;
    mud->friend_list_online[mud->friend_list_count] = 0;
    mud->friend_list_count++;
}

void mudclient_remove_friend(mudclient *mud, int64_t encoded_username) {
#ifndef REVISION_177
    if (mud->protocol_custom) {
        // custom friend-remove takes a name string, not a base37 long
        char name[USERNAME_LENGTH + 1] = {0};
        decode_username(encoded_username, name);
        mudclient_send_social_custom(mud, CLIENT_FRIEND_REMOVE, name);
    } else
#endif
    {
        packet_stream_new_packet(mud->packet_stream, CLIENT_FRIEND_REMOVE);
        packet_stream_put_long(mud->packet_stream, encoded_username);
        packet_stream_send_packet(mud->packet_stream);
    }

    for (int i = 0; i < mud->friend_list_count; i++) {
        if (mud->friend_list[i] != encoded_username) {
            continue;
        }

        mud->friend_list_count--;

        for (int j = i; j < mud->friend_list_count; j++) {
            mud->friend_list[j] = mud->friend_list[j + 1];
            mud->friend_list_online[j] = mud->friend_list_online[j + 1];
        }

        break;
    }

    char username[USERNAME_LENGTH] = {0};
    decode_username(encoded_username, username);

    char formatted[USERNAME_LENGTH + 46] = {0};

    sprintf(formatted, "@pri@%s has been removed from your friends list",
            username);

    mudclient_show_server_message(mud, formatted);
}

void mudclient_add_ignore(mudclient *mud, char *username) {
    int64_t encoded_username = encode_username(username);

#ifndef REVISION_177
    if (mud->protocol_custom) {
        mudclient_send_social_custom(mud, CLIENT_IGNORE_ADD, username);
    } else
#endif
    {
        packet_stream_new_packet(mud->packet_stream, CLIENT_IGNORE_ADD);
        packet_stream_put_long(mud->packet_stream, encoded_username);
        packet_stream_send_packet(mud->packet_stream);
    }

    for (int i = 0; i < mud->ignore_list_count; i++) {
        if (mud->ignore_list[i] == encoded_username) {
            return;
        }
    }

    if (mud->ignore_list_count >= SOCIAL_LIST_MAX) {
        return;
    }

    mud->ignore_list[mud->ignore_list_count++] = encoded_username;
}

void mudclient_remove_ignore(mudclient *mud, int64_t encoded_username) {
#ifndef REVISION_177
    if (mud->protocol_custom) {
        // custom ignore-remove takes a name string, not a base37 long
        char name[USERNAME_LENGTH + 1] = {0};
        decode_username(encoded_username, name);
        mudclient_send_social_custom(mud, CLIENT_IGNORE_REMOVE, name);
    } else
#endif
    {
        packet_stream_new_packet(mud->packet_stream, CLIENT_IGNORE_REMOVE);
        packet_stream_put_long(mud->packet_stream, encoded_username);
        packet_stream_send_packet(mud->packet_stream);
    }

    for (int i = 0; i < mud->ignore_list_count; i++) {
        if (mud->ignore_list[i] != encoded_username) {
            continue;
        }

        mud->ignore_list_count--;

        for (int j = i; j < mud->ignore_list_count; j++) {
            mud->ignore_list[j] = mud->ignore_list[j + 1];
        }

        return;
    }
}

void mudclient_send_private_message(mudclient *mud, int64_t username,
                                    int8_t *message, int length) {
    packet_stream_new_packet(mud->packet_stream, CLIENT_PM);
    packet_stream_put_long(mud->packet_stream, username);
    packet_stream_put_bytes(mud->packet_stream, message, 0, length);
    packet_stream_send_packet(mud->packet_stream);
}

#ifndef REVISION_177
// custom private message: name string + smart-length rs2-huffman body
void mudclient_send_private_message_custom(mudclient *mud, const char *username,
                                           const char *message) {
    int length = (int)strlen(message);

    if (length > ORSC_HUFFMAN_MAX_CHARS) {
        length = ORSC_HUFFMAN_MAX_CHARS;
    }

    uint8_t body[ORSC_HUFFMAN_MAX_CHARS * 4];

    int body_length =
        orsc_huffman_encode(message, length, body, (int)sizeof(body));

    if (body_length < 0) {
        return;
    }

    packet_stream_new_packet(mud->packet_stream, CLIENT_PM);
    packet_stream_put_string(mud->packet_stream, (char *)username);
    packet_stream_put_byte(mud->packet_stream, '\n');

    if (length < 128) {
        packet_stream_put_byte(mud->packet_stream, length);
    } else {
        packet_stream_put_short(mud->packet_stream, length + 32768);
    }

    packet_stream_put_bytes(mud->packet_stream, (int8_t *)body, 0, body_length);
    packet_stream_send_packet(mud->packet_stream);
}

// custom friend/ignore add-remove all take a name string
void mudclient_send_social_custom(mudclient *mud, int opcode,
                                  const char *username) {
    packet_stream_new_packet(mud->packet_stream, opcode);
    packet_stream_put_string(mud->packet_stream, (char *)username);
    packet_stream_put_byte(mud->packet_stream, '\n');
    packet_stream_send_packet(mud->packet_stream);
}
#endif

void mudclient_draw_ui_tab_social(mudclient *mud, int no_menus) {
    int ui_x = mud->surface->width - SOCIAL_WIDTH - 3;
    int ui_y = UI_BUTTON_SIZE + 1;

    int height = SOCIAL_HEIGHT;

    int is_touch = mudclient_is_touch(mud);

    if (is_touch) {
        height = 198;
        ui_x = UI_TABS_TOUCH_X - SOCIAL_WIDTH - 1;
        ui_y = (UI_TABS_TOUCH_Y + UI_TABS_TOUCH_HEIGHT) - height - 2;
    }

    if (mud->options->version_media >= 59) {
            mudclient_draw_ui_tab_label(mud, SOCIAL_TAB,
                                        SOCIAL_WIDTH + !is_touch,
                                        ui_x - !is_touch,
                                        ui_y - UI_TABS_LABEL_HEIGHT);
    }

    mud->ui_tab_min_x = ui_x;
    mud->ui_tab_max_x = mud->surface->width;
    mud->ui_tab_min_y = 0;
    mud->ui_tab_max_y = 240;

    if (is_touch) {
        mud->ui_tab_max_x = ui_x + SOCIAL_WIDTH;
        mud->ui_tab_min_y = ui_y - UI_TABS_LABEL_HEIGHT;
        mud->ui_tab_max_y = ui_y + height;
    }

    surface_draw_box_alpha(mud->surface, ui_x, ui_y + SOCIAL_TAB_HEIGHT,
                           SOCIAL_WIDTH, height - SOCIAL_TAB_HEIGHT, GREY_DC,
                           128);

    surface_draw_line_horizontal(mud->surface, ui_x, ui_y + height - 16,
                                 SOCIAL_WIDTH, BLACK);

    // resolves clan/party sub-tab indexes; -1 if absent
    const char *social_tab_names[4] = {"Friends", "Ignore", NULL, NULL};
    int social_tab_count = 2;
    int clan_tab_index = -1;
    int party_tab_index = -1;

#ifndef REVISION_177
    // clans also exist on the SP/co-op wire, like parties
    if ((mud->protocol_custom && mud->orsc.want_clans) || MUD_SP_WIRE(mud)) {
        clan_tab_index = social_tab_count;
        social_tab_names[social_tab_count++] = "Clan";
    }

    // parties also exist on the SP/co-op wire
    if ((mud->protocol_custom && mud->orsc.want_parties) ||
        MUD_SP_WIRE(mud)) {
        party_tab_index = social_tab_count;
        social_tab_names[social_tab_count++] = "Party";
    }
#endif

    if (mud->ui_tab_social_sub_tab >= social_tab_count) {
        mud->ui_tab_social_sub_tab = 0;
    }

    surface_draw_tabs(mud->surface, ui_x, ui_y, SOCIAL_WIDTH, SOCIAL_TAB_HEIGHT,
                      social_tab_names, social_tab_count,
                      mud->ui_tab_social_sub_tab);

    panel_clear_list(mud->panel_social_list, mud->control_list_social);

    if (mud->ui_tab_social_sub_tab == 0) {
        for (int i = 0; i < mud->friend_list_count; i++) {
            char colour[6] = "@red@";

            if (mud->friend_list_online[i] == MUD_FRIEND_ONLINE(mud)) {
                strcpy(colour, "@gre@");
            } else if (mud->friend_list_online[i] > 0) {
                strcpy(colour, "@yel@");
            }

            char username[USERNAME_LENGTH + 1] = {0};
            decode_username(mud->friend_list[i], username);

            char formatted_username[USERNAME_LENGTH + 30] = {0};

            sprintf(formatted_username, "%s%s~%d~@whi@Remove", colour, username,
                    ui_x + 126);

            panel_add_list_entry(mud->panel_social_list,
                                 mud->control_list_social, i,
                                 formatted_username);
        }
    } else if (mud->ui_tab_social_sub_tab == 1) {
        for (int i = 0; i < mud->ignore_list_count; i++) {
            char username[USERNAME_LENGTH + 1] = {0};
            decode_username(mud->ignore_list[i], username);

            char formatted_username[USERNAME_LENGTH + 30] = {0};

            sprintf(formatted_username, "@yel@%s~%04d~@whi@Remove", username,
                    ui_x + 126);

            panel_add_list_entry(mud->panel_social_list,
                                 mud->control_list_social, i,
                                 formatted_username);
        }
    }
#ifndef REVISION_177
    else if (mud->ui_tab_social_sub_tab == clan_tab_index &&
             mud->orsc_clan_in) {
        for (int i = 0; i < mud->orsc_clan_size; i++) {
            // leader gold, online members green, offline red
            int is_this_leader =
                strcmp(mud->orsc_clan_member_names[i],
                       mud->orsc_clan_leader) == 0;

            char *colour = is_this_leader
                               ? "@yel@"
                               : (mud->orsc_clan_member_online[i] ? "@gre@"
                                                                  : "@red@");

            char formatted_member[80] = {0};

            if (mud->orsc_clan_is_leader && !is_this_leader) {
                sprintf(formatted_member, "%s%s~%d~@whi@Kick", colour,
                        mud->orsc_clan_member_names[i], ui_x + 126);
            } else {
                sprintf(formatted_member, "%s%s", colour,
                        mud->orsc_clan_member_names[i]);
            }

            panel_add_list_entry(mud->panel_social_list,
                                 mud->control_list_social, i,
                                 formatted_member);
        }
    } else if (mud->ui_tab_social_sub_tab == clan_tab_index &&
               !mud->orsc_clan_in && mud->orsc_clan_browse_count > 0) {
        // clan browse results with join-setting colours (@gr2@/@yel@/@red@)
        for (int i = 0; i < mud->orsc_clan_browse_count; i++) {
            char *colour = mud->orsc_clan_browse_can_join[i] == 0
                               ? "@gr2@"
                               : (mud->orsc_clan_browse_can_join[i] == 1
                                      ? "@yel@"
                                      : "@red@");

            char formatted_clan[96] = {0};

            sprintf(formatted_clan, "%s%s (%s) - %d", colour,
                    mud->orsc_clan_browse_names[i],
                    mud->orsc_clan_browse_tags[i],
                    mud->orsc_clan_browse_members[i]);

            panel_add_list_entry(mud->panel_social_list,
                                 mud->control_list_social, i, formatted_clan);
        }
    } else if (mud->ui_tab_social_sub_tab == party_tab_index &&
               !mud->orsc_party_in && mud->orsc_party_browse_count > 0) {
        // party browse results: join setting takes the clan list colours
        for (int i = 0; i < mud->orsc_party_browse_count; i++) {
            char *colour = mud->orsc_party_browse_can_join[i] == 0
                               ? "@gr2@"
                               : (mud->orsc_party_browse_can_join[i] == 1
                                      ? "@yel@"
                                      : "@red@");

            char formatted_party[96] = {0};

            sprintf(formatted_party, "%sParty %d - %d members - %d points",
                    colour, mud->orsc_party_browse_ids[i],
                    mud->orsc_party_browse_members[i],
                    mud->orsc_party_browse_points[i]);

            panel_add_list_entry(mud->panel_social_list,
                                 mud->control_list_social, i,
                                 formatted_party);
        }
    } else if (mud->ui_tab_social_sub_tab == party_tab_index &&
               mud->orsc_party_in) {
        for (int i = 0; i < mud->orsc_party_size; i++) {
            int is_this_leader =
                strcmp(mud->orsc_party_member_names[i],
                       mud->orsc_party_leader) == 0;

            char *colour =
                is_this_leader
                    ? "@yel@"
                    : (mud->orsc_party_member_online[i] ? "@gre@" : "@red@");

            char formatted_member[96] = {0};

            // the snapshot carries live hits + combat level per member
            if (mud->orsc_party_is_leader && !is_this_leader) {
                sprintf(formatted_member, "%s%s (%d/%d)~%d~@whi@Kick", colour,
                        mud->orsc_party_member_names[i],
                        mud->orsc_party_member_cur_hits[i],
                        mud->orsc_party_member_max_hits[i], ui_x + 126);
            } else {
                sprintf(formatted_member, "%s%s (%d/%d) L%d", colour,
                        mud->orsc_party_member_names[i],
                        mud->orsc_party_member_cur_hits[i],
                        mud->orsc_party_member_max_hits[i],
                        mud->orsc_party_member_combat[i]);
            }

            panel_add_list_entry(mud->panel_social_list,
                                 mud->control_list_social, i,
                                 formatted_member);
        }
    }
#endif

    int mouse_x = mud->mouse_x - ui_x;
    int mouse_y = mud->mouse_y - ui_y;

    int handle_panel_input_early = is_touch;

#ifdef _3DS
    handle_panel_input_early = 1;

    panel_handle_mouse(mud->panel_social_list, mud->mouse_x, mud->mouse_y,
                       mud->last_mouse_button_down, mud->mouse_button_down,
                       mud->mouse_scroll_delta);
#else
    if (is_touch) {
        handle_panel_input_early = 1;

        /* feed the live finger while one is down, else the stick cursor + its
         * button, so the scrollbar drags by BOTH touch and joystick+X (a held
         * finger never raises mouse_button_down) */
        int px = mud->mouse_x, py = mud->mouse_y, pdown = mud->mouse_button_down;
        if (mudclient_finger_1_down) {
            px = mudclient_finger_1_x;
            py = mudclient_finger_1_y;
            pdown = 1;
        }

        panel_handle_mouse(mud->panel_social_list, px, py,
                           mud->last_mouse_button_down, pdown,
                           mud->mouse_scroll_delta);
    }
#endif

    panel_draw_panel(mud->panel_social_list);

    char *activate_verb = is_touch ? "Tap" : "Click";
    char formatted[SURFACE_STRING_MAX] = {0};

    if (mud->ui_tab_social_sub_tab == 0) {
        int friend_index = panel_get_list_entry_index(mud->panel_social_list,
                                                      mud->control_list_social);

        snprintf(formatted, SURFACE_STRING_MAX, "%s a name to send a message",
                 activate_verb);

        if (friend_index >= 0 && mud->mouse_x < ui_x + 176) {
            char username[USERNAME_LENGTH + 1] = {0};
            decode_username(mud->friend_list[friend_index], username);

            if (mud->mouse_x > ui_x + 116) {
                sprintf(formatted, "%s to remove %s", activate_verb, username);
            } else if (mud->friend_list_online[friend_index] ==
                       MUD_FRIEND_ONLINE(mud)) {
                sprintf(formatted, "%s to message %s", activate_verb, username);
            } else if (mud->friend_list_online[friend_index] > 0) {
#ifdef REVISION_177
                sprintf(formatted, "%s is on world %d", username,
                        mud->friend_list_online[friend_index]);
#else
                if (mud->protocol177) {
                    sprintf(formatted, "%s is on world %d", username,
                            mud->friend_list_online[friend_index]);
                } else if (mud->friend_list_online[friend_index] < 200) {
                    sprintf(formatted, "%s is on world %d", username,
                            mud->friend_list_online[friend_index] - 9);
                } else {
                    sprintf(formatted, "%s is on classic %d", username,
                            mud->friend_list_online[friend_index] - 219);
                }
#endif
            } else {
                sprintf(formatted, "%s is offline", username);
            }
        }

        surface_draw_string_centre(mud->surface, formatted,
                                   ui_x + (SOCIAL_WIDTH / 2), ui_y + 35,
                                   FONT_BOLD_12, WHITE);

        int text_colour = BLACK;

        if (mud->mouse_x > ui_x && mud->mouse_x < ui_x + SOCIAL_WIDTH &&
            mud->mouse_y > ui_y + height - 16 && mud->mouse_y < ui_y + height) {
            text_colour = YELLOW;
        } else {
            text_colour = WHITE;
        }

        surface_draw_stringf_centre(mud->surface, ui_x + (SOCIAL_WIDTH / 2),
                                    ui_y + height - 3, FONT_BOLD_12,
                                    text_colour, "%s here to add a friend",
                                    activate_verb);

    } else if (mud->ui_tab_social_sub_tab == 1) {
        int ignore_index = panel_get_list_entry_index(mud->panel_social_list,
                                                      mud->control_list_social);

        strcpy(formatted, "Blocking messages from:");

        if (ignore_index >= 0 && mud->mouse_x < ui_x + 176 &&
            mud->mouse_x > ui_x + 116) {
            char username[USERNAME_LENGTH + 1] = {0};
            decode_username(mud->ignore_list[ignore_index], username);
            sprintf(formatted, "%s to remove %s", activate_verb, username);
        }

        surface_draw_string_centre(mud->surface, formatted,
                                   ui_x + (SOCIAL_WIDTH / 2), ui_y + 35,
                                   FONT_BOLD_12, WHITE);

        int text_colour = BLACK;

        if (mud->mouse_x > ui_x && mud->mouse_x < ui_x + SOCIAL_WIDTH &&
            mud->mouse_y > ui_y + height - 16 && mud->mouse_y < ui_y + height) {
            text_colour = YELLOW;
        } else {
            text_colour = WHITE;
        }

        surface_draw_stringf_centre(
            mud->surface, ui_x + (SOCIAL_WIDTH / 2), ui_y + height - 3,
            FONT_BOLD_12, text_colour, "%s here to add a name", activate_verb);
    }
#ifndef REVISION_177
    else if (mud->ui_tab_social_sub_tab == clan_tab_index ||
             mud->ui_tab_social_sub_tab == party_tab_index) {
        int is_clan = mud->ui_tab_social_sub_tab == clan_tab_index;
        int in_group = is_clan ? mud->orsc_clan_in : mud->orsc_party_in;
        int is_leader =
            is_clan ? mud->orsc_clan_is_leader : mud->orsc_party_is_leader;

        if (in_group) {
            if (is_clan) {
                snprintf(formatted, SURFACE_STRING_MAX, "%s (%s)",
                         mud->orsc_clan_name, mud->orsc_clan_tag);
            } else {
                snprintf(formatted, SURFACE_STRING_MAX, "%s's party",
                         mud->orsc_party_leader);
            }

            // explicit Leave zone: the right 48px of the status line
            int leave_colour =
                mud->mouse_x > ui_x + SOCIAL_WIDTH - 48 &&
                        mud->mouse_x < ui_x + SOCIAL_WIDTH &&
                        mud->mouse_y > ui_y + 25 && mud->mouse_y < ui_y + 40
                    ? YELLOW
                    : RED;

            surface_draw_string(mud->surface, "Leave",
                                ui_x + SOCIAL_WIDTH - 44, ui_y + 35,
                                FONT_BOLD_12, leave_colour);
        } else {
            snprintf(formatted, SURFACE_STRING_MAX, "You are not in a %s",
                     is_clan ? "clan" : "party");

            // clanless/partyless: the Find zone requests the browse list, click a row to join
            {
                int find_colour =
                    mud->mouse_x > ui_x + SOCIAL_WIDTH - 48 &&
                            mud->mouse_x < ui_x + SOCIAL_WIDTH &&
                            mud->mouse_y > ui_y + 25 && mud->mouse_y < ui_y + 40
                        ? YELLOW
                        : GREEN;

                surface_draw_string(mud->surface, "Find",
                                    ui_x + SOCIAL_WIDTH - 40, ui_y + 35,
                                    FONT_BOLD_12, find_colour);
            }
        }

        surface_draw_string_centre(
            mud->surface, formatted,
            ui_x + (SOCIAL_WIDTH - 48) / 2,
            ui_y + 35, FONT_BOLD_12, WHITE);

        int text_colour = WHITE;

        if (mud->mouse_x > ui_x && mud->mouse_x < ui_x + SOCIAL_WIDTH &&
            mud->mouse_y > ui_y + height - 16 && mud->mouse_y < ui_y + height) {
            text_colour = YELLOW;
        }

        if (is_clan && !in_group) {
            surface_draw_stringf_centre(mud->surface,
                                        ui_x + (SOCIAL_WIDTH / 2),
                                        ui_y + height - 3, FONT_BOLD_12,
                                        text_colour,
                                        "%s here to create a clan",
                                        activate_verb);
        } else if (!is_clan && !in_group) {
            // inviting is what creates a party (INVITE_PLAYER_OR_MAKE)
            surface_draw_stringf_centre(mud->surface,
                                        ui_x + (SOCIAL_WIDTH / 2),
                                        ui_y + height - 3, FONT_BOLD_12,
                                        text_colour,
                                        "%s here to start a party",
                                        activate_verb);
        } else if (is_leader) {
            surface_draw_stringf_centre(mud->surface,
                                        ui_x + (SOCIAL_WIDTH / 2),
                                        ui_y + height - 3, FONT_BOLD_12,
                                        text_colour,
                                        "%s here to invite a player",
                                        activate_verb);
        }
    }
#endif

    if (!no_menus) {
        return;
    }

    if (!handle_panel_input_early) {
        int is_within_x = mud->options->off_handle_scroll_drag
                              ? 1
                              : mouse_x >= 0 && mouse_x < SOCIAL_WIDTH;

        if (!is_within_x || !(mouse_y >= 0 && mouse_y < height)) {
            return;
        }

        panel_handle_mouse(mud->panel_social_list, mouse_x + ui_x,
                           mouse_y + ui_y, mud->last_mouse_button_down,
                           mud->mouse_button_down, mud->mouse_scroll_delta);
    }

    if (mouse_y <= SOCIAL_TAB_HEIGHT && mud->mouse_button_click == 1) {
        int clicked_tab = mouse_x * social_tab_count / SOCIAL_WIDTH;

        if (clicked_tab < 0) {
            clicked_tab = 0;
        } else if (clicked_tab >= social_tab_count) {
            clicked_tab = social_tab_count - 1;
        }

        if (clicked_tab != mud->ui_tab_social_sub_tab) {
            mud->ui_tab_social_sub_tab = clicked_tab;
            panel_reset_list(mud->panel_social_list, mud->control_list_social);
        }
    }

    if (mud->mouse_button_click == 1 && mud->ui_tab_social_sub_tab == 0) {
        int friend_index = panel_get_list_entry_index(mud->panel_social_list,
                                                      mud->control_list_social);

        if (friend_index >= 0 && mouse_x < 176) {
            if (mouse_x > 116) {
                mudclient_remove_friend(mud, mud->friend_list[friend_index]);
            } else if (mud->friend_list_online[friend_index] != 0) {
                mud->show_dialog_social_input = SOCIAL_MESSAGE_FRIEND;

                mud->private_message_target = mud->friend_list[friend_index];

                memset(mud->input_pm_current, '\0', INPUT_PM_LENGTH + 1);
                memset(mud->input_pm_final, '\0', INPUT_PM_LENGTH + 1);
            }
        }
    }

    if (mud->mouse_button_click == 1 && mud->ui_tab_social_sub_tab == 1) {
        int ignore_index = panel_get_list_entry_index(mud->panel_social_list,
                                                      mud->control_list_social);

        if (ignore_index >= 0 && mouse_x < 176 && mouse_x > 116) {
            mudclient_remove_ignore(mud, mud->ignore_list[ignore_index]);
        }
    }

#ifndef REVISION_177
    // leader admin via right-click menus
    if (mud->ui_tab_social_sub_tab == clan_tab_index && mud->orsc_clan_in &&
        mud->orsc_clan_is_leader) {
        int member_index = panel_get_list_entry_index(mud->panel_social_list,
                                                      mud->control_list_social);

        if (member_index >= 0 && member_index < mud->orsc_clan_size &&
            mouse_x < 116 &&
            strcmp(mud->orsc_clan_member_names[member_index],
                   mud->orsc_clan_leader) != 0) {
            static const char *rank_actions[2] = {"Make General",
                                                  "Make member"};
            static const int rank_values[2] = {2, 0};

            for (int i = 0; i < 2; i++) {
                strcpy(mud->menu_items[mud->menu_items_count].action_text,
                       rank_actions[i]);
                sprintf(mud->menu_items[mud->menu_items_count].target_text,
                        "@whi@%s", mud->orsc_clan_member_names[member_index]);
                mud->menu_items[mud->menu_items_count].type = MENU_CLAN_RANK;
                mud->menu_items[mud->menu_items_count].index = member_index;
                mud->menu_items[mud->menu_items_count].target_index =
                    rank_values[i];
                mud->menu_items_count++;
            }

            strcpy(mud->menu_items[mud->menu_items_count].action_text,
                   "Give leadership");
            sprintf(mud->menu_items[mud->menu_items_count].target_text,
                    "@whi@%s", mud->orsc_clan_member_names[member_index]);
            mud->menu_items[mud->menu_items_count].type =
                MENU_CLAN_LEADERSHIP;
            mud->menu_items[mud->menu_items_count].index = member_index;
            mud->menu_items_count++;
        }

        // clan settings on the status line (the three select buttons)
        if (mouse_y > 25 && mouse_y < 40 && mouse_x < SOCIAL_WIDTH - 48) {
            static const char *setting_names[3] = {"Kick rank", "Invite rank",
                                                   "Requests"};
            static const char *setting_states[3][3] = {
                {"Anyone", "Owner", "General+"},
                {"Anyone", "Owner", "General+"},
                {"Anyone can join", "Invite only", "Closed"}};

            for (int mode = 0; mode < 3; mode++) {
                for (int state = 0; state < 3; state++) {
                    sprintf(mud->menu_items[mud->menu_items_count].action_text,
                            "%s:", setting_names[mode]);
                    sprintf(mud->menu_items[mud->menu_items_count].target_text,
                            "@whi@%s", setting_states[mode][state]);
                    mud->menu_items[mud->menu_items_count].type =
                        MENU_CLAN_SETTING;
                    mud->menu_items[mud->menu_items_count].index = mode;
                    mud->menu_items[mud->menu_items_count].target_index =
                        state;
                    mud->menu_items_count++;
                }
            }
        }
    }

    if (mud->protocol_custom && mud->ui_tab_social_sub_tab == party_tab_index &&
        mud->orsc_party_in && mouse_y > 25 && mouse_y < 40 &&
        mouse_x < SOCIAL_WIDTH - 48) {
        // per-player share toggles first, available to every member (custom worlds only)
        static const char *share_names[2] = {"Toggle loot share",
                                             "Toggle XP share"};

        for (int i = 0; i < 2; i++) {
            strcpy(mud->menu_items[mud->menu_items_count].action_text,
                   share_names[i]);
            mud->menu_items[mud->menu_items_count].target_text[0] = '\0';
            mud->menu_items[mud->menu_items_count].type = MENU_PARTY_SHARE;
            mud->menu_items[mud->menu_items_count].index = i;
            mud->menu_items_count++;
        }

        // party settings (kick/invite rank menus), leader only
        if (mud->orsc_party_is_leader) {
            static const char *party_setting_names[2] = {"Kick rank",
                                                         "Invite rank"};
            static const char *party_setting_states[3] = {"Anyone", "Owner",
                                                          "General+"};

            for (int mode = 0; mode < 2; mode++) {
                for (int state = 0; state < 3; state++) {
                    sprintf(mud->menu_items[mud->menu_items_count].action_text,
                            "%s:", party_setting_names[mode]);
                    sprintf(mud->menu_items[mud->menu_items_count].target_text,
                            "@whi@%s", party_setting_states[state]);
                    mud->menu_items[mud->menu_items_count].type =
                        MENU_PARTY_SETTING;
                    mud->menu_items[mud->menu_items_count].index = mode;
                    mud->menu_items[mud->menu_items_count].target_index =
                        state;
                    mud->menu_items_count++;
                }
            }
        }
    }

    if (mud->mouse_button_click == 1 &&
        mud->ui_tab_social_sub_tab == clan_tab_index && mud->orsc_clan_in) {
        // leader kicking a member (the Kick zone mirrors friends' Remove)
        if (mud->orsc_clan_is_leader) {
            int member_index = panel_get_list_entry_index(
                mud->panel_social_list, mud->control_list_social);

            if (member_index >= 0 && member_index < mud->orsc_clan_size &&
                mouse_x < 176 && mouse_x > 116 &&
                strcmp(mud->orsc_clan_member_names[member_index],
                       mud->orsc_clan_leader) != 0) {
                mudclient_orsc_send_clan_action(
                    mud, CLAN_OPTION_KICK_PLAYER,
                    mud->orsc_clan_member_names[member_index], NULL);
            }
        }

        // Leave zone on the status line
        if (mouse_x > SOCIAL_WIDTH - 48 && mouse_y > 25 && mouse_y < 40) {
            mudclient_orsc_send_clan_action(mud, CLAN_OPTION_LEAVE, NULL,
                                            NULL);
        }
    }

    if (mud->mouse_button_click == 1 &&
        mud->ui_tab_social_sub_tab == clan_tab_index && !mud->orsc_clan_in) {
        // Find zone requests the clan list
        if (mouse_x > SOCIAL_WIDTH - 48 && mouse_y > 25 && mouse_y < 40) {
            mudclient_orsc_send_clan_action(mud, CLAN_OPTION_SEND_CLAN_INFO,
                                            NULL, NULL);
        }

        // clicking a browse row joins that clan by name
        int browse_index = panel_get_list_entry_index(mud->panel_social_list,
                                                      mud->control_list_social);

        if (browse_index >= 0 &&
            browse_index < mud->orsc_clan_browse_count && mouse_x < 176) {
            char join_command[48] = {0};

            snprintf(join_command, sizeof(join_command), "joinclan %s",
                     mud->orsc_clan_browse_names[browse_index]);
            mudclient_send_command_string(mud, join_command);
        }
    }

    if (mud->mouse_button_click == 1 &&
        mud->ui_tab_social_sub_tab == party_tab_index && !mud->orsc_party_in) {
        // Find zone requests the party list
        if (mouse_x > SOCIAL_WIDTH - 48 && mouse_y > 25 && mouse_y < 40) {
            mudclient_orsc_send_party_action(
                mud, PARTY_OPTION_SEND_PARTY_INFO, NULL);
        }
    }

    if (mud->mouse_button_click == 1 &&
        mud->ui_tab_social_sub_tab == party_tab_index && mud->orsc_party_in) {
        if (mud->orsc_party_is_leader) {
            int member_index = panel_get_list_entry_index(
                mud->panel_social_list, mud->control_list_social);

            if (member_index >= 0 && member_index < mud->orsc_party_size &&
                mouse_x < 176 && mouse_x > 116 &&
                strcmp(mud->orsc_party_member_names[member_index],
                       mud->orsc_party_leader) != 0) {
                mudclient_orsc_send_party_action(
                    mud, PARTY_OPTION_KICK_PLAYER,
                    mud->orsc_party_member_names[member_index]);
            }
        }

        if (mouse_x > SOCIAL_WIDTH - 48 && mouse_y > 25 && mouse_y < 40) {
            mudclient_orsc_send_party_action(mud, PARTY_OPTION_LEAVE, NULL);
        }
    }
#endif

    if (mouse_y > 166 && mud->mouse_button_click == 1) {
        memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH);
        memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH);

        if (mud->ui_tab_social_sub_tab == 0) {
            mud->show_dialog_social_input = SOCIAL_ADD_FRIEND;
        } else if (mud->ui_tab_social_sub_tab == 1) {
            mud->show_dialog_social_input = SOCIAL_ADD_IGNORE;
        }
#ifndef REVISION_177
        else if (mud->ui_tab_social_sub_tab == clan_tab_index) {
            if (!mud->orsc_clan_in) {
                mud->show_dialog_social_input = SOCIAL_CLAN_CREATE_NAME;
            } else if (mud->orsc_clan_is_leader) {
                mud->show_dialog_social_input = SOCIAL_CLAN_INVITE;
            }
        } else if (mud->ui_tab_social_sub_tab == party_tab_index) {
            // one prompt for both flows: inviting creates the party
            if (!mud->orsc_party_in || mud->orsc_party_is_leader) {
                mud->show_dialog_social_input = SOCIAL_PARTY_INVITE;
            }
        }
#endif
    }

    mud->mouse_button_click = 0;
}

void mudclient_draw_social_input(mudclient *mud) {
    int box_width = mud->show_dialog_social_input == SOCIAL_MESSAGE_FRIEND
                        ? SOCIAL_DIALOG_MESSAGE_WIDTH
                        : SOCIAL_DIALOG_ADD_WIDTH;

    int dialog_x = mud->surface->width / 2 - box_width / 2;
    int dialog_y = (mud->surface->height / 2) - (SOCIAL_DIALOG_HEIGHT / 2) + 7;

    int cancel_offset_x = mud->surface->width / 2 - SOCIAL_CANCEL_SIZE / 2;
    int cancel_offset_y = mud->surface->height / 2 + SOCIAL_CANCEL_SIZE / 2;

    char *input_current = mud->show_dialog_social_input == SOCIAL_MESSAGE_FRIEND
                              ? mud->input_pm_current
                              : mud->input_text_current;

    if (mud->mouse_button_click != 0) {
        mud->mouse_button_click = 0;

        if (mud->mouse_x < dialog_x || mud->mouse_y < dialog_y ||
            mud->mouse_x > dialog_x + box_width ||
            mud->mouse_y > dialog_y + SOCIAL_DIALOG_HEIGHT) {
            mud->show_dialog_social_input = 0;
            return;
        }

        if (mud->mouse_x > cancel_offset_x &&
            mud->mouse_x < cancel_offset_x + SOCIAL_CANCEL_SIZE &&
            mud->mouse_y > cancel_offset_y &&
            mud->mouse_y < cancel_offset_y + SOCIAL_CANCEL_SIZE) {
            mud->show_dialog_social_input = 0;
            return;
        }

        if (mudclient_is_touch(mud)) {
            int keyboard_x = mud->surface->width / 2 - box_width / 2;

            int keyboard_y = dialog_y + 20;

            if (mud->mouse_x >= keyboard_x &&
                mud->mouse_x <= keyboard_x + box_width &&
                mud->mouse_y >= keyboard_y && mud->mouse_y <= keyboard_y + 20) {

                mudclient_trigger_keyboard(mud, input_current, 0, keyboard_x,
                                           keyboard_y, box_width - 5, 30,
                                           FONT_BOLD_14, 1,
                                           /* submit_on_enter */ 1);
            }
        }
    }

    surface_draw_box(mud->surface, dialog_x, dialog_y, box_width,
                     SOCIAL_DIALOG_HEIGHT, BLACK);

    surface_draw_border(mud->surface, dialog_x, dialog_y, box_width,
                        SOCIAL_DIALOG_HEIGHT, WHITE);

    dialog_y += 20;

    char formatted_current[INPUT_PM_LENGTH + 2] = {0};
    sprintf(formatted_current, "%s*", input_current);

    switch (mud->show_dialog_social_input) {
    case SOCIAL_ADD_FRIEND: {
        surface_draw_string_centre(
            mud->surface, "Enter name to add to friends list",
            mud->surface->width / 2, dialog_y, FONT_BOLD_14, WHITE);

        dialog_y += 20;

        surface_draw_string_centre(mud->surface, formatted_current,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        char *username = mud->input_text_final;

        if (username[0] != '\0') {
            int64_t encoded_username = encode_username(username);

            if (username[0] != '\0' &&
                encoded_username != mud->local_player->encoded_username) {
                mudclient_add_friend(mud, username);
            }

            memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
            memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH + 1);

            mud->show_dialog_social_input = 0;
        }
        break;
    }
    case SOCIAL_MESSAGE_FRIEND: {
        char target_name[USERNAME_LENGTH + 1];
        decode_username(mud->private_message_target, target_name);

        char formatted_message[USERNAME_LENGTH + 26] = {0};
        sprintf(formatted_message, "Enter message to send to %s", target_name);

        surface_draw_string_centre(mud->surface, formatted_message,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        dialog_y += 20;

        surface_draw_string_centre(mud->surface, formatted_current,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        char *message = mud->input_pm_final;

        if (message[0] != '\0') {
            int length = chat_message_encode(message);

#ifndef REVISION_177
            if (mud->protocol_custom) {
                // custom keys the recipient by name and huffman-codes the body
                char target[MAX_USER_LENGTH + 1] = {0};
                decode_username(mud->private_message_target, target);

                mudclient_send_private_message_custom(mud, target, message);
            } else
#endif
            {
                mudclient_send_private_message(mud, mud->private_message_target,
                                               chat_message_encoded, length);
            }

            char *decoded_message =
                chat_message_decode(chat_message_encoded, 0, length);

            /*if (mud->options->word_filter) {
                message = word_filter_filter(message);
            }*/

            char formatted_message[USERNAME_LENGTH + strlen(decoded_message) +
                                   17];

            // custom worlds echo sent PMs back (opcode 87) and render only that;
            // everywhere else the local echo is the only feedback
            if (!mud->protocol_custom) {
                sprintf(formatted_message, "@pri@You tell %s: %s", target_name,
                        decoded_message);

                mudclient_show_server_message(mud, formatted_message);
            }

            memset(mud->input_pm_current, '\0', INPUT_PM_LENGTH + 1);
            memset(mud->input_pm_final, '\0', INPUT_PM_LENGTH + 1);
            mud->show_dialog_social_input = 0;
        }
        break;
    }
    case SOCIAL_ADD_IGNORE: {
        surface_draw_string_centre(
            mud->surface, "Enter name to add to ignore list",
            mud->surface->width / 2, dialog_y, FONT_BOLD_14, WHITE);

        dialog_y += 20;

        surface_draw_string_centre(mud->surface, formatted_current,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        char *username = mud->input_text_final;

        if (username[0] != '\0') {
            int64_t encoded_username = encode_username(username);

            if (username[0] != '\0' &&
                encoded_username != mud->local_player->encoded_username) {
                mudclient_add_ignore(mud, username);
            }

            memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
            memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH + 1);

            mud->show_dialog_social_input = 0;
        }
        break;
    }
#ifndef REVISION_177
    case SOCIAL_CLAN_CREATE_NAME: {
        surface_draw_string_centre(mud->surface, "Enter a name for your clan",
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        dialog_y += 20;

        surface_draw_string_centre(mud->surface, formatted_current,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        char *clan_name = mud->input_text_final;

        if (clan_name[0] != '\0') {
            // step 1 of 2: stash the name, then prompt for the tag
            snprintf(mud->orsc_clan_create_name,
                     sizeof(mud->orsc_clan_create_name), "%s", clan_name);

            memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
            memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH + 1);

            mud->show_dialog_social_input = SOCIAL_CLAN_CREATE_TAG;
        }
        break;
    }
    case SOCIAL_CLAN_CREATE_TAG: {
        surface_draw_string_centre(mud->surface, "Enter a short clan tag",
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        dialog_y += 20;

        surface_draw_string_centre(mud->surface, formatted_current,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        char *clan_tag = mud->input_text_final;

        if (clan_tag[0] != '\0') {
            mudclient_orsc_send_clan_action(mud, CLAN_OPTION_CREATE,
                                            mud->orsc_clan_create_name,
                                            clan_tag);

            memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
            memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH + 1);

            mud->show_dialog_social_input = 0;
        }
        break;
    }
    case SOCIAL_CLAN_INVITE: {
        surface_draw_string_centre(mud->surface,
                                   "Enter name to invite to your clan",
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        dialog_y += 20;

        surface_draw_string_centre(mud->surface, formatted_current,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        char *username = mud->input_text_final;

        if (username[0] != '\0') {
            mudclient_orsc_send_clan_action(mud, CLAN_OPTION_INVITE_PLAYER,
                                            username, NULL);

            memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
            memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH + 1);

            mud->show_dialog_social_input = 0;
        }
        break;
    }
    case SOCIAL_PARTY_INVITE: {
        surface_draw_string_centre(mud->surface,
                                   "Enter name to invite to your party",
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        dialog_y += 20;

        surface_draw_string_centre(mud->surface, formatted_current,
                                   mud->surface->width / 2, dialog_y,
                                   FONT_BOLD_14, WHITE);

        char *username = mud->input_text_final;

        if (username[0] != '\0') {
            // INVITE_PLAYER_OR_MAKE also creates the party when not in one
            mudclient_orsc_send_party_action(
                mud, PARTY_OPTION_INVITE_PLAYER_OR_MAKE, username);

            memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
            memset(mud->input_text_final, '\0', INPUT_TEXT_LENGTH + 1);

            mud->show_dialog_social_input = 0;
        }
        break;
    }
#endif
    }

    int text_colour = WHITE;

    if (mud->mouse_x > cancel_offset_x &&
        mud->mouse_x < cancel_offset_x + SOCIAL_CANCEL_SIZE &&
        mud->mouse_y > cancel_offset_y &&
        mud->mouse_y < cancel_offset_y + SOCIAL_CANCEL_SIZE) {
        text_colour = YELLOW;
    }

    surface_draw_string_centre(mud->surface, "Cancel", mud->surface->width / 2,
                               dialog_y + 23, FONT_BOLD_12, text_colour);
}

#ifndef REVISION_177
// always-on-screen party box: per member a name/level line, a 100x4 hp bar
// (hidden for ~500 frames after damage), skull and leader-crown icons, a Party button
void mudclient_draw_party_hud(mudclient *mud) {
    if ((!mud->protocol_custom && !MUD_SP_WIRE(mud)) || !mud->orsc_party_in ||
        mud->orsc_party_size <= 0) {
        return;
    }

    // touch dialogue options render in the same left band; step aside while a menu is up
    if (mudclient_is_touch(mud) && mud->show_option_menu) {
        return;
    }

    int x = (mud->surface->width - 175) / 20;

#if defined(__vita__) && defined(RENDER_GL)
    // vita touch layout owns the top-left and lower-left, so the party box
    // takes the free band between them (button at 128, rows from 150)
    int y = 150;
#else
    int y = mud->surface->height - 310;

    if (y < 40) {
        y = 40;
    }
#endif

    int text_x = x - 20;

    if (text_x < 2) {
        text_x = 2;
    }

    // the Party button above the rows (row 0's icons start at y - 6)
    int button_x = x + 20;
    int button_y = y - 22;

    surface_draw_box_alpha(mud->surface, button_x, button_y, 75, 15, 0x454545,
                           128);
    surface_draw_border(mud->surface, button_x, button_y, 75, 15, WHITE);
    surface_draw_string_centre(mud->surface, "Party", button_x + 37,
                               button_y + 11, FONT_REGULAR_11, WHITE);

    if (mud->mouse_button_click == 1 && mud->mouse_x >= button_x &&
        mud->mouse_x <= button_x + 75 && mud->mouse_y >= button_y &&
        mud->mouse_y <= button_y + 15) {
        mud->show_ui_tab = SOCIAL_TAB;
        // Clan precedes Party whenever the Clan tab is shown
        mud->ui_tab_social_sub_tab =
            2 + (((mud->protocol_custom && mud->orsc.want_clans) ||
                  MUD_SP_WIRE(mud))
                     ? 1
                     : 0);
        mud->mouse_button_click = 0;
    }

    for (int i = 0; i < mud->orsc_party_size; i++) {
        int base_y = y + i * 20;

        if (mud->orsc_party_member_flash[i] > 0) {
            mud->orsc_party_member_flash[i]--;
        }

        surface_draw_stringf(mud->surface, text_x, base_y + 6, FONT_REGULAR_11,
                             WHITE, "@yel@%s@whi@-%d",
                             mud->orsc_party_member_names[i],
                             mud->orsc_party_member_combat[i]);

        if (mud->orsc_party_member_skull[i] > 0) {
            surface_draw_sprite_scale(mud->surface, x + 58, base_y - 6, 14, 14,
                                      mud->sprite_media + 13, 0);
        }

        if (mud->orsc_party_member_rank[i] == 1) {
            // leader crown, packed as index+1 in the high byte
            mudclient_draw_crown(mud, x + 71, base_y - 6, 2 << 24);
        }

        int max_hits = mud->orsc_party_member_max_hits[i];

        surface_draw_box(mud->surface, text_x, base_y + 8, 100, 4, 0xFF0000);

        if (max_hits > 0 && mud->orsc_party_member_flash[i] < 1) {
            int missing = max_hits - mud->orsc_party_member_cur_hits[i];
            int missing_width =
                (int)((missing * 100.0 / max_hits) + 0.5);

            if (missing_width < 0) {
                missing_width = 0;
            } else if (missing_width > 100) {
                missing_width = 100;
            }

            surface_draw_box(mud->surface, text_x, base_y + 8,
                             100 - missing_width, 4, 0x00FF00);
        }
    }
}
#endif
