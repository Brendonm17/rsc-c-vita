#include "welcome.h"

void mudclient_draw_welcome(mudclient *mud) {
    int is_compact = mud->surface->width < 400;
    int width = (is_compact ? MUD_MIN_WIDTH : 400);
    int height = WELCOME_HEIGHT;

    if (mud->welcome_last_ip != 0) {
        height += 15 * 3;
    }

    // the recovery-questions warning block adds five lines
    if (mud->welcome_recovery_set_days > 0) {
        height += 15 * 6;
    }

    int dialog_x = (mud->surface->width / 2) - (width / 2);
    int dialog_y = (mud->surface->height / 2) - (height / 2);

    surface_draw_box(mud->surface, dialog_x, dialog_y, width, height, BLACK);
    surface_draw_border(mud->surface, dialog_x, dialog_y, width, height, WHITE);

    int y = dialog_y + 20;
    int x = mud->surface->width / 2;

    // the title carries the world's pushed display name on custom worlds
    const char *welcome_server_name =
        (mud->protocol_custom && mud->orsc.server_name[0] != '\0')
            ? mud->orsc.server_name
            : "RuneScape";

    surface_draw_stringf_centre(mud->surface, x, y, FONT_BOLD_14, YELLOW,
                                "Welcome to %s %s", welcome_server_name,
                                mud->login_username);

    y += 30;

    char days_ago[21] = {0};

    if (mud->welcome_days_ago <= 0) {
        strcpy(days_ago, "earlier today");
    } else if (mud->welcome_days_ago == 1) {
        strcpy(days_ago, "yesterday");
    } else {
        sprintf(days_ago, "%d days ago", mud->welcome_days_ago);
    }

    if (mud->welcome_last_ip != 0) {
        surface_draw_stringf_centre(mud->surface, x, y, FONT_BOLD_12, WHITE,
                                    "You last logged in %s", days_ago);

        y += 15;

        if (mud->welcome_last_ip_string == NULL) {
            mud->welcome_last_ip_string = calloc(45, sizeof(char));
            ip_to_string(mud->welcome_last_ip, mud->welcome_last_ip_string);
        }

        surface_draw_stringf_centre(mud->surface, x, y, FONT_BOLD_12, WHITE,
                                    "from: %s", mud->welcome_last_ip_string);

        y += 15 * 2;
    }

    // the recovery-questions nag: a pending recovery change is the classic account-theft tell; cancelling sends
    // packet 196. the days value counts DOWN from 14
    if (mud->welcome_recovery_set_days > 0) {
        char requested_when[24];

        if (mud->welcome_recovery_set_days == 14) {
            strcpy(requested_when, "Earlier today");
        } else if (mud->welcome_recovery_set_days == 13) {
            strcpy(requested_when, "Yesterday");
        } else {
            sprintf(requested_when, "%d days ago",
                    14 - mud->welcome_recovery_set_days);
        }

        surface_draw_stringf_centre(mud->surface, x, y, FONT_BOLD_12, ORANGE,
                                    "%s you requested new recovery questions",
                                    requested_when);
        y += 15;
        surface_draw_string_centre(
            mud->surface, "If you do not remember making this request then", x,
            y, FONT_BOLD_12, ORANGE);
        y += 15;
        surface_draw_string_centre(
            mud->surface, "cancel it and change your password immediately!", x,
            y, FONT_BOLD_12, ORANGE);
        y += 15 * 2;

        int cancel_colour = WHITE;

        if (mud->mouse_y > y - 12 && mud->mouse_y <= y &&
            mud->mouse_x > dialog_x + 20 &&
            mud->mouse_x < dialog_x + width - 20) {
            cancel_colour = RED;
        }

        surface_draw_string_centre(mud->surface,
                                   "No that wasn't me - Cancel the request!",
                                   x, y, FONT_BOLD_12, cancel_colour);

        if (cancel_colour == RED && mud->mouse_button_click == 1) {
            packet_stream_new_packet(mud->packet_stream,
                                     CLIENT_RECOVER_CANCEL);
            packet_stream_send_packet(mud->packet_stream);
            mud->show_dialog_welcome = 0;
        }

        y += 15;

        int keep_colour = WHITE;

        if (mud->mouse_y > y - 12 && mud->mouse_y <= y &&
            mud->mouse_x > dialog_x + 20 &&
            mud->mouse_x < dialog_x + width - 20) {
            keep_colour = RED;
        }

        surface_draw_stringf_centre(
            mud->surface, x, y, FONT_BOLD_12, keep_colour,
            "That's ok, activate the new questions in %d days time",
            mud->welcome_recovery_set_days);

        if (keep_colour == RED && mud->mouse_button_click == 1) {
            mud->show_dialog_welcome = 0;
        }

        y += 15;
    }

    int text_colour = WHITE;

    if (mud->mouse_y > y - 12 && mud->mouse_y <= y &&
        mud->mouse_x > dialog_x + 50 && mud->mouse_x < dialog_x + width - 50) {
        text_colour = RED;
    }

    surface_draw_stringf_centre(mud->surface, x, y, FONT_BOLD_12, text_colour,
                                "%s here to close window",
                                mudclient_is_touch(mud) ? "Tap" : "Click");

    if (mud->mouse_button_click == 1) {
        if (text_colour == RED) {
            mud->show_dialog_welcome = 0;
        }

        if ((mud->mouse_x < dialog_x + 30 ||
             mud->mouse_x > dialog_x + width - 30) &&
            (mud->mouse_y < dialog_y - (height / 2) ||
             mud->mouse_y > dialog_y + (height / 2))) {
            mud->show_dialog_welcome = 0;
        }
    }

    mud->mouse_button_click = 0;
}
