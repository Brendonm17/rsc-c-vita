#include "kill-feed.h"

// kill feed row: killer, weapon icon, victim; newest first, 8s ttl, max 10

void mudclient_draw_kill_feed(mudclient *mud) {
    int anchor_right = mudclient_is_touch(mud)
                           ? UI_TABS_TOUCH_X - 8
                           : mud->surface->width - 5;

    int offset = 0;

    for (int i = 0; i < mud->orsc_kill_feed_count; i++) {
        int picture_width = 20;

        int victim_x = anchor_right -
                       surface_text_width(mud->orsc_kill_feed[i].victim,
                                          FONT_BOLD_12);
        int icon_x = victim_x - picture_width - 5;
        int killer_x = icon_x - 3 -
                       surface_text_width(mud->orsc_kill_feed[i].killer,
                                          FONT_BOLD_12);

        surface_draw_string(mud->surface, mud->orsc_kill_feed[i].killer,
                            killer_x, 50 + offset, FONT_BOLD_12, WHITE);

        if (mud->orsc_kill_feed[i].kill_type >= 0 &&
            mud->orsc_kill_feed[i].kill_type < game_data.item_count) {
            mudclient_draw_item(mud, icon_x, 36 + offset, picture_width, 18,
                                mud->orsc_kill_feed[i].kill_type);
        }

        surface_draw_string(mud->surface, mud->orsc_kill_feed[i].victim,
                            victim_x, 50 + offset, FONT_BOLD_12, WHITE);

        offset += 16;
    }
}
