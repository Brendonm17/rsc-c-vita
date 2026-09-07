#include "bot-manager.h"

#ifdef WITH_SINGLEPLAYER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "../singleplayer.h"
#include "appearance.h" // character-appearance panel, reused for a bot's look
#include "worldlist.h" // worldlist_bots_appearance_accept

// Bot Manager: 720x480 popup over the world editor; left = roster list + buttons,
// right = fixed header over a scrolling settings region (behaviour, levels, personality, appearance)

#define BM_W 720
#define BM_H 480
#define BM_ROW_H 22
#define BM_MAX_ROWS 72

// settings region, relative to the popup's top-left
#define BM_RX0 248
#define BM_RY0 74
#define BM_RX1 708
#define BM_RY1 452
#define BM_BAR_W 12
// disabled bot's list colour: silver, so the red selection stands out
#define BM_OFF_COLOUR "@sil@"

enum {
    BM_R_HEADER,
    BM_R_CYCLER,
    BM_R_TOGGLE,
    BM_R_LEVEL,
    BM_R_SLIDER,
    BM_R_BUTTON,
    BM_R_SETALL,
    BM_R_GAP
};

enum { BM_A_NONE, BM_A_ARCH, BM_A_APPEARANCE };

struct bm_row {
    int type;
    const char *label;
    int *value;
    int count;
    const char *(*name)(int);
    int skill;
    int action;
};

static Panel *bm_panel = NULL;
static int bm_shown = 0;
static char bm_world_id[32] = "";
static struct sp_bot_def bm_defs[SP_BOT_MAX];
static int bm_count = 0;
// bots' live levels from their save records: what they have now, vs the def's starting levels
static int bm_live[SP_BOT_MAX][SP_BOT_SKILL_COUNT];
static int bm_live_found[SP_BOT_MAX];
static int bm_selected = -1;
static int bm_delete_armed = -1;
static int bm_appearance_active = 0;

static int bm_list, bm_header, bm_subtitle, bm_new, bm_delete, bm_delete_txt,
    bm_all_on, bm_all_off, bm_auto, bm_save, bm_cancel, bm_name;

static struct bm_row bm_rows[BM_MAX_ROWS];
static int bm_row_count = 0;

// scroll state of the settings region, in whole rows (a clipped quad is the slow GL path)
static int bm_scroll = 0;
static int bm_drag_carry = 0;
static int8_t bm_thumb_dragged = 0;
static int bm_drag_active = 0;
static int bm_drag_last_y = 0;
static int bm_drag_accum = 0;
static int bm_drag_moved_ttl = 0;

// Auto-create popup
static Panel *au_panel = NULL;
static int au_shown = 0;
static int au_count = 10;
static int au_arch = 0; // 0 = random mix, 1.. = a preset
static int au_levels = 0; // index into AU_LEVEL_NAMES
static int au_paced = 0;
static int au_enabled = 1;
static int au_count_prev, au_count_val, au_count_next, au_arch_prev, au_arch_val,
    au_arch_next, au_lvl_prev, au_lvl_val, au_lvl_next, au_paced_txt,
    au_paced_btn, au_enabled_txt, au_enabled_btn, au_note, au_create, au_cancel;
static const int AU_STEPS[] = {1, 3, 5, 10, 15, 20, 30, 50, 75, 100};
#define AU_STEP_COUNT ((int)(sizeof(AU_STEPS) / sizeof(AU_STEPS[0])))
static const char *const AU_LEVEL_NAMES[] = {"Fresh (1)",   "Low (5-20)",
                                             "Medium (25-50)", "High (55-80)",
                                             "Max (99)",     "Random (1-99)"};
#define AU_LEVEL_COUNT 6

static const char *const BM_SKILL_LABELS[SP_BOT_SKILL_COUNT] = {
    "Attack",   "Defence",     "Strength",  "Hits",     "Ranged",
    "Prayer",   "Magic",       "Cooking",   "Woodcut",  "Fletching",
    "Fishing",  "Firemaking",  "Crafting",  "Smithing", "Mining",
    "Herblaw",  "Agility",     "Thieving",  "Runecraft", "Harvesting"};

// attack, defense, strength, hits, ranged, prayer, magic
static int bm_skill_is_combat(int i) { return i <= 6; }
static int bm_skill_default(int i) { return i == 3 ? 10 : 1; }

static const char *const BM_SLIDER_NAMES[7] = {"Aggression", "Risk",
                                               "Greed",      "Sociability",
                                               "Diligence",  "Curiosity",
                                               "Patience"};

static int *bm_slider_field(struct sp_bot_def *d, int i) {
    switch (i) {
    case 0: return &d->aggression;
    case 1: return &d->risk;
    case 2: return &d->greed;
    case 3: return &d->sociability;
    case 4: return &d->diligence;
    case 5: return &d->curiosity;
    default: return &d->patience;
    }
}

static struct sp_bot_def *bm_def(void) {
    return (bm_selected >= 0 && bm_selected < bm_count) ? &bm_defs[bm_selected]
                                                        : NULL;
}

// level shown for a skill: what you set, else what the bot has now, else the default
static int bm_eff_level(const struct sp_bot_def *d, int index, int i) {
    if (d->levels[i] > 0) {
        return d->levels[i];
    }
    if (index >= 0 && bm_live_found[index] && bm_live[index][i] > 0) {
        return bm_live[index][i];
    }
    return bm_skill_default(i);
}

// Player.getCombatLevel: (attack + defence + strength + hits) / 4
static int bm_live_combat_level(int index) {
    const int *l = bm_live[index];
    return (l[0] + l[1] + l[2] + l[3]) / 4;
}

// random bots (Auto-create)

static int bm_seeded = 0;
static void bm_seed(void) {
    if (!bm_seeded) {
        srand((unsigned)time(NULL));
        bm_seeded = 1;
    }
}

// readable RSC-ish name from gendered syllable pools, letters only, max 12 chars
static void bm_gen_name(char *out, int cap, int gender) {
    static const char *const male_pre[] = {
        "thor", "grim", "mor",  "bran", "dra",  "fen",  "gor",  "hal",  "kel",
        "lys",  "mal",  "nyx",  "orl",  "perr", "quen", "ryn",  "syl",  "tor",
        "vex",  "wyn",  "zar",  "ald",  "bor",  "cael", "dun",  "eld",  "far",
        "gwyn", "hex",  "irk",  "jor",  "krae", "lun",  "mux",  "oz",   "pyr",
        "rusk", "sten", "urd",  "vael", "bald", "cor",  "dag",  "erik", "gar",
        "harl", "ivar", "keld", "ragn", "ulf"};
    static const char *const male_suf[] = {
        "dor", "gar",  "wyn",  "ric",  "mund", "ling", "fax", "born", "grim",
        "thas", "vek", "rok",  "dain", "lith", "nar",  "vos", "wick", "fell",
        "gast", "mir", "ade",  "eth",  "in",   "or",   "us",  "an",   "ell",
        "ish",  "oth", "ux",   "ulf",  "wald", "brand", "helm"};
    static const char *const female_pre[] = {
        "ael",  "bel",  "cor",  "del",  "eli",  "fay",  "gwen", "isa",  "jen",
        "kira", "lia",  "mira", "nia",  "ora",  "ros",  "sera", "tara", "vera",
        "wren", "yara", "ada",  "bryn", "cass", "dara", "elo",  "fia",  "gala",
        "hild", "ines", "jade", "kat",  "lin",  "mae",  "nell", "oria", "pip",
        "rhi",  "sian", "tess", "una",  "vi",   "wil",  "xen",  "yse",  "zia",
        "ama",  "bea",  "cleo", "dove", "essa"};
    static const char *const female_suf[] = {
        "a",   "ia",  "ina", "ette", "elle", "lyn", "wen", "ara", "issa", "ora",
        "ith", "iel", "wyn", "eth",  "anna", "ella", "ley", "ie", "ynn",  "ena",
        "ika", "ola", "ris", "una",  "ave",  "ine",  "isa", "iah", "ess", "ith",
        "ah",  "y",   "ea",  "ie"};
    static const char *const mid[] = {"a", "e", "i", "o", "ar", "en", "il", "or"};
    const char *const *pre = gender == 2 ? female_pre : male_pre;
    const char *const *suf = gender == 2 ? female_suf : male_suf;
    const int npre = gender == 2 ? (int)(sizeof(female_pre) / sizeof(female_pre[0]))
                                 : (int)(sizeof(male_pre) / sizeof(male_pre[0]));
    const int nsuf = gender == 2 ? (int)(sizeof(female_suf) / sizeof(female_suf[0]))
                                 : (int)(sizeof(male_suf) / sizeof(male_suf[0]));
    const int nmid = (int)(sizeof(mid) / sizeof(mid[0]));
    char buf[40];
    const char *p = pre[rand() % npre];
    const char *s = suf[rand() % nsuf];
    const char *m = "";
    if (rand() % 3 == 0) {
        const char *cand = mid[rand() % nmid];
        if (cand[0] != p[strlen(p) - 1] && cand[strlen(cand) - 1] != s[0]) {
            m = cand;
        }
    }
    snprintf(buf, sizeof(buf), "%s%s%s", p, m, s);
    if (buf[0] >= 'a' && buf[0] <= 'z') {
        buf[0] = (char)(buf[0] - 'a' + 'A');
    }
    for (int i = 1; buf[i] != '\0'; i++) {
        if (buf[i] == buf[i - 1] && buf[i] != 'l' && buf[i] != 's' &&
            buf[i] != 'n' && buf[i] != 'r' && buf[i] != 't') {
            memmove(&buf[i], &buf[i + 1], strlen(&buf[i + 1]) + 1);
            i--;
        }
    }
    buf[12] = '\0';
    snprintf(out, cap, "%s", buf);
}

static int bm_name_taken(const char *name, int count, int skip) {
    for (int i = 0; i < count; i++) {
        if (i != skip && strcasecmp(bm_defs[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void bm_gen_unique_name(char *out, int cap, int count, int gender) {
    for (int tries = 0; tries < 40; tries++) {
        bm_gen_name(out, cap, gender);
        if (!bm_name_taken(out, count, -1)) {
            return;
        }
    }
    for (int n = 1;; n++) {
        char cand[16];
        snprintf(cand, sizeof(cand), "Bot%d", n);
        if (!bm_name_taken(cand, count, -1)) {
            snprintf(out, cap, "%s", cand);
            return;
        }
    }
}

// random valid head (want_low 1) or body (2) wire value for `gender`
static int bm_random_anim(int gender, int want_low, int fallback) {
    static const int head_values[] = {1, 4, 6, 7, 8};
    static const int body_values[] = {2, 5};
    const int *values = want_low == 1 ? head_values : body_values;
    int value_count = want_low == 1 ? 5 : 2;
    int matches[8];
    int n = 0;

    for (int k = 0; k < value_count; k++) {
        int anim = values[k] - 1;
        if (anim < 0 || anim >= game_data.animation_count) {
            continue;
        }
        int g = game_data.animations[anim].gender;
        if ((g & 3) == want_low && (g & (4 * gender)) != 0) {
            matches[n++] = values[k];
        }
    }
    return n == 0 ? fallback : matches[rand() % n];
}

static int bm_rand_between(int lo, int hi) {
    if (hi <= lo) {
        return lo;
    }
    return lo + rand() % (hi - lo + 1);
}

// levels for a preset, flavoured by archetype (warrior high on combat, skiller the reverse, merchant on trade skills)
static void bm_gen_levels(struct sp_bot_def *d, int preset) {
    int lo = 1, hi = 1;
    switch (preset) {
    case 1: lo = 5; hi = 20; break;
    case 2: lo = 25; hi = 50; break;
    case 3: lo = 55; hi = 80; break;
    case 4: lo = 99; hi = 99; break;
    case 5: lo = 1; hi = 99; break;
    default:
        memset(d->levels, 0, sizeof(d->levels));
        return;
    }
    int mid = (lo + hi) / 2;
    int warrior = strcmp(d->archetype, "warrior") == 0;
    int skiller = strcmp(d->archetype, "skiller") == 0;
    int merchant = strcmp(d->archetype, "merchant") == 0;
    for (int i = 0; i < SP_BOT_SKILL_COUNT; i++) {
        int a = lo, b = hi;
        int trade_skill = i == 7 || i == 10 || i == 12 || i == 13;
        if (warrior) {
            if (bm_skill_is_combat(i)) { a = mid; } else { b = mid; }
        } else if (skiller) {
            if (bm_skill_is_combat(i)) { b = mid; } else { a = mid; }
        } else if (merchant && trade_skill) {
            a = mid;
        }
        int v = bm_rand_between(a, b);
        if (i == 3 && v < 10) {
            v = 10;
        }
        d->levels[i] = v;
    }
}

static void bm_gen_def(struct sp_bot_def *d, const char *arch) {
    memset(d, 0, sizeof(*d));
    if (!arch) {
        int na = singleplayer_bot_archetype_count();
        arch = singleplayer_bot_archetype_name(na > 0 ? rand() % na : 0);
    }
    singleplayer_bot_archetype_preset(arch, d);
    for (int i = 0; i < 7; i++) {
        int *f = bm_slider_field(d, i);
        int v = *f + (rand() % 31) - 15;
        *f = v < 0 ? 0 : (v > 100 ? 100 : v);
    }
    d->money = rand() % SP_BOT_MONEY_COUNT;
    d->combat_style = rand() % SP_BOT_STYLE_COUNT;
    d->focus = rand() % SP_BOT_FOCUS_COUNT;
    d->pvp = rand() % SP_BOT_PVP_COUNT;
    d->party = rand() % 4;
    d->trade = rand() % 4;
    d->faction_join = rand() % 4;
    d->paced = 0;
    d->enabled = 1;
    d->hair_colour = rand() % PLAYER_HAIR_COLOUR_COUNT;
    d->top_colour = rand() % PLAYER_TOP_BOTTOM_COLOUR_COUNT;
    d->trouser_colour = rand() % PLAYER_TOP_BOTTOM_COLOUR_COUNT;
    d->skin_colour = rand() % 5;
    int gender = 1 + (rand() % 2);
    d->head_sprite = bm_random_anim(gender, 1, 1);
    d->body_sprite = bm_random_anim(gender, 2, 2);
}

// drawing helpers

static int bm_hot(mudclient *mud, int x, int y, int w, int h) {
    return mud->mouse_x >= x && mud->mouse_x < x + w && mud->mouse_y >= y &&
           mud->mouse_y < y + h;
}

// a click this frame, as panel_handle_mouse sees one (a touch tap arrives as press+release at finger lift)
static int bm_clicked(mudclient *mud) { return mud->last_mouse_button_down == 1; }

// hand-drawn controls reuse the panel toolkit's button box + palette; hot = yellow hover text
static void bm_button(mudclient *mud, int x, int y, int w, int h,
                      const char *label, int hot, int accent) {
    panel_draw_box(bm_panel, x, y, w, h);
    surface_draw_string_centre(mud->surface, label, x + w / 2, y + h - 5,
                               FONT_BOLD_12,
                               hot ? YELLOW : (accent ? GREEN : WHITE));
}

static void bm_pill(mudclient *mud, int x, int y, int w, int h, int on,
                    const char *on_text, const char *off_text, int hot) {
    panel_draw_box(bm_panel, x, y, w, h);
    surface_draw_string_centre(mud->surface, on ? on_text : off_text,
                               x + w / 2, y + h - 5, FONT_BOLD_12,
                               hot ? YELLOW : (on ? GREEN : RED));
}

// the settings rows

static void bm_add_row(int type, const char *label, int *value, int count,
                       const char *(*name)(int), int skill, int action) {
    if (bm_row_count >= BM_MAX_ROWS) {
        return;
    }
    struct bm_row *r = &bm_rows[bm_row_count++];
    r->type = type;
    r->label = label;
    r->value = value;
    r->count = count;
    r->name = name;
    r->skill = skill;
    r->action = action;
}

static void bm_build_rows(struct sp_bot_def *d) {
    bm_row_count = 0;
    if (!d) {
        return;
    }
    bm_add_row(BM_R_HEADER, "Behaviour", NULL, 0, NULL, 0, 0);
    bm_add_row(BM_R_CYCLER, "Archetype", NULL, 0, NULL, 0, BM_A_ARCH);
    bm_add_row(BM_R_CYCLER, "Money", &d->money, SP_BOT_MONEY_COUNT,
               singleplayer_bot_money_name, 0, 0);
    bm_add_row(BM_R_CYCLER, "Combat style", &d->combat_style,
               SP_BOT_STYLE_COUNT, singleplayer_bot_style_name, 0, 0);
    bm_add_row(BM_R_CYCLER, "Combat focus", &d->focus, SP_BOT_FOCUS_COUNT,
               singleplayer_bot_focus_name, 0, 0);
    bm_add_row(BM_R_CYCLER, "PvP (wilderness)", &d->pvp, SP_BOT_PVP_COUNT,
               singleplayer_bot_pvp_name, 0, 0);
    bm_add_row(BM_R_CYCLER, "Party invites", &d->party, SP_BOT_PVP_COUNT,
               singleplayer_bot_pvp_name, 0, 0);
    bm_add_row(BM_R_CYCLER, "Trading", &d->trade, SP_BOT_PVP_COUNT,
               singleplayer_bot_pvp_name, 0, 0);
    bm_add_row(BM_R_CYCLER, "Joins clans", &d->faction_join,
               SP_BOT_PVP_COUNT, singleplayer_bot_pvp_name, 0, 0);
    bm_add_row(BM_R_TOGGLE, "Level cap (paced)", &d->paced, 0, NULL, 0, 0);
    bm_add_row(BM_R_GAP, NULL, NULL, 0, NULL, 0, 0);
    bm_add_row(BM_R_HEADER, "Levels (green = changed; applies when the world starts)", NULL, 0, NULL, 0, 0);
    bm_add_row(BM_R_SETALL, "Set all", NULL, 0, NULL, 0, 0);
    for (int i = 0; i < SP_BOT_SKILL_COUNT; i++) {
        bm_add_row(BM_R_LEVEL, BM_SKILL_LABELS[i], NULL, 0, NULL, i, 0);
    }
    bm_add_row(BM_R_GAP, NULL, NULL, 0, NULL, 0, 0);
    bm_add_row(BM_R_HEADER, "Personality (archetype = preset)", NULL, 0, NULL, 0,
               0);
    for (int i = 0; i < 7; i++) {
        bm_add_row(BM_R_SLIDER, BM_SLIDER_NAMES[i], bm_slider_field(d, i), 0,
                   NULL, 0, 0);
    }
    bm_add_row(BM_R_GAP, NULL, NULL, 0, NULL, 0, 0);
    bm_add_row(BM_R_BUTTON, "Appearance...", NULL, 0, NULL, 0, BM_A_APPEARANCE);
}

// control x positions inside the region (relative to BM_RX0)
#define BM_CX_LABEL 10
#define BM_CX_PREV 236
#define BM_CX_VAL 302
#define BM_CX_NEXT 352
#define BM_CX_LL 212
#define BM_CX_L 240
#define BM_CX_R 340
#define BM_CX_RR 368
#define BM_CX_PILL 258
#define BM_CX_BAR 250
#define BM_CX_MINUS 224
#define BM_CX_PLUS 380
#define BM_BTN 18

static void bm_draw_row(mudclient *mud, struct sp_bot_def *d, struct bm_row *r,
                        int x, int y) {
    Surface *s = mud->surface;
    int ty = y + 15;
    switch (r->type) {
    case BM_R_HEADER:
        surface_draw_box(s, x + 2, y + 3, BM_RX1 - BM_RX0 - BM_BAR_W - 6,
                         BM_ROW_H - 6, PANEL_BOX_TOP_COLOUR);
        surface_draw_line_horizontal(s, x + 2, y + BM_ROW_H - 3,
                                     BM_RX1 - BM_RX0 - BM_BAR_W - 6,
                                     PANEL_BOX_BOTTOM_COLOUR);
        surface_draw_string(s, r->label, x + BM_CX_LABEL, ty, FONT_BOLD_12,
                            YELLOW);
        break;
    case BM_R_CYCLER: {
        // one strip per row: "<  value  >"
        surface_draw_string(s, r->label, x + BM_CX_LABEL, ty, FONT_BOLD_12,
                            WHITE);
        const char *v = "";
        if (r->action == BM_A_ARCH) {
            v = d->archetype;
        } else if (r->name) {
            v = r->name(*r->value);
        }
        panel_draw_box(bm_panel, x + BM_CX_PREV, y + 2,
                       BM_CX_NEXT + BM_BTN - BM_CX_PREV, BM_BTN);
        surface_draw_string_centre(s, "<", x + BM_CX_PREV + BM_BTN / 2, ty,
                                   FONT_BOLD_12,
                                   bm_hot(mud, x + BM_CX_PREV, y, BM_BTN + 4, BM_ROW_H) ? YELLOW : WHITE);
        surface_draw_string_centre(s, v, x + BM_CX_VAL, ty, FONT_BOLD_12, CYAN);
        surface_draw_string_centre(s, ">", x + BM_CX_NEXT + BM_BTN / 2, ty,
                                   FONT_BOLD_12,
                                   bm_hot(mud, x + BM_CX_NEXT, y, BM_BTN + 4, BM_ROW_H) ? YELLOW : WHITE);
        break;
    }
    case BM_R_TOGGLE:
        surface_draw_string(s, r->label, x + BM_CX_LABEL, ty, FONT_BOLD_12,
                            WHITE);
        bm_pill(mud, x + BM_CX_PILL, y + 2, 84, BM_BTN, *r->value, "On", "Off",
                bm_hot(mud, x + BM_CX_PILL, y + 2, 84, BM_BTN));
        break;
    case BM_R_LEVEL: {
        // same shape as a personality row: "-  [bar]  +  number"; number is the level now, green once changed
        int lvl = bm_eff_level(d, bm_selected, r->skill);
        int changed = d->levels[r->skill] > 0;
        char v[32];
        surface_draw_string(s, r->label, x + BM_CX_LABEL, ty, FONT_BOLD_12,
                            bm_skill_is_combat(r->skill) ? 0xffc080 : WHITE);
        panel_draw_box(bm_panel, x + BM_CX_MINUS, y + 2,
                       BM_CX_PLUS + BM_BTN - BM_CX_MINUS, BM_BTN);
        surface_draw_string_centre(s, "-", x + BM_CX_MINUS + BM_BTN / 2, ty, FONT_BOLD_12,
                                   bm_hot(mud, x + BM_CX_MINUS, y, BM_BTN + 4, BM_ROW_H) ? YELLOW : WHITE);
        surface_draw_box(s, x + BM_CX_BAR, y + 6, 122, 10, BLACK);
        surface_draw_box(s, x + BM_CX_BAR + 1, y + 7, (lvl * 120) / 99, 8,
                         changed ? GREEN : PANEL_ROUNDED_BOX_OUT_COLOUR);
        surface_draw_string_centre(s, "+", x + BM_CX_PLUS + BM_BTN / 2, ty, FONT_BOLD_12,
                                   bm_hot(mud, x + BM_CX_PLUS, y, BM_BTN + 4, BM_ROW_H) ? YELLOW : WHITE);
        snprintf(v, sizeof(v), "%d", lvl);
        surface_draw_string(s, v, x + BM_CX_PLUS + 24, ty, FONT_BOLD_12,
                            changed ? GREEN : CYAN);
        break;
    }
    case BM_R_SETALL: {
        static const char *const names[] = {"undo", "1", "25", "50", "75", "99"};
        surface_draw_string(s, "Set all:", x + BM_CX_LABEL, ty, FONT_BOLD_12,
                            WHITE);
        for (int k = 0; k < 6; k++) {
            int bx = x + 92 + k * 50;
            bm_button(mud, bx, y + 2, 44, BM_BTN, names[k],
                      bm_hot(mud, bx, y + 2, 44, BM_BTN), 0);
        }
        break;
    }
    case BM_R_SLIDER: {
        int v = *r->value;
        char t[8];
        surface_draw_string(s, r->label, x + BM_CX_LABEL, ty, FONT_BOLD_12,
                            WHITE);
        // one strip: "-  [bar]  +"
        panel_draw_box(bm_panel, x + BM_CX_MINUS, y + 2,
                       BM_CX_PLUS + BM_BTN - BM_CX_MINUS, BM_BTN);
        surface_draw_string_centre(s, "-", x + BM_CX_MINUS + BM_BTN / 2, ty, FONT_BOLD_12,
                                   bm_hot(mud, x + BM_CX_MINUS, y, BM_BTN + 4, BM_ROW_H) ? YELLOW : WHITE);
        surface_draw_box(s, x + BM_CX_BAR, y + 6, 122, 10, BLACK);
        surface_draw_box(s, x + BM_CX_BAR + 1, y + 7, (v * 120) / 100, 8,
                         PANEL_ROUNDED_BOX_OUT_COLOUR);
        surface_draw_string_centre(s, "+", x + BM_CX_PLUS + BM_BTN / 2, ty, FONT_BOLD_12,
                                   bm_hot(mud, x + BM_CX_PLUS, y, BM_BTN + 4, BM_ROW_H) ? YELLOW : WHITE);
        snprintf(t, sizeof(t), "%d", v);
        surface_draw_string(s, t, x + BM_CX_PLUS + 24, ty, FONT_BOLD_12, CYAN);
        break;
    }
    case BM_R_BUTTON:
        bm_button(mud, x + 130, y + 1, 180, BM_ROW_H - 2, r->label,
                  bm_hot(mud, x + 130, y + 1, 180, BM_ROW_H - 2), 1);
        break;
    default:
        break;
    }
}

#define BM_VISIBLE_ROWS ((BM_RY1 - BM_RY0 - 4) / BM_ROW_H)

static int bm_max_scroll(void) {
    int m = bm_row_count - BM_VISIBLE_ROWS;
    return m < 0 ? 0 : m;
}

static void bm_clamp_scroll(void) {
    int max_scroll = bm_max_scroll();
    if (bm_scroll > max_scroll) {
        bm_scroll = max_scroll;
    }
    if (bm_scroll < 0) {
        bm_scroll = 0;
    }
}

// panel + list

static void bm_build(mudclient *mud) {
    bm_panel = malloc(sizeof(Panel));
    panel_new(bm_panel, mud->surface, 64);

    int px = (mud->surface->width - BM_W) / 2;
    int py = (mud->surface->height - BM_H) / 2;

    bm_header = panel_add_text(bm_panel, px + 16, py + 40, "Bots:", FONT_BOLD_12, 1);
    bm_subtitle = panel_add_text(bm_panel, px + 248, py + 40, "", FONT_BOLD_12, 1);
    bm_list = panel_add_text_list_interactive(bm_panel, px + 16, py + 52, 216, 326,
                                              FONT_BOLD_12, SP_BOT_MAX, 1);

    int bx = px + 16 + 54;
    panel_add_button_background(bm_panel, bx, py + 396, 104, 20);
    panel_add_text_centre(bm_panel, bx, py + 396, "New", FONT_BOLD_12, 0);
    bm_new = panel_add_button(bm_panel, bx, py + 396, 104, 20);
    panel_add_button_background(bm_panel, bx + 108, py + 396, 104, 20);
    bm_delete_txt = panel_add_text_centre(bm_panel, bx + 108, py + 396, "Delete",
                                          FONT_BOLD_12, 0);
    bm_delete = panel_add_button(bm_panel, bx + 108, py + 396, 104, 20);
    panel_add_button_background(bm_panel, bx, py + 420, 104, 20);
    panel_add_text_centre(bm_panel, bx, py + 420, "@gre@All on", FONT_BOLD_12, 0);
    bm_all_on = panel_add_button(bm_panel, bx, py + 420, 104, 20);
    panel_add_button_background(bm_panel, bx + 108, py + 420, 104, 20);
    panel_add_text_centre(bm_panel, bx + 108, py + 420, "@red@All off",
                          FONT_BOLD_12, 0);
    bm_all_off = panel_add_button(bm_panel, bx + 108, py + 420, 104, 20);
    panel_add_button_background(bm_panel, px + 16 + 108, py + 444, 216, 20);
    panel_add_text_centre(bm_panel, px + 16 + 108, py + 444, "@gre@Auto-create...",
                          FONT_BOLD_12, 0);
    bm_auto = panel_add_button(bm_panel, px + 16 + 108, py + 444, 216, 20);

    panel_add_text(bm_panel, px + 248, py + 60, "Name:", FONT_BOLD_12, 1);
    panel_add_button_background(bm_panel, px + 248 + 118, py + 60, 150, 18);
    bm_name = panel_add_text_input(bm_panel, px + 248 + 118, py + 60, 150, 18,
                                   FONT_BOLD_12, 16, 0, 0);

    panel_add_button_background(bm_panel, px + BM_W - 86, py + 466, 150, 22);
    panel_add_text_centre(bm_panel, px + BM_W - 86, py + 466, "@gre@Save & Close",
                          FONT_BOLD_12, 0);
    bm_save = panel_add_button(bm_panel, px + BM_W - 86, py + 466, 150, 22);
    // Cancel: forget every change since the roster was last saved
    panel_add_button_background(bm_panel, px + BM_W - 218, py + 466, 100, 22);
    panel_add_text_centre(bm_panel, px + BM_W - 218, py + 466, "Cancel",
                          FONT_BOLD_12, 0);
    bm_cancel = panel_add_button(bm_panel, px + BM_W - 218, py + 466, 100, 22);
}

static void bm_refresh_list(void) {
    int on = 0;
    panel_clear_list(bm_panel, bm_list);
    for (int i = 0; i < bm_count; i++) {
        char row[48];
        // the list paints its selected row red itself, so the selected row carries no colour code
        const char *colour = i == bm_selected ? "" : (bm_defs[i].enabled ? "@gre@" : BM_OFF_COLOUR);
        char cb[32] = "";
        if (bm_live_found[i]) {
            snprintf(cb, sizeof(cb), " (cb %d)", bm_live_combat_level(i));
        }
        snprintf(row, sizeof(row), "%s%s%s%s%s", colour,
                 i == bm_selected ? "> " : "", bm_defs[i].name, cb,
                 bm_defs[i].enabled ? "" : " (off)");
        panel_add_list_entry(bm_panel, bm_list, i, row);
        on += bm_defs[i].enabled ? 1 : 0;
    }
    char header[40];
    snprintf(header, sizeof(header), "Bots (%d, %d on):", bm_count, on);
    panel_update_text(bm_panel, bm_header, header);
}

static void bm_refresh_selected(void) {
    struct sp_bot_def *d = bm_def();
    int has = d != NULL;
    bm_panel->control_activated[bm_list] = has ? bm_selected : -1;
    bm_panel->control_shown[bm_name] = has ? 1 : 0;
    panel_update_text(bm_panel, bm_delete_txt,
                      (has && bm_delete_armed == bm_selected) ? "@red@Confirm?"
                                                              : "Delete");
    if (has) {
        panel_update_text(bm_panel, bm_name, d->name);
    }
    bm_build_rows(d);
    bm_clamp_scroll();
}

static void bm_commit_name(void) {
    struct sp_bot_def *d = bm_def();
    if (!d) {
        return;
    }
    char *t = panel_get_text(bm_panel, bm_name);
    if (t && t[0]) {
        snprintf(d->name, 32, "%s", t);
    }
}

static void bm_select(int index) {
    bm_commit_name();
    bm_selected = index;
    bm_delete_armed = -1;
    bm_scroll = 0;
    bm_refresh_list(); // the selected row is drawn differently
    bm_refresh_selected();
}

void bot_manager_open(mudclient *mud, const char *world_id) {
    if (bm_panel == NULL) {
        bm_build(mud);
    }
    snprintf(bm_world_id, sizeof(bm_world_id), "%s",
             world_id ? world_id : "default");
    int n = singleplayer_bots_load(bm_world_id, bm_defs, SP_BOT_MAX);
    bm_count = n > 0 ? n : 0;
    singleplayer_bots_live_levels(bm_world_id, bm_defs, bm_count, bm_live,
                                  bm_live_found);
    bm_selected = bm_count > 0 ? 0 : -1;
    bm_delete_armed = -1;
    bm_scroll = 0;
    char sub[64];
    snprintf(sub, sizeof(sub), "@cya@World: %s", bm_world_id);
    panel_update_text(bm_panel, bm_subtitle, sub);
    bm_refresh_list();
    bm_refresh_selected();
    bm_shown = 1;
}

int bot_manager_shown(void) { return bm_shown; }

static void bm_save_close(void) {
    bm_commit_name();
    singleplayer_bots_save(bm_world_id, bm_defs, bm_count);
    bm_shown = 0;
    bm_delete_armed = -1;
}

// Auto-create

static void au_add_cycler(Panel *p, int cx, int y, const char *label,
                          int *out_prev, int *out_val, int *out_next) {
    panel_add_text(p, cx - 150, y, (char *)label, FONT_BOLD_12, 1);
    panel_add_button_background(p, cx + 10, y, 16, 16);
    panel_add_text_centre(p, cx + 10, y, "@whi@<", FONT_BOLD_12, 0);
    *out_prev = panel_add_button(p, cx + 10, y, 16, 16);
    *out_val = panel_add_text_centre(p, cx + 75, y, "", FONT_BOLD_12, 1);
    panel_add_button_background(p, cx + 140, y, 16, 16);
    panel_add_text_centre(p, cx + 140, y, "@whi@>", FONT_BOLD_12, 0);
    *out_next = panel_add_button(p, cx + 140, y, 16, 16);
}

static void au_add_toggle(Panel *p, int cx, int y, const char *label,
                          int *out_txt, int *out_btn) {
    panel_add_text(p, cx - 150, y, (char *)label, FONT_BOLD_12, 1);
    panel_add_button_background(p, cx + 75, y, 100, 16);
    *out_txt = panel_add_text_centre(p, cx + 75, y, "", FONT_BOLD_12, 0);
    *out_btn = panel_add_button(p, cx + 75, y, 100, 16);
}

static void au_build(mudclient *mud) {
    au_panel = malloc(sizeof(Panel));
    panel_new(au_panel, mud->surface, 96);
    int cx = mud->surface->width / 2;
    int cy = mud->surface->height / 2;
    panel_add_text_centre(au_panel, cx, cy - 118, "@yel@Auto-create bots",
                          FONT_BOLD_14, 1);
    panel_add_text_centre(au_panel, cx, cy - 98,
                          "@cya@Random names, looks and traits; tweak any bot after",
                          FONT_BOLD_12, 1);
    au_add_cycler(au_panel, cx, cy - 66, "How many:", &au_count_prev, &au_count_val,
                  &au_count_next);
    au_add_cycler(au_panel, cx, cy - 40, "Archetype:", &au_arch_prev, &au_arch_val,
                  &au_arch_next);
    au_add_cycler(au_panel, cx, cy - 14, "Levels:", &au_lvl_prev, &au_lvl_val,
                  &au_lvl_next);
    au_add_toggle(au_panel, cx, cy + 12, "Level cap (paced):", &au_paced_txt,
                  &au_paced_btn);
    au_add_toggle(au_panel, cx, cy + 36, "Enabled:", &au_enabled_txt,
                  &au_enabled_btn);
    au_note = panel_add_text_centre(au_panel, cx, cy + 64, "", FONT_BOLD_12, 1);
    panel_add_button_background(au_panel, cx - 60, cy + 98, 100, 22);
    panel_add_text_centre(au_panel, cx - 60, cy + 98, "@gre@Create", FONT_BOLD_12, 0);
    au_create = panel_add_button(au_panel, cx - 60, cy + 98, 100, 22);
    panel_add_button_background(au_panel, cx + 60, cy + 98, 100, 22);
    panel_add_text_centre(au_panel, cx + 60, cy + 98, "Cancel", FONT_BOLD_12, 0);
    au_cancel = panel_add_button(au_panel, cx + 60, cy + 98, 100, 22);
}

static int au_step_index(void) {
    for (int i = 0; i < AU_STEP_COUNT; i++) {
        if (AU_STEPS[i] >= au_count) {
            return i;
        }
    }
    return AU_STEP_COUNT - 1;
}

static void au_refresh(void) {
    char v[24];
    int room = SP_BOT_MAX - bm_count;
    int shown = au_count > room ? room : au_count;
    snprintf(v, sizeof(v), "%d", shown);
    panel_update_text(au_panel, au_count_val, v);
    panel_update_text(au_panel, au_arch_val,
                      au_arch == 0
                          ? "Random mix"
                          : (char *)singleplayer_bot_archetype_name(au_arch - 1));
    panel_update_text(au_panel, au_lvl_val, (char *)AU_LEVEL_NAMES[au_levels]);
    panel_update_text(au_panel, au_paced_txt, au_paced ? "@gre@On" : "@red@Off");
    panel_update_text(au_panel, au_enabled_txt,
                      au_enabled ? "@gre@Yes" : "@red@No (disabled)");
    char note[48];
    if (room <= 0) {
        snprintf(note, sizeof(note), "@red@Roster full (%d)", SP_BOT_MAX);
    } else {
        snprintf(note, sizeof(note), "@cya@%d free slots of %d", room, SP_BOT_MAX);
    }
    panel_update_text(au_panel, au_note, note);
}

static void au_open(mudclient *mud) {
    if (au_panel == NULL) {
        au_build(mud);
    }
    bm_seed();
    int room = SP_BOT_MAX - bm_count;
    if (au_count > room) {
        au_count = room > 0 ? room : 1;
    }
    au_refresh();
    au_shown = 1;
}

static void au_do_create(void) {
    int room = SP_BOT_MAX - bm_count;
    int want = au_count > room ? room : au_count;
    const char *arch =
        au_arch == 0 ? NULL : singleplayer_bot_archetype_name(au_arch - 1);
    int first = bm_count;
    for (int k = 0; k < want; k++) {
        struct sp_bot_def *nd = &bm_defs[bm_count];
        bm_gen_def(nd, arch);
        bm_gen_levels(nd, au_levels);
        nd->paced = au_paced ? 1 : 0;
        nd->enabled = au_enabled ? 1 : 0;
        bm_gen_unique_name(nd->name, sizeof(nd->name), bm_count,
                           nd->body_sprite == 5 ? 2 : 1);
        bm_count++;
    }
    au_shown = 0;
    bm_refresh_list();
    bm_select(want > 0 ? first : bm_selected);
}

static void au_draw(mudclient *mud) {
    if (!au_shown || au_panel == NULL) {
        return;
    }
    int cx = mud->surface->width / 2;
    int cy = mud->surface->height / 2;
    surface_draw_box_alpha(mud->surface, (mud->surface->width - BM_W) / 2,
                           (mud->surface->height - BM_H) / 2, BM_W, BM_H, 0, 128);
    surface_draw_box_alpha(mud->surface, cx - 190, cy - 136, 380, 260, 0, 245);
    surface_draw_border(mud->surface, cx - 190, cy - 136, 380, 260,
                        PANEL_ROUNDED_BOX_OUT_COLOUR);
    surface_draw_border(mud->surface, cx - 189, cy - 135, 378, 258,
                        PANEL_ROUNDED_BOX_MIDDLE_COLOUR);
    panel_draw_panel(au_panel);
}

static int au_handle(mudclient *mud) {
    if (!au_shown || au_panel == NULL) {
        return 0;
    }
    {
        int px = mud->mouse_x, py = mud->mouse_y, pdown = mud->mouse_button_down;
        if (mudclient_is_touch(mud) && mudclient_finger_1_down) {
            px = mudclient_finger_1_x;
            py = mudclient_finger_1_y;
            pdown = 1;
        }
        panel_handle_mouse(au_panel, px, py, mud->last_mouse_button_down, pdown,
                           mud->mouse_scroll_delta);
    }
    if (panel_is_clicked(au_panel, au_cancel)) {
        au_shown = 0;
    } else if (panel_is_clicked(au_panel, au_create)) {
        au_do_create();
    } else if (panel_is_clicked(au_panel, au_count_prev)) {
        int i = (au_step_index() - 1 + AU_STEP_COUNT) % AU_STEP_COUNT;
        au_count = AU_STEPS[i];
        au_refresh();
    } else if (panel_is_clicked(au_panel, au_count_next)) {
        int i = (au_step_index() + 1) % AU_STEP_COUNT;
        au_count = AU_STEPS[i];
        au_refresh();
    } else if (panel_is_clicked(au_panel, au_arch_prev)) {
        int n = singleplayer_bot_archetype_count() + 1;
        au_arch = (au_arch - 1 + n) % n;
        au_refresh();
    } else if (panel_is_clicked(au_panel, au_arch_next)) {
        int n = singleplayer_bot_archetype_count() + 1;
        au_arch = (au_arch + 1) % n;
        au_refresh();
    } else if (panel_is_clicked(au_panel, au_lvl_prev)) {
        au_levels = (au_levels - 1 + AU_LEVEL_COUNT) % AU_LEVEL_COUNT;
        au_refresh();
    } else if (panel_is_clicked(au_panel, au_lvl_next)) {
        au_levels = (au_levels + 1) % AU_LEVEL_COUNT;
        au_refresh();
    } else if (panel_is_clicked(au_panel, au_paced_btn)) {
        au_paced = !au_paced;
        au_refresh();
    } else if (panel_is_clicked(au_panel, au_enabled_btn)) {
        au_enabled = !au_enabled;
        au_refresh();
    }
    return 1;
}

// appearance hand-off

int worldlist_bots_appearance_active(void) { return bm_appearance_active; }

int worldlist_bots_appearance_accept(mudclient *mud) {
    if (!bm_appearance_active) {
        return 0;
    }
    struct sp_bot_def *d = bm_def();
    if (d) {
        d->head_sprite = mud->appearance_head_type + 1;
        d->body_sprite = mud->appearance_body_type + 1;
        d->hair_colour = mud->appearance_hair_colour;
        d->top_colour = mud->appearance_top_colour;
        d->trouser_colour = mud->appearance_bottom_colour;
        d->skin_colour = mud->appearance_skin_colour;
    }
    bm_appearance_active = 0;
    mud->show_appearance_change = 0;
    return 1;
}

static void bm_open_appearance(mudclient *mud, struct sp_bot_def *d) {
    mud->appearance_head_type = d->head_sprite > 0 ? d->head_sprite - 1 : 0;
    mud->appearance_body_type = d->body_sprite > 0 ? d->body_sprite - 1 : 1;
    mud->appearance_hair_colour = d->hair_colour;
    mud->appearance_top_colour = d->top_colour;
    mud->appearance_bottom_colour = d->trouser_colour;
    mud->appearance_skin_colour = d->skin_colour;
    {
        int body_anim = mud->appearance_body_type;
        int g = (body_anim >= 0 && body_anim < game_data.animation_count)
                    ? game_data.animations[body_anim].gender
                    : 0;
        mud->appearance_head_gender = ((g & 8) && !(g & 4)) ? 2 : 1;
    }
    bm_appearance_active = 1;
    mud->show_appearance_change = 1;
}

// drawing

void bot_manager_draw(mudclient *mud) {
    if (!bm_shown || bm_panel == NULL) {
        return;
    }
    // login screens don't draw the appearance panel, so draw it here while it edits a bot's look
    if (bm_appearance_active) {
        // login screen is already painted under us; black it out first
        surface_draw_box(mud->surface, 0, 0, mud->surface->width,
                         mud->surface->height, BLACK);
        mudclient_draw_appearance_panel(mud);
        return;
    }
    Surface *s = mud->surface;
    int px = (s->width - BM_W) / 2;
    int py = (s->height - BM_H) / 2;

    // same dimmed backdrop the world editor uses, with a panel border
    surface_draw_box_alpha(s, px, py, BM_W, BM_H, 0, 235);
    surface_draw_border(s, px, py, BM_W, BM_H, PANEL_ROUNDED_BOX_OUT_COLOUR);
    surface_draw_border(s, px + 1, py + 1, BM_W - 2, BM_H - 2,
                        PANEL_ROUNDED_BOX_MIDDLE_COLOUR);
    surface_draw_box(s, px + 2, py + 2, BM_W - 4, 30, PANEL_BOX_TOP_COLOUR);
    surface_draw_line_horizontal(s, px + 2, py + 31, BM_W - 4,
                                 PANEL_BOX_BOTTOM_COLOUR);
    surface_draw_string_centre(s, "Bot Manager", px + BM_W / 2, py + 22,
                               FONT_BOLD_14, YELLOW);

    // settings region: a panel box like the list's
    panel_draw_rounded_box(bm_panel, px + BM_RX0, py + BM_RY0, BM_RX1 - BM_RX0,
                           BM_RY1 - BM_RY0);

    panel_draw_panel(bm_panel);

    struct sp_bot_def *d = bm_def();
    if (d) {
        char cl[80];
        bm_pill(mud, px + 452, py + 51, 92, 18, d->enabled, "Enabled",
                "Disabled", bm_hot(mud, px + 452, py + 51, 92, 18));
        // Player.getCombatLevel on the levels shown
        {
            int a = bm_eff_level(d, bm_selected, 0), df = bm_eff_level(d, bm_selected, 1);
            int st = bm_eff_level(d, bm_selected, 2), h = bm_eff_level(d, bm_selected, 3);
            snprintf(cl, sizeof(cl), "Combat level %d", (a + df + st + h) / 4);
        }
        surface_draw_string_right(s, cl, px + BM_RX1 - 4, py + 66, FONT_BOLD_12,
                                  CYAN);

        int region_h = BM_RY1 - BM_RY0;
        // whole rows only, no clip bounds (see bm_scroll)
        for (int i = bm_scroll; i < bm_row_count && i < bm_scroll + BM_VISIBLE_ROWS; i++) {
            int y = py + BM_RY0 + 2 + (i - bm_scroll) * BM_ROW_H;
            bm_draw_row(mud, d, &bm_rows[i], px + BM_RX0, y);
        }

        if (bm_row_count > BM_VISIBLE_ROWS) {
            int bar_x = px + BM_RX1 - BM_BAR_W;
            int bar_y = py + BM_RY0;
            int scrub_h = ((region_h - 27) * BM_VISIBLE_ROWS) / bm_row_count;
            if (scrub_h < 8) {
                scrub_h = 8;
            }
            int max_scroll = bm_max_scroll();
            int scrub_y = max_scroll > 0
                              ? ((region_h - 27 - scrub_h) * bm_scroll) / max_scroll
                              : 0;
            surface_draw_scrollbar(s, bar_x, bar_y, BM_BAR_W, region_h, scrub_y,
                                   scrub_h);
        }
    } else {
        surface_draw_string_centre(s, "No bot selected", px + BM_RX0 + 230,
                                   py + BM_RY0 + 180, FONT_BOLD_12, CYAN);
        surface_draw_string_centre(s, "Pick one on the left, or New / Auto-create",
                                   px + BM_RX0 + 230, py + BM_RY0 + 200,
                                   FONT_BOLD_12, WHITE);
    }

    // under the left column, clear of Cancel / Save & Close
    surface_draw_string(s, "Changes apply when the world next starts",
                        px + 16, py + 471, FONT_BOLD_12, CYAN);

    au_draw(mud);
}

// input

// set a skill's level in the definition (clamped; hits never under 10)
static void bm_level_set(struct sp_bot_def *d, int skill, int v) {
    int lo = bm_skill_default(skill);
    if (v < lo) {
        v = lo;
    }
    if (v > 99) {
        v = 99;
    }
    d->levels[skill] = v;
}

// nudge from the level shown (what you set, else what the bot has now)
static void bm_level_step(struct sp_bot_def *d, int skill, int delta) {
    bm_level_set(d, skill, bm_eff_level(d, bm_selected, skill) + delta);
}

static void bm_set_all_levels(struct sp_bot_def *d, int value) {
    for (int i = 0; i < SP_BOT_SKILL_COUNT; i++) {
        d->levels[i] = value == 0 ? 0 : (i == 3 && value < 10 ? 10 : value);
    }
}

static void bm_slider_step(int *f, int delta) {
    int v = *f + delta;
    *f = v < 0 ? 0 : (v > 100 ? 100 : v);
}

static void bm_arch_step(struct sp_bot_def *d, int delta) {
    int n = singleplayer_bot_archetype_count();
    int cur = 0;
    for (int i = 0; i < n; i++) {
        if (strcmp(d->archetype, singleplayer_bot_archetype_name(i)) == 0) {
            cur = i;
            break;
        }
    }
    singleplayer_bot_archetype_preset(
        singleplayer_bot_archetype_name((cur + delta + n) % n), d);
}

static int bm_in(int mx, int my, int x, int y, int w, int h) {
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

// a click inside the region at (mx, my), already on row `r`
static void bm_row_click(mudclient *mud, struct sp_bot_def *d, struct bm_row *r,
                         int x, int y, int mx, int my) {
    switch (r->type) {
    case BM_R_CYCLER: {
        int delta = 0;
        if (bm_in(mx, my, x + BM_CX_PREV, y, BM_BTN + 4, BM_ROW_H)) {
            delta = -1;
        } else if (bm_in(mx, my, x + BM_CX_NEXT, y, BM_BTN + 4, BM_ROW_H)) {
            delta = 1;
        }
        if (!delta) {
            return;
        }
        if (r->action == BM_A_ARCH) {
            bm_arch_step(d, delta);
        } else {
            *r->value = (*r->value + delta + r->count) % r->count;
        }
        break;
    }
    case BM_R_TOGGLE:
        if (bm_in(mx, my, x + BM_CX_PILL, y, 84, BM_ROW_H)) {
            *r->value = !*r->value;
        }
        break;
    case BM_R_LEVEL:
        if (bm_in(mx, my, x + BM_CX_MINUS, y, BM_BTN + 4, BM_ROW_H)) {
            bm_level_step(d, r->skill, -1);
        } else if (bm_in(mx, my, x + BM_CX_PLUS, y, BM_BTN + 4, BM_ROW_H)) {
            bm_level_step(d, r->skill, 1);
        } else if (bm_in(mx, my, x + BM_CX_BAR, y, 122, BM_ROW_H)) {
            // a tap on the bar sets the level directly
            int v = 1 + ((mx - (x + BM_CX_BAR)) * 98) / 122;
            bm_level_set(d, r->skill, v);
        }
        break;
    case BM_R_SETALL: {
        static const int values[] = {0, 1, 25, 50, 75, 99};
        for (int k = 0; k < 6; k++) {
            if (bm_in(mx, my, x + 92 + k * 50, y, 46, BM_ROW_H)) {
                bm_set_all_levels(d, values[k]);
            }
        }
        break;
    }
    case BM_R_SLIDER:
        if (bm_in(mx, my, x + BM_CX_MINUS, y, BM_BTN + 4, BM_ROW_H)) {
            bm_slider_step(r->value, -5);
        } else if (bm_in(mx, my, x + BM_CX_PLUS, y, BM_BTN + 4, BM_ROW_H)) {
            bm_slider_step(r->value, 5);
        } else if (bm_in(mx, my, x + BM_CX_BAR, y, 122, BM_ROW_H)) {
            // tap on the bar sets the value directly
            int v = ((mx - (x + BM_CX_BAR)) * 100) / 122;
            *r->value = v < 0 ? 0 : (v > 100 ? 100 : v);
        }
        break;
    case BM_R_BUTTON:
        if (bm_in(mx, my, x + 130, y, 180, BM_ROW_H) &&
            r->action == BM_A_APPEARANCE) {
            bm_open_appearance(mud, d);
        }
        break;
    default:
        break;
    }
}

static void bm_region_input(mudclient *mud, struct sp_bot_def *d) {
    int px = (mud->surface->width - BM_W) / 2;
    int py = (mud->surface->height - BM_H) / 2;
    int rx0 = px + BM_RX0, ry0 = py + BM_RY0, rx1 = px + BM_RX1, ry1 = py + BM_RY1;
    int region_h = ry1 - ry0;
    int content_w = rx1 - rx0 - BM_BAR_W;
    int bar_x = rx1 - BM_BAR_W;

    int touch_held = mudclient_is_touch(mud) && mudclient_finger_1_down;
    int pointer_down = touch_held || mud->mouse_button_down == 1;
    int pointer_x = touch_held ? mudclient_finger_1_x : mud->mouse_x;
    int pointer_y = touch_held ? mudclient_finger_1_y : mud->mouse_y;

    // wheel: a row per notch
    if (mud->mouse_scroll_delta != 0 &&
        bm_in(mud->mouse_x, mud->mouse_y, rx0, ry0, rx1 - rx0, region_h)) {
        bm_scroll += mud->mouse_scroll_delta;
    }

    // scrollbar: arrows + thumb
    if (bm_row_count > BM_VISIBLE_ROWS) {
        int scrub_h = ((region_h - 27) * BM_VISIBLE_ROWS) / bm_row_count;
        if (scrub_h < 8) {
            scrub_h = 8;
        }
        if (pointer_down && pointer_x >= bar_x && pointer_x <= bar_x + BM_BAR_W) {
            if (pointer_y > ry0 && pointer_y < ry0 + 12) {
                bm_scroll--;
            }
            if (pointer_y > ry1 - 12 && pointer_y < ry1) {
                bm_scroll++;
            }
        }
        if (pointer_down) {
            int on_bar = pointer_x >= bar_x && pointer_x <= bar_x + BM_BAR_W;
            int near_bar = pointer_x >= bar_x - 12 && pointer_x <= bar_x + 24;
            int within_y = pointer_y > ry0 + 12 && pointer_y < ry1 - 12;
            if ((on_bar || (bm_thumb_dragged && near_bar)) && within_y) {
                bm_thumb_dragged = 1;
                bm_scroll = ((pointer_y - ry0 - 12 - scrub_h / 2) * bm_row_count) /
                            (region_h - 24);
            }
        } else {
            bm_thumb_dragged = 0;
        }
    }

    // a finger drag over the content scrolls a row per row-height; a lift after a real drag isn't a tap
    if (touch_held && !bm_thumb_dragged &&
        bm_in(pointer_x, pointer_y, rx0, ry0, content_w, region_h)) {
        if (!bm_drag_active) {
            bm_drag_active = 1;
            bm_drag_last_y = pointer_y;
            bm_drag_accum = 0;
            bm_drag_carry = 0;
        } else {
            int dy = pointer_y - bm_drag_last_y;
            if (dy != 0) {
                bm_drag_carry -= dy;
                bm_drag_last_y = pointer_y;
                bm_drag_accum += dy < 0 ? -dy : dy;
                if (bm_drag_accum > 8) {
                    bm_drag_moved_ttl = 3;
                }
                while (bm_drag_carry >= BM_ROW_H) {
                    bm_scroll++;
                    bm_drag_carry -= BM_ROW_H;
                }
                while (bm_drag_carry <= -BM_ROW_H) {
                    bm_scroll--;
                    bm_drag_carry += BM_ROW_H;
                }
            }
        }
    } else if (!touch_held) {
        bm_drag_active = 0;
    }

    bm_clamp_scroll();

    if (!bm_clicked(mud)) {
        if (bm_drag_moved_ttl > 0 && !touch_held) {
            bm_drag_moved_ttl--;
        }
        return;
    }
    if (bm_drag_moved_ttl > 0) {
        bm_drag_moved_ttl = 0;
        return;
    }

    int mx = mud->mouse_x, my = mud->mouse_y;
    if (!bm_in(mx, my, rx0, ry0, content_w, region_h)) {
        return;
    }
    int rel = my - (ry0 + 2);
    if (rel < 0) {
        return;
    }
    int row = bm_scroll + rel / BM_ROW_H;
    if (row < bm_scroll || row >= bm_scroll + BM_VISIBLE_ROWS || row >= bm_row_count) {
        return;
    }
    int y = ry0 + 2 + (row - bm_scroll) * BM_ROW_H;
    bm_row_click(mud, d, &bm_rows[row], rx0, y, mx, my);
}

int bot_manager_handle(mudclient *mud) {
    if (!bm_shown || bm_panel == NULL) {
        return 0;
    }
    if (bm_appearance_active) {
        // the appearance panel owns the input; Accept returns via worldlist_bots_appearance_accept
        mudclient_handle_appearance_panel_input(mud);
        return 1;
    }
    if (au_shown) {
        return au_handle(mud);
    }
    {
        int px = mud->mouse_x, py = mud->mouse_y, pdown = mud->mouse_button_down;
        if (mudclient_is_touch(mud) && mudclient_finger_1_down) {
            px = mudclient_finger_1_x;
            py = mudclient_finger_1_y;
            pdown = 1;
        }
        panel_handle_mouse(bm_panel, px, py, mud->last_mouse_button_down, pdown,
                           mud->mouse_scroll_delta);
    }

    struct sp_bot_def *d = bm_def();
    int px = (mud->surface->width - BM_W) / 2;
    int py = (mud->surface->height - BM_H) / 2;

    if (panel_is_clicked(bm_panel, bm_save)) {
        bm_save_close();
        return 1;
    } else if (panel_is_clicked(bm_panel, bm_cancel)) {
        // nothing is written; the next open reloads the saved roster
        bm_shown = 0;
        bm_delete_armed = -1;
        return 1;
    } else if (panel_is_clicked(bm_panel, bm_auto)) {
        bm_commit_name();
        au_open(mud);
        return 1;
    } else if (panel_is_clicked(bm_panel, bm_list)) {
        int e = panel_get_list_entry_index(bm_panel, bm_list);
        if (e >= 0 && e < bm_count) {
            bm_select(e);
        }
        return 1;
    } else if (panel_is_clicked(bm_panel, bm_new)) {
        if (bm_count < SP_BOT_MAX) {
            bm_commit_name();
            struct sp_bot_def *nd = &bm_defs[bm_count];
            memset(nd, 0, sizeof(*nd));
            snprintf(nd->name, sizeof(nd->name), "bot%d", bm_count + 1);
            singleplayer_bot_archetype_preset("casual", nd);
            nd->enabled = 1;
            nd->hair_colour = 2;
            nd->top_colour = 8;
            nd->trouser_colour = 14;
            nd->skin_colour = 0;
            nd->head_sprite = 1;
            nd->body_sprite = 2;
            bm_count++;
            bm_refresh_list();
            bm_select(bm_count - 1);
        }
        return 1;
    } else if (panel_is_clicked(bm_panel, bm_delete)) {
        if (d) {
            if (bm_delete_armed == bm_selected) {
                for (int i = bm_selected; i < bm_count - 1; i++) {
                    bm_defs[i] = bm_defs[i + 1];
                }
                bm_count--;
                bm_delete_armed = -1;
                bm_refresh_list();
                bm_selected = -1;
                bm_select(bm_count > 0 ? 0 : -1);
            } else {
                bm_delete_armed = bm_selected;
                bm_refresh_selected();
            }
        }
        return 1;
    } else if (panel_is_clicked(bm_panel, bm_all_on)) {
        // panel_is_clicked clears the flag it reads, so each button gets its own branch
        for (int i = 0; i < bm_count; i++) {
            bm_defs[i].enabled = 1;
        }
        bm_refresh_list();
        bm_refresh_selected();
        return 1;
    } else if (panel_is_clicked(bm_panel, bm_all_off)) {
        for (int i = 0; i < bm_count; i++) {
            bm_defs[i].enabled = 0;
        }
        bm_refresh_list();
        bm_refresh_selected();
        return 1;
    }

    if (!d) {
        return 1;
    }

    // the Enabled pill in the fixed header
    if (bm_clicked(mud) &&
        bm_in(mud->mouse_x, mud->mouse_y, px + 452, py + 51, 92, 18)) {
        d->enabled = !d->enabled;
        bm_refresh_list();
        bm_refresh_selected();
        return 1;
    }

    bm_region_input(mud, d);
    return 1;
}

int bot_manager_key(mudclient *mud, int key_code) {
    (void)mud;
    if (bm_shown && bm_panel != NULL && !au_shown && !bm_appearance_active) {
        panel_key_press(bm_panel, key_code);
        return 1;
    }
    return 0;
}

#endif // WITH_SINGLEPLAYER
