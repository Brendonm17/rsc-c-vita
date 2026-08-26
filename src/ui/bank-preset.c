#include "bank-preset.h"

#ifndef REVISION_177

#include "../colours.h"
#include "bank.h"


#define PRESET_PANEL_WIDTH 509
#define PRESET_PANEL_HEIGHT 331
#define PRESET_INV_COLUMNS 5
#define PRESET_INV_ROWS (INVENTORY_ITEMS_MAX / PRESET_INV_COLUMNS)
#define PRESET_CELL_WIDTH 49
#define PRESET_CELL_HEIGHT 34
#define PRESET_SELECTED_COLOUR 0x7E1F1C

// equipment slot positions within the panel, relative to y+21
static const int preset_equip_icon_x[11] = {98,  98, 98, 153, 43,  43,
                                            98,  98, 43, 153, 153};
static const int preset_equip_icon_y[11] = {5,   85, 125, 85, 85, 165,
                                            165, 45, 45,  45, 165};

static int preset_panel_x(mudclient *mud) {
    return (mud->surface->width - PRESET_PANEL_WIDTH) / 2;
}

static int preset_panel_y(mudclient *mud) {
    // OpenRSC onRender(): (gameHeight - height) / 2 - 3
    return ((mud->surface->height - PRESET_PANEL_HEIGHT) / 2) - 3;
}

void mudclient_draw_bank_preset(mudclient *mud) {
    int x = preset_panel_x(mud);
    int y = preset_panel_y(mud);

    int slot = mud->bank_preset_selected_slot;

    if (slot < 0 || slot >= ORSC_PRESET_COUNT) {
        slot = 0;
    }

    int inv_x = x + PRESET_PANEL_WIDTH - PRESET_INV_COLUMNS * PRESET_CELL_WIDTH -
                2;
    int inv_y = y + 21;
    int rows_height = PRESET_INV_ROWS * PRESET_CELL_HEIGHT;
    int button_width = PRESET_PANEL_WIDTH / ORSC_PRESET_COUNT;

    surface_draw_box(mud->surface, x, y, PRESET_PANEL_WIDTH, 21,
                     TITLE_BAR_COLOUR);

    surface_draw_box_alpha(mud->surface, x, y + 21, PRESET_PANEL_WIDTH, 309,
                           GREY_98, 160);

    surface_draw_border(mud->surface, x, y, PRESET_PANEL_WIDTH,
                        PRESET_PANEL_HEIGHT, BLACK);

    surface_draw_string(mud->surface, "Assign Presets", x + 208, y + 15,
                        FONT_BOLD_12, WHITE);

    int close_width = surface_text_width("Close Window", FONT_BOLD_12);

    int close_hot = mud->mouse_x >= x + 420 &&
                    mud->mouse_x <= x + 420 + close_width &&
                    mud->mouse_y >= y && mud->mouse_y < y + 15;

    surface_draw_string(mud->surface, "Close Window", x + 420, y + 15,
                        FONT_BOLD_12, close_hot ? RED : WHITE);

    for (int i = 0; i < PRESET_INV_COLUMNS; i++) {
        surface_draw_line_vertical(mud->surface, inv_x + i * PRESET_CELL_WIDTH,
                                   inv_y, rows_height, BLACK);
    }

    for (int i = 0; i < PRESET_INV_ROWS + 1; i++) {
        surface_draw_line_horizontal(mud->surface, inv_x,
                                     inv_y + i * PRESET_CELL_HEIGHT,
                                     PRESET_INV_COLUMNS * PRESET_CELL_WIDTH,
                                     BLACK);
    }

    int col = 0;
    int row = 0;

    for (int i = 0; i < INVENTORY_ITEMS_MAX; i++) {
        int slot_x = inv_x + col * PRESET_CELL_WIDTH + 1;
        int slot_y = inv_y + row * PRESET_CELL_HEIGHT + 1;

        surface_draw_box_alpha(mud->surface, slot_x, slot_y, 48, 33, GREY_B5,
                               128);

        int item_id = mud->orsc_preset_inventory_id[slot][i];

        if (item_id >= 0 && item_id < game_data.item_count) {
            int noted = mud->orsc_preset_inventory_noted[slot][i];

            if (noted) {
                // notes draw the certificate sprite; icon inset +7
                mudclient_draw_noted_item(mud, slot_x, slot_y,
                                          PRESET_CELL_WIDTH,
                                          PRESET_CELL_HEIGHT, item_id, 7);
            } else {
                mudclient_draw_item(mud, slot_x, slot_y, PRESET_CELL_WIDTH,
                                    PRESET_CELL_HEIGHT, item_id);
            }

            if (game_data_item_stacks(item_id, noted)) {
                char amount[16] = {0};

                mudclient_format_item_amount(
                    mud, mud->orsc_preset_inventory_amount[slot][i], amount);

                surface_draw_string(
                    mud->surface, amount, slot_x,
                    inv_y + row * PRESET_CELL_HEIGHT - 3 +
                        surface_text_height(FONT_BOLD_12),
                    FONT_BOLD_12, YELLOW);
            }
        }

        col++;

        if (col >= PRESET_INV_COLUMNS) {
            col = 0;
            row++;
        }
    }

    // empty slot placeholder alpha 128, filled highlight alpha 192
    for (int i = 0; i < 11; i++) {
        int slot_x = x + preset_equip_icon_x[i];
        int slot_y = inv_y + preset_equip_icon_y[i];

        int item_id = mud->orsc_preset_equipment_id[slot][i];

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        if (item_id < 0 || item_id >= game_data.item_count) {
            surface_draw_sprite_alpha(
                mud->surface, slot_x, slot_y,
                mud->sprite_item + GL_CUSTOM_EQUIPSLOT_BASE + i, 128);
            continue;
        }

        surface_draw_sprite_alpha(mud->surface, slot_x, slot_y,
                                  mud->sprite_item + GL_CUSTOM_EQUIPSLOT_BASE +
                                      GL_CUSTOM_EQUIPSLOT_HIGHLIGHT,
                                  192);
#else
        if (item_id < 0 || item_id >= game_data.item_count) {
            continue;
        }
#endif

        mudclient_draw_item(mud, slot_x, slot_y, PRESET_CELL_WIDTH,
                            PRESET_CELL_HEIGHT, item_id);

        if (game_data_item_stacks(item_id, 0)) {
            char amount[16] = {0};

            mudclient_format_item_amount(
                mud, mud->orsc_preset_equipment_amount[slot][i], amount);

            surface_draw_string(mud->surface, amount, slot_x + 2, slot_y + 11,
                                FONT_BOLD_12, YELLOW);
        }
    }

    // preset slot buttons
    for (int p = 0; p < ORSC_PRESET_COUNT; p++) {
        surface_draw_box_alpha(mud->surface, x + p * button_width,
                               inv_y + rows_height + 1, button_width, 33,
                               slot == p ? PRESET_SELECTED_COLOUR : GREY_98,
                               160);

        surface_draw_border(mud->surface, x + p * button_width,
                            inv_y + rows_height, button_width, 34, BLACK);

        char label[16] = {0};
        snprintf(label, sizeof(label), "Preset Slot %d", p + 1);

        surface_draw_string(mud->surface, label,
                            x + button_width / 2 + button_width * p -
                                (surface_text_width(label, FONT_BOLD_12) / 2),
                            inv_y + rows_height + 21, FONT_BOLD_12, WHITE);
    }

    surface_draw_string(mud->surface,
                        "Click here to save your inventory and equipment to the "
                        "currently selected preset slot",
                        x + 10, inv_y + rows_height + 75, FONT_BOLD_12, BLACK);

    if (!mud->orsc_preset_known[slot]) {
        surface_draw_string_centre(mud->surface, "@yel@(preset not loaded yet)",
                                   x + PRESET_PANEL_WIDTH / 2,
                                   inv_y + rows_height + 60, FONT_BOLD_12,
                                   YELLOW);
    }
}

void mudclient_handle_bank_preset_input(mudclient *mud) {
    if (mud->mouse_button_click == 0) {
        return;
    }

    int x = preset_panel_x(mud);
    int y = preset_panel_y(mud);
    int inv_y = y + 21;
    int rows_height = PRESET_INV_ROWS * PRESET_CELL_HEIGHT;
    int button_width = PRESET_PANEL_WIDTH / ORSC_PRESET_COUNT;

    int close_width = surface_text_width("Close Window", FONT_BOLD_12);

    // click priority: outside panel, close button, preset row, save
    if (mud->mouse_x > x + PRESET_PANEL_WIDTH || mud->mouse_x < x ||
        mud->mouse_y > y + PRESET_PANEL_HEIGHT || mud->mouse_y < y) {
        mud->show_dialog_bank_preset = 0;
    } else if (mud->mouse_x >= x + 420 &&
               mud->mouse_x <= x + 420 + close_width && mud->mouse_y >= y &&
               mud->mouse_y < y + 15) {
        mud->show_dialog_bank_preset = 0;
    } else if (mud->mouse_y >= inv_y + rows_height + 1 &&
               mud->mouse_y < inv_y + rows_height + 35) {
        int slot = (mud->mouse_x - x) / button_width;

        if (slot >= 0 && slot < ORSC_PRESET_COUNT) {
            mud->bank_preset_selected_slot = slot;
        }
    } else if (mud->mouse_y >= inv_y + rows_height + 35) {
        // sends opcode 27 + u16 slot to save the preset
        packet_stream_new_packet(mud->packet_stream, CLIENT_BANK_SAVE_PRESET);
        packet_stream_put_short(mud->packet_stream,
                                mud->bank_preset_selected_slot);
        packet_stream_send_packet(mud->packet_stream);
    }

    mud->mouse_button_click = 0;
}

#endif
