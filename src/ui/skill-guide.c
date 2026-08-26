#include "skill-guide.h"

#ifndef REVISION_177

// Skill and quest guide dialogs drawn over the game.

#define SKILL_GUIDE_WIDTH 430
#define SKILL_GUIDE_HEIGHT 330
#define SKILL_GUIDE_ROW_HEIGHT 37
#define SKILL_GUIDE_VISIBLE_ROWS 6

#define QUEST_GUIDE_WIDTH 430
#define QUEST_GUIDE_HEIGHT 320
#define QUEST_GUIDE_LINE_HEIGHT 15
#define QUEST_GUIDE_VISIBLE_LINES 17
#define QUEST_GUIDE_MAX_LINES 100

static int skill_guide_visible = 0;
static int skill_guide_skill = -1; // guide_skills index
static int skill_guide_tab = 0;
static int skill_guide_scroll = 0;

static int quest_guide_visible = 0;
static int quest_guide_id = -1;
static int quest_guide_stage = 0;
static char quest_guide_title[ORSC_QUEST_NAME_MAX + 1];
static int quest_guide_scroll = 0;

int mudclient_guides_visible(mudclient *mud) {
    (void)mud;
    return skill_guide_visible || quest_guide_visible;
}

// custom-sprite rows need custom item sprites (custom worlds, SP)
static int mudclient_guide_has_custom_sprites(mudclient *mud) {
#ifdef WITH_SINGLEPLAYER
    if (MUD_SP_WIRE(mud)) {
        return 1;
    }
#endif
    return mud->protocol_custom && mud->orsc.custom_sprites;
}

void mudclient_skill_guide_open(mudclient *mud, const char *skill_name) {
    for (int i = 0; i < guide_skill_count; i++) {
        size_t guide_length = strlen(guide_skills[i].skill);
        size_t name_length = strlen(skill_name);
        size_t compare = guide_length < name_length ? guide_length : name_length;

        // prefix match both ways covers Harvest/Harvesting pairs
        if (strncasecmp(guide_skills[i].skill, skill_name, compare) == 0) {
            skill_guide_skill = i;
            skill_guide_tab = 0;
            skill_guide_scroll = 0;
            skill_guide_visible = 1;
            quest_guide_visible = 0;
            mud->show_ui_tab = 0;
            return;
        }
    }
}

void mudclient_quest_guide_open(mudclient *mud, int quest_id,
                                const char *quest_name, int stage) {
    // custom quests past the guide tables have no walkthrough data
    if (quest_id < 0 || quest_id >= quest_guide_count) {
        return;
    }

    quest_guide_id = quest_id;
    quest_guide_stage = stage;
    strncpy(quest_guide_title, quest_name, sizeof(quest_guide_title) - 1);
    quest_guide_title[sizeof(quest_guide_title) - 1] = '\0';
    quest_guide_scroll = 0;
    quest_guide_visible = 1;
    skill_guide_visible = 0;
    mud->show_ui_tab = 0;
}

// grey box, red when hovered/checked
static int mudclient_guide_button(mudclient *mud, int x, int y, int width,
                                  int height, const char *text, int checked) {
    int hovered = mud->mouse_x >= x && mud->mouse_x <= x + width &&
                  mud->mouse_y >= y && mud->mouse_y <= y + height;

    int background = (checked || hovered) ? 0xFF0000 : 0x333333;

    surface_draw_box_alpha(mud->surface, x, y, width, height, background, 192);
    surface_draw_border(mud->surface, x, y, width, height, 0x242424);
    surface_draw_string_centre(mud->surface, (char *)text, x + (width / 2),
                               y + (height / 2) + 4, FONT_REGULAR_11, WHITE);

    if (hovered && mud->mouse_button_click == 1) {
        mud->mouse_button_click = 0;
        return 1;
    }

    return 0;
}

static void mudclient_draw_skill_guide(mudclient *mud) {
    const GuideSkill *skill = &guide_skills[skill_guide_skill];

    int x = (mud->surface->width - SKILL_GUIDE_WIDTH) / 2;
    int y = (mud->surface->height - SKILL_GUIDE_HEIGHT) / 2;

    surface_draw_box_alpha(mud->surface, x, y, SKILL_GUIDE_WIDTH,
                           SKILL_GUIDE_HEIGHT, 0x989898, 160);
    surface_draw_border(mud->surface, x, y, SKILL_GUIDE_WIDTH,
                        SKILL_GUIDE_HEIGHT, BLACK);

    // tab list minus custom-sprite tabs the session lacks
    int tabs[8];
    int tab_count = 0;

    for (int i = 0; i < skill->tab_count && tab_count < 8; i++) {
        const GuideTab *tab = &guide_tabs[skill->tab_start + i];

        if (tab->custom_only && !mudclient_guide_has_custom_sprites(mud)) {
            continue;
        }

        tabs[tab_count++] = skill->tab_start + i;
    }

    if (skill_guide_tab >= tab_count) {
        skill_guide_tab = 0;
    }

    int large = tab_count > 4;

    surface_draw_string_centre(mud->surface, (char *)skill->skill,
                               x + (SKILL_GUIDE_WIDTH / 2),
                               y + (large ? 20 : 28), FONT_BOLD_16, WHITE);

    if (mudclient_guide_button(mud, x + 394, y + 6, 30, 30, "X", 0)) {
        skill_guide_visible = 0;
        return;
    }

    // tab pickers, 220 - 45*n layout, second row after four
    // tabless skill (Strength) has one unnamed tab, no picker
    if (tab_count > 1 || guide_tabs[tabs[0]].name[0] != '\0') {
        int row_tabs = large ? 4 : tab_count;
        int tab_x = x + 220 - (45 * row_tabs);
        int tab_y = y + (large ? 27 : 45);

        for (int i = 0; i < tab_count; i++) {
            if (i == 4) {
                tab_y += 25;
                tab_x = x + 220 - (45 * (tab_count - i));
            }

            if (mudclient_guide_button(mud, tab_x, tab_y, 75, 20,
                                       guide_tabs[tabs[i]].name,
                                       i == skill_guide_tab)) {
                skill_guide_tab = i;
                skill_guide_scroll = 0;
            }

            tab_x += 85;
        }
    }

    surface_draw_box_alpha(mud->surface, x + 1, y + 82, SKILL_GUIDE_WIDTH - 2,
                           16, 0x6580B7, 192);
    surface_draw_string(mud->surface, "Level", x + 5, y + 94, FONT_BOLD_12,
                        WHITE);
    surface_draw_string(mud->surface, "Advancement", x + 85, y + 94,
                        FONT_BOLD_12, WHITE);

    const GuideTab *tab = &guide_tabs[tabs[skill_guide_tab]];

    // visible rows minus custom-sprite rows the session lacks
    int row_ids[128];
    int row_count = 0;

    for (int i = 0; i < tab->row_count && row_count < 128; i++) {
        const GuideRow *row = &guide_rows[tab->row_start + i];

        if (row->custom_only && !mudclient_guide_has_custom_sprites(mud)) {
            continue;
        }

        row_ids[row_count++] = tab->row_start + i;
    }

    int max_scroll = row_count - SKILL_GUIDE_VISIBLE_ROWS;

    if (max_scroll < 0) {
        max_scroll = 0;
    }

    if (mud->mouse_scroll_delta != 0 && mud->mouse_x >= x &&
        mud->mouse_x <= x + SKILL_GUIDE_WIDTH) {
        skill_guide_scroll += mud->mouse_scroll_delta;
    }

    // native scrollbar: 12px arrow buttons, draggable thumb, wheel
    if (row_count > SKILL_GUIDE_VISIBLE_ROWS) {
        static int8_t thumb_dragged = 0;

        int bar_x = x + SKILL_GUIDE_WIDTH - 18;
        int bar_y = y + 98;
        int bar_h = SKILL_GUIDE_VISIBLE_ROWS * SKILL_GUIDE_ROW_HEIGHT;

        int scrub_height =
            ((bar_h - 27) * SKILL_GUIDE_VISIBLE_ROWS) / row_count;

        if (scrub_height < 6) {
            scrub_height = 6;
        }

        // touch drag reads finger state, not mouse_button_down
        int touch_held = mudclient_is_touch(mud) && mudclient_finger_1_down;
        int pointer_down = touch_held || mud->mouse_button_down == 1;
        int pointer_x = touch_held ? mudclient_finger_1_x : mud->mouse_x;
        int pointer_y = touch_held ? mudclient_finger_1_y : mud->mouse_y;

        if (pointer_down && pointer_x >= bar_x && pointer_x <= bar_x + 12) {
            if (pointer_y > bar_y && pointer_y < bar_y + 12) {
                skill_guide_scroll--;
            }

            if (pointer_y > bar_y + bar_h - 12 && pointer_y < bar_y + bar_h) {
                skill_guide_scroll++;
            }
        }

        if (pointer_down) {
            int on_bar = pointer_x >= bar_x && pointer_x <= bar_x + 12;
            int near_bar = pointer_x >= bar_x - 12 && pointer_x <= bar_x + 24;
            int within_y =
                pointer_y > bar_y + 12 && pointer_y < bar_y + bar_h - 12;

            if ((on_bar || (thumb_dragged && near_bar)) && within_y) {
                thumb_dragged = 1;

                skill_guide_scroll =
                    ((pointer_y - bar_y - 12 - scrub_height / 2) * row_count) /
                    (bar_h - 24);
            }
        } else {
            thumb_dragged = 0;
        }

        if (skill_guide_scroll > max_scroll) {
            skill_guide_scroll = max_scroll;
        }

        if (skill_guide_scroll < 0) {
            skill_guide_scroll = 0;
        }

        int scrub_y = max_scroll > 0
                          ? ((bar_h - 27 - scrub_height) * skill_guide_scroll) /
                                max_scroll
                          : 0;

        surface_draw_scrollbar(mud->surface, bar_x, bar_y, 12, bar_h, scrub_y,
                               scrub_height);
    }

    if (skill_guide_scroll > max_scroll) {
        skill_guide_scroll = max_scroll;
    }

    if (skill_guide_scroll < 0) {
        skill_guide_scroll = 0;
    }

    int row_y = y + 98;

    for (int i = skill_guide_scroll;
         i < row_count && i < skill_guide_scroll + SKILL_GUIDE_VISIBLE_ROWS;
         i++) {
        const GuideRow *row = &guide_rows[row_ids[i]];

        surface_draw_box_alpha(mud->surface, x + 5, row_y,
                               SKILL_GUIDE_WIDTH - 40, SKILL_GUIDE_ROW_HEIGHT,
                               0x454545, 90);

        surface_draw_string(mud->surface, (char *)row->level, x + 10,
                            row_y + 25, FONT_BOLD_12, WHITE);

        if (!row->is_npc) {
            mudclient_draw_item(mud, x + 30, row_y + 2, 48, 32, row->id);
        }

        // step down a font when the detail is too wide for the row
        {
            FontStyle detail_font =
                surface_text_width((char *)row->detail, FONT_BOLD_12) >
                        SKILL_GUIDE_WIDTH - 40 - 90
                    ? FONT_REGULAR_11
                    : FONT_BOLD_12;

            surface_draw_string(mud->surface, (char *)row->detail, x + 90,
                                row_y + 25, detail_font, WHITE);
        }

        row_y += SKILL_GUIDE_ROW_HEIGHT;
    }
}

// wrap a long line at the last space that fits the window
static int mudclient_quest_guide_add(char lines[][160], int line_count,
                                     const char *text) {
    if (line_count >= QUEST_GUIDE_MAX_LINES) {
        return line_count;
    }

    size_t length = strlen(text);

    if (length < 76 ||
        surface_text_width((char *)text, FONT_BOLD_12) <=
            QUEST_GUIDE_WIDTH - 16) {
        snprintf(lines[line_count], 160, "%s", text);
        return line_count + 1;
    }

    size_t split = 75 < length ? 75 : length - 1;

    while (split > 0 && text[split] != ' ') {
        split--;
    }

    if (split == 0) {
        snprintf(lines[line_count], 160, "%s", text);
        return line_count + 1;
    }

    snprintf(lines[line_count], 160, "%.*s", (int)split, text);
    line_count++;

    if (line_count < QUEST_GUIDE_MAX_LINES) {
        snprintf(lines[line_count], 160, "%s", text + split + 1);
        line_count++;
    }

    return line_count;
}

static void mudclient_draw_quest_guide(mudclient *mud) {
    int x = (mud->surface->width - QUEST_GUIDE_WIDTH) / 2;
    int y = (mud->surface->height - QUEST_GUIDE_HEIGHT) / 2;

    surface_draw_box_alpha(mud->surface, x, y, QUEST_GUIDE_WIDTH,
                           QUEST_GUIDE_HEIGHT, 0x989898, 160);
    surface_draw_border(mud->surface, x, y, QUEST_GUIDE_WIDTH,
                        QUEST_GUIDE_HEIGHT, BLACK);

    surface_draw_string_centre(mud->surface, quest_guide_title,
                               x + (QUEST_GUIDE_WIDTH / 2), y + 28,
                               FONT_BOLD_16, WHITE);

    if (mudclient_guide_button(mud, x + 394, y + 6, 30, 30, "X", 0)) {
        quest_guide_visible = 0;
        return;
    }

    surface_draw_line_horizontal(mud->surface, x, y + 35, QUEST_GUIDE_WIDTH,
                                 BLACK);

    // body by quest stage: start + requirements, placeholder, or congrats
    // rewards always shown
    char lines[QUEST_GUIDE_MAX_LINES][160];
    int line_count = 0;
    char formatted[256];

    if (quest_guide_stage == 0) {
        snprintf(formatted, sizeof(formatted),
                 "I can start the quest by speaking to %s %s.",
                 quest_guide_whos[quest_guide_id],
                 quest_guide_wheres[quest_guide_id]);
        line_count = mudclient_quest_guide_add(lines, line_count, formatted);

        line_count = mudclient_quest_guide_add(lines, line_count, "");
        line_count =
            mudclient_quest_guide_add(lines, line_count, "Requirements: ");

        for (int i = 0; i < quest_guide_requirements_index[quest_guide_id][1];
             i++) {
            snprintf(
                formatted, sizeof(formatted), "  - %s",
                quest_guide_requirements[quest_guide_requirements_index
                                             [quest_guide_id][0] +
                                         i]);
            line_count =
                mudclient_quest_guide_add(lines, line_count, formatted);
        }
    } else if (quest_guide_stage > 0) {
        line_count = mudclient_quest_guide_add(lines, line_count,
                                               "Quest progress coming soon...");
    } else {
        snprintf(formatted, sizeof(formatted),
                 "Congratulations you have completed %s.", quest_guide_title);
        line_count = mudclient_quest_guide_add(lines, line_count, formatted);
    }

    line_count = mudclient_quest_guide_add(lines, line_count, "");
    line_count = mudclient_quest_guide_add(lines, line_count, "Rewards: ");

    for (int i = 0; i < quest_guide_rewards_index[quest_guide_id][1]; i++) {
        snprintf(formatted, sizeof(formatted), "  - %s",
                 quest_guide_rewards[quest_guide_rewards_index[quest_guide_id]
                                                              [0] +
                                     i]);
        line_count = mudclient_quest_guide_add(lines, line_count, formatted);
    }

    int max_scroll = line_count - QUEST_GUIDE_VISIBLE_LINES;

    if (max_scroll < 0) {
        max_scroll = 0;
    }

    if (mud->mouse_scroll_delta != 0 && mud->mouse_x >= x &&
        mud->mouse_x <= x + QUEST_GUIDE_WIDTH) {
        quest_guide_scroll += mud->mouse_scroll_delta;
    }

    if (line_count > QUEST_GUIDE_VISIBLE_LINES) {
        if (mudclient_guide_button(mud, x + 400, y + 44, 24, 24, "up", 0)) {
            quest_guide_scroll--;
        }

        if (mudclient_guide_button(mud, x + 400, y + QUEST_GUIDE_HEIGHT - 30,
                                   24, 24, "dn", 0)) {
            quest_guide_scroll++;
        }
    }

    if (quest_guide_scroll > max_scroll) {
        quest_guide_scroll = max_scroll;
    }

    if (quest_guide_scroll < 0) {
        quest_guide_scroll = 0;
    }

    int line_y = y + 55;

    for (int i = quest_guide_scroll;
         i < line_count && i < quest_guide_scroll + QUEST_GUIDE_VISIBLE_LINES;
         i++) {
        surface_draw_string(mud->surface, lines[i], x + 8, line_y,
                            FONT_BOLD_12, WHITE);
        line_y += QUEST_GUIDE_LINE_HEIGHT;
    }

    // bottom progress line
    const char *progress = quest_guide_stage == 0
                               ? "Not started"
                               : quest_guide_stage < 0 ? "Completed"
                                                       : "In progress";

    surface_draw_stringf(mud->surface, x + QUEST_GUIDE_WIDTH - 150,
                         y + QUEST_GUIDE_HEIGHT - 10, FONT_BOLD_12, WHITE,
                         "Progress: %s", progress);
}

void mudclient_draw_guides(mudclient *mud) {
    if (!skill_guide_visible && !quest_guide_visible) {
        return;
    }

    int x = (mud->surface->width - SKILL_GUIDE_WIDTH) / 2;
    int y = (mud->surface->height - SKILL_GUIDE_HEIGHT) / 2;

    if (skill_guide_visible && skill_guide_skill >= 0) {
        mudclient_draw_skill_guide(mud);
    } else if (quest_guide_visible && quest_guide_id >= 0) {
        mudclient_draw_quest_guide(mud);
    }

    // click outside closes the window; neither reaches the game
    if (mud->mouse_button_click == 1) {
        if (mud->mouse_x < x || mud->mouse_x > x + SKILL_GUIDE_WIDTH ||
            mud->mouse_y < y || mud->mouse_y > y + SKILL_GUIDE_HEIGHT) {
            skill_guide_visible = 0;
            quest_guide_visible = 0;
        }

        mud->mouse_button_click = 0;
    }
}

#endif
