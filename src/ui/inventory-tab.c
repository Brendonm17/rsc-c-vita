#include "inventory-tab.h"

#ifndef REVISION_177
// custom equipment tab: worn-gear paperdoll in place of inventory grid
static void mudclient_draw_equipment_paperdoll(mudclient *mud, int ui_x, int ui_y,
                                               int width, int height,
                                               int no_menus) {
    static const int equip_icon_x[11] = {98, 98,  98, 153, 43, 43,
                                         98, 98,  43, 153, 153};
    static const int equip_icon_y[11] = {5,   85, 125, 85, 85, 165,
                                         165, 45, 45,  45, 165};

    surface_draw_box_alpha(mud->surface, ui_x, ui_y, width, height, GREY_98, 128);

    int hover_slot = -1;

    for (int i = 0; i < 11; i++) {
        int slot_x = ui_x + equip_icon_x[i];
        int slot_y = ui_y + equip_icon_y[i];
        int item_id = mud->equipped_item_id[i];

        surface_draw_box_alpha(mud->surface, slot_x, slot_y,
                               ITEM_GRID_SLOT_WIDTH, ITEM_GRID_SLOT_HEIGHT,
                               GREY_B5, 128);

        if (item_id > 0) {
            mudclient_draw_item(mud, slot_x, slot_y, ITEM_GRID_SLOT_WIDTH,
                                ITEM_GRID_SLOT_HEIGHT, item_id);

            // stackable == 0 means it stacks (config85 polarity)
            if (item_id < game_data.item_count &&
                game_data.items[item_id].stackable == 0 &&
                mud->equipped_item_amount[i] > 1) {
                char formatted_amount[16] = {0};
                mudclient_format_item_amount(mud, mud->equipped_item_amount[i],
                                             formatted_amount);
                surface_draw_string(mud->surface, formatted_amount, slot_x + 1,
                                    slot_y + 10, FONT_BOLD_12, YELLOW);
            }

            if (no_menus && mud->mouse_x >= slot_x &&
                mud->mouse_x < slot_x + ITEM_GRID_SLOT_WIDTH &&
                mud->mouse_y >= slot_y &&
                mud->mouse_y < slot_y + ITEM_GRID_SLOT_HEIGHT) {
                hover_slot = i;
            }
        }
    }

    // bonus readout drawn just below the equipment tab region
    surface_draw_box_alpha(mud->surface, ui_x, ui_y + height, width, 60,
                           GREY_98, 128);
    mudclient_draw_equipment_status(mud, ui_x, ui_y + height + 12, 12,
                                    no_menus);

    if (hover_slot < 0) {
        return;
    }

    int item_id = mud->equipped_item_id[hover_slot];
    char *item_name = game_data.items[item_id].name;

    char formatted_item_name[strlen(item_name) + 6];
    sprintf(formatted_item_name, "@lre@%s", item_name);

    // while the bank is open, removing gear sends unequip-to-bank instead
    if (mud->show_dialog_bank) {
        strcpy(mud->menu_items[mud->menu_items_count].action_text,
               "Unequip to bank");
        strcpy(mud->menu_items[mud->menu_items_count].target_text,
               formatted_item_name);
        mud->menu_items[mud->menu_items_count].type = MENU_EQUIP_REMOVE_TO_BANK;
        mud->menu_items[mud->menu_items_count].index = hover_slot;
        mud->menu_items_count++;
    } else {
        strcpy(mud->menu_items[mud->menu_items_count].action_text, "Remove");
        strcpy(mud->menu_items[mud->menu_items_count].target_text,
               formatted_item_name);
        mud->menu_items[mud->menu_items_count].type = MENU_EQUIP_UNEQUIP;
        mud->menu_items[mud->menu_items_count].index = hover_slot;
        mud->menu_items_count++;
    }

    // the filled-slot menu continues: the item's own command rows, then Use and Drop, before Examine
    {
        int command_count = game_data_item_command_count(item_id);

        for (int c = 0; c < command_count; c++) {
            char command[64] = {0};

            if (!game_data_item_command_at(item_id, c, command,
                                           (int)sizeof(command))) {
                break;
            }

            strcpy(mud->menu_items[mud->menu_items_count].action_text,
                   command);
            strcpy(mud->menu_items[mud->menu_items_count].target_text,
                   formatted_item_name);
            mud->menu_items[mud->menu_items_count].type = MENU_EQUIP_COMMAND;
            mud->menu_items[mud->menu_items_count].index = hover_slot;
            mud->menu_items[mud->menu_items_count].source_index = c;
            mud->menu_items_count++;
        }
    }

    strcpy(mud->menu_items[mud->menu_items_count].action_text, "Use");
    strcpy(mud->menu_items[mud->menu_items_count].target_text,
           formatted_item_name);
    mud->menu_items[mud->menu_items_count].type = MENU_EQUIP_USE;
    mud->menu_items[mud->menu_items_count].index = hover_slot;
    mud->menu_items_count++;

    strcpy(mud->menu_items[mud->menu_items_count].action_text, "Drop");
    strcpy(mud->menu_items[mud->menu_items_count].target_text,
           formatted_item_name);
    mud->menu_items[mud->menu_items_count].type = MENU_EQUIP_DROP;
    mud->menu_items[mud->menu_items_count].index = hover_slot;
    mud->menu_items_count++;

    strcpy(mud->menu_items[mud->menu_items_count].action_text, "Examine");
    strcpy(mud->menu_items[mud->menu_items_count].target_text,
           formatted_item_name);
    mud->menu_items[mud->menu_items_count].type = MENU_INVENTORY_EXAMINE;
    mud->menu_items[mud->menu_items_count].index = item_id;
    mud->menu_items_count++;
}
#endif

void mudclient_draw_ui_tab_inventory(mudclient *mud, int no_menus) {
    int is_touch = mudclient_is_touch(mud);

    int columns = mud->surface->height < 260 ? 6 : 5;
    int rows = (INVENTORY_ITEMS_MAX / columns);

    int slot_height = ITEM_GRID_SLOT_HEIGHT - is_touch;

    int width = (ITEM_GRID_SLOT_WIDTH * columns);
    int height = slot_height * rows;

    int ui_x = mud->surface->width - width - 3;
    int ui_y = UI_BUTTON_SIZE + 1;

    if (is_touch) {
        if (mud->show_dialog_bank) {
            ui_x = mud->surface->width - width - 5;
        } else {
            ui_x = UI_TABS_TOUCH_X - width - 1;
        }

        ui_y = (UI_TABS_TOUCH_Y + UI_TABS_TOUCH_HEIGHT) - height - 2;
    }

    if (mud->options->version_media >= 59) {
            mudclient_draw_ui_tab_label(mud, INVENTORY_TAB, width, ui_x,
                                        ui_y - UI_TABS_LABEL_HEIGHT);
    }

    mud->ui_tab_min_x = ui_x;
    mud->ui_tab_max_x = mud->surface->width;
    mud->ui_tab_min_y = 0;
    mud->ui_tab_max_y = ui_y + height;

    if (is_touch) {
        mud->ui_tab_max_x = ui_x + width;
        mud->ui_tab_min_y = ui_y - UI_TABS_LABEL_HEIGHT;
        mud->ui_tab_max_y = ui_y + height;
    }

#ifndef REVISION_177
    // equipment tab: items|worn toggle renders the paperdoll when active
    if (mud->protocol_custom && mud->orsc.want_equipment_tab) {
        int toggle_h = 14;
        int half = width / 2;
        int worn = mud->tab_equipment_index == 1;

        surface_draw_box_alpha(mud->surface, ui_x, ui_y, half, toggle_h,
                               worn ? GREY_80 : GREY_C6, 160);
        surface_draw_box_alpha(mud->surface, ui_x + half, ui_y, width - half,
                               toggle_h, worn ? GREY_C6 : GREY_80, 160);
        surface_draw_string_centre(mud->surface, "Items", ui_x + half / 2,
                                   ui_y + 10, FONT_BOLD_12, BLACK);
        surface_draw_string_centre(mud->surface, "Worn",
                                   ui_x + half + (width - half) / 2, ui_y + 10,
                                   FONT_BOLD_12, BLACK);

        if (!mud->show_right_click_menu && mud->mouse_button_click != 0 &&
            mud->mouse_y >= ui_y && mud->mouse_y < ui_y + toggle_h &&
            mud->mouse_x >= ui_x && mud->mouse_x < ui_x + width) {
            mud->tab_equipment_index = mud->mouse_x < ui_x + half ? 0 : 1;
            mud->mouse_button_click = 0;
            worn = mud->tab_equipment_index == 1;
        }

        ui_y += toggle_h;
        mud->ui_tab_max_y = ui_y + height;

        if (worn) {
            // the bonus strip extends 60px below the paperdoll grid
            mud->ui_tab_max_y = ui_y + height + 60;
            mudclient_draw_equipment_paperdoll(mud, ui_x, ui_y, width, height,
                                               no_menus);
            return;
        }
    }
#endif

    /* item slots */
    for (int i = 0; i < INVENTORY_ITEMS_MAX; i++) {
        int slot_x = ui_x + (i % columns) * ITEM_GRID_SLOT_WIDTH;
        int slot_y = ui_y + (i / columns) * slot_height;
        int slot_colour = GREY_B5;

        if (i < mud->inventory_items_count && mud->inventory_equipped[i]) {
            slot_colour = RED;
        }

        surface_draw_box_alpha(mud->surface, slot_x, slot_y,
                               ITEM_GRID_SLOT_WIDTH, slot_height, slot_colour,
                               128);

        if (i < mud->inventory_items_count) {
            int item_id = mud->inventory_item_id[i];
            int noted = mudclient_item_noted(mud, i);

            // a noted item draws the certificate/note sprite, not its own icon
            if (noted) {
                mudclient_draw_noted_item(mud, slot_x, slot_y,
                                          ITEM_GRID_SLOT_WIDTH,
                                          ITEM_GRID_SLOT_HEIGHT, item_id, 4);
            } else {
                mudclient_draw_item(mud, slot_x, slot_y, ITEM_GRID_SLOT_WIDTH,
                                    ITEM_GRID_SLOT_HEIGHT, item_id);
            }

            char formatted_amount[12] = {0};

            mudclient_format_item_amount(
                mud, mud->inventory_item_stack_count[i], formatted_amount);

            // a noted item always stacks even if the base item doesn't
            if (game_data_item_stacks(item_id, noted)) {
                surface_draw_string(mud->surface, formatted_amount, slot_x + 1,
                                    slot_y + 10, FONT_BOLD_12, YELLOW);
            }
        }
    }

    /* row and column lines */
    for (int i = 1; i <= columns - 1; i++) {
        surface_draw_line_vertical(
            mud->surface, ui_x + i * ITEM_GRID_SLOT_WIDTH, ui_y, height, BLACK);
    }

    for (int i = 1; i <= rows - 1; i++) {
        surface_draw_line_horizontal(mud->surface, ui_x, ui_y + i * slot_height,
                                     width, BLACK);
    }

    if (!no_menus) {
        return;
    }

    int mouse_x = mud->mouse_x - ui_x;
    int mouse_y = mud->mouse_y - ui_y;

    if (mouse_x < 0 || mouse_y < 0 || mouse_x > width || mouse_y > height) {
        return;
    }

    int item_index =
        (mouse_x / ITEM_GRID_SLOT_WIDTH) + (mouse_y / slot_height) * columns;

    if (item_index >= mud->inventory_items_count) {
        return;
    }

    int item_id = mud->inventory_item_id[item_index];

    // noted items are named "<item> Certificate" (or plain name on some worlds)
    char item_name[ITEM_NAME_DISPLAY_MAX];

    mudclient_item_display_name(mud, item_id, mudclient_item_noted(mud, item_index),
                                item_name, sizeof(item_name));

    char formatted_item_name[ITEM_NAME_DISPLAY_MAX + 6];
    sprintf(formatted_item_name, "@lre@%s", item_name);

    if (mud->selected_wiki) {
        mudclient_menu_add_id_wiki(mud, formatted_item_name, "item", item_id);
    } else if (mud->show_dialog_bank) {
        int item_amount = mudclient_get_inventory_count(mud, item_id);

        mudclient_add_offer_menus(mud, "Deposit", MENU_BANK_DEPOSIT, item_id,
                                  item_amount, formatted_item_name,
                                  mud->bank_last_deposit_offer);

#ifndef REVISION_177
        // want_cert_deposit grows the deposit menu by two rows on the 27 certificate items
        if (mud->protocol_custom && mud->orsc.want_cert_deposit &&
            mudclient_bank_item_is_cert(item_id)) {
            if (item_amount > 1) {
                mudclient_add_offer_menu(mud, MENU_BANK_DEPOSIT_UNCERT,
                                         item_id, -item_amount,
                                         "Uncert+Deposit-X",
                                         formatted_item_name);
            }

            if (item_amount >= 1) {
                mudclient_add_offer_menu(mud, MENU_BANK_DEPOSIT_UNCERT,
                                         item_id, item_amount,
                                         "Uncert+Deposit-All",
                                         formatted_item_name);
            }
        }
#endif
    } else {
        if (mud->selected_spell >= 0) {
            // suppresses cast-alchemy-on-item entry for the nature rune itself
            int nat_rune_protected = 0;
#ifndef REVISION_177
            if (mud->protocol_custom && mud->orsc.want_nature_rune_protection &&
                (mud->selected_spell == 10 || mud->selected_spell == 28) &&
                strcmp(game_data.items[item_id].name, "Nature-Rune") == 0) {
                nat_rune_protected = 1;
            }
#endif
            if (game_data.spells[mud->selected_spell].type == 3 &&
                !nat_rune_protected) {
                sprintf(mud->menu_items[mud->menu_items_count].action_text,
                        "Cast %s on",
                        game_data.spells[mud->selected_spell].name);

                strcpy(mud->menu_items[mud->menu_items_count].target_text,
                       formatted_item_name);

                mud->menu_items[mud->menu_items_count].type = MENU_CAST_INVITEM;
                mud->menu_items[mud->menu_items_count].index = item_index;

                mud->menu_items[mud->menu_items_count].source_index =
                    mud->selected_spell;

                mud->menu_items_count++;

                return;
            }
        } else {
            if (mud->selected_item_inventory_index >= 0) {
                sprintf(mud->menu_items[mud->menu_items_count].action_text,
                        "Use %s with:", mud->selected_item_name);

                strcpy(mud->menu_items[mud->menu_items_count].target_text,
                       formatted_item_name);

                mud->menu_items[mud->menu_items_count].type =
                    MENU_USEWITH_INVITEM;
                mud->menu_items[mud->menu_items_count].index = item_index;

                mud->menu_items[mud->menu_items_count].source_index =
                    mud->selected_item_inventory_index;

                mud->menu_items_count++;

                return;
            }

            if (mud->inventory_equipped[item_index] == 1) {
                strcpy(mud->menu_items[mud->menu_items_count].action_text,
                       "Remove");

                strcpy(mud->menu_items[mud->menu_items_count].target_text,
                       formatted_item_name);

                mud->menu_items[mud->menu_items_count].type =
                    MENU_INVENTORY_UNEQUIP;
                mud->menu_items[mud->menu_items_count].index = item_index;
                mud->menu_items_count++;
            } else if (game_data.items[item_id].wearable != 0) {
                int is_wield = (game_data.items[item_id].wearable & 24);

                strcpy(mud->menu_items[mud->menu_items_count].action_text,
                       is_wield ? "Wield" : "Wear");

                strcpy(mud->menu_items[mud->menu_items_count].target_text,
                       formatted_item_name);

                mud->menu_items[mud->menu_items_count].type =
                    MENU_INVENTORY_WEAR;
                mud->menu_items[mud->menu_items_count].index = item_index;
                mud->menu_items_count++;
            }

            // one menu entry per comma-separated command in the item def
            int command_count = game_data_item_command_count(item_id);

            for (int c = 0; c < command_count; c++) {
                char command[64] = {0};

                if (!game_data_item_command_at(item_id, c, command,
                                               (int)sizeof(command))) {
                    break;
                }

                strcpy(mud->menu_items[mud->menu_items_count].action_text,
                       command);

                strcpy(mud->menu_items[mud->menu_items_count].target_text,
                       formatted_item_name);

                mud->menu_items[mud->menu_items_count].type =
                    MENU_INVENTORY_COMMAND;
                mud->menu_items[mud->menu_items_count].index = item_index;
                mud->menu_items[mud->menu_items_count].source_index = c;
                mud->menu_items_count++;
            }

#ifndef REVISION_177
            // "Bury All" on want_drop_x worlds: first command bury, never noted
            if (mud->protocol_custom && mud->orsc.want_drop_x &&
                command_count > 0 && !mudclient_item_noted(mud, item_index)) {
                char first_command[64] = {0};

                if (game_data_item_command_at(item_id, 0, first_command,
                                              (int)sizeof(first_command)) &&
                    strcasecmp(first_command, "bury") == 0) {
                    strcpy(mud->menu_items[mud->menu_items_count].action_text,
                           "Bury All");

                    strcpy(mud->menu_items[mud->menu_items_count].target_text,
                           formatted_item_name);

                    mud->menu_items[mud->menu_items_count].type =
                        MENU_INVENTORY_COMMAND_ALL;
                    mud->menu_items[mud->menu_items_count].index = item_index;
                    mud->menu_items[mud->menu_items_count].source_index = 0;
                    mud->menu_items_count++;
                }
            }
#endif

#ifndef REVISION_177
            // auction sell mode offers "Auction" ahead of use/drop
            if (mud->orsc_auction_sell_mode) {
                strcpy(mud->menu_items[mud->menu_items_count].action_text,
                       "Auction");

                strcpy(mud->menu_items[mud->menu_items_count].target_text,
                       formatted_item_name);

                mud->menu_items[mud->menu_items_count].type =
                    MENU_INVENTORY_AUCTION;
                mud->menu_items[mud->menu_items_count].index = item_id;
                mud->menu_items_count++;
            }
#endif

            strcpy(mud->menu_items[mud->menu_items_count].action_text, "Use");

            strcpy(mud->menu_items[mud->menu_items_count].target_text,
                   formatted_item_name);

            mud->menu_items[mud->menu_items_count].type = MENU_INVENTORY_USE;
            mud->menu_items[mud->menu_items_count].index = item_index;
            mud->menu_items_count++;

            strcpy(mud->menu_items[mud->menu_items_count].action_text, "Drop");

            strcpy(mud->menu_items[mud->menu_items_count].target_text,
                   formatted_item_name);

            mud->menu_items[mud->menu_items_count].type = MENU_INVENTORY_DROP;
            mud->menu_items[mud->menu_items_count].index = item_index;
            mud->menu_items_count++;

            // drop-x lets you drop a chosen quantity of a stack > 1 drop-x only shown on custom worlds that enable it
            int drop_x_ok = mud->options->offer_x && !mud->protocol177;
#ifndef REVISION_177
            if (mud->protocol_custom && !mud->orsc.want_drop_x) {
                drop_x_ok = 0;
            }
#endif
            if (drop_x_ok &&
                game_data.items[item_id].stackable == 0 &&
                mud->inventory_item_stack_count[item_index] > 1) {
                strcpy(mud->menu_items[mud->menu_items_count].action_text,
                       "Drop-X");

                strcpy(mud->menu_items[mud->menu_items_count].target_text,
                       formatted_item_name);

                mud->menu_items[mud->menu_items_count].type =
                    MENU_INVENTORY_DROP_X;
                mud->menu_items[mud->menu_items_count].index = item_index;
                mud->menu_items[mud->menu_items_count].target_index =
                    mud->inventory_item_stack_count[item_index];
                mud->menu_items_count++;
            }

            strcpy(mud->menu_items[mud->menu_items_count].action_text,
                   "Examine");

            strcpy(mud->menu_items[mud->menu_items_count].target_text,
                   formatted_item_name);

            mud->menu_items[mud->menu_items_count].type =
                MENU_INVENTORY_EXAMINE;
            mud->menu_items[mud->menu_items_count].index = item_id;
            mud->menu_items_count++;
        }
    }
}

// drop-x: prompts for an amount then sends it as a trailing int
int mudclient_handle_drop_x(mudclient *mud) {
    if (mud->drop_offer_index < 0 || !mud->show_dialog_offer_x) {
        return 0;
    }

    // the slot may have shifted/emptied since the menu opened; bail safely
    if (mud->drop_offer_index >= mud->inventory_items_count) {
        mud->drop_offer_index = -1;
        mud->show_dialog_offer_x = 0;
        mud->input_digits_final = 0;
        return 0;
    }

    if (mud->input_digits_final > 0) {
        int amount = mud->input_digits_final;

        if (amount > mud->offer_max) {
            amount = mud->offer_max;
        }

        packet_stream_new_packet(mud->packet_stream, CLIENT_INVENTORY_DROP);
        packet_stream_put_short(mud->packet_stream, mud->drop_offer_index);
        packet_stream_put_int(mud->packet_stream, amount);
        packet_stream_send_packet(mud->packet_stream);

        char *item_name = game_data.items[mud->offer_id].name;
        char formatted_drop[64];
        snprintf(formatted_drop, sizeof(formatted_drop), "Dropping %s",
                 item_name);
        mudclient_show_message(mud, formatted_drop, MESSAGE_TYPE_BOR);

        mud->drop_offer_index = -1;
        mud->show_dialog_offer_x = 0;
        mud->input_digits_final = 0;
        return 1;
    }

    mudclient_draw_offer_x(mud);
    mudclient_handle_offer_x_input(mud);

    // cancel resets the dialog without dropping
    if (!mud->show_dialog_offer_x) {
        mud->drop_offer_index = -1;
    }

    return 1;
}
