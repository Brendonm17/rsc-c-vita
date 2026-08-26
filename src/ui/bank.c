#include "bank.h"

static void mudclient_draw_bank_page(mudclient *mud, int x, int y, int page,
                                     int width);

#ifndef REVISION_177
// the 27 authentic certificate items, the only ids the Uncert+Deposit rows apply to
static const short bank_cert_item_ids[] = {
    517,  518,  519,  520,  521,          // ores
    528,  529,  530,  531,  532,          // bars
    533,  534,  535,  536,  628,  629, 630, 631, // fish
    711,  712,  713,                      // logs
    1270, 1271, 1272, 1273, 1274, 1275};  // misc

int mudclient_bank_item_is_cert(int item_id) {
    for (size_t i = 0;
         i < sizeof(bank_cert_item_ids) / sizeof(*bank_cert_item_ids); i++) {
        if (bank_cert_item_ids[i] == item_id) {
            return 1;
        }
    }

    return 0;
}

// sendCertMode: 199[0][mode], sent only when the mode changed
static void mudclient_bank_send_cert_mode(mudclient *mud, int mode) {
    if (mud->orsc_bank_cert_mode == mode) {
        return;
    }

    packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
    packet_stream_put_byte(mud->packet_stream, 0);
    packet_stream_put_byte(mud->packet_stream, mode ? 1 : 0);
    packet_stream_send_packet(mud->packet_stream);

    mud->orsc_bank_cert_mode = mode;
}
#endif

/* handle withdrawing or depositing */
void mudclient_bank_transaction(mudclient *mud, int item_id, int amount,
                                int opcode) {
    int is_withdraw = opcode == CLIENT_BANK_WITHDRAW;

    if (is_withdraw && game_data.items[item_id].stackable != 0 &&
        amount + mud->inventory_items_count >= INVENTORY_ITEMS_MAX) {
        amount = INVENTORY_ITEMS_MAX - mud->inventory_items_count;

        if (amount <= 0) {
            return;
        }
    }

#ifndef REVISION_177
    if (mud->protocol_custom) {
        // every deposit leads with the cert mode: uncert rows arm 1, plain deposits re-arm 0; withdraws never touch it
        if (!is_withdraw && mud->orsc.want_cert_deposit) {
            mudclient_bank_send_cert_mode(mud, mud->bank_offer_uncert);
        }

        // custom bank packet: u16 item id, i32 amount, u8 noted
        packet_stream_new_packet(mud->packet_stream, opcode);
        packet_stream_put_short(mud->packet_stream, item_id);
        packet_stream_put_int(mud->packet_stream, amount);

        if (is_withdraw && mud->orsc.want_bank_notes) {
            packet_stream_put_byte(mud->packet_stream,
                                   mud->bank_swap_note_mode ? 1 : 0);
        }

        packet_stream_send_packet(mud->packet_stream);
    } else
#endif
    {
        // no queueing if the split exceeds the packet buffer
        int total_packets = (int)ceil((float)amount / 32767.0f);

        for (int i = 0; i < total_packets; i++) {
            packet_stream_new_packet(mud->packet_stream, opcode);
            packet_stream_put_short(mud->packet_stream, item_id);

            int send_amount = amount;

            if (send_amount > 32767) {
                send_amount = 32767;
                amount -= 32767;
            }

            packet_stream_put_short(mud->packet_stream, send_amount);

#ifndef REVISION_177
            if (!mud->protocol177) { // magic ints absent in the 177 dialect
                packet_stream_put_int(mud->packet_stream,
                                      is_withdraw ? BANK_MAGIC_WITHDRAW
                                                  : BANK_MAGIC_DEPOSIT);
            }
#endif

            packet_stream_send_packet(mud->packet_stream);
        }
    }

    /* select the item if it isn't already */
    if (mud->bank_selected_item != item_id) {
        // memset(mud->input_pm_current, '\0', INPUT_PM_LENGTH + 1);
        memset(mud->input_pm_final, '\0', INPUT_PM_LENGTH + 1);

        for (int i = 0; i < mud->bank_item_count; i++) {
            int bank_item_id = mud->bank_items[i];

            if (bank_item_id == item_id) {
                mud->bank_selected_item_slot = i;
                mud->bank_selected_item = item_id;
                break;
            }
        }
    }

    // TODO don't like this - DRY
    int is_compact = mud->surface->width < MUD_VANILLA_WIDTH ||
                     mud->surface->height < MUD_VANILLA_HEIGHT;

    int visible_columns = BANK_COLUMNS;

    if (is_compact) {
        visible_columns = (mud->surface->width - 28) / ITEM_GRID_SLOT_WIDTH;

        if (visible_columns > BANK_COLUMNS) {
            visible_columns = BANK_COLUMNS;
        }
    }

    /* scroll or switch to selected item page */
    if (is_compact || mud->options->bank_scroll) {
        int item_row = mud->bank_selected_item_slot / visible_columns;

        if (item_row < mud->bank_scroll_row ||
            item_row > mud->bank_scroll_row + mud->bank_visible_rows) {
            mud->bank_scroll_row = item_row;
        }
    } else {
        int items_per_page = mud->bank_visible_rows * visible_columns;
        mud->bank_active_page = mud->bank_selected_item_slot / items_per_page;
    }
}

// deposit the entire inventory or worn equipment in one packet
void mudclient_bank_deposit_all(mudclient *mud, int opcode) {
    packet_stream_new_packet(mud->packet_stream, opcode);
    packet_stream_send_packet(mud->packet_stream);
}

// deposit-all requires custom banks; equipment also needs the tab
int mudclient_bank_can_deposit_all(mudclient *mud, int from_equipment) {
#ifdef WITH_SINGLEPLAYER
    if (mud->singleplayer) {
        return 1;
    }
#endif

#ifndef REVISION_177
    if (!mud->protocol_custom || !mud->orsc.want_custom_banks) {
        return 0;
    }

    if (from_equipment && !mud->orsc.want_equipment_tab) {
        return 0;
    }

    return 1;
#else
    (void)from_equipment;
    return 0;
#endif
}

/* draw the page numbers in the title bar */
static void mudclient_draw_bank_page(mudclient *mud, int x, int y, int page,
                                     int width) {
    int text_colour = WHITE;

    if (mud->bank_active_page == page - 1) {
        text_colour = RED;
    } else if (mud->mouse_x > x && mud->mouse_y >= y &&
               mud->mouse_x < x + width && mud->mouse_y < y + 12) {
        text_colour = YELLOW;
    }

    surface_draw_stringf(mud->surface, x, y + 10, FONT_BOLD_12, text_colour,
                         "<page %d>", page);
}

void mudclient_draw_bank_amounts(mudclient *mud, int amount, int last_x, int x,
                                 int y) {
    int text_colour = WHITE;
    int offset_x = 0;

    if (mud->mouse_x >= x && mud->mouse_y >= y && mud->mouse_x < x + 30 &&
        mud->mouse_y <= y + 11) {
        text_colour = RED;
    }

    surface_draw_string(mud->surface, "One", x + 2, y + 10, FONT_BOLD_12,
                        text_colour);

    offset_x += 30;

    if (amount >= 5) {
        text_colour = WHITE;

        if (mud->mouse_x >= x + offset_x && mud->mouse_y >= y &&
            mud->mouse_x < x + offset_x + 30 && mud->mouse_y <= y + 11) {
            text_colour = RED;
        }

        surface_draw_string(mud->surface, "Five", x + offset_x + 2, y + 10,
                            FONT_BOLD_12, text_colour);
    }

    offset_x += 30; // 60

    int ui_amount = mud->options->offer_x ? 10 : 25;

    if (amount >= ui_amount) {
        text_colour = WHITE;

        if (mud->mouse_x >= x + offset_x && mud->mouse_y >= y &&
            mud->mouse_x < x + offset_x + 25 && mud->mouse_y <= y + 11) {
            text_colour = RED;
        }

        surface_draw_stringf(mud->surface, x + offset_x + 2, y + 10,
                             FONT_BOLD_12, text_colour, "%d", ui_amount);
    }

    offset_x += 25; // 85

    int show_last_offer_x = mud->options->offer_x && mud->options->last_offer_x;

    ui_amount = mud->options->offer_x ? 50 : 100;

    if (amount >= ui_amount) {
        text_colour = WHITE;

        if (mud->mouse_x >= x + offset_x && mud->mouse_y >= y &&
            mud->mouse_x < x + offset_x + (show_last_offer_x ? 23 : 30) &&
            mud->mouse_y <= y + 11) {
            text_colour = RED;
        }

        surface_draw_stringf(mud->surface, x + offset_x + 2, y + 10,
                             FONT_BOLD_12, text_colour, "%d", ui_amount);
    }

    offset_x += 30 - (show_last_offer_x ? 7 : 0); // 115 or 108

    ui_amount = mud->options->offer_x ? 2 : 500;

    if (amount >= ui_amount) {
        text_colour = WHITE;

        if (mud->mouse_x >= x + offset_x && mud->mouse_y >= y &&
            mud->mouse_x < x + offset_x + (show_last_offer_x ? 16 : 33) &&
            mud->mouse_y <= y + 11) {
            text_colour = RED;
        }

        surface_draw_string(mud->surface, mud->options->offer_x ? "X" : "500",
                            x + offset_x + 2, y + 10, FONT_BOLD_12,
                            text_colour);
    }

    offset_x += 33 - (show_last_offer_x ? 17 : 0);

    if (show_last_offer_x && last_x != 0 && amount >= last_x) {
        text_colour = WHITE;

        if (mud->mouse_x >= x + offset_x && mud->mouse_y >= y &&
            mud->mouse_x < x + offset_x + 30 && mud->mouse_y <= y + 11) {
            text_colour = RED;
        }

        char formatted[255] = {0};
        format_amount_suffix(last_x, 0, 1, 0, formatted);

        surface_draw_string(mud->surface, formatted, x + offset_x + 2, y + 10,
                            FONT_BOLD_12, text_colour);
    }

    if (show_last_offer_x) {
        offset_x += 37;
    }

    ui_amount = mud->options->offer_x ? amount : 2500;

    if (amount >= ui_amount) {
        text_colour = WHITE;

        if (mud->mouse_x >= x + offset_x + 2 && mud->mouse_y >= y &&
            mud->mouse_x < x + offset_x + 32 && mud->mouse_y <= y + 11) {
            text_colour = RED;
        }

        surface_draw_string(
            mud->surface, mud->options->offer_x ? "All" : "2500",
            x + offset_x + 2, y + 10, FONT_BOLD_12, text_colour);
    }
}

void mudclient_handle_bank_amounts_input(mudclient *mud, int item_id,
                                         int amount, int last_x, int x, int y,
                                         int transaction_opcode) {
    // int item_id = mud->bank_items[mud->bank_selected_item_slot];
    int offset_x = 0;

    if (mud->mouse_x >= x && mud->mouse_y >= y && mud->mouse_x < x + 30 &&
        mud->mouse_y <= y + 11) {
        mudclient_bank_transaction(mud, item_id, 1, transaction_opcode);
    }

    offset_x += 30;

    if (amount >= 5 && mud->mouse_x >= x + offset_x && mud->mouse_y >= y &&
        mud->mouse_x < x + offset_x + 30 && mud->mouse_y <= y + 11) {
        mudclient_bank_transaction(mud, item_id, 5, transaction_opcode);
    }

    offset_x += 30; // 60

    int ui_amount = mud->options->offer_x ? 10 : 25;

    if (amount >= ui_amount && mud->mouse_x >= x + offset_x &&
        mud->mouse_y >= y && mud->mouse_x < x + offset_x + 25 &&
        mud->mouse_y <= y + 11) {
        mudclient_bank_transaction(mud, item_id, ui_amount, transaction_opcode);
    }

    offset_x += 25; // 85

    int show_last_offer_x = mud->options->offer_x && mud->options->last_offer_x;

    ui_amount = mud->options->offer_x ? 50 : 100;

    if (amount >= ui_amount && mud->mouse_x >= x + offset_x &&
        mud->mouse_y >= y &&
        mud->mouse_x < x + offset_x + 30 - (show_last_offer_x ? 7 : 0) &&
        mud->mouse_y <= y + 11) {
        mudclient_bank_transaction(mud, item_id, ui_amount, transaction_opcode);
    }

    offset_x += 30 - (show_last_offer_x ? 7 : 0); // 115 or 108

    ui_amount = mud->options->offer_x ? 2 : 500;

    if (amount >= ui_amount && mud->mouse_x >= x + offset_x &&
        mud->mouse_y >= y &&
        mud->mouse_x < x + offset_x + 33 - (show_last_offer_x ? 17 : 0) &&
        mud->mouse_y <= y + 11) {

        if (mud->options->offer_x) {
            mud->bank_offer_type = transaction_opcode == CLIENT_BANK_WITHDRAW
                                       ? BANK_OFFER_WITHDRAW
                                       : BANK_OFFER_DEPOSIT;

            mud->offer_id = item_id;
            mud->offer_max = amount;

            mud->input_digits_final = 0;
            mud->show_dialog_offer_x = 1;
        } else {
            mudclient_bank_transaction(mud, item_id, ui_amount,
                                       transaction_opcode);
        }
    }

    offset_x += 33 - (show_last_offer_x ? 17 : 0);

    if (show_last_offer_x && last_x != 0 && mud->mouse_x >= x + offset_x &&
        mud->mouse_y >= y && mud->mouse_x < x + offset_x + 30 &&
        mud->mouse_y <= y + 11) {
        mudclient_bank_transaction(mud, item_id, last_x, transaction_opcode);
    }

    if (show_last_offer_x) {
        offset_x += 37;
    }

    ui_amount = mud->options->offer_x ? amount : 2500;

    if (amount >= ui_amount && mud->mouse_x >= x + offset_x + 2 &&
        mud->mouse_y >= y && mud->mouse_x < x + offset_x + 32 &&
        mud->mouse_y <= y + 11) {
        mudclient_bank_transaction(mud, item_id, ui_amount, transaction_opcode);
    }
}

void mudclient_draw_bank(mudclient *mud) {
    int is_touch = mudclient_is_touch(mud);

    int is_compact = mud->surface->width < MUD_VANILLA_WIDTH ||
                     mud->surface->height < MUD_VANILLA_HEIGHT;

    int visible_columns = BANK_COLUMNS;

    if (is_compact) {
        visible_columns = (mud->surface->width - 28) / ITEM_GRID_SLOT_WIDTH;

        if (visible_columns > BANK_COLUMNS) {
            visible_columns = BANK_COLUMNS;
        } else if (visible_columns < BANK_COLUMNS_MIN) {
            visible_columns = BANK_COLUMNS_MIN;
        }
    }

    int horizontal_padding = (is_compact ? 100 : 142) + (is_touch ? 18 : 0);

    int visible_rows = mud->options->bank_expand
                           ? (mud->surface->height - horizontal_padding) /
                                 ITEM_GRID_SLOT_HEIGHT
                           : BANK_ROWS;

    if (visible_rows > 12) {
        visible_rows = 12;
    } else if (visible_rows < BANK_ROWS_MIN) {
        visible_rows = BANK_ROWS_MIN;
    }

    mud->bank_visible_rows = visible_rows;

    int offset_y = is_compact ? 12 : 0;

    int item_grid_height = visible_rows * ITEM_GRID_SLOT_HEIGHT;
    int bank_height = item_grid_height + 76 - offset_y;

    /*if (mud->options->bank_search) {
        bank_height += 24;
    }*/

    int page_offset_x = 50;
    int page_width = BANK_PAGE_BUTTON_WIDTH;
    int items_per_page = visible_rows * visible_columns;

    char bank_title[15] = "Bank";

    if (mud->options->bank_capacity) {
        sprintf(bank_title, "Bank (%d/%d)", mud->bank_item_count,
                mud->bank_items_max);

        page_offset_x = surface_text_width(bank_title, 1) + 5;
        page_width = 60;
    }

    int active_page = mud->bank_active_page;

    int bank_item_offset = (is_compact || mud->options->bank_scroll)
                               ? mud->bank_scroll_row * visible_columns
                               : active_page * items_per_page;

    int bank_item_count = 0;
    int bank_items[BANK_ITEMS_MAX] = {0};
    int bank_items_count[BANK_ITEMS_MAX] = {0};

    // TODO strtrim
    if (mud->options->bank_search && strlen(mud->input_pm_current) > 0) {
        int prefix_match_items[BANK_ITEMS_MAX] = {0};
        int prefix_match_items_count[BANK_ITEMS_MAX] = {0};
        int prefix_match_length = 0;

        int contains_match_items[BANK_ITEMS_MAX] = {0};
        int contains_match_items_count[BANK_ITEMS_MAX] = {0};
        int contains_match_length = 0;

        for (int i = 0; i < mud->bank_item_count; i++) {
            char *item_name = game_data.items[mud->bank_items[i]].name;
            char lower_item_name[strlen(item_name) + 1];
            strcpy(lower_item_name, item_name);
            strtolower(lower_item_name);

            size_t search_length = strlen(mud->input_pm_current);
            char lower_search[search_length + 1];
            strcpy(lower_search, mud->input_pm_current);
            strtolower(lower_search);

            int bank_item_id = mud->bank_items[i];
            int item_amount = mud->bank_items_count[i];

            if (prefix_match_length < BANK_ITEMS_MAX &&
                strncmp(lower_search, lower_item_name, search_length) == 0) {
                prefix_match_items[prefix_match_length] = bank_item_id;
                prefix_match_items_count[prefix_match_length] = item_amount;
                prefix_match_length++;
            } else if (contains_match_length < BANK_ITEMS_MAX &&
                       strstr(lower_item_name, lower_search) != NULL) {
                contains_match_items[contains_match_length] = bank_item_id;
                contains_match_items_count[contains_match_length] = item_amount;
                contains_match_length++;
            }
        }

        /* add items that begin with search first */
        for (int i = 0; i < prefix_match_length; i++) {
            bank_items[i] = prefix_match_items[i];
            bank_items_count[i] = prefix_match_items_count[i];
        }

        for (int i = prefix_match_length; i < BANK_ITEMS_MAX; i++) {
            int contains_index = i - prefix_match_length;

            if (contains_index < contains_match_length) {
                bank_items[i] = contains_match_items[contains_index];

                bank_items_count[i] =
                    contains_match_items_count[contains_index];
            } else {
                bank_items[i] = -1;
                bank_items_count[i] = 0;
            }
        }

        bank_item_count = prefix_match_length + contains_match_length;
    } else {
        bank_item_count = mud->bank_item_count;

        for (int i = 0; i < bank_item_count; i++) {
            bank_items[i] = mud->bank_items[i];
            bank_items_count[i] = mud->bank_items_count[i];
        }
    }

    int show_bank_scroll = (is_compact || mud->options->bank_scroll) &&
                           bank_item_count > visible_rows * visible_columns;

    int max_scroll_height = item_grid_height - 28;
    int total_rows = ceil(bank_item_count / (float)visible_columns);

    int scrub_height =
        max_scroll_height / ((float)total_rows / (float)visible_rows);

    int bank_width =
        ((ITEM_GRID_SLOT_WIDTH * visible_columns) + (is_compact ? 11 : 16));

    if (show_bank_scroll) {
        bank_width += 13;
    } else {
        mud->bank_scroll_row = 0;
    }

    int columns = mud->surface->height < 260 ? 6 : 5;
    int inventory_width = ITEM_GRID_SLOT_WIDTH * columns;

    int show_inventory =
        mud->options->bank_inventory &&
        mud->surface->width > inventory_width + bank_width + 15;

    if (show_inventory) {
        mudclient_draw_ui_tab_inventory(mud, !mud->show_right_click_menu &&
                                                 !mud->show_dialog_offer_x);
    }

    int contain_width = show_inventory && mud->surface->width <
                                              inventory_width + bank_width + 265
                            ? mud->surface->width - inventory_width - 4
                            : mud->surface->width;

    int ui_x = (contain_width / 2) - (bank_width / 2);
    int ui_y = (mud->surface->height / 2) - (bank_height / 2) - 30;

    /* the title bar makes it hard to see */
    if (ui_y < 18 && mud->options->bank_menus) {
        ui_y = 18;
    }

    if (is_touch) {
        ui_y = 34;
    }

    int item_grid_y = ui_y + 28 - offset_y;

    if (mud->input_digits_final > 0) {
        if (mud->input_digits_final > mud->offer_max) {
            mudclient_show_message(mud, "You don't have that many!",
                                   MESSAGE_TYPE_GAME);
        } else {
            int is_withdraw = mud->bank_offer_type == BANK_OFFER_WITHDRAW;

            mudclient_bank_transaction(
                mud, mud->offer_id, mud->input_digits_final,
                is_withdraw ? CLIENT_BANK_WITHDRAW : CLIENT_BANK_DEPOSIT);

            if (is_withdraw) {
                mud->bank_last_withdraw_offer = mud->input_digits_final;
            } else {
                mud->bank_last_deposit_offer = mud->input_digits_final;
            }
        }

        mud->show_dialog_offer_x = 0;
        mud->input_digits_final = 0;
    }

    if (active_page > 0 && bank_item_count <= items_per_page) {
        mud->bank_active_page = 0;
    }

    if (active_page > 1 && bank_item_count <= (items_per_page * 2)) {
        mud->bank_active_page = 1;
    }

    if (active_page > 2 && bank_item_count <= (items_per_page * 3)) {
        mud->bank_active_page = 2;
    }

    if (mud->bank_selected_item_slot >= bank_item_count ||
        mud->bank_selected_item_slot < 0) {
        mud->bank_selected_item_slot = -1;
    }

    if (mud->bank_selected_item_slot != -1 &&
        bank_items[mud->bank_selected_item_slot] != mud->bank_selected_item) {
        if (mud->options->bank_maintain_slot) {
            if (mud->bank_selected_item != -2) {
                for (int i = 0; i < bank_item_count; i++) {
                    if (bank_items[i] == mud->bank_selected_item) {
                        mud->bank_selected_item_slot = i;
                        break;
                    }
                }
            } else {
                mud->bank_selected_item_slot = -1;
            }
        } else {
            mud->bank_selected_item_slot = -1;
            mud->bank_selected_item = -2;
        }
    }

    int item_id = 0;

    if (mud->bank_selected_item_slot < 0) {
        item_id = -1;
    } else {
        item_id = bank_items[mud->bank_selected_item_slot];
    }

    int mouse_x = mud->mouse_x - ui_x;
    int mouse_y = mud->mouse_y - ui_y + offset_y;
    int mouse_handled = 0;

    int bank_search_y = ui_y + item_grid_height + (is_compact ? 66 : 92);

    int keyboard_x = ui_x + 49;
    int keyboard_y = bank_search_y - 15;

    if (mud->show_dialog_offer_x) {
        mouse_handled = 1;
    } else if (mud->mouse_button_click != 0) {
        if (mud->mouse_x >= ui_x && mud->mouse_x <= ui_x + bank_width &&
            mud->mouse_y >= keyboard_y && mud->mouse_y <= keyboard_y + 20) {
            if (is_touch) {
                mudclient_trigger_keyboard(
                    mud, mud->input_pm_current, 0, keyboard_x, keyboard_y,
                    bank_width - 49, 20, FONT_BOLD_12, 0);
            }

            mud->bank_search_focus = 1;

            mud->mouse_button_click = 0;
        } else {
            mud->bank_search_focus = 0;
        }
    }

    if (!mouse_handled && mouse_x >= 0 && mouse_y >= 12 + offset_y &&
        mouse_x < bank_width && mouse_y < item_grid_height + 76) {
        mouse_handled = 1;

        int slot_index = bank_item_offset;

        for (int row = 0; row < visible_rows; row++) {
            for (int column = 0; column < visible_columns; column++) {
                int slot_x = 7 + column * ITEM_GRID_SLOT_WIDTH;
                int slot_y = 28 + row * ITEM_GRID_SLOT_HEIGHT;

                if (mouse_x > slot_x &&
                    mouse_x < slot_x + ITEM_GRID_SLOT_WIDTH &&
                    mouse_y > slot_y &&
                    mouse_y < slot_y + ITEM_GRID_SLOT_HEIGHT &&
                    slot_index < bank_item_count &&
                    bank_items[slot_index] != -1) {
                    int selected_item_id = bank_items[slot_index];

                    /* only left click selects if bank menus are enabled */
                    int clicked_slot = mud->options->bank_menus
                                           ? !mud->show_right_click_menu &&
                                                 mud->mouse_button_click == 1
                                           : mud->mouse_button_click != 0;

                    if (!mud->selected_wiki && !mud->show_dialog_offer_x &&
                        clicked_slot) {
                        /* only withdraw if they click an item that's already
                         * selected */
                        if (mud->bank_selected_item != selected_item_id) {
                            mud->mouse_button_click = 0;
                        }

                        mud->bank_selected_item = selected_item_id;
                        mud->bank_selected_item_slot = slot_index;
                    }

                    char *item_name = game_data.items[selected_item_id].name;

                    char formatted_item_name[strlen(item_name) + 6];
                    sprintf(formatted_item_name, "@lre@%s", item_name);

                    if (mud->selected_wiki) {
                        mudclient_menu_add_id_wiki(mud, formatted_item_name,
                                                   "item", selected_item_id);
                    } else if (mud->options->bank_menus &&
                               !mud->show_right_click_menu) {
                        int item_amount = bank_items_count[slot_index];

                        mudclient_add_offer_menus(
                            mud, "Withdraw", MENU_BANK_WITHDRAW,
                            selected_item_id, item_amount, formatted_item_name,
                            mud->bank_last_withdraw_offer);

#ifndef REVISION_177
                        // equip a wearable item directly from the bank
                        if (mud->protocol_custom &&
                            mud->orsc.want_equipment_tab &&
                            game_data.items[selected_item_id].wearable != 0) {
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .action_text,
                                   "Equip");
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .target_text,
                                   formatted_item_name);
                            mud->menu_items[mud->menu_items_count].type =
                                MENU_BANK_EQUIP;
                            mud->menu_items[mud->menu_items_count].index =
                                slot_index;
                            mud->menu_items_count++;
                        }

                        // reordering disabled while a search filter is active
                        int organize_ok = mud->protocol_custom &&
                                          mud->orsc.want_custom_banks &&
                                          !(mud->options->bank_search &&
                                            strlen(mud->input_pm_current) > 0);

                        if (organize_ok && mud->bank_organize_slot < 0) {
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .action_text,
                                   "Swap");
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .target_text,
                                   formatted_item_name);
                            mud->menu_items[mud->menu_items_count].type =
                                MENU_BANK_ORGANIZE_SWAP;
                            mud->menu_items[mud->menu_items_count].index =
                                slot_index;
                            mud->menu_items_count++;

                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .action_text,
                                   "Insert");
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .target_text,
                                   formatted_item_name);
                            mud->menu_items[mud->menu_items_count].type =
                                MENU_BANK_ORGANIZE_INSERT;
                            mud->menu_items[mud->menu_items_count].index =
                                slot_index;
                            mud->menu_items_count++;
                        } else if (organize_ok &&
                                   mud->bank_organize_slot != slot_index) {
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .action_text,
                                   mud->bank_organize_insert ? "Insert before"
                                                             : "Swap with");
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .target_text,
                                   formatted_item_name);
                            mud->menu_items[mud->menu_items_count].type =
                                mud->bank_organize_insert
                                    ? MENU_BANK_ORGANIZE_INSERT
                                    : MENU_BANK_ORGANIZE_SWAP;
                            mud->menu_items[mud->menu_items_count].index =
                                slot_index;
                            mud->menu_items_count++;
                        } else if (organize_ok) {
                            // release on the held slot to cancel the move
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .action_text,
                                   "Cancel move");
                            strcpy(mud->menu_items[mud->menu_items_count]
                                       .target_text,
                                   formatted_item_name);
                            mud->menu_items[mud->menu_items_count].type =
                                MENU_BANK_ORGANIZE_SWAP;
                            mud->menu_items[mud->menu_items_count].index =
                                slot_index;
                            mud->menu_items_count++;
                        }
#endif
                    }
                }

                slot_index++;
            }
        }
    }

    int mouse_in_inventory =
        show_inventory && mud->mouse_x > mud->surface->width - inventory_width;

    if (!mud->show_right_click_menu && mud->mouse_button_click != 0 &&
        !mouse_in_inventory) {
        int bank_count = mud->bank_selected_item_slot >= 0
                             ? bank_items_count[mud->bank_selected_item_slot]
                             : 0;

        if (!mud->options->bank_unstackble_withdraw &&
            game_data.items[item_id].stackable == 1 && bank_count > 1) {
            bank_count = 1;
        }

        int offset_x = is_compact ? 100 : 0;

        if (bank_count > 0) {
            mudclient_handle_bank_amounts_input(
                mud, item_id, bank_count, mud->bank_last_withdraw_offer,
                ui_x + 220 - offset_x,
                ui_y + item_grid_height + (is_compact ? 20 : 34),
                CLIENT_BANK_WITHDRAW);
        }

        int inventory_count = mudclient_get_inventory_count(mud, item_id);

        if (inventory_count > 0) {
            mudclient_handle_bank_amounts_input(
                mud, item_id, inventory_count, mud->bank_last_deposit_offer,
                ui_x + 220 - offset_x,
                ui_y + item_grid_height + (is_compact ? 39 : 59),
                CLIENT_BANK_DEPOSIT);
        }

        if (mud->show_dialog_offer_x && mouse_x > 0 && mouse_x < bank_width &&
            mouse_y > 0 && mouse_y < bank_height) {
            mouse_x = 0;
            mouse_y = 13;
        }

        if (!mouse_handled) {
            if (!show_bank_scroll && bank_item_count > items_per_page &&
                mouse_x >= page_offset_x &&
                mouse_x <= page_offset_x + page_width && mouse_y <= 12) {
                mud->bank_active_page = 0;
            } else if (!show_bank_scroll && bank_item_count > items_per_page &&
                       mouse_x >= (page_offset_x + page_width) &&
                       mouse_x <= (page_offset_x + page_width * 2) &&
                       mouse_y <= 12) {
                mud->bank_active_page = 1;
            } else if (!show_bank_scroll &&
                       bank_item_count > items_per_page * 2 &&
                       mouse_x >= (page_offset_x + page_width * 2) &&
                       mouse_x <= (page_offset_x + page_width * 3) &&
                       mouse_y <= 12) {
                mud->bank_active_page = 2;
            } else if (!show_bank_scroll &&
                       bank_item_count > items_per_page * 3 &&
                       mouse_x >= (page_offset_x + page_width * 3) &&
                       mouse_x <= (page_offset_x + page_width * 4) &&
                       mouse_y <= 12) {
                mud->bank_active_page = 3;
            } else if (mouse_y <= 12 && mudclient_bank_can_deposit_all(mud, 0) &&
                       (bank_width - 160) > page_offset_x &&
                       mouse_x >= (bank_width - 160) &&
                       mouse_x < (bank_width - 92)) {
                // deposit-all button; only shown where the world allows it
                mudclient_bank_deposit_all(mud,
                                           CLIENT_BANK_DEPOSIT_ALL_INVENTORY);
            }
#ifndef REVISION_177
            else if (mud->protocol_custom && mud->orsc.want_bank_presets &&
                     mouse_y <= 12 && mouse_x >= (bank_width - 274) &&
                     mouse_x < (bank_width - 168)) {
                // preset hit zones, right to left: presets, ld1, ld2
                if (mouse_x >= (bank_width - 194) &&
                    (bank_width - 194) > page_offset_x) {
                    packet_stream_new_packet(mud->packet_stream,
                                             CLIENT_BANK_LOAD_PRESET);
                    packet_stream_put_short(mud->packet_stream, 1);
                    packet_stream_send_packet(mud->packet_stream);
                } else if (mouse_x >= (bank_width - 224) &&
                           mouse_x < (bank_width - 198) &&
                           (bank_width - 224) > page_offset_x) {
                    packet_stream_new_packet(mud->packet_stream,
                                             CLIENT_BANK_LOAD_PRESET);
                    packet_stream_put_short(mud->packet_stream, 0);
                    packet_stream_send_packet(mud->packet_stream);
                } else if (mouse_x < (bank_width - 228) &&
                           (bank_width - 274) > page_offset_x) {
                    mud->show_dialog_bank_preset = 1;
                    mud->bank_preset_selected_slot = 0;
                }
            } else if (mud->protocol_custom && mud->orsc.want_bank_notes &&
                       mouse_y <= 12 && mouse_x >= (bank_width - 318) &&
                       mouse_x < (bank_width - 284) &&
                       (bank_width - 318) > page_offset_x) {
                // toggles withdraw mode between item and note, client-side only
                mud->bank_swap_note_mode = !mud->bank_swap_note_mode;
            }
#endif
            else {
                packet_stream_new_packet(mud->packet_stream, CLIENT_BANK_CLOSE);
                packet_stream_send_packet(mud->packet_stream);
                mud->show_dialog_bank = 0;
                mud->show_dialog_offer_x = 0;
                mud->bank_organize_slot = -1;
                mud->bank_swap_note_mode = 0; // reset to item mode
                return;
            }
        }
    }

    if (show_bank_scroll && !mud->show_right_click_menu &&
        !mud->show_dialog_offer_x) {
        if (!is_touch && mud->mouse_scroll_delta != 0) {
            mud->bank_scroll_row -= mud->mouse_scroll_delta * -1;
        }

        if ((mud->mouse_button_down != 0 || mudclient_finger_1_down) &&
            get_ticks() - mud->bank_last_scroll > BANK_SCROLL_SPEED) {
            int scrollbar_x =
                (ITEM_GRID_SLOT_WIDTH * visible_columns) + (is_compact ? 6 : 8);

            /* up arrow */
            if (mud->bank_scroll_row > 0 && mud->mouse_x > ui_x + scrollbar_x &&
                mud->mouse_x < ui_x + scrollbar_x + 21 &&
                mud->mouse_y > item_grid_y && mud->mouse_y < item_grid_y + 15) {
                mud->bank_scroll_row -= 1;
                mud->bank_last_scroll = get_ticks();
            }

            /* scrub area */
            int is_dragging = mud->bank_handle_dragged;

            if (!mud->options->off_handle_scroll_drag) {
                is_dragging = is_dragging &&
                              mud->mouse_x >= ui_x + scrollbar_x - 12 &&
                              mud->mouse_x <= ui_x + scrollbar_x + 33;
            }

            if ((is_dragging || (mud->mouse_x > ui_x + scrollbar_x &&
                                 mud->mouse_x < ui_x + scrollbar_x + 21)) &&
                mud->mouse_y > item_grid_y + 15 &&
                mud->mouse_y < ui_y + item_grid_height + 15 - offset_y) {
                int scrub_y =
                    mud->mouse_y - (ui_y + 43 - offset_y) - (scrub_height / 2);

                int scroll_row =
                    (scrub_y / (float)max_scroll_height) * total_rows;

                mud->bank_scroll_row = scroll_row;
                mud->bank_handle_dragged = 1;
            }

            /* down arrow */
            if (mud->bank_scroll_row < (total_rows - visible_rows) &&
                mud->mouse_x > ui_x + scrollbar_x &&
                mud->mouse_x < ui_x + scrollbar_x + 21 &&
                mud->mouse_y > ui_y + item_grid_height + 15 - offset_y &&
                mud->mouse_y < ui_y + item_grid_height + 30 - offset_y) {
                mud->bank_scroll_row += 1;
                mud->bank_last_scroll = get_ticks();
            }
        } else {
            mud->bank_handle_dragged = 0;
        }

        if (mud->bank_scroll_row < 0) {
            mud->bank_scroll_row = 0;
        } else if (mud->bank_scroll_row > total_rows - visible_rows) {
            mud->bank_scroll_row = total_rows - visible_rows;
        }
    }

    surface_draw_box(mud->surface, ui_x, ui_y, bank_width, 12,
                     TITLE_BAR_COLOUR);

    /* alpha box under title bar */
    surface_draw_box_alpha(mud->surface, ui_x, ui_y + 12, bank_width,
                           17 - offset_y, GREY_98, 160);

    /* alpha box to left of item grid */
    surface_draw_box_alpha(mud->surface, ui_x, ui_y + 29 - offset_y,
                           is_compact ? 5 : 8, item_grid_height, GREY_98, 160);

    /* alpha box to right of item grid */
    surface_draw_box_alpha(
        mud->surface,
        ui_x + (ITEM_GRID_SLOT_WIDTH * visible_columns) + (is_compact ? 5 : 7),
        ui_y + 29 - offset_y,
        9 + (show_bank_scroll ? 13 : 0) - (is_compact ? 3 : 0),
        item_grid_height, GREY_98, 160);

    surface_draw_string(mud->surface, bank_title, ui_x + 1, ui_y + 10,
                        FONT_BOLD_12, WHITE);

    if (!show_bank_scroll) {
        if (bank_item_count > items_per_page) {
            mudclient_draw_bank_page(mud, ui_x + page_offset_x, ui_y, 1,
                                     page_width);

            page_offset_x += page_width;

            mudclient_draw_bank_page(mud, ui_x + page_offset_x, ui_y, 2,
                                     page_width);

            page_offset_x += page_width;
        }

        if (bank_item_count > items_per_page * 2) {
            mudclient_draw_bank_page(mud, ui_x + page_offset_x, ui_y, 3,
                                     page_width);

            page_offset_x += page_width;
        }

        if (bank_item_count > items_per_page * 3) {
            mudclient_draw_bank_page(mud, ui_x + page_offset_x, ui_y, 4,
                                     page_width);

            page_offset_x += page_width;
        }
    }

    int text_colour = WHITE;

    if (mud->mouse_x > ui_x + bank_width - 88 && mud->mouse_y >= ui_y &&
        mud->mouse_x < ui_x + bank_width && mud->mouse_y < ui_y + 12) {
        text_colour = RED;
    }

    surface_draw_string_right(mud->surface, "Close window",
                              ui_x + bank_width - 2, ui_y + 10, FONT_BOLD_12,
                              text_colour);

    // deposit-all button; hidden if the title bar has no room
    int deposit_inv_right = ui_x + bank_width - 92;
    int deposit_inv_width = 68;
    int deposit_inv_x = deposit_inv_right - deposit_inv_width;
    int show_deposit_inv = deposit_inv_x > ui_x + page_offset_x &&
                           mudclient_bank_can_deposit_all(mud, 0);

    if (show_deposit_inv) {
        int deposit_hot = mud->mouse_x >= deposit_inv_x &&
                          mud->mouse_x < deposit_inv_right &&
                          mud->mouse_y >= ui_y && mud->mouse_y < ui_y + 12;

        surface_draw_string_right(mud->surface, "Deposit inv", deposit_inv_right,
                                  ui_y + 10, FONT_BOLD_12,
                                  deposit_hot ? RED : WHITE);
    }

#ifndef REVISION_177
    // preset load buttons; saving happens in the preset viewer
    if (mud->protocol_custom && mud->orsc.want_bank_presets) {
        static const char *preset_labels[2] = {"Ld1", "Ld2"};
        int preset_right = deposit_inv_x - 8;

        for (int i = 1; i >= 0; i--) {
            int zone_right = preset_right - (1 - i) * 30;
            int zone_left = zone_right - 26;

            if (zone_left <= ui_x + page_offset_x) {
                break;
            }

            int preset_hot = mud->mouse_x >= zone_left &&
                             mud->mouse_x < zone_right &&
                             mud->mouse_y >= ui_y && mud->mouse_y < ui_y + 12;

            surface_draw_string_right(mud->surface, preset_labels[i],
                                      zone_right, ui_y + 10, FONT_BOLD_12,
                                      preset_hot ? RED : WHITE);
        }

        int viewer_right = preset_right - 2 * 30;
        int viewer_left = viewer_right - 46;

        if (viewer_left > ui_x + page_offset_x) {
            int viewer_hot = mud->mouse_x >= viewer_left &&
                             mud->mouse_x < viewer_right &&
                             mud->mouse_y >= ui_y && mud->mouse_y < ui_y + 12;

            surface_draw_string_right(mud->surface, "Presets", viewer_right,
                                      ui_y + 10, FONT_BOLD_12,
                                      viewer_hot ? RED : WHITE);
        }
    }

    // shows current withdraw mode: item or note
    if (mud->protocol_custom && mud->orsc.want_bank_notes) {
        int note_right = ui_x + bank_width - 284;
        int note_left = note_right - 34;

        if (note_left > ui_x + page_offset_x) {
            int note_hot = mud->mouse_x >= note_left &&
                           mud->mouse_x < note_right && mud->mouse_y >= ui_y &&
                           mud->mouse_y < ui_y + 12;

            surface_draw_string_right(
                mud->surface, mud->bank_swap_note_mode ? "Note" : "Item",
                note_right, ui_y + 10, FONT_BOLD_12, note_hot ? RED : WHITE);
        }
    }
#endif

    if (!is_compact) {
        surface_draw_string(mud->surface, "Number in bank in green", ui_x + 7,
                            ui_y + 24, FONT_BOLD_12, GREEN);

        surface_draw_string(mud->surface, "Number held in blue",
                            ui_x + bank_width - 119, ui_y + 24, FONT_BOLD_12,
                            CYAN);

        if (mud->options->bank_value) {
            int total_value = 0;

            for (int i = 0; i < mud->bank_item_count; i++) {
                for (int j = 0; j < mud->bank_items_count[i]; j++) {
                    total_value +=
                        game_data.items[mud->bank_items[i]].base_price;
                }

                /* overflow */
                if (total_value < 0) {
                    break;
                }
            }

            char formatted_money[30] = {0};

            if (total_value < 0) {
                sprintf(formatted_money, "Total value: a lot!");
            } else {
                char formatted_amount[15] = {0};

                mudclient_format_number_commas(mud, total_value,
                                               formatted_amount);

                sprintf(formatted_money, "Total value: %sgp", formatted_amount);
            }

            FontStyle font =
                total_value >= 10000000 ? FONT_REGULAR_11 : FONT_BOLD_12;

            surface_draw_string_centre(mud->surface, formatted_money,
                                       ui_x + (bank_width / 2) + 12, ui_y + 24,
                                       font, YELLOW);
        }
    }

    surface_draw_item_grid(
        mud->surface, ui_x + (is_compact ? 5 : 7), item_grid_y, visible_rows,
        visible_columns, ITEM_GRID_SLOT_WIDTH, ITEM_GRID_SLOT_HEIGHT,
        bank_items + bank_item_offset, bank_items_count + bank_item_offset,
        bank_item_count - bank_item_offset,
        mud->bank_selected_item_slot - bank_item_offset, 1);

    if (show_bank_scroll) {
        int scrub_y = ((float)mud->bank_scroll_row / (float)total_rows) *
                      (float)max_scroll_height;

        int scrollbar_x =
            (ITEM_GRID_SLOT_WIDTH * visible_columns) - (is_compact ? 4 : 0);

        surface_draw_scrollbar(mud->surface, ui_x + 24, item_grid_y,
                               scrollbar_x, item_grid_height, 1 + scrub_y,
                               scrub_height);

        surface_draw_line_vertical(mud->surface, ui_x + scrollbar_x + 24,
                                   item_grid_y, item_grid_height + 1, BLACK);

        surface_draw_line_horizontal(mud->surface, ui_x + scrollbar_x + 12,
                                     item_grid_y + item_grid_height, 12, BLACK);
    }

    surface_draw_box_alpha(mud->surface, ui_x,
                           ui_y + (item_grid_height - 204) + 233 - offset_y,
                           bank_width, 47 - offset_y, GREY_98, 160);

    surface_draw_line_horizontal(mud->surface, ui_x + 5,
                                 ui_y + item_grid_height +
                                     (is_compact ? 34 : 52),
                                 bank_width - 10, BLACK);

    if (mud->options->bank_search) {
        surface_draw_box_alpha(mud->surface, ui_x,
                               ui_y + item_grid_height + (is_compact ? 52 : 76),
                               bank_width, (is_compact ? 18 : 20), GREY_98,
                               160);

        surface_draw_line_horizontal(mud->surface, ui_x + 5,
                                     ui_y + item_grid_height +
                                         (is_compact ? 52 : 76),
                                     bank_width - 10, BLACK);

        surface_draw_stringf(mud->surface, ui_x + 2, bank_search_y,
                             FONT_BOLD_12, WHITE, "Search: %s%c",
                             mud->input_pm_current,
                             mud->bank_search_focus ? '*' : ' ');
    }

    if (item_id != -1) {
        int bank_count = bank_items_count[mud->bank_selected_item_slot];

        if (!mud->options->bank_unstackble_withdraw &&
            game_data.items[item_id].stackable == 1 && bank_count > 1) {
            bank_count = 1;
        }

        int offset_x = is_compact ? 100 : 0;

        if (bank_count > 0) {
            char *item_name = game_data.items[item_id].name;

            surface_draw_stringf(
                mud->surface, ui_x + 2,
                ui_y + item_grid_height + (is_compact ? 30 : 44), FONT_BOLD_12,
                WHITE, "Withdraw %s", is_compact ? "" : item_name);

            mudclient_draw_bank_amounts(
                mud, bank_count, mud->bank_last_withdraw_offer,
                ui_x + 220 - offset_x,
                ui_y + item_grid_height + (is_compact ? 20 : 34));
        }

        int inventory_count = mudclient_get_inventory_count(mud, item_id);

        if (inventory_count > 0) {
            char *item_name = game_data.items[item_id].name;

            surface_draw_stringf(
                mud->surface, ui_x + 2,
                ui_y + item_grid_height + (is_compact ? 48 : 69), FONT_BOLD_12,
                WHITE, "Deposit %s", is_compact ? "" : item_name);

            mudclient_draw_bank_amounts(
                mud, inventory_count, mud->bank_last_deposit_offer,
                ui_x + 220 - offset_x,
                ui_y + item_grid_height + (is_compact ? 39 : 59));
        }
    } else {
        surface_draw_string_centre(
            mud->surface, "Select an object to withdraw or deposit",
            ui_x + bank_width / 2,
            ui_y + item_grid_height + (is_compact ? 30 : 44), FONT_BOLD_13,
            YELLOW);
    }

    if (mud->show_dialog_offer_x) {
        mudclient_draw_offer_x(mud);
        mudclient_handle_offer_x_input(mud);
    }
}
