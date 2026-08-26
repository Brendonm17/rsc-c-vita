#include "online-list.h"

#ifndef REVISION_177

#include "../colours.h"
#include "crowns.h"


#define ONLINE_PANEL_WIDTH 408
#define ONLINE_PANEL_HEIGHT 246
#define ONLINE_TITLE_HEIGHT 20
#define ONLINE_CONTAINER_X 1
#define ONLINE_CONTAINER_Y 21
#define ONLINE_CONTAINER_WIDTH (ONLINE_PANEL_WIDTH - 3)
#define ONLINE_TITLE_COLOUR 0x2f329f
#define ONLINE_TITLE_BORDER 0x7e8d09
#define ONLINE_CROWN_BUDGET 15

#define ONLINE_MENU_MIN_WIDTH 65
#define ONLINE_MENU_HOVER_COLOUR 0x454545

static const char *online_menu_options[] = {"Add friend", "Add ignore",
                                            "Invite to Party"};

#define ONLINE_MENU_OPTION_COUNT                                               \
    ((int)(sizeof(online_menu_options) / sizeof(*online_menu_options)))

static int online_menu_width(void) {
    int width = ONLINE_MENU_MIN_WIDTH;

    for (int i = 0; i < ONLINE_MENU_OPTION_COUNT; i++) {
        int w = surface_text_width((char *)online_menu_options[i],
                                   FONT_REGULAR_11);

        if (w > width) {
            width = w;
        }
    }

    return width;
}

static int online_menu_row_height(void) {
    return surface_text_height(FONT_REGULAR_11) + 3;
}

static int online_panel_x(mudclient *mud) {
    return (mud->surface->width - ONLINE_PANEL_WIDTH) / 2;
}

static int online_panel_y(mudclient *mud) {
    return (mud->surface->height - ONLINE_PANEL_HEIGHT) / 2;
}

// entry text: "user" or "user (location)", comma-separated
static void online_entry_text(mudclient *mud, int index, char *out,
                              int out_size) {
    int last = index == (mud->orsc_online_count - 1);

    if (mud->orsc_online_location[index][0] != '\0') {
        snprintf(out, out_size, "%s (%s)%s", mud->orsc_online_name[index],
                 mud->orsc_online_location[index], last ? "" : ", ");
    } else {
        snprintf(out, out_size, "%s%s", mud->orsc_online_name[index],
                 last ? "" : ", ");
    }
}

// lays out entries; negative x/y only computes layout, else returns hit entry or -1
static int online_list_layout(mudclient *mud, int hit_x, int hit_y) {
    int x = online_panel_x(mud);
    int y = online_panel_y(mud);
    int container_x = x + ONLINE_CONTAINER_X;
    int container_y = y + ONLINE_CONTAINER_Y;
    int font_height = surface_text_height(FONT_BOLD_12);

    int current_x = 5;
    int current_y = 0;

    int start = mud->online_list_scroll;
    int end = start + (ORSC_ONLINE_LIST_WINDOW - 1);

    for (int i = start; i < mud->orsc_online_count && i <= end; i++) {
        char text[MAX_USER_LENGTH + ORSC_ONLINE_LOCATION_MAX + 8] = {0};
        online_entry_text(mud, i, text, sizeof(text));

        int text_width = surface_text_width(text, FONT_BOLD_12) +
                         (mud->orsc_online_crown[i] > 0 ? ONLINE_CROWN_BUDGET
                                                        : 0) +
                         5;

        if (current_x + text_width >= ONLINE_CONTAINER_WIDTH) {
            current_x = 5;
            current_y += font_height;
        }

        int baseline = container_y + current_y + 3 + font_height;

        if (baseline > y + ONLINE_PANEL_HEIGHT) {
            break;
        }

        if (hit_x >= 0 && hit_x >= container_x + current_x &&
            hit_x < container_x + current_x + text_width &&
            hit_y > baseline - font_height && hit_y <= baseline) {
            return i;
        }

        current_x += text_width;
    }

    return -1;
}

int mudclient_online_list_entry_at(mudclient *mud, int x, int y) {
    return online_list_layout(mud, x, y);
}

void mudclient_draw_online_list(mudclient *mud) {
    int x = online_panel_x(mud);
    int y = online_panel_y(mud);

    surface_draw_box_alpha(mud->surface, x, y, ONLINE_PANEL_WIDTH,
                           ONLINE_PANEL_HEIGHT, GREY_98, 128);

    surface_draw_box_alpha(mud->surface, x, y, ONLINE_PANEL_WIDTH,
                           ONLINE_TITLE_HEIGHT, ONLINE_TITLE_COLOUR, 192);

    surface_draw_border(mud->surface, x, y, ONLINE_PANEL_WIDTH,
                        ONLINE_TITLE_HEIGHT, ONLINE_TITLE_BORDER);

    int font_height = surface_text_height(FONT_BOLD_12);

    char title[48] = {0};
    snprintf(title, sizeof(title), "Online Players: %d", mud->orsc_online_count);

    surface_draw_string(mud->surface, title, x + 2, y + 1 + font_height,
                        FONT_BOLD_12, WHITE);

    int close_hot = mud->mouse_x >= x + 326 && mud->mouse_x < x + 326 + 81 &&
                    mud->mouse_y >= y + 1 &&
                    mud->mouse_y < y + 1 + ONLINE_TITLE_HEIGHT;

    surface_draw_string(mud->surface, "Close window", x + 326,
                        y + 1 + font_height, FONT_BOLD_12,
                        close_hot ? RED : WHITE);

    int container_x = x + ONLINE_CONTAINER_X;
    int container_y = y + ONLINE_CONTAINER_Y;

    int current_x = 5;
    int current_y = 0;

    // OpenRSC update(): listEndPoint = startComponentIndex + 49
    int start = mud->online_list_scroll;
    int end = start + (ORSC_ONLINE_LIST_WINDOW - 1);

    for (int i = start; i < mud->orsc_online_count && i <= end; i++) {
        char text[MAX_USER_LENGTH + ORSC_ONLINE_LOCATION_MAX + 8] = {0};
        online_entry_text(mud, i, text, sizeof(text));

        int crown = mud->orsc_online_crown[i];

        int text_width = surface_text_width(text, FONT_BOLD_12) +
                         (crown > 0 ? ONLINE_CROWN_BUDGET : 0) + 5;

        if (current_x + text_width >= ONLINE_CONTAINER_WIDTH) {
            current_x = 5;
            current_y += font_height;
        }

        // stops before overflowing the panel's bottom edge
        if (container_y + current_y + 3 + font_height >
            y + ONLINE_PANEL_HEIGHT) {
            break;
        }

        int draw_x = container_x + current_x;
        int baseline = container_y + current_y + 3 + font_height;

        int advance = mudclient_draw_crown(mud, draw_x, baseline, crown);

        int hot = mud->online_list_menu_entry < 0 && mud->mouse_x >= draw_x &&
                  mud->mouse_x < draw_x + text_width &&
                  mud->mouse_y > baseline - font_height &&
                  mud->mouse_y <= baseline;

        surface_draw_string(mud->surface, text, draw_x + advance, baseline,
                            FONT_BOLD_12, hot ? RED : WHITE);

        current_x += text_width;
    }

    // open right-click menu, drawn last so it's on top
    if (mud->online_list_menu_entry >= 0) {
        int menu_width = online_menu_width();
        int row_height = online_menu_row_height();
        int menu_x = mud->online_list_menu_x;
        int menu_y = mud->online_list_menu_y;

        surface_draw_box_alpha(mud->surface, menu_x, menu_y, menu_width,
                              row_height * ONLINE_MENU_OPTION_COUNT, BLACK, 192);

        for (int i = 0; i < ONLINE_MENU_OPTION_COUNT; i++) {
            int row_y = menu_y + i * row_height;

            int row_hot = mud->mouse_x >= menu_x &&
                          mud->mouse_x < menu_x + menu_width &&
                          mud->mouse_y >= row_y &&
                          mud->mouse_y < row_y + row_height;

            if (row_hot) {
                surface_draw_box_alpha(mud->surface, menu_x, row_y, menu_width,
                                       row_height - 1, ONLINE_MENU_HOVER_COLOUR,
                                       192);
            }

            surface_draw_string_centre(
                mud->surface, (char *)online_menu_options[i],
                menu_x + (menu_width / 2), row_y + row_height - 3,
                FONT_REGULAR_11, WHITE);
        }
    }
}

void mudclient_handle_online_list_input(mudclient *mud) {
    int x = online_panel_x(mud);
    int y = online_panel_y(mud);

    // scroll window of 50 entries, stepped by page
    if (mud->mouse_scroll_delta != 0 && mud->mouse_x >= x &&
        mud->mouse_x < x + ONLINE_PANEL_WIDTH && mud->mouse_y >= y &&
        mud->mouse_y < y + ONLINE_PANEL_HEIGHT) {
        mud->online_list_scroll += mud->mouse_scroll_delta * ORSC_ONLINE_LIST_WINDOW;

        int max_scroll = mud->orsc_online_count - ORSC_ONLINE_LIST_WINDOW;

        if (max_scroll < 0) {
            max_scroll = 0;
        }

        if (mud->online_list_scroll > max_scroll) {
            mud->online_list_scroll = max_scroll;
        }

        if (mud->online_list_scroll < 0) {
            mud->online_list_scroll = 0;
        }

        mud->online_list_menu_entry = -1;
        mud->mouse_scroll_delta = 0;
    }

    if (mud->mouse_button_click == 0) {
        return;
    }

    // open menu swallows the click: option or dismiss
    if (mud->online_list_menu_entry >= 0) {
        int menu_width = online_menu_width();
        int row_height = online_menu_row_height();
        int menu_x = mud->online_list_menu_x;
        int menu_y = mud->online_list_menu_y;

        int entry = mud->online_list_menu_entry;

        mud->online_list_menu_entry = -1;
        mud->mouse_button_click = 0;

        if (mud->mouse_x < menu_x || mud->mouse_x >= menu_x + menu_width ||
            mud->mouse_y < menu_y ||
            mud->mouse_y >= menu_y + row_height * ONLINE_MENU_OPTION_COUNT) {
            return; // clicked away: OpenRSC onMouseMove just hides it
        }

        int option = (mud->mouse_y - menu_y) / row_height;

        if (entry < 0 || entry >= mud->orsc_online_count) {
            return;
        }

        // extracts username: strips ", (location)" and spaces to underscores
        char username[MAX_USER_LENGTH + 1] = {0};
        strcpy(username, mud->orsc_online_name[entry]);

        for (char *c = username; *c != '\0'; c++) {
            if (*c == ' ') {
                *c = '_';
            }
        }

        switch (option) {
        case 0:
            mudclient_add_friend(mud, username);
            break;
        case 1:
            mudclient_add_ignore(mud, username);
            break;
        case 2:
            mudclient_orsc_send_party_action(
                mud, PARTY_OPTION_INVITE_PLAYER_OR_MAKE, username);
            break;
        default:
            break;
        }

        return;
    }

    if (mud->mouse_x >= x + 326 && mud->mouse_x < x + 326 + 81 &&
        mud->mouse_y >= y + 1 && mud->mouse_y < y + 1 + ONLINE_TITLE_HEIGHT) {
        mud->show_dialog_online_list = 0;
        mud->mouse_button_click = 0;
        return;
    }

    // clicking an entry opens its menu (no right-click on touch)
    int entry = mudclient_online_list_entry_at(mud, mud->mouse_x, mud->mouse_y);

    if (entry >= 0) {
        mud->online_list_menu_entry = entry;
        mud->online_list_menu_x = mud->mouse_x;
        mud->online_list_menu_y = mud->mouse_y;
        mud->mouse_button_click = 0;
        return;
    }

    // click outside the panel closes it
    if (mud->mouse_x < x || mud->mouse_x >= x + ONLINE_PANEL_WIDTH ||
        mud->mouse_y < y || mud->mouse_y >= y + ONLINE_PANEL_HEIGHT) {
        mud->show_dialog_online_list = 0;
    }

    mud->mouse_button_click = 0;
}

#endif
