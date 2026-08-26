#include "bank-pin.h"


static const char *BANK_PIN_STAGE_TEXT[4] = {
    "First click the FIRST digit.", "Now click the SECOND digit.",
    "Time for the THIRD digit.", "Finally, the FOURTH digit."};

static void bank_pin_send(mudclient *mud, int action, const char *text) {
    packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
    packet_stream_put_byte(mud->packet_stream, INTERFACE_OPTION_BANK_PIN);
    packet_stream_put_byte(mud->packet_stream, action);

    if (text != NULL) {
        packet_stream_put_string_newline(mud->packet_stream, (char *)text);
    }

    packet_stream_send_packet(mud->packet_stream);
}

void mudclient_draw_bank_pin(mudclient *mud) {
    int panel_x = mud->surface->width / 2 - 150;
    int panel_y = mud->surface->height / 2 - 125;

    surface_draw_box(mud->surface, panel_x, panel_y, 300, 250, 0x483E33);
    surface_draw_border(mud->surface, panel_x, panel_y, 300, 250, 0x4E4836);

    // title bar
    surface_draw_border(mud->surface, panel_x, panel_y, 300, 25, 0x4E4836);
    surface_draw_string(mud->surface, "Bank of RuneScape", panel_x + 3,
                        panel_y + 18, FONT_BOLD_13, 0x9B0907);

    char digit_mask[8] = {0};

    for (int i = 0; i < BANK_PIN_DIGITS; i++) {
        digit_mask[i * 2] = i < mud->bank_pin_length ? '*' : '?';
        digit_mask[i * 2 + 1] = ' ';
    }

    surface_draw_string(mud->surface, digit_mask, panel_x + 243, panel_y + 18,
                        FONT_BOLD_13, 0xBF751D);

    // prompt lines (content box origin = panel + (15, 25))
    int content_x = panel_x + 15;
    int content_y = panel_y + 25;

    surface_draw_string_centre(mud->surface,
                               "Please enter your PIN using the buttons below.",
                               content_x + 135, content_y + 12, FONT_BOLD_12,
                               0xFF981F);

    int stage = mud->bank_pin_length < 4 ? mud->bank_pin_length : 3;

    surface_draw_string_centre(mud->surface, BANK_PIN_STAGE_TEXT[stage],
                               content_x + 135, content_y + 28, FONT_BOLD_12,
                               WHITE);

    // digits 0..9, 50x50 in rows of four with 23px gaps, from (0, 38)
    int clicked_digit = -1;
    int number_x = 0;
    int number_y = 38;

    for (int number = 0; number < 10; number++) {
        int box_x = content_x + number_x + 1;
        int box_y = content_y + number_y;

        int hovered = mud->mouse_x >= box_x && mud->mouse_x < box_x + 50 &&
                      mud->mouse_y >= box_y && mud->mouse_y < box_y + 50;

        surface_draw_box(mud->surface, box_x, box_y, 50, 50,
                         hovered ? 0x63140B : 0x4C0E09);
        surface_draw_border(mud->surface, box_x, box_y, 50, 50, 0xAB837F);

        char digit_label[2] = {(char)('0' + number), '\0'};

        surface_draw_string_centre(mud->surface, digit_label, box_x + 25,
                                   box_y + 32, FONT_BOLD_20, 0xFF981F);

        if (hovered && mud->mouse_button_click != 0) {
            clicked_digit = number;
        }

        number_x += 50 + 23;

        if (number_x + 50 > 285) {
            number_y += 50 + 15;
            number_x = 0;
        }
    }

    // alternative box: Exit + the decorative "I don't know it"
    int alt_x = panel_x + 162;
    int alt_y = panel_y + 193;

    surface_draw_box(mud->surface, alt_x, alt_y, 123, 50, 0x524B31);
    surface_draw_border(mud->surface, alt_x, alt_y, 123, 50, 0x565040);

    int exit_hovered = mud->mouse_x >= alt_x &&
                       mud->mouse_x < alt_x + 123 &&
                       mud->mouse_y >= alt_y + 2 && mud->mouse_y < alt_y + 17;

    surface_draw_string_centre(mud->surface, "Exit", alt_x + 61, alt_y + 13,
                               FONT_BOLD_12,
                               exit_hovered ? 0xFF981F : 0xBF751D);

    // draws but does nothing (stub)
    surface_draw_string_centre(mud->surface, "I don't know it", alt_x + 61,
                               alt_y + 37, FONT_BOLD_12, 0xBF751D);

    if (mud->mouse_button_click == 0) {
        return;
    }

    if (clicked_digit >= 0) {
        mud->mouse_button_click = 0;

        if (mud->bank_pin_length < BANK_PIN_DIGITS) {
            mud->bank_pin_input[mud->bank_pin_length++] =
                (char)('0' + clicked_digit);
            mud->bank_pin_input[mud->bank_pin_length] = '\0';

            if (mud->bank_pin_length == BANK_PIN_DIGITS) {
                // OpenRSC flow: submit and hide immediately
                bank_pin_send(mud, 0, mud->bank_pin_input);
                mud->show_dialog_bank_pin = 0;
                mud->bank_pin_length = 0;
                mud->bank_pin_input[0] = '\0';
            }
        }
    } else if (exit_hovered) {
        mud->mouse_button_click = 0;

        bank_pin_send(mud, 1, "cancel");
        mud->show_dialog_bank_pin = 0;
        mud->bank_pin_length = 0;
        mud->bank_pin_input[0] = '\0';
    } else if (mud->mouse_x >= panel_x && mud->mouse_x < panel_x + 300 &&
               mud->mouse_y >= panel_y && mud->mouse_y < panel_y + 250) {
        // modal: the OpenRSC root component swallows every in-panel click
        mud->mouse_button_click = 0;
    }
}
