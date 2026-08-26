#include "./experience-drops.h"

void mudclient_drop_experience(mudclient *mud, int skill_index,
                               int experience) {
    if (!mud->options->experience_drops ||
        mud->experience_drop_count >= EXPERIENCE_DROPS_MAX) {
        return;
    }

    // online, xp drops exist only on custom worlds with config 19 on; authentic worlds never show them
#ifdef WITH_SINGLEPLAYER
    int session_has_drops = MUD_SP_WIRE(mud);
#else
    int session_has_drops = 0;
#endif
    if (!session_has_drops &&
        !(mud->protocol_custom && mud->orsc.experience_drops_toggle)) {
        return;
    }

    // xp-drop text starts a quarter of the way down the real surface height
#if defined(__vita__) && defined(RENDER_GL)
    int max_y = mud->surface->height / 4;
#else
    int max_y = MUD_HEIGHT / 4;
#endif

    mud->experience_drop_y[mud->experience_drop_count] =
        max_y + (mud->experience_drop_count * 16);

    mud->experience_drop_skill[mud->experience_drop_count] = skill_index;
    mud->experience_drop_amount[mud->experience_drop_count] = experience;
    mud->experience_drop_speed[mud->experience_drop_count] = -30;
    mud->experience_drop_count++;
}

void mudclient_draw_experience_drops(mudclient *mud) {
    int x = mud->surface->width / 2;

    for (int i = 0; i < mud->experience_drop_count; i++) {
        int drop_y = (int)mud->experience_drop_y[i];
        int experience = mud->experience_drop_amount[i] / 4;

        char formatted_remainder[5] = {0};

        sprintf(formatted_remainder, "%.2f",
                (mud->experience_drop_amount[i] % 4) / 4.0f);

        char formatted_amount[15] = {0};
        mudclient_format_number_commas(mud, experience, formatted_amount);

        // bounds skill index to skill_names size, not the wire's larger max
        int drop_skill = mud->experience_drop_skill[i];
        if (drop_skill < 0 || drop_skill >= SKILL_NAMES_COUNT) {
            continue;
        }
        const char *skill_name = skill_names[drop_skill];

        size_t n = strlen(skill_name) + strlen(formatted_amount) +
                   strlen(formatted_remainder);
        char formatted_drop[n + 5];

        sprintf(formatted_drop, "%s%s %s XP", formatted_amount,
                formatted_remainder + 1, skill_name);

        surface_draw_string_centre(mud->surface, formatted_drop, x, drop_y, 1,
                                   WHITE);

        if (mud->experience_drop_speed[i] > 0) {
            mud->experience_drop_y[i] -= mud->experience_drop_speed[i];
        }

        mud->experience_drop_speed[i] += 0.5f;
    }

    int is_drop_visible = 0;

    for (int i = 0; i < mud->experience_drop_count; i++) {
        if ((int)mud->experience_drop_y[i] > 0) {
            is_drop_visible = 1;
        }
    }

    if (!is_drop_visible) {
        mud->experience_drop_count = 0;
    }
}

#ifndef REVISION_177
// the top-centre xp box. visibility (account-synced on custom worlds): 0 never, 1 recent (only while an xp drop
// is on screen), 2 always. shows "Skill: level: xp" over the green/red progress strip, or "Total: xp" before any
// gain; the details block adds Next lvl / Til lvl / Gained / Xp per hr while the cursor rests on the box
void mudclient_draw_experience_counter(mudclient *mud) {
    int state;

    if (mud->protocol_custom) {
        if (!mud->orsc.experience_counter_toggle ||
            !mud->options->xp_counter) {
            return;
        }

        state = mud->orsc_xp_counter_synced;
    } else {
#ifdef WITH_SINGLEPLAYER
        if (!MUD_SP_WIRE(mud)) {
            return;
        }
#else
        return;
#endif
        state = mud->options->xp_counter ? 1 : 0;
    }

    if (state <= 0) {
        return;
    }

    if (state == 1 && mud->experience_drop_count <= 0) {
        return;
    }

    int skill = mud->xp_last_gain_skill;
    char text[96];

    if (skill >= 0 && skill < SKILL_NAMES_COUNT &&
        mud->player_skill_base[skill] > 0) {
        sprintf(text, "%s: %d: %d", skill_names[skill],
                mud->player_skill_base[skill],
                mud->player_experience[skill] / 4);
    } else {
        skill = -1;
        int64_t total_xp = 0;

        for (int i = 0; i < PLAYER_SKILL_MAX; i++) {
            total_xp += (int64_t)mud->player_experience[i];
        }

        sprintf(text, "Total: %lld", (long long)(total_xp / 4));
    }

    int text_width = surface_text_width(text, FONT_BOLD_13);
    int x = (mud->surface->width / 2) - (text_width / 2) - 10;
    int width = text_width + 6;

    surface_draw_box_alpha(mud->surface, x, 0, width, 20, 0x989898, 90);
    surface_draw_string(mud->surface, text,
                        (mud->surface->width / 2) - (text_width / 2) - 4, 15,
                        FONT_BOLD_13, WHITE);

    // level-progress strip: green done, red remaining; the ratio uses raw server units so the /4 scale cancels,
    // and the /0.9 is the reference formula
    if (skill >= 0 && mud->player_skill_base[skill] < 99 &&
        mud->player_skill_base[skill] >= 1) {
        int base_level = mud->player_skill_base[skill];
        int til_level =
            experience_array[base_level - 1] - mud->player_experience[skill];
        int base_til_level =
            experience_array[base_level] - experience_array[base_level - 1];

        if (til_level >= 0 && base_til_level > 0) {
            double progress =
                ((double)til_level) / ((double)base_til_level) / 0.9;
            int progress_width = (int)(progress * width);

            if (progress_width < 0) {
                progress_width = 0;
            } else if (progress_width > width) {
                progress_width = width;
            }

            surface_draw_box(mud->surface, x, 19, width - progress_width, 2,
                             0x00FF00);
            surface_draw_box(mud->surface, x + width - progress_width, 19,
                             progress_width, 2, 0xFF0000);
        }
    }

    // the hover details block
    if (mud->options->xp_counter_details && mud->mouse_x >= x &&
        mud->mouse_x <= x + width && mud->mouse_y >= 0 && mud->mouse_y <= 20) {
        uint64_t now = get_ticks();
        int64_t gained;
        uint64_t since_ms;

        if (skill >= 0) {
            gained = mud->player_experience_gained[skill];
            since_ms = mud->xp_gain_start_ms[skill];
        } else {
            gained = mud->xp_gained_total;
            since_ms = mud->xp_gained_total_start_ms;
        }

        int64_t xp_per_hour = 0;

        if (gained > 0 && since_ms > 0 && now > since_ms + 1000) {
            xp_per_hour =
                (int64_t)((gained / 4.0) / ((now - since_ms) / 3600000.0));
        }

        int details_y = 19;
        int details_height = skill >= 0 ? 61 : 31;

        surface_draw_box_alpha(mud->surface, x, details_y, width,
                               details_height, 0x989898, 90);

        int line_y = details_y + 12;

        if (skill >= 0 && mud->player_skill_base[skill] >= 1 &&
            mud->player_skill_base[skill] < 99) {
            surface_draw_stringf(
                mud->surface, x + 3, line_y, FONT_BOLD_13, WHITE, "Next lvl: %d",
                experience_array[mud->player_skill_base[skill] - 1] / 4);
            line_y += 15;
            surface_draw_stringf(
                mud->surface, x + 3, line_y, FONT_BOLD_13, WHITE, "Til lvl: %d",
                (experience_array[mud->player_skill_base[skill] - 1] -
                 mud->player_experience[skill]) /
                    4);
            line_y += 15;
        }

        surface_draw_stringf(mud->surface, x + 3, line_y, FONT_BOLD_13, WHITE,
                             "Gained: %lld", (long long)(gained / 4));
        line_y += 15;
        surface_draw_stringf(mud->surface, x + 3, line_y, FONT_BOLD_13, WHITE,
                             "Xp/hr: %lld", (long long)xp_per_hour);
    }
}
#endif
