#include "auction.h"


static void auction_send_simple(mudclient *mud, int action) {
    packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
    packet_stream_put_byte(mud->packet_stream, INTERFACE_OPTION_AUCTION);
    packet_stream_put_byte(mud->packet_stream, action);
    packet_stream_send_packet(mud->packet_stream);
}

static void auction_send_id(mudclient *mud, int action, int auction_id) {
    packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
    packet_stream_put_byte(mud->packet_stream, INTERFACE_OPTION_AUCTION);
    packet_stream_put_byte(mud->packet_stream, action);
    packet_stream_put_int(mud->packet_stream, auction_id);
    packet_stream_send_packet(mud->packet_stream);
}

void mudclient_auction_start_sell(mudclient *mud, int item_id) {
    mud->orsc_auction_create_item = item_id;
    mud->orsc_auction_offer_stage = AUCTION_OFFER_SELL_AMOUNT;
    mud->offer_max = mudclient_get_inventory_count(mud, item_id);
    mud->input_digits_final = 0;
    mud->show_dialog_offer_x = 1;
}

// owns the input dialog during an auction prompt; 1 while active
int mudclient_handle_auction_offer(mudclient *mud) {
    if (mud->orsc_auction_offer_stage == AUCTION_OFFER_NONE ||
        !mud->show_dialog_offer_x) {
        if (mud->orsc_auction_offer_stage != AUCTION_OFFER_NONE) {
            // dialog was cancelled
            mud->orsc_auction_offer_stage = AUCTION_OFFER_NONE;
        }
        return 0;
    }

    if (mud->input_digits_final > 0) {
        int entered = mud->input_digits_final;

        if (entered > mud->offer_max) {
            entered = mud->offer_max;
        }

        switch (mud->orsc_auction_offer_stage) {
        case AUCTION_OFFER_BUY_AMOUNT: {
            int selected = mud->orsc_auction_selected;

            if (selected >= 0 && selected < mud->orsc_auction_count) {
                packet_stream_new_packet(mud->packet_stream,
                                         CLIENT_INTERFACE_OPTIONS);
                packet_stream_put_byte(mud->packet_stream,
                                       INTERFACE_OPTION_AUCTION);
                packet_stream_put_byte(mud->packet_stream, AUCTION_OPTION_BUY);
                packet_stream_put_int(mud->packet_stream,
                                      mud->orsc_auction_ids[selected]);
                packet_stream_put_int(mud->packet_stream, entered);
                packet_stream_send_packet(mud->packet_stream);
            }

            mud->orsc_auction_offer_stage = AUCTION_OFFER_NONE;
            break;
        }
        case AUCTION_OFFER_SELL_AMOUNT:
            mud->orsc_auction_create_amount = entered;
            // chain straight into the price prompt
            mud->orsc_auction_offer_stage = AUCTION_OFFER_SELL_PRICE;
            mud->offer_max = 0x7fffffff;
            mud->input_digits_final = 0;
            mud->show_dialog_offer_x = 1;
            return 1;
        case AUCTION_OFFER_SELL_PRICE:
            packet_stream_new_packet(mud->packet_stream,
                                     CLIENT_INTERFACE_OPTIONS);
            packet_stream_put_byte(mud->packet_stream,
                                   INTERFACE_OPTION_AUCTION);
            packet_stream_put_byte(mud->packet_stream, AUCTION_OPTION_CREATE);
            packet_stream_put_int(mud->packet_stream,
                                  mud->orsc_auction_create_item);
            packet_stream_put_int(mud->packet_stream,
                                  mud->orsc_auction_create_amount);
            packet_stream_put_int(mud->packet_stream, entered);
            packet_stream_send_packet(mud->packet_stream);

            mud->orsc_auction_offer_stage = AUCTION_OFFER_NONE;
            mud->orsc_auction_sell_mode = 0;
            break;
        }

        mud->show_dialog_offer_x = 0;
        mud->input_digits_final = 0;
        return 1;
    }

    mudclient_draw_offer_x(mud);
    mudclient_handle_offer_x_input(mud);

    if (!mud->show_dialog_offer_x) {
        mud->orsc_auction_offer_stage = AUCTION_OFFER_NONE;
    }

    return 1;
}

static void auction_draw_button(mudclient *mud, const char *label, int x,
                                int width, int y, int *hovered_out) {
    int hovered = mud->mouse_x >= x && mud->mouse_x < x + width &&
                  mud->mouse_y >= y && mud->mouse_y < y + 16;

    surface_draw_box_alpha(mud->surface, x, y, width, 15, GREY_98, 160);
    surface_draw_border(mud->surface, x, y, width, 15, BLACK);
    surface_draw_string_centre(mud->surface, label, x + width / 2, y + 11,
                               FONT_BOLD_12, hovered ? YELLOW : BLACK);

    *hovered_out = hovered;
}

void mudclient_draw_auction(mudclient *mud) {
    int dialog_x = mud->surface->width / 2 - AUCTION_DIALOG_WIDTH / 2;
    int dialog_y = mud->surface->height / 2 - AUCTION_DIALOG_HEIGHT / 2;

    surface_draw_box(mud->surface, dialog_x, dialog_y, AUCTION_DIALOG_WIDTH,
                     AUCTION_DIALOG_HEIGHT, BLACK);

    surface_draw_border(mud->surface, dialog_x, dialog_y,
                        AUCTION_DIALOG_WIDTH, AUCTION_DIALOG_HEIGHT, WHITE);

    int centre_x = mud->surface->width / 2;

    surface_draw_string_centre(mud->surface, "Auction House", centre_x,
                               dialog_y + 15, FONT_BOLD_14, YELLOW);

    // scroll with the mouse wheel
    if (mud->mouse_scroll_delta != 0) {
        mud->orsc_auction_scroll -= mud->mouse_scroll_delta;
        mud->mouse_scroll_delta = 0;
    }

    int max_scroll = mud->orsc_auction_count - AUCTION_VISIBLE_ROWS;

    if (max_scroll < 0) {
        max_scroll = 0;
    }

    if (mud->orsc_auction_scroll > max_scroll) {
        mud->orsc_auction_scroll = max_scroll;
    }

    if (mud->orsc_auction_scroll < 0) {
        mud->orsc_auction_scroll = 0;
    }

    int list_y = dialog_y + 26;
    int clicked_row = -1;

    if (mud->orsc_auction_count == 0) {
        surface_draw_string_centre(mud->surface, "No auctions listed",
                                   centre_x, list_y + 40, FONT_BOLD_12, WHITE);
    }

    for (int row = 0; row < AUCTION_VISIBLE_ROWS; row++) {
        int i = mud->orsc_auction_scroll + row;

        if (i >= mud->orsc_auction_count) {
            break;
        }

        int row_y = list_y + row * AUCTION_ROW_HEIGHT;
        int hovered = mud->mouse_x >= dialog_x + 4 &&
                      mud->mouse_x < dialog_x + AUCTION_DIALOG_WIDTH - 4 &&
                      mud->mouse_y >= row_y &&
                      mud->mouse_y < row_y + AUCTION_ROW_HEIGHT;

        if (i == mud->orsc_auction_selected) {
            surface_draw_box_alpha(mud->surface, dialog_x + 4, row_y,
                                   AUCTION_DIALOG_WIDTH - 8,
                                   AUCTION_ROW_HEIGHT, GREY_80, 128);
        }

        int item_id = mud->orsc_auction_item_ids[i];
        char *item_name = item_id >= 0 && item_id < game_data.item_count
                              ? game_data.items[item_id].name
                              : "?";

        // item sprite column, matching the OpenRSC auction rows
        if (item_id >= 0 && item_id < game_data.item_count) {
            mudclient_draw_item(mud, dialog_x + 6, row_y, 20, 13, item_id);
        }

        char left[80] = {0};
        snprintf(left, sizeof(left), "%s%s x%d",
                 mud->orsc_auction_mine[i] ? "@yel@" : "@whi@", item_name,
                 mud->orsc_auction_amounts[i]);
        surface_draw_string(mud->surface, left, dialog_x + 30, row_y + 11,
                            FONT_BOLD_12, hovered ? YELLOW : WHITE);

        char mid[32] = {0};
        snprintf(mid, sizeof(mid), "%dgp", mud->orsc_auction_prices[i]);
        surface_draw_string(mud->surface, mid, dialog_x + 250, row_y + 11,
                            FONT_BOLD_12, GREEN);

        char right[48] = {0};
        snprintf(right, sizeof(right), "%s (%dh)",
                 mud->orsc_auction_sellers[i], mud->orsc_auction_hours[i]);
        surface_draw_string_right(mud->surface, right,
                                  dialog_x + AUCTION_DIALOG_WIDTH - 8,
                                  row_y + 11, FONT_BOLD_12, GREY_D0);

        if (hovered && mud->mouse_button_click != 0) {
            clicked_row = i;
        }
    }

    // action bar
    int button_y = dialog_y + AUCTION_DIALOG_HEIGHT - 22;
    int hover_buy, hover_abort, hover_sell, hover_refresh, hover_close;

    auction_draw_button(mud, "Buy", dialog_x + 8, 80, button_y, &hover_buy);
    auction_draw_button(mud, "Abort", dialog_x + 96, 80, button_y,
                        &hover_abort);
    auction_draw_button(mud, "Sell", dialog_x + 184, 80, button_y,
                        &hover_sell);
    auction_draw_button(mud, "Refresh", dialog_x + 272, 90, button_y,
                        &hover_refresh);
    auction_draw_button(mud, "Close", dialog_x + 370, 90, button_y,
                        &hover_close);

    if (mud->mouse_button_click == 0) {
        return;
    }

    if (clicked_row >= 0) {
        mud->orsc_auction_selected = clicked_row;
    } else if (hover_buy) {
        int selected = mud->orsc_auction_selected;

        if (selected >= 0 && selected < mud->orsc_auction_count &&
            !mud->orsc_auction_mine[selected]) {
            // amount prompt, capped at the listed stack
            mud->orsc_auction_offer_stage = AUCTION_OFFER_BUY_AMOUNT;
            mud->offer_max = mud->orsc_auction_amounts[selected];
            mud->input_digits_final = 0;
            mud->show_dialog_offer_x = 1;
        }
    } else if (hover_abort) {
        int selected = mud->orsc_auction_selected;

        if (selected >= 0 && selected < mud->orsc_auction_count &&
            mud->orsc_auction_mine[selected]) {
            auction_send_id(mud, AUCTION_OPTION_ABORT,
                            mud->orsc_auction_ids[selected]);
        }
    } else if (hover_sell) {
        // hides the panel and arms sell mode until the next list refresh
        mud->orsc_auction_sell_mode = 1;
        mud->orsc_auction_visible = 0;
        mudclient_show_message(
            mud, "@yel@Right-click an inventory item to auction it",
            MESSAGE_TYPE_GAME);
    } else if (hover_refresh) {
        auction_send_simple(mud, AUCTION_OPTION_REFRESH);
    } else if (hover_close) {
        auction_send_simple(mud, AUCTION_OPTION_CLOSE);
        mud->orsc_auction_visible = 0;
        mud->orsc_auction_sell_mode = 0;
    }

    // modal: swallow in-dialog clicks
    if (mud->mouse_x >= dialog_x &&
        mud->mouse_x < dialog_x + AUCTION_DIALOG_WIDTH &&
        mud->mouse_y >= dialog_y &&
        mud->mouse_y < dialog_y + AUCTION_DIALOG_HEIGHT) {
        mud->mouse_button_click = 0;
    }
}
