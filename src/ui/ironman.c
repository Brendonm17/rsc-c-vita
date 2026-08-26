#include "ironman.h"


static const char *IRONMAN_TITLES[4] = {"Standard Ironman", "Hardcore Ironman",
                                        "Ultimate Ironman", "None"};
static const int IRONMAN_SELECT_MODE[4] = {1, 3, 2, 0};
static const int IRONMAN_CHECKED_BOX[4] = {3, 0, 2, 1};

// description text for the hovered row
static const char *IRONMAN_DESCRIPTIONS[4] = {
    "An Ironman cannot trade, stake, receive PK loot, scavenge dropped items, "
    "nor play certain multiplayer minigames.",
    "In addition to the standard Ironman rules, a Hardcore Ironman only has 1 "
    "life. A dangerous death will result in being downgraded to a standard "
    "Ironman.",
    "In addition to the standard Ironman rules, an Ultimate Ironman cannot "
    "use banks, nor retain any items on death in dangerous areas.",
    "- No Ironman restrictions will apply to this account."};

static const char *IRONMAN_RESTRICTIONS[2] = {"PIN", "Permanent"};

static const char *IRONMAN_RESTRICTION_DESCRIPTIONS[2] = {
    "You must enter your Bank Pin to request that Ironman restrictions be "
    "removed.",
    "- The Ironman restrictions can never be removed."};

static void ironman_send(mudclient *mud, int which, int value) {
    packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
    packet_stream_put_byte(mud->packet_stream, INTERFACE_OPTION_IRONMAN);
    packet_stream_put_byte(mud->packet_stream, which);
    packet_stream_put_byte(mud->packet_stream, value);
    packet_stream_send_packet(mud->packet_stream);
}

void mudclient_draw_ironman_interface(mudclient *mud) {
    int dialog_x = mud->surface->width / 2 - IRONMAN_DIALOG_WIDTH / 2;
    int dialog_y = mud->surface->height / 2 - IRONMAN_DIALOG_HEIGHT / 2;

    surface_draw_box(mud->surface, dialog_x, dialog_y, IRONMAN_DIALOG_WIDTH,
                     IRONMAN_DIALOG_HEIGHT, BLACK);

    surface_draw_border(mud->surface, dialog_x, dialog_y,
                        IRONMAN_DIALOG_WIDTH, IRONMAN_DIALOG_HEIGHT, WHITE);

    int centre_x = mud->surface->width / 2;

    surface_draw_string_centre(mud->surface, "Ironman Mode", centre_x,
                               dialog_y + 16, FONT_BOLD_14, YELLOW);

    int clicked_which = -1;
    int clicked_value = 0;
    const char *hovered_description = NULL;
    int row_y = dialog_y + 26;

    for (int i = 0; i < 4; i++) {
        int hovered = mud->mouse_x >= dialog_x + 6 &&
                      mud->mouse_x < dialog_x + IRONMAN_DIALOG_WIDTH - 6 &&
                      mud->mouse_y >= row_y &&
                      mud->mouse_y < row_y + IRONMAN_ROW_HEIGHT;

        if (hovered) {
            hovered_description = IRONMAN_DESCRIPTIONS[i];
        }

        int selected = mud->orsc_ironman_type >= 0 &&
                       mud->orsc_ironman_type <= 3 &&
                       IRONMAN_CHECKED_BOX[mud->orsc_ironman_type] == i;

        surface_draw_box_alpha(mud->surface, dialog_x + 6, row_y,
                               IRONMAN_DIALOG_WIDTH - 12,
                               IRONMAN_ROW_HEIGHT - 2,
                               selected ? GREY_80 : GREY_98, 160);

        surface_draw_string(
            mud->surface, IRONMAN_TITLES[i], dialog_x + 14, row_y + 15,
            FONT_BOLD_12, hovered ? YELLOW : (selected ? WHITE : BLACK));

        if (selected) {
            surface_draw_string(mud->surface, "*",
                                dialog_x + IRONMAN_DIALOG_WIDTH - 22,
                                row_y + 16, FONT_BOLD_14, YELLOW);
        }

        if (hovered && mud->mouse_button_click != 0) {
            clicked_which = 0;
            clicked_value = IRONMAN_SELECT_MODE[i];
        }

        row_y += IRONMAN_ROW_HEIGHT;
    }

    surface_draw_string_centre(mud->surface, "Removal restriction", centre_x,
                               row_y + 12, FONT_BOLD_12, YELLOW);

    row_y += 18;

    for (int i = 0; i < 2; i++) {
        int box_width = (IRONMAN_DIALOG_WIDTH - 18) / 2;
        int box_x = dialog_x + 6 + i * (box_width + 6);

        int hovered = mud->mouse_x >= box_x &&
                      mud->mouse_x < box_x + box_width &&
                      mud->mouse_y >= row_y &&
                      mud->mouse_y < row_y + IRONMAN_ROW_HEIGHT;

        if (hovered) {
            hovered_description = IRONMAN_RESTRICTION_DESCRIPTIONS[i];
        }

        int selected = mud->orsc_ironman_restriction == i;

        surface_draw_box_alpha(mud->surface, box_x, row_y, box_width,
                               IRONMAN_ROW_HEIGHT - 2,
                               selected ? GREY_80 : GREY_98, 160);

        surface_draw_string_centre(
            mud->surface, IRONMAN_RESTRICTIONS[i], box_x + box_width / 2,
            row_y + 15, FONT_BOLD_12,
            hovered ? YELLOW : (selected ? WHITE : BLACK));

        if (hovered && mud->mouse_button_click != 0) {
            clicked_which = 1;
            clicked_value = i;
        }

        row_y += 0;
    }

    row_y += IRONMAN_ROW_HEIGHT;

    // hovered row's description text
    if (hovered_description != NULL) {
        surface_draw_paragraph(mud->surface, hovered_description,
                               dialog_x + 8, row_y + 10, FONT_REGULAR_11,
                               WHITE, IRONMAN_DIALOG_WIDTH - 16);
    }

    row_y += 54;

    // closes locally without ending the underlying dialogue
    int close_hovered =
        mud->mouse_x >= centre_x - 30 && mud->mouse_x < centre_x + 30 &&
        mud->mouse_y >= row_y && mud->mouse_y < row_y + 16;

    surface_draw_string_centre(mud->surface, "Close", centre_x, row_y + 12,
                               FONT_BOLD_12, close_hovered ? YELLOW : WHITE);

    if (mud->mouse_button_click != 0) {
        if (clicked_which >= 0) {
            ironman_send(mud, clicked_which, clicked_value);
        } else if (close_hovered) {
            mud->show_dialog_ironman = 0;
        }

        // modal: swallow in-dialog clicks either way
        if (clicked_which >= 0 || close_hovered ||
            (mud->mouse_x >= dialog_x &&
             mud->mouse_x < dialog_x + IRONMAN_DIALOG_WIDTH &&
             mud->mouse_y >= dialog_y &&
             mud->mouse_y < dialog_y + IRONMAN_DIALOG_HEIGHT)) {
            mud->mouse_button_click = 0;
        }
    }
}
