#include "worldlist.h"
#include "login.h" // LOGIN_STAGE_WORLD
#include <stdio.h>

#ifdef WITH_SINGLEPLAYER
#include "../singleplayer.h"
#include "../sp-net.h"
#endif

#ifdef EMSCRIPTEN
#define USE_WEBSOCKS 1
#else
#define USE_WEBSOCKS 0
#endif

// login protocol id, worlds.cfg column 6 (absent/unknown = 177)
#define PROTO_OPENRSC_177   0
#define PROTO_AUTHENTIC_204 1
#define PROTO_CUSTOM_10010  2 // Cabbage/Coleslaw custom content

struct server_type {
    char name[32];
    char host[64];
    int port;
    char rsa_exponent[512];
    char rsa_modulus[512];
    int protocol; // PROTO_* value, worlds.cfg column 6
    char content[32]; // content-pack id, worlds.cfg column 7
};

static struct server_type list[256] = {0};

static void worldlist_set_defaults(void);
static void worldlist_read_presets(struct mudclient *mud);
static void worldlist_select(mudclient *, int);

// shared RSA login keypair for all OpenRSC worlds (exponent 65537)
#define OPENRSC_RSA_MODULUS \
    "87cef754966ecb19806238d9fecf0f421e816976f74f365c86a584e51049794d41fefbdc5fed3a3ed3b7495ba24262bb7d1dd5d2ff9e306b5bbf5522a2e85b25"

// curated OpenRSC worlds, re-appended if missing from worlds.cfg
static const struct server_type openrsc_default_worlds[] = {
    // Preservation: near-authentic RSC replica, 1x XP, staff-moderated
    {"OpenRSC_Preservation", "game.openrsc.com", USE_WEBSOCKS ? 43496 : 43596,
     "00010001", OPENRSC_RSA_MODULUS, PROTO_OPENRSC_177, ""},
    // 2001scape: RSC as of 8 May 2001, 1x XP
    {"OpenRSC_2001scape", "game.openrsc.com", USE_WEBSOCKS ? 43493 : 43593,
     "00010001", OPENRSC_RSA_MODULUS, PROTO_OPENRSC_177, ""},
    // Uranium: botting-allowed, 1x XP
    {"OpenRSC_Uranium", "game.openrsc.com", USE_WEBSOCKS ? 43435 : 43235,
     "00010001", OPENRSC_RSA_MODULUS, PROTO_OPENRSC_177, ""},
    // Coleslaw: botting-allowed, 2x XP, custom content
    {"OpenRSC_Coleslaw", "game.openrsc.com", USE_WEBSOCKS ? 43499 : 43599,
     "00010001", OPENRSC_RSA_MODULUS, PROTO_CUSTOM_10010, ""},
    // Cabbage: staff/development world
    {"OpenRSC_Cabbage", "game.openrsc.com", USE_WEBSOCKS ? 43495 : 43595,
     "00010001", OPENRSC_RSA_MODULUS, PROTO_CUSTOM_10010, ""},
};

#define NUM_OPENRSC_DEFAULT_WORLDS \
    ((int)(sizeof(openrsc_default_worlds) / sizeof(openrsc_default_worlds[0])))

// writes the default worlds.cfg: curated OpenRSC worlds + community extras
static void worldlist_set_defaults(void) {
    int i;

    for (i = 0; i < NUM_OPENRSC_DEFAULT_WORLDS; i++) {
        list[i] = openrsc_default_worlds[i];
    }

    // community F2P server, own RSA keypair
    strcpy(list[i].name, "Neat_F2P");
    strcpy(list[i].host, "192.3.118.9");
    list[i].port = USE_WEBSOCKS ? 43494 : 43594; // 43494 ws / 43594 native TCP
    strcpy(list[i].rsa_exponent, "00010001");
    strcpy(list[i].rsa_modulus, "86b03ac30518bdb3e508ca9660efc7738a73ee7dbedbcebf8c56d030a2bdae70503c60829b7fb5eceb529442234c21bce6d529c8da4fce870e83ceffc379e281");
    list[i].protocol = PROTO_OPENRSC_177;
    list[i].content[0] = '\0';
}

static void worldlist_read_presets(struct mudclient *mud) {
    char path[PATH_MAX];
    int num = 0;

    panel_clear_list(mud->panel_login_worldlist, mud->control_list_worlds);

    get_config_path("worlds.cfg", path);

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        worldlist_set_defaults();
        file = fopen(path, "w");
        if (file != NULL) {
            for (int i = 0; list[i].name[0] != '\0'; ++i) {
                fprintf(file, "%s %s %d %s %s %d %s\n",
                    list[i].name, list[i].host, list[i].port,
                    list[i].rsa_exponent, list[i].rsa_modulus,
                    list[i].protocol,
                    list[i].content[0] ? list[i].content : "-");
            }
            fclose(file);
        }
        return;
    }
    // parses 5-field (legacy) or 7-field worlds.cfg lines; skips bad rows
    char line[1200];
    while (num < 255 && fgets(line, sizeof(line), file) != NULL) {
        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') {
            continue; // blank line
        }
        list[num].protocol = PROTO_OPENRSC_177;
        list[num].content[0] = '\0';
        char content_tmp[32] = "";
        int n = sscanf(line, "%31s %63s %d %500s %500s %d %31s",
            list[num].name, list[num].host, &list[num].port,
            list[num].rsa_exponent, list[num].rsa_modulus,
            &list[num].protocol, content_tmp);
        if (n < 5) {
            list[num].name[0] = '\0'; // garbage line: skip, keep parsing
            continue;
        }
        if (n < 6) {
            list[num].protocol = PROTO_OPENRSC_177; // legacy 5-field line
        }
        // unknown protocol id falls back to 177
        if (list[num].protocol != PROTO_OPENRSC_177 &&
            list[num].protocol != PROTO_AUTHENTIC_204 &&
            list[num].protocol != PROTO_CUSTOM_10010) {
            list[num].protocol = PROTO_OPENRSC_177;
        }
        if (n >= 7 && strcmp(content_tmp, "-") != 0) {
            snprintf(list[num].content, sizeof(list[num].content), "%s",
                     content_tmp);
        }
        num++;
    }
    fclose(file);
}

// appends missing curated worlds, matched by host+port; not persisted
static void worldlist_ensure_defaults(void) {
    int count = 0;

    while (count < 255 && list[count].name[0] != '\0') {
        count++;
    }

    for (int d = 0; d < NUM_OPENRSC_DEFAULT_WORLDS && count < 255; d++) {
        int present = 0;

        for (int i = 0; i < count; i++) {
            if (list[i].port == openrsc_default_worlds[d].port &&
                strcmp(list[i].host, openrsc_default_worlds[d].host) == 0) {
                present = 1;
                break;
            }
        }

        if (!present) {
            list[count++] = openrsc_default_worlds[d];
        }
    }
}

#ifdef WITH_SINGLEPLAYER
// a single-player world: one save profile per line in sp-worlds.cfg
struct sp_world_type {
    char id[32];
    char name[48];
    int xp_rate;
    int members;
    int fatigue; // 1 = fatigue on (classic), 0 = no fatigue
    int remember_style; // 1 = remember last combat style between logins
    int game_speed; // tick multiplier: 1 authentic, 2 double
    int custom_quests; // 1 = OpenRSC custom quests enabled
    int holiday_events; // 1 = holiday events enabled

    // feature toggles, applied at the next server boot
    int tutorial_island; // tutorialIsland
    int skillcape_perks; // wantSkillcapePerks
    int combat_odyssey; // wantCombatOdyssey
    int poison_npcs; // wantPoisonNpcs
    int leftclick_webs; // wantLeftclickWebs (no editor row)
    int guild_greetings; // wantMissingGuildGreetings
    int faster_yohnus; // fasterYohnus
    int uses_classes; // usesClasses gates the Class selector
    int spawn_ironman; // spawnIronMan gates the Mode selector

    // second set of feature toggles, same config.json key convention
    int custom_firemaking; // customFiremaking
    int better_jewelry_crafting; // wantBetterJewelryCrafting
    int custom_leather; // wantCustomLeather
    int new_rare_drop_tables; // wantNewRareDropTables
    int npc_kill_messages; // npcKillMessages
    int enchanted_crowns; // wantEnchantedCrowns
    int batch_progression; // wantBatchProgression

    // multiplayer host mode: 0 = offline, 1 = LAN, 2 = ad-hoc
    int net_mode;
};

// net_mode values, keep in sync with spnet_mode in sp-net.h
#define SP_NET_OFFLINE 0
#define SP_NET_LAN     1
#define SP_NET_ADHOC   2

static struct sp_world_type sp_list[64] = {0};
static int sp_count = 0;
static int control_list_sp = -1; // the single-player world list (right column)

static void worldlist_select_sp(mudclient *mud, int index);

static void sp_worldlist_read(void) {
    char path[PATH_MAX];
    get_config_path("sp-worlds.cfg", path);
    sp_count = 0;

    FILE *file = fopen(path, "r");
    if (file == NULL) {
        // first launch: one default world, saved at the existing saves/ root
        strcpy(sp_list[0].id, "default");
        strcpy(sp_list[0].name, "Single-player");
        sp_list[0].xp_rate = 1;
        sp_list[0].members = 1;
        sp_list[0].fatigue = 1;
        sp_list[0].remember_style = 0;
        sp_list[0].game_speed = 1;
        sp_list[0].custom_quests = 1;
        sp_list[0].holiday_events = 1;
        sp_list[0].tutorial_island = 0;
        sp_list[0].skillcape_perks = 1;
        sp_list[0].combat_odyssey = 1;
        sp_list[0].poison_npcs = 0;
        sp_list[0].leftclick_webs = 0;
        sp_list[0].guild_greetings = 1;
        sp_list[0].faster_yohnus = 0;
        sp_list[0].uses_classes = 1;
        sp_list[0].spawn_ironman = 1;
        sp_list[0].custom_firemaking = 1;
        sp_list[0].better_jewelry_crafting = 1;
        sp_list[0].custom_leather = 1;
        sp_list[0].new_rare_drop_tables = 1;
        sp_list[0].npc_kill_messages = 1;
        sp_list[0].enchanted_crowns = 1;
        sp_list[0].batch_progression = 1;
        sp_list[0].net_mode = SP_NET_OFFLINE;
        sp_count = 1;

        file = fopen(path, "w");
        if (file != NULL) {
            fprintf(file,
                    "%s %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d "
                    "%d %d %d %d %d %d %d\n",
                    sp_list[0].id, sp_list[0].name, sp_list[0].xp_rate,
                    sp_list[0].members, sp_list[0].fatigue,
                    sp_list[0].remember_style, sp_list[0].game_speed,
                    sp_list[0].custom_quests, sp_list[0].holiday_events,
                    sp_list[0].tutorial_island, sp_list[0].skillcape_perks,
                    sp_list[0].combat_odyssey, sp_list[0].poison_npcs,
                    sp_list[0].leftclick_webs, sp_list[0].guild_greetings,
                    sp_list[0].faster_yohnus, sp_list[0].uses_classes,
                    sp_list[0].spawn_ironman, sp_list[0].custom_firemaking,
                    sp_list[0].better_jewelry_crafting,
                    sp_list[0].custom_leather,
                    sp_list[0].new_rare_drop_tables,
                    sp_list[0].npc_kill_messages,
                    sp_list[0].enchanted_crowns,
                    sp_list[0].batch_progression,
                    sp_list[0].net_mode);
            fclose(file);
        }
        return;
    }

    // older shorter lines still load; missing fields default to on
    char line[256];
    while (sp_count < 64 && fgets(line, sizeof(line), file) != NULL) {
        struct sp_world_type *w = &sp_list[sp_count];
        w->fatigue = 1;
        w->remember_style = 0;
        w->game_speed = 1;
        w->custom_quests = 1;
        w->holiday_events = 1;
        w->tutorial_island = 0;
        w->skillcape_perks = 1;
        w->combat_odyssey = 1;
        w->poison_npcs = 0;
        w->leftclick_webs = 0;
        w->guild_greetings = 1;
        w->faster_yohnus = 0;
        w->uses_classes = 1;
        w->spawn_ironman = 1;
        w->custom_firemaking = 1;
        w->better_jewelry_crafting = 1;
        w->custom_leather = 1;
        w->new_rare_drop_tables = 1;
        w->npc_kill_messages = 1;
        w->enchanted_crowns = 1;
        w->batch_progression = 1;
        w->net_mode = SP_NET_OFFLINE;
        int n = sscanf(line,
                       "%31s %47s %d %d %d %d %d %d %d %d %d %d %d %d %d %d "
                       "%d %d %d %d %d %d %d %d %d %d",
                       w->id, w->name, &w->xp_rate, &w->members, &w->fatigue,
                       &w->remember_style, &w->game_speed, &w->custom_quests,
                       &w->holiday_events, &w->tutorial_island,
                       &w->skillcape_perks, &w->combat_odyssey, &w->poison_npcs,
                       &w->leftclick_webs, &w->guild_greetings,
                       &w->faster_yohnus, &w->uses_classes, &w->spawn_ironman,
                       &w->custom_firemaking, &w->better_jewelry_crafting,
                       &w->custom_leather, &w->new_rare_drop_tables,
                       &w->npc_kill_messages, &w->enchanted_crowns,
                       &w->batch_progression, &w->net_mode);
        if (n >= 4) {
            sp_count++;
        }
    }
    fclose(file);
}

// writes sp-worlds.cfg; names store spaces as underscores
static void sp_worldlist_write(void) {
    char path[PATH_MAX];
    get_config_path("sp-worlds.cfg", path);

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }

    for (int i = 0; i < sp_count; i++) {
        char stored[64];
        snprintf(stored, sizeof(stored), "%s", sp_list[i].name);
        for (int j = 0; stored[j] != '\0'; j++) {
            if (stored[j] == ' ') {
                stored[j] = '_';
            }
        }
        fprintf(file,
                "%s %s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d "
                "%d %d %d %d %d %d %d\n",
                sp_list[i].id, stored, sp_list[i].xp_rate, sp_list[i].members,
                sp_list[i].fatigue, sp_list[i].remember_style,
                sp_list[i].game_speed, sp_list[i].custom_quests,
                sp_list[i].holiday_events, sp_list[i].tutorial_island,
                sp_list[i].skillcape_perks, sp_list[i].combat_odyssey,
                sp_list[i].poison_npcs, sp_list[i].leftclick_webs,
                sp_list[i].guild_greetings, sp_list[i].faster_yohnus,
                sp_list[i].uses_classes, sp_list[i].spawn_ironman,
                sp_list[i].custom_firemaking,
                sp_list[i].better_jewelry_crafting,
                sp_list[i].custom_leather, sp_list[i].new_rare_drop_tables,
                sp_list[i].npc_kill_messages, sp_list[i].enchanted_crowns,
                sp_list[i].batch_progression, sp_list[i].net_mode);
    }
    fclose(file);
}

// builds a unique, filesystem-safe save id from a display name
static void sp_world_make_id(const char *name, char *out, size_t outsz) {
    size_t j = 0;
    for (size_t i = 0; name[i] != '\0' && j < outsz - 6; i++) {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[j++] = c;
        }
    }
    if (j == 0) {
        out[j++] = 'w';
    }
    out[j] = '\0';

    char base[32];
    snprintf(base, sizeof(base), "%s", out);

    int n = 1;
    for (;;) {
        int collide = (strcmp(out, "default") == 0);
        for (int i = 0; i < sp_count && !collide; i++) {
            if (strcmp(sp_list[i].id, out) == 0) {
                collide = 1;
            }
        }
        if (!collide) {
            break;
        }
        snprintf(out, outsz, "%.20s%d", base, ++n);
    }
}

// single-player world editor: create / rename / XP / f2p / delete

static Panel *sp_editor_panel = NULL;
static int sp_editor_shown = 0;
static int sp_editor_index = -1; // -1 = creating a new world
static int sp_editor_xp = 1;
static int sp_editor_speed = 1;
static int sp_editor_fatigue = 1;
static int sp_editor_remember = 0;
static int sp_editor_quests = 1;
static int sp_editor_holiday = 1;

// editor state mirroring sp_world_type's feature toggles
static int sp_editor_tutorial_island = 0;
static int sp_editor_skillcape_perks = 1;
static int sp_editor_combat_odyssey = 1;
static int sp_editor_poison_npcs = 0;
static int sp_editor_guild_greetings = 1;
static int sp_editor_faster_yohnus = 0;
static int sp_editor_uses_classes = 1;
static int sp_editor_spawn_ironman = 1;

// second set of feature-toggle editor state, all default on
static int sp_editor_custom_firemaking = 1;
static int sp_editor_better_jewelry_crafting = 1;
static int sp_editor_custom_leather = 1;
static int sp_editor_new_rare_drop_tables = 1;
static int sp_editor_npc_kill_messages = 1;
static int sp_editor_enchanted_crowns = 1;
static int sp_editor_batch_progression = 1;

// per-world host mode; cycles on the Multiplayer row
static int sp_editor_net_mode = SP_NET_OFFLINE;

static int sp_ec_title, sp_ec_name, sp_ec_xp_val, sp_ec_xp_prev, sp_ec_xp_next,
    sp_ec_speed_val, sp_ec_speed_prev, sp_ec_speed_next, sp_ec_fatigue_txt,
    sp_ec_fatigue_btn, sp_ec_remember_txt, sp_ec_remember_btn, sp_ec_quests_txt,
    sp_ec_quests_btn, sp_ec_holiday_txt, sp_ec_holiday_btn, sp_ec_save,
    sp_ec_cancel, sp_ec_delete, sp_ec_delete_bg, sp_ec_delete_txt;
static int control_new_world = -1; // "+ New world" (world-list panel)
static int control_edit_world = -1; // "Edit" (world-list panel)

// editor pages: World / Content / Gameplay / Players, switched by tabs; each
// is a control-id range shown/hidden together
static int sp_editor_page = 0; // 0 World, 1 Content, 2 Gameplay, 3 Players
static int sp_editor_page1_start, sp_editor_page2_start, sp_editor_page2_end,
    sp_editor_page3_start, sp_editor_page3_end, sp_editor_page4_start,
    sp_editor_page4_end;
static int sp_ec_tab_world, sp_ec_tab_world_txt;
static int sp_ec_tab_content, sp_ec_tab_content_txt;
static int sp_ec_tab_gameplay, sp_ec_tab_gameplay_txt;
static int sp_ec_tab_players, sp_ec_tab_players_txt;

// Players page: account management for the world being edited
#define SP_MAX_ACCOUNT_LIST 64
static int sp_ec_players_list; // the scrollable username list
static int sp_ec_players_header; // "Accounts (N):"
static int sp_ec_players_delete; // delete-selected button
static int sp_ec_players_delete_txt;
// usernames shown, indexed to match the list rows
static char sp_account_names[SP_MAX_ACCOUNT_LIST][32];
static int sp_account_count = 0;
// selected account row, latched on list click; -1 = none
static int sp_account_selected = -1;
// two-click delete confirm; -1 = disarmed
static int sp_account_delete_armed = -1;
static int sp_ec_preset_authentic, sp_ec_preset_everything;

static int sp_ec_tutorial_txt, sp_ec_tutorial_btn;
static int sp_ec_skillcape_txt, sp_ec_skillcape_btn;
static int sp_ec_odyssey_txt, sp_ec_odyssey_btn;
static int sp_ec_poison_txt, sp_ec_poison_btn;
static int sp_ec_greetings_txt, sp_ec_greetings_btn;
static int sp_ec_yohnus_txt, sp_ec_yohnus_btn;
static int sp_ec_classes_txt, sp_ec_classes_btn;
static int sp_ec_ironman_txt, sp_ec_ironman_btn;

static int sp_ec_firemaking_txt, sp_ec_firemaking_btn;
static int sp_ec_jewelry_txt, sp_ec_jewelry_btn;
static int sp_ec_leather_txt, sp_ec_leather_btn;
static int sp_ec_raredrops_txt, sp_ec_raredrops_btn;
static int sp_ec_killmsgs_txt, sp_ec_killmsgs_btn;
static int sp_ec_crowns_txt, sp_ec_crowns_btn;
static int sp_ec_batch_txt, sp_ec_batch_btn;
static int sp_ec_netmode_txt, sp_ec_netmode_btn;

// shows the active page's controls, hides the rest; recolours tabs
static void sp_editor_set_page(int page) {
    sp_editor_page = page;

    for (int i = sp_editor_page1_start; i < sp_editor_page2_start; i++) {
        if (page == 0) {
            panel_show(sp_editor_panel, i);
        } else {
            panel_hide(sp_editor_panel, i);
        }
    }

    for (int i = sp_editor_page2_start; i < sp_editor_page2_end; i++) {
        if (page == 1) {
            panel_show(sp_editor_panel, i);
        } else {
            panel_hide(sp_editor_panel, i);
        }
    }

    for (int i = sp_editor_page3_start; i < sp_editor_page3_end; i++) {
        if (page == 2) {
            panel_show(sp_editor_panel, i);
        } else {
            panel_hide(sp_editor_panel, i);
        }
    }

    for (int i = sp_editor_page4_start; i < sp_editor_page4_end; i++) {
        if (page == 3) {
            panel_show(sp_editor_panel, i);
        } else {
            panel_hide(sp_editor_panel, i);
        }
    }

    // leaving the Players page disarms a pending delete-confirm
    if (page != 3) {
        sp_account_delete_armed = -1;
    }

    panel_update_text(sp_editor_panel, sp_ec_tab_world_txt,
                      page == 0 ? "@yel@World" : "@whi@World");
    panel_update_text(sp_editor_panel, sp_ec_tab_content_txt,
                      page == 1 ? "@yel@Content" : "@whi@Content");
    panel_update_text(sp_editor_panel, sp_ec_tab_gameplay_txt,
                      page == 2 ? "@yel@Gameplay" : "@whi@Gameplay");
    panel_update_text(sp_editor_panel, sp_ec_tab_players_txt,
                      page == 3 ? "@yel@Players" : "@whi@Players");
}

// adds a "label [On/Off]" toggle row
static void sp_editor_add_toggle_row(Panel *panel, int cx, int y,
                                     const char *label, int *out_txt,
                                     int *out_btn) {
    panel_add_text(panel, cx - 135, y, (char *)label, FONT_BOLD_12, 1);
    panel_add_button_background(panel, cx + 30, y - 4, 120, 18);
    *out_txt = panel_add_text_centre(panel, cx + 30, y - 4, "", FONT_BOLD_12, 0);
    *out_btn = panel_add_button(panel, cx + 30, y - 4, 120, 18);
}

static void worldlist_select_sp(mudclient *mud, int index);

// UI label for a net_mode value (editor row + boot status).
static const char *sp_net_mode_label(int m) {
    if (m == SP_NET_LAN) {
        return "LAN (WiFi)";
    }
    if (m == SP_NET_ADHOC) {
        return "Ad-hoc";
    }
    return "Offline";
}

// rebuild the right-hand single-player list after a create/rename/delete
static void sp_worldlist_refresh_panel(mudclient *mud) {
    panel_clear_list(mud->panel_login_worldlist, control_list_sp);
    for (int i = 0; i < sp_count; i++) {
        char disp[48];
        snprintf(disp, sizeof(disp), "%s", sp_list[i].name);
        for (int j = 0; disp[j] != '\0'; j++) {
            if (disp[j] == '_') {
                disp[j] = ' ';
            }
        }
        panel_add_list_entry(mud->panel_login_worldlist, control_list_sp, i, disp);
    }
}

static void sp_editor_refresh_values(void) {
    char buf[24];
    panel_update_text(sp_editor_panel, sp_ec_title,
                      sp_editor_index < 0 ? "New single-player world"
                                          : "Edit single-player world");
    snprintf(buf, sizeof(buf), "@yel@x%d", sp_editor_xp);
    panel_update_text(sp_editor_panel, sp_ec_xp_val, buf);
    snprintf(buf, sizeof(buf), "@yel@x%d", sp_editor_speed);
    panel_update_text(sp_editor_panel, sp_ec_speed_val, buf);
    panel_update_text(sp_editor_panel, sp_ec_fatigue_txt,
                      sp_editor_fatigue ? "On (classic)" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_remember_txt,
                      sp_editor_remember ? "Yes" : "No");
    panel_update_text(sp_editor_panel, sp_ec_quests_txt,
                      sp_editor_quests ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_holiday_txt,
                      sp_editor_holiday ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_tutorial_txt,
                      sp_editor_tutorial_island ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_skillcape_txt,
                      sp_editor_skillcape_perks ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_odyssey_txt,
                      sp_editor_combat_odyssey ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_poison_txt,
                      sp_editor_poison_npcs ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_greetings_txt,
                      sp_editor_guild_greetings ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_yohnus_txt,
                      sp_editor_faster_yohnus ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_classes_txt,
                      sp_editor_uses_classes ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_ironman_txt,
                      sp_editor_spawn_ironman ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_firemaking_txt,
                      sp_editor_custom_firemaking ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_jewelry_txt,
                      sp_editor_better_jewelry_crafting ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_leather_txt,
                      sp_editor_custom_leather ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_raredrops_txt,
                      sp_editor_new_rare_drop_tables ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_killmsgs_txt,
                      sp_editor_npc_kill_messages ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_crowns_txt,
                      sp_editor_enchanted_crowns ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_batch_txt,
                      sp_editor_batch_progression ? "On" : "Off");
    panel_update_text(sp_editor_panel, sp_ec_netmode_txt,
                      (char *)sp_net_mode_label(sp_editor_net_mode));
}

// applies the Authentic (0) or Everything (1) feature-toggle preset
static void sp_editor_apply_preset(int everything) {
    sp_editor_tutorial_island = 1;
    sp_editor_quests = everything;
    sp_editor_holiday = everything;
    sp_editor_combat_odyssey = everything;
    sp_editor_enchanted_crowns = everything;
    sp_editor_custom_leather = everything;
    sp_editor_custom_firemaking = everything;
    sp_editor_better_jewelry_crafting = everything;
    sp_editor_new_rare_drop_tables = everything;
    sp_editor_skillcape_perks = everything;
    sp_editor_batch_progression = everything;
    sp_editor_poison_npcs = everything;
    sp_editor_guild_greetings = everything;
    sp_editor_faster_yohnus = everything;
    sp_editor_npc_kill_messages = everything;
    sp_editor_uses_classes = everything;
    sp_editor_spawn_ironman = everything;
    sp_editor_refresh_values();
}

// builds the editor panel (absolute, screen-centred coordinates)
static void sp_editor_build(mudclient *mud) {
    sp_editor_panel = malloc(sizeof(Panel));
    // 4 pages of rows plus the tab strip and action buttons
    panel_new(sp_editor_panel, mud->surface, 180);

    int cx = mud->surface->width / 2;
    int cy = mud->surface->height / 2;

    sp_ec_title = panel_add_text_centre(sp_editor_panel, cx, cy - 150, "",
                                        FONT_BOLD_14, 1);

    // page tabs: World / Content / Gameplay / Players
    panel_add_button_background(sp_editor_panel, cx - 150, cy - 128, 92, 18);
    sp_ec_tab_world_txt = panel_add_text_centre(
        sp_editor_panel, cx - 150, cy - 128, "@yel@World", FONT_BOLD_12, 0);
    sp_ec_tab_world =
        panel_add_button(sp_editor_panel, cx - 150, cy - 128, 92, 18);

    panel_add_button_background(sp_editor_panel, cx - 50, cy - 128, 92, 18);
    sp_ec_tab_content_txt = panel_add_text_centre(
        sp_editor_panel, cx - 50, cy - 128, "@whi@Content", FONT_BOLD_12, 0);
    sp_ec_tab_content =
        panel_add_button(sp_editor_panel, cx - 50, cy - 128, 92, 18);

    panel_add_button_background(sp_editor_panel, cx + 50, cy - 128, 92, 18);
    sp_ec_tab_gameplay_txt = panel_add_text_centre(
        sp_editor_panel, cx + 50, cy - 128, "@whi@Gameplay", FONT_BOLD_12, 0);
    sp_ec_tab_gameplay =
        panel_add_button(sp_editor_panel, cx + 50, cy - 128, 92, 18);

    panel_add_button_background(sp_editor_panel, cx + 150, cy - 128, 92, 18);
    sp_ec_tab_players_txt = panel_add_text_centre(
        sp_editor_panel, cx + 150, cy - 128, "@whi@Players", FONT_BOLD_12, 0);
    sp_ec_tab_players =
        panel_add_button(sp_editor_panel, cx + 150, cy - 128, 92, 18);

    sp_editor_page1_start = sp_editor_panel->control_count;

    // Page 1: World (name, XP, speed, fatigue, presets)

    // name field
    panel_add_text(sp_editor_panel, cx - 135, cy - 104, "Name:", FONT_BOLD_12, 1);
    panel_add_button_background(sp_editor_panel, cx + 30, cy - 108, 150, 20);
    sp_ec_name = panel_add_text_input(sp_editor_panel, cx + 30, cy - 108, 150, 20,
                                      FONT_BOLD_12, 20, 0, 0);

    // XP rate
    panel_add_text(sp_editor_panel, cx - 135, cy - 80, "XP rate:", FONT_BOLD_12,
                   1);
    panel_add_button_background(sp_editor_panel, cx - 5, cy - 84, 14, 14);
    panel_add_text_centre(sp_editor_panel, cx - 5, cy - 84, "@whi@<", FONT_BOLD_12,
                          0);
    sp_ec_xp_prev = panel_add_button(sp_editor_panel, cx - 5, cy - 84, 14, 14);
    sp_ec_xp_val = panel_add_text_centre(sp_editor_panel, cx + 30, cy - 84, "",
                                         FONT_BOLD_12, 1);
    panel_add_button_background(sp_editor_panel, cx + 65, cy - 84, 14, 14);
    panel_add_text_centre(sp_editor_panel, cx + 65, cy - 84, "@whi@>", FONT_BOLD_12,
                          0);
    sp_ec_xp_next = panel_add_button(sp_editor_panel, cx + 65, cy - 84, 14, 14);

    // Game speed
    panel_add_text(sp_editor_panel, cx - 135, cy - 56, "Game speed:", FONT_BOLD_12,
                   1);
    panel_add_button_background(sp_editor_panel, cx - 5, cy - 60, 14, 14);
    panel_add_text_centre(sp_editor_panel, cx - 5, cy - 60, "@whi@<", FONT_BOLD_12,
                          0);
    sp_ec_speed_prev = panel_add_button(sp_editor_panel, cx - 5, cy - 60, 14, 14);
    sp_ec_speed_val = panel_add_text_centre(sp_editor_panel, cx + 30, cy - 60, "",
                                            FONT_BOLD_12, 1);
    panel_add_button_background(sp_editor_panel, cx + 65, cy - 60, 14, 14);
    panel_add_text_centre(sp_editor_panel, cx + 65, cy - 60, "@whi@>",
                          FONT_BOLD_12, 0);
    sp_ec_speed_next = panel_add_button(sp_editor_panel, cx + 65, cy - 60, 14, 14);

    // Fatigue on/off
    panel_add_text(sp_editor_panel, cx - 135, cy - 32, "Fatigue:", FONT_BOLD_12, 1);
    panel_add_button_background(sp_editor_panel, cx + 30, cy - 36, 120, 18);
    sp_ec_fatigue_txt = panel_add_text_centre(sp_editor_panel, cx + 30, cy - 36,
                                              "", FONT_BOLD_12, 0);
    sp_ec_fatigue_btn = panel_add_button(sp_editor_panel, cx + 30, cy - 36, 120,
                                         18);

    // Remember combat style
    panel_add_text(sp_editor_panel, cx - 135, cy - 8, "Remember style:",
                   FONT_BOLD_12, 1);
    panel_add_button_background(sp_editor_panel, cx + 30, cy - 12, 120, 18);
    sp_ec_remember_txt = panel_add_text_centre(sp_editor_panel, cx + 30, cy - 12,
                                               "", FONT_BOLD_12, 0);
    sp_ec_remember_btn = panel_add_button(sp_editor_panel, cx + 30, cy - 12, 120,
                                          18);

    // character-creation toggles: gate the Class/Mode selectors
    panel_add_text_centre(sp_editor_panel, cx, cy + 16, "Character creation:",
                          FONT_BOLD_12, 1);

    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 40, "Classes:",
                             &sp_ec_classes_txt, &sp_ec_classes_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 64, "Ironman modes:",
                             &sp_ec_ironman_txt, &sp_ec_ironman_btn);

    // presets: set every feature toggle at once
    panel_add_text(sp_editor_panel, cx - 135, cy + 88, "Presets:", FONT_BOLD_12,
                   1);
    panel_add_button_background(sp_editor_panel, cx + 10, cy + 84, 80, 18);
    panel_add_text_centre(sp_editor_panel, cx + 10, cy + 84, "Authentic",
                          FONT_BOLD_12, 0);
    sp_ec_preset_authentic =
        panel_add_button(sp_editor_panel, cx + 10, cy + 84, 80, 18);
    panel_add_button_background(sp_editor_panel, cx + 98, cy + 84, 80, 18);
    panel_add_text_centre(sp_editor_panel, cx + 98, cy + 84, "Everything",
                          FONT_BOLD_12, 0);
    sp_ec_preset_everything =
        panel_add_button(sp_editor_panel, cx + 98, cy + 84, 80, 18);

    sp_editor_page2_start = sp_editor_panel->control_count;

    // Page 2: Content (quests, holiday events, custom content)
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 104, "OpenRSC quests:",
                             &sp_ec_quests_txt, &sp_ec_quests_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 80, "Holiday events:",
                             &sp_ec_holiday_txt, &sp_ec_holiday_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 56, "Tutorial Island:",
                             &sp_ec_tutorial_txt, &sp_ec_tutorial_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 32, "Combat Odyssey:",
                             &sp_ec_odyssey_txt, &sp_ec_odyssey_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 8,
                             "Enchanted crowns:", &sp_ec_crowns_txt,
                             &sp_ec_crowns_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 16, "Custom leather:",
                             &sp_ec_leather_txt, &sp_ec_leather_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 40,
                             "Custom firemaking:", &sp_ec_firemaking_txt,
                             &sp_ec_firemaking_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 64, "Better jewelry:",
                             &sp_ec_jewelry_txt, &sp_ec_jewelry_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 88, "Rare drop tables:",
                             &sp_ec_raredrops_txt, &sp_ec_raredrops_btn);

    sp_editor_page2_end = sp_editor_panel->control_count;
    sp_editor_page3_start = sp_editor_panel->control_count;

    // Page 3: Gameplay (remaining QoL/behaviour toggles)
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 104, "Skillcape perks:",
                             &sp_ec_skillcape_txt, &sp_ec_skillcape_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 80,
                             "Batch progression:", &sp_ec_batch_txt,
                             &sp_ec_batch_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 56, "Poison NPCs:",
                             &sp_ec_poison_txt, &sp_ec_poison_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 32, "Guild greetings:",
                             &sp_ec_greetings_txt, &sp_ec_greetings_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy - 8, "Faster Yohnus:",
                             &sp_ec_yohnus_txt, &sp_ec_yohnus_btn);
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 16, "NPC kill msgs:",
                             &sp_ec_killmsgs_txt, &sp_ec_killmsgs_btn);
    // multiplayer host mode: cycles Offline/LAN/Ad-hoc
    sp_editor_add_toggle_row(sp_editor_panel, cx, cy + 40, "Multiplayer:",
                             &sp_ec_netmode_txt, &sp_ec_netmode_btn);

    sp_editor_page3_end = sp_editor_panel->control_count;
    sp_editor_page4_start = sp_editor_panel->control_count;

    // Page 4: Players (account roster, edit-mode only)
    sp_ec_players_header = panel_add_text(sp_editor_panel, cx - 150, cy - 106,
                                          "Accounts:", FONT_BOLD_12, 1);
    sp_ec_players_list = panel_add_text_list_interactive(
        sp_editor_panel, cx - 150, cy - 90, 300, 150, FONT_REGULAR_11, 24, 1);
    sp_editor_panel->control_activated[sp_ec_players_list] = -1;

    // hint text + delete button, side by side
    panel_add_text(sp_editor_panel, cx - 150, cy + 72,
                   "@gr1@Select an account", FONT_BOLD_12, 1);
    panel_add_button_background(sp_editor_panel, cx + 110, cy + 70, 120, 18);
    sp_ec_players_delete_txt = panel_add_text_centre(
        sp_editor_panel, cx + 110, cy + 70, "Delete account", FONT_BOLD_12, 0);
    sp_ec_players_delete =
        panel_add_button(sp_editor_panel, cx + 110, cy + 70, 120, 18);

    sp_editor_page4_end = sp_editor_panel->control_count;

    // action buttons (shared across all pages)
    panel_add_button_background(sp_editor_panel, cx - 72, cy + 118, 60, 18);
    panel_add_text_centre(sp_editor_panel, cx - 72, cy + 118, "Cancel",
                          FONT_BOLD_12, 0);
    sp_ec_cancel = panel_add_button(sp_editor_panel, cx - 72, cy + 118, 60, 18);

    panel_add_button_background(sp_editor_panel, cx, cy + 118, 60, 18);
    panel_add_text_centre(sp_editor_panel, cx, cy + 118, "Save", FONT_BOLD_12, 0);
    sp_ec_save = panel_add_button(sp_editor_panel, cx, cy + 118, 60, 18);

    sp_ec_delete_bg =
        panel_add_button_background(sp_editor_panel, cx + 72, cy + 118, 60, 18);
    sp_ec_delete_txt = panel_add_text_centre(sp_editor_panel, cx + 72, cy + 118,
                                             "Delete", FONT_BOLD_12, 0);
    sp_ec_delete = panel_add_button(sp_editor_panel, cx + 72, cy + 118, 60, 18);
}

// reloads the account roster for the world being edited
static void sp_editor_refresh_players(void) {
    sp_account_delete_armed = -1;
    sp_account_selected = -1;
    panel_update_text(sp_editor_panel, sp_ec_players_delete_txt, "Delete account");
    panel_clear_list(sp_editor_panel, sp_ec_players_list);
    sp_editor_panel->control_activated[sp_ec_players_list] = -1;
    sp_account_count = 0;

    if (sp_editor_index < 0 || sp_editor_index >= sp_count) {
        panel_update_text(sp_editor_panel, sp_ec_players_header, "Accounts:");
        return;
    }

    int n = singleplayer_players_list(sp_list[sp_editor_index].id,
                                      sp_account_names, SP_MAX_ACCOUNT_LIST);
    if (n < 0) {
        n = 0;
    }
    sp_account_count = n;
    for (int i = 0; i < n; i++) {
        panel_add_list_entry(sp_editor_panel, sp_ec_players_list, i,
                             sp_account_names[i]);
    }

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "Accounts (%d):", n);
    panel_update_text(sp_editor_panel, sp_ec_players_header, hdr);
}

static void worldlist_open_editor(mudclient *mud, int index) {
    if (sp_editor_panel == NULL) {
        return;
    }
    sp_editor_index = index;

    // Delete + Players tab only apply to an existing world
    int show_delete = (index >= 0);
    sp_editor_panel->control_shown[sp_ec_delete] = show_delete;
    sp_editor_panel->control_shown[sp_ec_delete_bg] = show_delete;
    sp_editor_panel->control_shown[sp_ec_delete_txt] = show_delete;
    sp_editor_panel->control_shown[sp_ec_tab_players] = show_delete;
    sp_editor_panel->control_shown[sp_ec_tab_players_txt] = show_delete;

    if (index >= 0 && index < sp_count) {
        sp_editor_xp = sp_list[index].xp_rate;
        sp_editor_speed = sp_list[index].game_speed;
        sp_editor_fatigue = sp_list[index].fatigue;
        sp_editor_remember = sp_list[index].remember_style;
        sp_editor_quests = sp_list[index].custom_quests;
        sp_editor_holiday = sp_list[index].holiday_events;
        sp_editor_tutorial_island = sp_list[index].tutorial_island;
        sp_editor_skillcape_perks = sp_list[index].skillcape_perks;
        sp_editor_combat_odyssey = sp_list[index].combat_odyssey;
        sp_editor_poison_npcs = sp_list[index].poison_npcs;
        // leftclick_webs has no editor state, preserved as-is
        sp_editor_guild_greetings = sp_list[index].guild_greetings;
        sp_editor_faster_yohnus = sp_list[index].faster_yohnus;
        sp_editor_uses_classes = sp_list[index].uses_classes;
        sp_editor_spawn_ironman = sp_list[index].spawn_ironman;
        sp_editor_custom_firemaking = sp_list[index].custom_firemaking;
        sp_editor_better_jewelry_crafting =
            sp_list[index].better_jewelry_crafting;
        sp_editor_custom_leather = sp_list[index].custom_leather;
        sp_editor_new_rare_drop_tables = sp_list[index].new_rare_drop_tables;
        sp_editor_npc_kill_messages = sp_list[index].npc_kill_messages;
        sp_editor_enchanted_crowns = sp_list[index].enchanted_crowns;
        sp_editor_batch_progression = sp_list[index].batch_progression;
        sp_editor_net_mode = sp_list[index].net_mode;
        char disp[48];
        snprintf(disp, sizeof(disp), "%s", sp_list[index].name);
        for (int j = 0; disp[j] != '\0'; j++) {
            if (disp[j] == '_') {
                disp[j] = ' ';
            }
        }
        panel_update_text(sp_editor_panel, sp_ec_name, disp);
    } else {
        sp_editor_xp = 1;
        sp_editor_speed = 1;
        sp_editor_fatigue = 1;
        sp_editor_remember = 0;
        sp_editor_quests = 1;
        sp_editor_holiday = 1;
        // new-world defaults
        sp_editor_tutorial_island = 0;
        sp_editor_skillcape_perks = 1;
        sp_editor_combat_odyssey = 1;
        sp_editor_poison_npcs = 0;
        sp_editor_guild_greetings = 1;
        sp_editor_faster_yohnus = 0;
        sp_editor_uses_classes = 1;
        sp_editor_spawn_ironman = 1;
        sp_editor_custom_firemaking = 1;
        sp_editor_better_jewelry_crafting = 1;
        sp_editor_custom_leather = 1;
        sp_editor_new_rare_drop_tables = 1;
        sp_editor_npc_kill_messages = 1;
        sp_editor_enchanted_crowns = 1;
        sp_editor_batch_progression = 1;
        sp_editor_net_mode = SP_NET_OFFLINE; // new worlds default to solo
        panel_update_text(sp_editor_panel, sp_ec_name, "");
    }

    sp_editor_refresh_values();
    sp_editor_refresh_players(); // load this world's account roster
    sp_editor_set_page(0); // always open on the World page
    sp_editor_shown = 1;
}

static void sp_editor_save(mudclient *mud) {
    char *typed = panel_get_text(sp_editor_panel, sp_ec_name);
    char name[48];
    snprintf(name, sizeof(name), "%s",
             (typed != NULL && typed[0] != '\0') ? typed : "World");

    if (sp_editor_index < 0) {
        if (sp_count < 64) {
            struct sp_world_type *w = &sp_list[sp_count];
            sp_world_make_id(name, w->id, sizeof(w->id));
            snprintf(w->name, sizeof(w->name), "%s", name);
            w->xp_rate = sp_editor_xp;
            w->members = 1; // single-player worlds always get the full game
            w->fatigue = sp_editor_fatigue;
            w->remember_style = sp_editor_remember;
            w->game_speed = sp_editor_speed;
            w->custom_quests = sp_editor_quests;
            w->holiday_events = sp_editor_holiday;
            w->tutorial_island = sp_editor_tutorial_island;
            w->skillcape_perks = sp_editor_skillcape_perks;
            w->combat_odyssey = sp_editor_combat_odyssey;
            w->poison_npcs = sp_editor_poison_npcs;
            // no editor row; explicit default for a new world
            w->leftclick_webs = 0;
            w->guild_greetings = sp_editor_guild_greetings;
            w->faster_yohnus = sp_editor_faster_yohnus;
            w->uses_classes = sp_editor_uses_classes;
            w->spawn_ironman = sp_editor_spawn_ironman;
            w->custom_firemaking = sp_editor_custom_firemaking;
            w->better_jewelry_crafting = sp_editor_better_jewelry_crafting;
            w->custom_leather = sp_editor_custom_leather;
            w->new_rare_drop_tables = sp_editor_new_rare_drop_tables;
            w->npc_kill_messages = sp_editor_npc_kill_messages;
            w->enchanted_crowns = sp_editor_enchanted_crowns;
            w->batch_progression = sp_editor_batch_progression;
            w->net_mode = sp_editor_net_mode;
            sp_count++;
        }
    } else if (sp_editor_index < sp_count) {
        // keep the id (renaming a world must not orphan its saves)
        struct sp_world_type *w = &sp_list[sp_editor_index];
        snprintf(w->name, sizeof(w->name), "%s", name);
        w->xp_rate = sp_editor_xp;
        w->members = 1; // single-player worlds always get the full game
        w->fatigue = sp_editor_fatigue;
        w->remember_style = sp_editor_remember;
        w->game_speed = sp_editor_speed;
        w->custom_quests = sp_editor_quests;
        w->holiday_events = sp_editor_holiday;
        w->tutorial_island = sp_editor_tutorial_island;
        w->skillcape_perks = sp_editor_skillcape_perks;
        w->combat_odyssey = sp_editor_combat_odyssey;
        w->poison_npcs = sp_editor_poison_npcs;
        // w->leftclick_webs untouched: no editor row, round-trips as-is
        w->guild_greetings = sp_editor_guild_greetings;
        w->faster_yohnus = sp_editor_faster_yohnus;
        w->uses_classes = sp_editor_uses_classes;
        w->spawn_ironman = sp_editor_spawn_ironman;
        w->custom_firemaking = sp_editor_custom_firemaking;
        w->better_jewelry_crafting = sp_editor_better_jewelry_crafting;
        w->custom_leather = sp_editor_custom_leather;
        w->new_rare_drop_tables = sp_editor_new_rare_drop_tables;
        w->npc_kill_messages = sp_editor_npc_kill_messages;
        w->enchanted_crowns = sp_editor_enchanted_crowns;
        w->batch_progression = sp_editor_batch_progression;
        w->net_mode = sp_editor_net_mode;
    }

    sp_worldlist_write();
    sp_worldlist_refresh_panel(mud);
    sp_editor_shown = 0;
}

static void sp_editor_delete(mudclient *mud) {
    if (sp_editor_index < 0 || sp_editor_index >= sp_count) {
        sp_editor_shown = 0;
        return;
    }
    // deletes this world's save files (players + playerID keys) too
    singleplayer_world_wipe(sp_list[sp_editor_index].id);

    for (int i = sp_editor_index; i < sp_count - 1; i++) {
        sp_list[i] = sp_list[i + 1];
    }
    sp_count--;
    sp_worldlist_write();
    sp_worldlist_refresh_panel(mud);
    sp_editor_shown = 0;
}

void worldlist_draw_editor(mudclient *mud) {
    if (!sp_editor_shown || sp_editor_panel == NULL) {
        return;
    }
    int cx = mud->surface->width / 2;
    int cy = mud->surface->height / 2;
    // widened to 420 for the 4-tab strip + Players account list
    surface_draw_box_alpha(mud->surface, cx - 210, cy - 170, 420, 340, 0, 220);
    panel_draw_panel(sp_editor_panel);
}

// feeds a character into the editor's focused control (the Name box)
void worldlist_handle_key(mudclient *mud, int key_code) {
    (void)mud;
    if (sp_editor_shown && sp_editor_panel != NULL) {
        panel_key_press(sp_editor_panel, key_code);
    }
}

// Returns 1 if the editor is open and consumed the input.
static int worldlist_handle_editor(mudclient *mud) {
    if (!sp_editor_shown || sp_editor_panel == NULL) {
        return 0;
    }

    panel_handle_mouse(sp_editor_panel, mud->mouse_x, mud->mouse_y,
                       mud->last_mouse_button_down, mud->mouse_button_down,
                       mud->mouse_scroll_delta);

    if (panel_is_clicked(sp_editor_panel, sp_ec_xp_prev)) {
        if (sp_editor_xp > 1) {
            sp_editor_xp--;
        }
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_xp_next)) {
        if (sp_editor_xp < 50) {
            sp_editor_xp++;
        }
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_speed_prev)) {
        if (sp_editor_speed > 1) {
            sp_editor_speed--;
        }
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_speed_next)) {
        if (sp_editor_speed < 5) {
            sp_editor_speed++;
        }
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_fatigue_btn)) {
        sp_editor_fatigue = !sp_editor_fatigue;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_remember_btn)) {
        sp_editor_remember = !sp_editor_remember;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_quests_btn)) {
        sp_editor_quests = !sp_editor_quests;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_holiday_btn)) {
        sp_editor_holiday = !sp_editor_holiday;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_tab_world)) {
        sp_editor_set_page(0);
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_tab_content)) {
        sp_editor_set_page(1);
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_tab_gameplay)) {
        sp_editor_set_page(2);
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_tab_players)) {
        sp_editor_set_page(3);
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_players_list)) {
        // latches the clicked row; changing selection cancels delete-confirm
        int e = panel_get_list_entry_index(sp_editor_panel, sp_ec_players_list);
        if (e >= 0 && e < sp_account_count) {
            sp_account_selected = e;
            sp_editor_panel->control_activated[sp_ec_players_list] = e;
            sp_account_delete_armed = -1;
            panel_update_text(sp_editor_panel, sp_ec_players_delete_txt,
                              "Delete account");
        }
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_players_delete)) {
        // deletes the latched selection, with a two-click confirm
        int sel = sp_account_selected;
        if (sel < 0 || sel >= sp_account_count) {
            sp_account_delete_armed = -1;
            panel_update_text(sp_editor_panel, sp_ec_players_delete_txt,
                              "Delete account");
        } else if (sp_account_delete_armed != sel) {
            sp_account_delete_armed = sel; // first click on this row -> arm
            panel_update_text(sp_editor_panel, sp_ec_players_delete_txt,
                              "@red@Confirm?");
        } else {
            // confirmed: remove from players.json and reload the list
            if (sp_editor_index >= 0 && sp_editor_index < sp_count) {
                singleplayer_players_delete(sp_list[sp_editor_index].id,
                                            sp_account_names[sel]);
            }
            sp_editor_refresh_players();
        }
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_preset_authentic)) {
        sp_editor_apply_preset(0);
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_preset_everything)) {
        sp_editor_apply_preset(1);
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_tutorial_btn)) {
        sp_editor_tutorial_island = !sp_editor_tutorial_island;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_skillcape_btn)) {
        sp_editor_skillcape_perks = !sp_editor_skillcape_perks;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_odyssey_btn)) {
        sp_editor_combat_odyssey = !sp_editor_combat_odyssey;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_poison_btn)) {
        sp_editor_poison_npcs = !sp_editor_poison_npcs;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_greetings_btn)) {
        sp_editor_guild_greetings = !sp_editor_guild_greetings;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_yohnus_btn)) {
        sp_editor_faster_yohnus = !sp_editor_faster_yohnus;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_classes_btn)) {
        sp_editor_uses_classes = !sp_editor_uses_classes;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_ironman_btn)) {
        sp_editor_spawn_ironman = !sp_editor_spawn_ironman;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_firemaking_btn)) {
        sp_editor_custom_firemaking = !sp_editor_custom_firemaking;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_jewelry_btn)) {
        sp_editor_better_jewelry_crafting = !sp_editor_better_jewelry_crafting;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_leather_btn)) {
        sp_editor_custom_leather = !sp_editor_custom_leather;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_raredrops_btn)) {
        sp_editor_new_rare_drop_tables = !sp_editor_new_rare_drop_tables;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_killmsgs_btn)) {
        sp_editor_npc_kill_messages = !sp_editor_npc_kill_messages;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_crowns_btn)) {
        sp_editor_enchanted_crowns = !sp_editor_enchanted_crowns;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_batch_btn)) {
        sp_editor_batch_progression = !sp_editor_batch_progression;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_netmode_btn)) {
        // 3-way cycle: Offline -> LAN -> Ad-hoc -> Offline
        sp_editor_net_mode = (sp_editor_net_mode + 1) % 3;
        sp_editor_refresh_values();
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_save)) {
        sp_editor_save(mud);
    } else if (panel_is_clicked(sp_editor_panel, sp_ec_cancel)) {
        sp_editor_shown = 0;
    } else if (sp_editor_index >= 0 &&
               panel_is_clicked(sp_editor_panel, sp_ec_delete)) {
        sp_editor_delete(mud);
    }

    return 1;
}
#endif

#ifdef WITH_SINGLEPLAYER
// Join column: LAN/ad-hoc co-op; lazily inits spnet on first use
static int spnet_mode_inited = 0;
static spnet_mode join_mode = SPNET_MODE_LAN;
static int control_join_refresh = -1;
static int control_join_mode = -1;
static int control_join_mode_txt = -1;
static int control_join_status = -1;

// mud reference for worldlist_join_ensure_ready
static mudclient *join_ensure_mud = NULL;

// rows [0, join_preset_count) are curated presets, rest are scanned co-op worlds
static int join_preset_count = 0;
#define JOIN_SCAN_MAX 16
static spnet_world_info join_scan_results[JOIN_SCAN_MAX];
static int join_scan_count = 0;

// change-detection cache: -1 means "rebuild the list next tick"
static int join_last_scan_count = -1;
static spnet_world_info join_last_scan_snapshot[JOIN_SCAN_MAX];
static spnet_status join_last_status = SPNET_STATUS_OFF;
static spnet_mode join_last_mode = SPNET_MODE_OFF;

// selected single-player world's display name, spaces not underscores
static char sp_selected_display_name[48] = "Single-player";
// selected world's multiplayer host mode
static int sp_selected_net_mode = SP_NET_OFFLINE;

// LAN comes up when the screen is shown (handle_mouse); ad-hoc only via the toggle
void worldlist_join_ensure_ready(void) {
    spnet_mode_inited = 1;
}

// brings the Join transport up in the current join_mode if not already up
void worldlist_join_activate(void) {
    spnet_mode_inited = 1;
    if (spnet_get_mode() != join_mode) {
        spnet_init(join_mode);
    }
}

// rebuilds the Join list from curated presets and/or a scan snapshot
static void worldlist_join_rebuild(mudclient *mud, spnet_status status,
                                   spnet_world_info *scan, int scan_count) {
    panel_clear_list(mud->panel_login_worldlist, mud->control_list_worlds);

    int connected = status == SPNET_STATUS_READY ||
                    status == SPNET_STATUS_HOSTING ||
                    status == SPNET_STATUS_SCANNING;
    // online presets use the game's own network path, independent of spnet
    int show_presets = join_mode != SPNET_MODE_ADHOC;
    int show_scans = join_mode == SPNET_MODE_ADHOC ||
                     (join_mode == SPNET_MODE_LAN && connected);

    int row = 0;
    join_preset_count = 0;

    if (show_presets) {
        for (int i = 0; list[i].name[0] != '\0'; ++i) {
            for (int j = 0; list[i].name[j] != '\0'; ++j) {
                if (list[i].name[j] == '_') {
                    list[i].name[j] = ' ';
                }
            }
            panel_add_list_entry(mud->panel_login_worldlist,
                                 mud->control_list_worlds, row++, list[i].name);
        }
        join_preset_count = row;
    }

    if (show_scans) {
        for (int i = 0; i < scan_count; i++) {
            // player_count is guests only; +1 for the host. -1 = unknown
            char label[80];
            if (scan[i].player_count >= 0) {
                snprintf(label, sizeof(label), "%.63s (%d)",
                         scan[i].name, scan[i].player_count + 1);
            } else {
                snprintf(label, sizeof(label), "%.63s", scan[i].name);
            }
            panel_add_list_entry(mud->panel_login_worldlist,
                                 mud->control_list_worlds, row++, label);
        }
    }

    join_scan_count = show_scans ? scan_count : 0;
    if (join_scan_count > 0) {
        memcpy(join_scan_results, scan,
               sizeof(spnet_world_info) * (size_t)join_scan_count);
    }

    int highlight = -1;
    int lw = mud->options->last_world;
    if (lw >= 0 && lw < join_preset_count) {
        highlight = lw;
    } else if (mud->spnet_guest) {
        for (int i = 0; i < join_scan_count; i++) {
            if (strcmp(join_scan_results[i].address, mud->spnet_address) == 0) {
                highlight = join_preset_count + i;
                break;
            }
        }
    }
    mud->panel_login_worldlist->control_activated[mud->control_list_worlds] =
        highlight;
}

// pumps the transport, refreshes status line, rebuilds list on change
static void worldlist_join_tick(mudclient *mud) {
    join_ensure_mud = mud;
    worldlist_join_ensure_ready();
    spnet_pump();

    spnet_status status = spnet_get_status();
    spnet_world_info snap[JOIN_SCAN_MAX];
    int n = spnet_scan_results(snap, JOIN_SCAN_MAX);

    int changed = join_last_scan_count < 0 || n != join_last_scan_count ||
                 status != join_last_status || join_mode != join_last_mode ||
                 (n > 0 && memcmp(snap, join_last_scan_snapshot,
                                   sizeof(spnet_world_info) * (size_t)n) != 0);

    if (changed) {
        worldlist_join_rebuild(mud, status, snap, n);
        if (n > 0) {
            memcpy(join_last_scan_snapshot, snap,
                   sizeof(spnet_world_info) * (size_t)n);
        }
        join_last_scan_count = n;
        join_last_status = status;
        join_last_mode = join_mode;
    }

    if (control_join_status >= 0) {
        panel_update_text(mud->panel_login_worldlist, control_join_status,
                          (char *)spnet_status_text());
    }
}

// selects a scanned co-op world (a Join-list row beyond the presets)
static void worldlist_select_join_scan(mudclient *mud, int scan_index) {
    if (scan_index < 0 || scan_index >= join_scan_count) {
        return;
    }

    mud->spnet_guest = 1;
    snprintf(mud->spnet_address, sizeof(mud->spnet_address), "%s",
             join_scan_results[scan_index].address);
    snprintf(mud->server, sizeof(mud->server), "%s",
             join_scan_results[scan_index].name);
    mud->singleplayer = 0; // guest join, not the local embedded server
    mud->protocol177 = 0; // guests speak the SP wire, not 177
    mud->protocol_custom = 0; // clear any prior custom-10010 selection
    mud->port = 0; // spnet_address has the connection details

    printf("INFO: Selected LAN/ad-hoc world %s\n",
          join_scan_results[scan_index].name);

    // a scanned co-op world has no persisted slot; last_world is untouched

    mud->panel_login_worldlist->control_activated[mud->control_list_worlds] =
        join_preset_count + scan_index;
    if (control_list_sp >= 0) {
        mud->panel_login_worldlist->control_activated[control_list_sp] = -1;
    }
}

// selected single-player world's display name
const char *worldlist_selected_sp_display_name(void) {
    return sp_selected_display_name;
}

// brings co-op hosting up if the selected SP world opted into it
void worldlist_boot_host_for_selected_world(void) {
    // skip re-init when the transport is already up in the target mode
    spnet_status st = spnet_get_status();
    int already_up = (st != SPNET_STATUS_OFF && st != SPNET_STATUS_NO_NETWORK &&
                      st != SPNET_STATUS_ERROR);

    if (sp_selected_net_mode == SP_NET_ADHOC) {
        if (spnet_get_mode() != SPNET_MODE_ADHOC || !already_up) {
            spnet_init(SPNET_MODE_ADHOC);
        }
        singleplayer_host_set_enabled(1, sp_selected_display_name);
    } else if (sp_selected_net_mode == SP_NET_LAN) {
        if (spnet_get_mode() != SPNET_MODE_LAN || !already_up) {
            spnet_init(SPNET_MODE_LAN);
        }
        singleplayer_host_set_enabled(1, sp_selected_display_name);
    } else {
        // offline: stop any prior hosting and bring the transport down
        singleplayer_host_set_enabled(0, NULL);
        spnet_shutdown();
    }
}
#endif

void worldlist_new(mudclient *mud) {
    int is_compact = mud->surface->width < MUD_VANILLA_WIDTH ||
                     mud->surface->height < MUD_VANILLA_HEIGHT;

    int login_background_height = is_compact ? 125 : 200;

    int x = (is_compact ? MUD_MIN_WIDTH : MUD_VANILLA_WIDTH) / 2;
    int y = login_background_height + 18;

    mud->panel_login_worldlist = malloc(sizeof(Panel));
    assert(mud->panel_login_worldlist != NULL);
    panel_new(mud->panel_login_worldlist, mud->surface, 30);

#ifdef WITH_SINGLEPLAYER
    // online servers on the left, single-player worlds on the right
    int list_y = y + 12;
    int list_w = 280;
    int list_h = 170;
    int left_x = x - 300;
    int right_x = x + 20;

    // bottom row: New world / Edit under the single-player (Host) list
    int sp_btn_y = list_y + list_h + 16;

    // Join column: Refresh + WiFi/Ad-hoc toggle, status line beneath
    int join_btn_w = 84;
    int join_btn_h = 20;
    int join_btn_gap = 12;
    int join_col_centre = left_x + list_w / 2;
    int join_refresh_x = join_col_centre - join_btn_gap / 2 - join_btn_w / 2;
    int join_mode_x = join_col_centre + join_btn_gap / 2 + join_btn_w / 2;
    // Back sits on the shared button row, centred between the two columns
    int back_y = sp_btn_y;
    int join_status_y = sp_btn_y + 22; // status line beneath the Join buttons

    panel_add_text_centre(mud->panel_login_worldlist, left_x + list_w / 2, y,
                          "Join", FONT_BOLD_12, 1);
    panel_add_text_centre(mud->panel_login_worldlist, right_x + list_w / 2, y,
                          "Host", FONT_BOLD_12, 1);

    mud->control_list_worlds = panel_add_text_list_interactive(
        mud->panel_login_worldlist, left_x, list_y, list_w, list_h,
        FONT_REGULAR_11, 256, 1);
    control_list_sp = panel_add_text_list_interactive(
        mud->panel_login_worldlist, right_x, list_y, list_w, list_h,
        FONT_REGULAR_11, 64, 1);

    // nothing highlighted until a world is picked below
    mud->panel_login_worldlist->control_activated[mud->control_list_worlds] = -1;
    mud->panel_login_worldlist->control_activated[control_list_sp] = -1;

    panel_add_button_background(mud->panel_login_worldlist, join_refresh_x,
                                sp_btn_y, join_btn_w, join_btn_h);
    panel_add_text_centre(mud->panel_login_worldlist, join_refresh_x, sp_btn_y,
                          "Refresh", FONT_BOLD_12, 0);
    control_join_refresh = panel_add_button(
        mud->panel_login_worldlist, join_refresh_x, sp_btn_y, join_btn_w,
        join_btn_h);

    panel_add_button_background(mud->panel_login_worldlist, join_mode_x,
                                sp_btn_y, join_btn_w, join_btn_h);
    control_join_mode_txt = panel_add_text_centre(
        mud->panel_login_worldlist, join_mode_x, sp_btn_y, "WiFi", FONT_BOLD_12,
        0);
    control_join_mode = panel_add_button(mud->panel_login_worldlist, join_mode_x,
                                         sp_btn_y, join_btn_w, join_btn_h);

    control_join_status = panel_add_text_centre(
        mud->panel_login_worldlist, join_col_centre, join_status_y,
        "Not connected", FONT_REGULAR_11, 1);

    panel_add_button_background(mud->panel_login_worldlist, x, back_y, 80, 20);
    panel_add_text_centre(mud->panel_login_worldlist, x, back_y, "Back",
                          FONT_BOLD_12, 0);
    mud->control_worldlist_button =
        panel_add_button(mud->panel_login_worldlist, x, back_y, 80, 20);

    panel_add_button_background(mud->panel_login_worldlist, x + 108, sp_btn_y, 96,
                                20);
    panel_add_text_centre(mud->panel_login_worldlist, x + 108, sp_btn_y,
                          "+ New world", FONT_BOLD_12, 0);
    control_new_world = panel_add_button(mud->panel_login_worldlist, x + 108,
                                         sp_btn_y, 96, 20);

    panel_add_button_background(mud->panel_login_worldlist, x + 214, sp_btn_y, 70,
                                20);
    panel_add_text_centre(mud->panel_login_worldlist, x + 214, sp_btn_y, "Edit",
                          FONT_BOLD_12, 0);
    control_edit_world = panel_add_button(mud->panel_login_worldlist, x + 214,
                                          sp_btn_y, 70, 20);

    if (sp_editor_panel == NULL) {
        sp_editor_build(mud);
    }

    worldlist_read_presets(mud);
    worldlist_ensure_defaults();
    sp_worldlist_read();

    // the Join (left) list is populated by worldlist_join_tick() below

    // single-player entries (right)
    for (int i = 0; i < sp_count; ++i) {
        for (int j = 0; sp_list[i].name[j] != '\0'; ++j) {
            if (sp_list[i].name[j] == '_') {
                sp_list[i].name[j] = ' ';
            }
        }
        panel_add_list_entry(mud->panel_login_worldlist, control_list_sp, i,
                             sp_list[i].name);
    }

    // build the Join list immediately, before the first handle_mouse tick
    worldlist_join_tick(mud);

    // restore the last-selected world; default to the first SP world
    {
        int lw = mud->options->last_world;
        int online_count = 0;
        while (list[online_count].name[0] != '\0') {
            online_count++;
        }

        if (lw >= 1000 && (lw - 1000) < sp_count) {
            worldlist_select_sp(mud, lw - 1000);
        } else if (lw >= 0 && lw < online_count) {
            worldlist_select(mud, lw);
        } else {
            worldlist_select_sp(mud, 0); // fall back to first single-player world
        }
    }
#else
    int button_x = (is_compact ? MUD_MIN_WIDTH : MUD_VANILLA_WIDTH) - 36;
    int button_y = is_compact ? MUD_MIN_HEIGHT - 24 : MUD_VANILLA_HEIGHT - 32;
    panel_add_button_background(mud->panel_login_worldlist, button_x, button_y, 60,
                                20);
    panel_add_text_centre(mud->panel_login_worldlist, button_x, button_y, "Back",
                          FONT_BOLD_12, 0);
    mud->control_worldlist_button = panel_add_button(mud->panel_login_worldlist,
                                                     button_x, button_y, 60, 20);

    panel_add_text_centre(mud->panel_login_worldlist, x, y, "Select a world:",
                          FONT_BOLD_12, 1);

    mud->control_list_worlds = panel_add_text_list_interactive(
        mud->panel_login_worldlist, x - 150, y + 12, 250, 170, FONT_REGULAR_11,
        256, 1);
    worldlist_read_presets(mud);
    worldlist_ensure_defaults();

    int world_count = 0;
    for (int i = 0; list[i].name[0] != '\0'; ++i) {
        world_count++;
        for (int j = 0; list[i].name[j] != '\0'; ++j) {
            if (list[i].name[j] == '_') {
                list[i].name[j] = ' ';
            }
        }
        panel_add_list_entry(mud->panel_login_worldlist,
                             mud->control_list_worlds, i, list[i].name);
    }

    if (mud->server[0] == '\0') {
        int world_id = mud->options->last_world;
        if (world_id >= 0 && world_id < world_count) {
            worldlist_select(mud, world_id);
        } else {
            worldlist_select(mud, 0);
        }
    }
#endif
}

static void worldlist_select(mudclient *mud, int index) {
    strcpy(mud->server, list[index].host);
    strcpy(mud->rsa_exponent, list[index].rsa_exponent);
    strcpy(mud->rsa_modulus, list[index].rsa_modulus);
#ifdef WITH_SINGLEPLAYER
    mud->singleplayer = 0; // online worlds are never single-player
    mud->spnet_guest = 0; // nor a scanned LAN/ad-hoc co-op guest join
#endif
    // protocol177=1 for OpenRSC worlds (177 dialect), else the 204 path
    mud->protocol177 = (list[index].protocol == PROTO_OPENRSC_177);
    mud->protocol_custom = (list[index].protocol == PROTO_CUSTOM_10010);
    printf("INFO: Changed world to %s (%s)\n", list[index].name,
           mud->protocol_custom ? "custom-10010"
                                : (mud->protocol177 ? "177" : "204"));
    mud->port = list[index].port;

    int world_changed = mud->options->last_world != index;
    mud->options->last_world = index;

    // persist the choice immediately, before reaching the login screen
    if (world_changed) {
        options_save(mud->options);
    }

    mud->panel_login_worldlist->control_activated[mud->control_list_worlds] = index;
#ifdef WITH_SINGLEPLAYER
    // keep the selection exclusive across both lists (clear the SP highlight)
    if (control_list_sp >= 0) {
        mud->panel_login_worldlist->control_activated[control_list_sp] = -1;
    }
#endif
}

#ifdef WITH_SINGLEPLAYER
static void worldlist_select_sp(mudclient *mud, int index) {
    if (index < 0 || index >= sp_count) {
        return;
    }

    strcpy(mud->server, "singleplayer");
    strcpy(mud->rsa_exponent, "00010001");
    strcpy(mud->rsa_modulus, OPENRSC_RSA_MODULUS);
    mud->singleplayer = 1;
    mud->spnet_guest = 0; // an SP selection clears any guest join
    mud->protocol177 = 0; // the embedded server speaks the 204 protocol
    mud->protocol_custom = 0; // not the custom 10010 dialect

    // for login.c's SP-boot host-enable touchpoint
    snprintf(sp_selected_display_name, sizeof(sp_selected_display_name), "%s",
             sp_list[index].name);
    sp_selected_net_mode = sp_list[index].net_mode;
    // apply this world's game rules + save folder
    singleplayer_set_world_rules(sp_list[index].xp_rate, 1, // 1 = members
                                 sp_list[index].fatigue,
                                 sp_list[index].remember_style,
                                 sp_list[index].game_speed,
                                 sp_list[index].custom_quests,
                                 sp_list[index].holiday_events,
                                 sp_list[index].tutorial_island,
                                 sp_list[index].skillcape_perks,
                                 sp_list[index].combat_odyssey,
                                 sp_list[index].poison_npcs,
                                 sp_list[index].leftclick_webs,
                                 sp_list[index].guild_greetings,
                                 sp_list[index].faster_yohnus,
                                 sp_list[index].uses_classes,
                                 sp_list[index].spawn_ironman,
                                 sp_list[index].custom_firemaking,
                                 sp_list[index].better_jewelry_crafting,
                                 sp_list[index].custom_leather,
                                 sp_list[index].new_rare_drop_tables,
                                 sp_list[index].npc_kill_messages,
                                 sp_list[index].enchanted_crowns,
                                 sp_list[index].batch_progression);
    singleplayer_set_world(sp_list[index].id);
    mud->port = 0;

    printf("INFO: Selected single-player world %s\n", sp_list[index].name);

    // last_world stores SP worlds as 1000+index to distinguish from online
    int stored = 1000 + index;
    int world_changed = mud->options->last_world != stored;
    mud->options->last_world = stored;

    if (world_changed) {
        options_save(mud->options);
    }

    mud->panel_login_worldlist->control_activated[control_list_sp] = index;
    // keep the selection exclusive across both lists (clear the online highlight)
    mud->panel_login_worldlist->control_activated[mud->control_list_worlds] = -1;
}
#endif

void worldlist_handle_mouse(mudclient *mud) {
#ifdef WITH_SINGLEPLAYER
    // bring LAN up once the screen is actually shown so the status line reads real connectivity on entry;
    // dialog-free, and never at boot. ad-hoc still only comes up via the toggle (its init shows the CONN dialog)
    if (join_mode == SPNET_MODE_LAN && spnet_get_mode() != SPNET_MODE_LAN) {
        spnet_init(SPNET_MODE_LAN);
    }

    // drives the Join column every frame, even while the editor popup is open
    worldlist_join_tick(mud);

    if (worldlist_handle_editor(mud)) {
        return; // editor popup open, consumes all input
    }
#endif

    panel_handle_mouse(mud->panel_login_worldlist, mud->mouse_x,
                           mud->mouse_y, mud->last_mouse_button_down,
                           mud->mouse_button_down, mud->mouse_scroll_delta);
    if (panel_is_clicked(mud->panel_login_worldlist,
                         mud->control_list_worlds)) {
        int world_index =
            mud->panel_login_worldlist
                ->control_list_entry_mouse_over[mud->control_list_worlds];
        if (world_index >= 0) {
#ifdef WITH_SINGLEPLAYER
            // presets are [0, join_preset_count); the rest are scanned worlds
            if (world_index < join_preset_count) {
                worldlist_select(mud, world_index);
            } else {
                worldlist_select_join_scan(mud, world_index - join_preset_count);
            }
#else
            worldlist_select(mud, world_index);
#endif
        }
    }
#ifdef WITH_SINGLEPLAYER
    else if (control_list_sp >= 0 &&
             panel_is_clicked(mud->panel_login_worldlist, control_list_sp)) {
        int sp_index = mud->panel_login_worldlist
                           ->control_list_entry_mouse_over[control_list_sp];
        if (sp_index >= 0) {
            worldlist_select_sp(mud, sp_index);
        }
    } else if (control_new_world >= 0 &&
               panel_is_clicked(mud->panel_login_worldlist, control_new_world)) {
        worldlist_open_editor(mud, -1);
    } else if (control_edit_world >= 0 &&
               panel_is_clicked(mud->panel_login_worldlist, control_edit_world)) {
        int sel = mud->panel_login_worldlist->control_activated[control_list_sp];
        if (sel >= 0 && sel < sp_count) {
            worldlist_open_editor(mud, sel);
        }
    } else if (control_join_refresh >= 0 &&
               panel_is_clicked(mud->panel_login_worldlist,
                                control_join_refresh)) {
        // Refresh scans the current transport; never triggers the ad-hoc dialog
        if (join_mode == SPNET_MODE_LAN &&
            spnet_get_mode() != SPNET_MODE_LAN) {
            spnet_init(SPNET_MODE_LAN);
        }
        spnet_scan_start();
        join_last_scan_count = -1; // force the list to refresh next tick
    } else if (control_join_mode >= 0 &&
               panel_is_clicked(mud->panel_login_worldlist, control_join_mode)) {
        // switches transport and re-scans; the only place ad-hoc's dialog fires
        join_mode = join_mode == SPNET_MODE_LAN ? SPNET_MODE_ADHOC
                                                : SPNET_MODE_LAN;
        spnet_init(join_mode);
        spnet_scan_start();
        panel_update_text(mud->panel_login_worldlist, control_join_mode_txt,
                          join_mode == SPNET_MODE_LAN ? "WiFi" : "Ad-hoc");
        join_last_scan_count = -1; // preset visibility rule depends on mode
    }
#endif
    else if (panel_is_clicked(mud->panel_login_worldlist,
                              mud->control_worldlist_button)) {
        mud->login_screen = 0;
#ifdef WITH_SINGLEPLAYER
        // force a Join-list rebuild next time this screen is entered
        join_last_scan_count = -1;
#endif
    }
}
