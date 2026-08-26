#include "progress-bar.h"

// batching progress panel: bar shows completed/total, cancel sends cancel_batch

void mudclient_draw_progress_bar(mudclient *mud) {
    if (mud->orsc_progress_total <= 0) {
        return;
    }

    int panel_x = (mud->surface->width - 138) / 2;
    int panel_y = mud->surface->height - 100;

    surface_draw_box_alpha(mud->surface, panel_x, panel_y, 138, 59, WHITE,
                           128);

    // header
    surface_draw_box_alpha(mud->surface, panel_x, panel_y, 138, 19, BLACK,
                           156);
    surface_draw_string_centre(mud->surface, "Batching", panel_x + 69,
                               panel_y + 13, FONT_BOLD_12, WHITE);

    // the bar itself at panel-relative (9, 24)
    int bar_x = panel_x + 9;
    int bar_y = panel_y + 24;

    surface_draw_box_alpha(mud->surface, bar_x - 2, bar_y - 2, 124, 14, BLACK,
                           128);
    surface_draw_box_alpha(mud->surface, bar_x, bar_y, 120, 10, WHITE, 125);

    int fill_width =
        (mud->orsc_progress_current * 120) / mud->orsc_progress_total;

    if (fill_width > 120) {
        fill_width = 120;
    }

    if (fill_width > 0) {
        surface_draw_box_alpha(mud->surface, bar_x, bar_y, fill_width - 1, 10,
                               0x0000ff, 200);
    }

    char progress_label[24] = {0};
    snprintf(progress_label, sizeof(progress_label), "%d/%d",
             mud->orsc_progress_current, mud->orsc_progress_total);
    surface_draw_string_centre(mud->surface, progress_label, bar_x + 60,
                               bar_y + 9, FONT_REGULAR_11, WHITE);

    // Cancel button at panel-relative (31, 39), 75x16
    int cancel_x = panel_x + 31;
    int cancel_y = panel_y + 39;

    int cancel_hovered = mud->mouse_x >= cancel_x &&
                         mud->mouse_x < cancel_x + 75 &&
                         mud->mouse_y >= cancel_y &&
                         mud->mouse_y < cancel_y + 16;

    surface_draw_box_alpha(mud->surface, cancel_x, cancel_y, 75, 16, 0x454545,
                           128);
    surface_draw_border(mud->surface, cancel_x, cancel_y, 75, 16, WHITE);
    surface_draw_string_centre(mud->surface, "Cancel", cancel_x + 37,
                               cancel_y + 12, FONT_REGULAR_11,
                               cancel_hovered ? RED : WHITE);

    if (cancel_hovered && mud->mouse_button_click != 0) {
        mud->mouse_button_click = 0;

        // cancel resets locally and sends cancel; server confirms
        mud->orsc_progress_visible = 0;

        packet_stream_new_packet(mud->packet_stream, CLIENT_INTERFACE_OPTIONS);
        packet_stream_put_byte(mud->packet_stream,
                               INTERFACE_OPTION_CANCEL_BATCH);
        packet_stream_send_packet(mud->packet_stream);
    }
}
