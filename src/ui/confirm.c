#include "confirm.h"

void mudclient_draw_confirm(mudclient *mud) {
    int dialog_x = mud->surface->width / 2 - CONFIRM_DIALOG_WIDTH / 2;
    int dialog_y = mud->surface->height / 2 - CONFIRM_DIALOG_HEIGHT / 2;

    surface_draw_box(mud->surface, dialog_x, dialog_y, CONFIRM_DIALOG_WIDTH,
                     CONFIRM_DIALOG_HEIGHT, BLACK);

    surface_draw_border(mud->surface, dialog_x, dialog_y, CONFIRM_DIALOG_WIDTH,
                        CONFIRM_DIALOG_HEIGHT, WHITE);

    int x = mud->surface->width / 2;
    int y = dialog_y + 20;

    surface_draw_string_centre(mud->surface, mud->confirm_text_top, x, y, 4,
                               WHITE);

    y += 20;

    surface_draw_string_centre(mud->surface, mud->confirm_text_bottom, x, y, 4,
                               WHITE);

    int cancel_x = dialog_x + (CONFIRM_DIALOG_WIDTH / 2 - CONFIRM_BUTTON_SIZE);
    int ok_x = cancel_x + (CONFIRM_BUTTON_SIZE * 2);
    int button_y = dialog_y + CONFIRM_DIALOG_HEIGHT - 15;

    int text_colour = WHITE;

    if (mud->mouse_x >= cancel_x - (CONFIRM_BUTTON_SIZE / 2) &&
        mud->mouse_x <= cancel_x + (CONFIRM_BUTTON_SIZE / 2) &&
        mud->mouse_y >= button_y - (CONFIRM_BUTTON_SIZE / 2) &&
        mud->mouse_y <= button_y + (CONFIRM_BUTTON_SIZE / 2)) {
        text_colour = YELLOW;
    }

    surface_draw_string_centre(mud->surface, "Cancel", cancel_x, button_y, 1,
                               text_colour);

    text_colour = WHITE;

    if (mud->mouse_x >= ok_x - (CONFIRM_BUTTON_SIZE / 2) &&
        mud->mouse_x <= ok_x + (CONFIRM_BUTTON_SIZE / 2) &&
        mud->mouse_y >= button_y - (CONFIRM_BUTTON_SIZE / 2) &&
        mud->mouse_y <= button_y + (CONFIRM_BUTTON_SIZE / 2)) {
        text_colour = YELLOW;
    }

    surface_draw_string_centre(mud->surface, "OK", ok_x, button_y, 1,
                               text_colour);
}

void mudclient_handle_confirm_input(mudclient *mud) {
    if (mud->last_mouse_button_down == 0) {
        return;
    }

    int dialog_x = mud->surface->width / 2 - CONFIRM_DIALOG_WIDTH / 2;
    int dialog_y = mud->surface->height / 2 - CONFIRM_DIALOG_HEIGHT / 2;
    int cancel_x = dialog_x + (CONFIRM_DIALOG_WIDTH / 2 - CONFIRM_BUTTON_SIZE);
    int ok_x = cancel_x + (CONFIRM_BUTTON_SIZE * 2);
    int button_y = dialog_y + CONFIRM_DIALOG_HEIGHT - 15;

    if (mud->mouse_x < dialog_x ||
        mud->mouse_x > dialog_x + CONFIRM_DIALOG_WIDTH ||
        mud->mouse_y < dialog_y ||
        mud->mouse_y > dialog_y + CONFIRM_DIALOG_HEIGHT ||
        (mud->mouse_x >= cancel_x - (CONFIRM_BUTTON_SIZE / 2) &&
         mud->mouse_x <= cancel_x + (CONFIRM_BUTTON_SIZE / 2) &&
         mud->mouse_y >= button_y - (CONFIRM_BUTTON_SIZE / 2) &&
         mud->mouse_y <= button_y + (CONFIRM_BUTTON_SIZE / 2))) {
#ifndef REVISION_177
        if (mud->confirm_type == CONFIRM_CLAN_INVITE) {
            mudclient_orsc_send_clan_action(mud, CLAN_OPTION_DECLINE_INVITE,
                                            NULL, NULL);
        } else if (mud->confirm_type == CONFIRM_PARTY_INVITE) {
            mudclient_orsc_send_party_action(mud, PARTY_OPTION_DECLINE_INVITE,
                                             NULL);
        }
#endif
        mud->show_dialog_confirm = 0;
    } else if (mud->mouse_x >= ok_x - (CONFIRM_BUTTON_SIZE / 2) &&
               mud->mouse_x <= ok_x + (CONFIRM_BUTTON_SIZE / 2) &&
               mud->mouse_y >= button_y - (CONFIRM_BUTTON_SIZE / 2) &&
               mud->mouse_y <= button_y + (CONFIRM_BUTTON_SIZE / 2)) {
        switch (mud->confirm_type) {
        case CONFIRM_TUTORIAL:
            mudclient_send_command_string(mud, "skiptutorial");
            break;
#ifndef REVISION_177
        case CONFIRM_CLAN_INVITE:
            mudclient_orsc_send_clan_action(mud, CLAN_OPTION_ACCEPT_INVITE,
                                            NULL, NULL);
            break;
        case CONFIRM_PARTY_INVITE:
            mudclient_orsc_send_party_action(mud, PARTY_OPTION_ACCEPT_INVITE,
                                             NULL);
            break;
        case CONFIRM_CLAN_LEADERSHIP:
            // 199 [11] [RANK_PLAYER=6] name rank-1 (Owner transfer)
            packet_stream_new_packet(mud->packet_stream,
                                     CLIENT_INTERFACE_OPTIONS);
            packet_stream_put_byte(mud->packet_stream, INTERFACE_OPTION_CLAN);
            packet_stream_put_byte(mud->packet_stream,
                                   CLAN_OPTION_RANK_PLAYER);
            packet_stream_put_string_newline(mud->packet_stream,
                                             mud->orsc_clan_pending_leader);
            packet_stream_put_byte(mud->packet_stream, 1);
            packet_stream_send_packet(mud->packet_stream);
            break;
#endif
        case CONFIRM_OPTIONS_DEFAULT:
            options_set_defaults(mud->options);
            mudclient_sync_options_panels(mud);
            break;
        case CONFIRM_OPTIONS_VANILLA:
            options_set_vanilla(mud->options);
            mudclient_sync_options_panels(mud);

#if !defined(WII) && !defined(_3DS)
#ifdef RENDER_SW
#ifndef SDL12
            SDL_RestoreWindow(mud->window);
            SDL_SetWindowSize(mud->window, MUD_WIDTH, MUD_HEIGHT);
#endif
#endif
#ifdef RENDER_GL
#ifndef SDL12
#if defined(__vita__)
            // panel is a fixed 960x544, do not shrink for vanilla options
#else
            SDL_RestoreWindow(mud->gl_window);
            SDL_SetWindowSize(mud->gl_window, MUD_WIDTH, MUD_HEIGHT);
#endif
#endif
#endif
#endif
#ifdef SDL12
            mudclient_sdl1_on_resize(mud, mud->game_width, mud->game_height);
#else
            mudclient_on_resize(mud);
#endif
            break;
        }

        mud->show_dialog_confirm = 0;
    }

    mud->last_mouse_button_down = 0;
}
