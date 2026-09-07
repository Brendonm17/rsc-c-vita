#include "appearance.h"
#include "worldlist.h" // worldlist_bots_appearance_accept (Bot Manager reuse)

#ifdef WITH_SINGLEPLAYER
#include "../singleplayer.h"
#endif

// creation selector option names, ordered by the wire index

// IronmanMode: 0 None(Regular) 1 Ironman 2 Ultimate 3 Hardcore
static const char *const appearance_ironman_mode_names[APPEARANCE_IRONMAN_MODE_COUNT] = {
    "Regular", "Ironman", "Ultimate", "Hardcore"};

// Classes ordinal: 0 ADVENTURER .. 5 MINER
static const char *const appearance_class_names[APPEARANCE_CLASS_COUNT] = {
    "Adventurer", "Warrior", "Wizard", "Necromancer", "Ranger", "Miner"};

// isOneXp byte: 0 = world rate, 1 = original 1x
static const char *const appearance_one_xp_names[APPEARANCE_ONE_XP_COUNT] = {
    "World rate", "Original 1x"};

struct appearance_buttons {
    int left;
    int right;
};

static struct appearance_buttons
mudclient_create_appearance_box(mudclient *mud, char *type, int x, int y);

// [start, end) control-id ranges for the mode/class/xp-rate selector rows
static int appearance_ironman_row_start = 0;
static int appearance_ironman_row_end = 0;
static int appearance_class_row_start = 0;
static int appearance_class_row_end = 0;
static int appearance_onexp_row_start = 0;
static int appearance_onexp_row_end = 0;

// gates the mode/class/xp-rate selectors by the active session's rules
static void appearance_apply_creation_gating(mudclient *mud, int *out_show_ironman,
                                             int *out_show_class,
                                             int *out_show_onexp) {
    // online creation mode from SEND_SERVER_CONFIGS position 71: 0 none, 1 ironman + 1x,
    // 2 classes; authentic worlds have no configs packet so they resolve to 0
    int mode = mud->protocol_custom ? mud->orsc.character_creation_mode : 0;
    int show_ironman = mode == 1;
    int show_class = mode == 2;
    int show_onexp = mode == 1;

#ifdef WITH_SINGLEPLAYER
    if (mud->singleplayer) {
        // SP worlds gate by their own rules; the 1x choice is offered everywhere
        show_ironman = singleplayer_world_spawns_ironman();
        show_class = singleplayer_world_uses_classes();
        show_onexp = 1;
    } else if (mud->spnet_guest) {
        // co-op guest: the host's rules aren't known here, the host server enforces them
        show_ironman = 1;
        show_class = 1;
        show_onexp = 1;
    }

    // a bot's look (Bot Manager): mode/class/xp rate are a player's choices, not a bot's
    if (worldlist_bots_appearance_active()) {
        show_ironman = 0;
        show_class = 0;
        show_onexp = 0;
    }
#endif

    for (int i = appearance_ironman_row_start; i < appearance_ironman_row_end;
         i++) {
        if (show_ironman) {
            panel_show(mud->panel_appearance, i);
        } else {
            panel_hide(mud->panel_appearance, i);
        }
    }

    for (int i = appearance_class_row_start; i < appearance_class_row_end;
         i++) {
        if (show_class) {
            panel_show(mud->panel_appearance, i);
        } else {
            panel_hide(mud->panel_appearance, i);
        }
    }

    for (int i = appearance_onexp_row_start; i < appearance_onexp_row_end;
         i++) {
        if (show_onexp) {
            panel_show(mud->panel_appearance, i);
        } else {
            panel_hide(mud->panel_appearance, i);
        }
    }

    *out_show_ironman = show_ironman;
    *out_show_class = show_class;
    *out_show_onexp = show_onexp;
}

static struct appearance_buttons
mudclient_create_appearance_box(mudclient *mud, char *type, int x, int y) {
    int is_compact = mud->surface->width < MUD_VANILLA_WIDTH ||
                     mud->surface->height < MUD_VANILLA_HEIGHT;

    /* box around type text */
    int box_padding = (is_compact ? 5 : 8);
    int box_height = (box_padding + (is_compact ? 25 : 33));

    panel_add_box_rounded(mud->panel_appearance, x, y, APPEARANCE_BOX_WIDTH,
                          box_height);

    char type_copy[strlen(type) + 1];
    strcpy(type_copy, type);

    char *type_split = strtok(type_copy, "\n");

    char type_1[strlen(type_split) + 1];
    strcpy(type_1, type_split);

    type_split = strtok(NULL, "\n");

    if (type_split == NULL) {
        panel_add_text_centre(mud->panel_appearance, x, y, type_1, FONT_BOLD_12,
                              1);
    } else {
        panel_add_text_centre(mud->panel_appearance, x, y - box_padding, type_1,
                              FONT_BOLD_12, 1);

        char type_2[strlen(type_split) + 1];
        strcpy(type_2, type_split);

        panel_add_text_centre(mud->panel_appearance, x, y + box_padding, type_2,
                              FONT_BOLD_12, 1);
    }

    struct appearance_buttons buttons = {0};

    // draw the cycler arrows as text "<"/">" glyphs instead of the media arrow sprites
    buttons.left =
        panel_add_button(mud->panel_appearance, x - 40, y,
                         APPEARANCE_ARROW_SIZE, APPEARANCE_ARROW_SIZE);
    panel_add_text_centre(mud->panel_appearance, x - 40, y, "@whi@<",
                          FONT_BOLD_13, 0);

    buttons.right =
        panel_add_button(mud->panel_appearance, x + 40, y,
                         APPEARANCE_ARROW_SIZE, APPEARANCE_ARROW_SIZE);
    panel_add_text_centre(mud->panel_appearance, x + 40, y, "@whi@>",
                          FONT_BOLD_13, 0);

    return buttons;
}

void mudclient_create_appearance_panel(mudclient *mud) {
    mud->panel_appearance = malloc(sizeof(Panel));
    panel_new(mud->panel_appearance, mud->surface, 100);

    int is_compact = mud->surface->width < MUD_VANILLA_WIDTH ||
                     mud->surface->height < MUD_VANILLA_HEIGHT;

    int x = (is_compact ? MUD_MIN_WIDTH : MUD_VANILLA_WIDTH) / 2;
    int y = 10;

    if (!is_compact) {
        panel_add_text_centre(mud->panel_appearance, x, y,
                              "Please design Your Character", FONT_BOLD_14, 1);

        y += 14;

        panel_add_text_centre(mud->panel_appearance, x - 55, y + 110, "Front",
                              FONT_BOLD_13, 1);

        panel_add_text_centre(mud->panel_appearance, x, y + 110, "Side",
                              FONT_BOLD_13, 1);

        panel_add_text_centre(mud->panel_appearance, x + 55, y + 110, "Back",
                              FONT_BOLD_13, 1);

        y += 145;
    } else {
        y += 102;
    }

    /* box around type text */
    int box_padding = (is_compact ? 5 : 8);
    int box_height = (box_padding + (is_compact ? 25 : 33));
    int box_margin = (box_height + (is_compact ? 3 : 9));

    struct appearance_buttons head_buttons = mudclient_create_appearance_box(
        mud, "Head\nType", x - APPEARANCE_COLUMN_WIDTH, y);

    mud->control_appearance_head_left = head_buttons.left;
    mud->control_appearance_head_right = head_buttons.right;

    struct appearance_buttons hair_buttons = mudclient_create_appearance_box(
        mud, "Hair\nColor", x + APPEARANCE_COLUMN_WIDTH, y);

    mud->control_appearance_hair_left = hair_buttons.left;
    mud->control_appearance_hair_right = hair_buttons.right;

    y += box_margin;

    struct appearance_buttons gender_buttons = mudclient_create_appearance_box(
        mud, "Gender", x - APPEARANCE_COLUMN_WIDTH, y);

    mud->control_appearance_gender_left = gender_buttons.left;
    mud->control_appearance_gender_right = gender_buttons.right;

    struct appearance_buttons top_buttons = mudclient_create_appearance_box(
        mud, "Top\nColor", x + APPEARANCE_COLUMN_WIDTH, y);

    mud->control_appearance_top_left = top_buttons.left;
    mud->control_appearance_top_right = top_buttons.right;

    y += box_margin;

    struct appearance_buttons skin_buttons = mudclient_create_appearance_box(
        mud, "Skin\nColor", x - APPEARANCE_COLUMN_WIDTH, y);

    mud->control_appearance_skin_left = skin_buttons.left;
    mud->control_appearance_skin_right = skin_buttons.right;

    struct appearance_buttons bottom_buttons = mudclient_create_appearance_box(
        mud, "Bottom\nColor", x + APPEARANCE_COLUMN_WIDTH, y);

    mud->control_appearance_bottom_left = bottom_buttons.left;
    mud->control_appearance_bottom_right = bottom_buttons.right;

    // creation selectors (mode/class/xp-rate) as arrow cyclers in a right-hand column
    int selector_x =
        (is_compact ? MUD_MIN_WIDTH : MUD_VANILLA_WIDTH) - APPEARANCE_COLUMN_WIDTH - 24;
    int selector_y = (is_compact ? 40 : 60);
    int selector_margin = box_margin + (is_compact ? 6 : 14);

    // bounds captured so the row can be hidden as a unit
    int ironman_row_start = mud->panel_appearance->control_count;

    // two-line label so the value text sits below the category label
    struct appearance_buttons ironman_buttons =
        mudclient_create_appearance_box(mud, "Mode\n ", selector_x, selector_y);

    mud->control_appearance_ironman_left = ironman_buttons.left;
    mud->control_appearance_ironman_right = ironman_buttons.right;
    mud->appearance_ironman_box_x = selector_x;
    mud->appearance_ironman_box_y = selector_y;

    int ironman_row_end = mud->panel_appearance->control_count;

    selector_y += selector_margin;

    int class_row_start = mud->panel_appearance->control_count;

    struct appearance_buttons class_buttons =
        mudclient_create_appearance_box(mud, "Class\n ", selector_x, selector_y);

    mud->control_appearance_class_left = class_buttons.left;
    mud->control_appearance_class_right = class_buttons.right;
    mud->appearance_class_box_x = selector_x;
    mud->appearance_class_box_y = selector_y;

    int class_row_end = mud->panel_appearance->control_count;

    appearance_ironman_row_start = ironman_row_start;
    appearance_ironman_row_end = ironman_row_end;
    appearance_class_row_start = class_row_start;
    appearance_class_row_end = class_row_end;

    selector_y += selector_margin;

    int onexp_row_start = mud->panel_appearance->control_count;

    struct appearance_buttons onexp_buttons =
        mudclient_create_appearance_box(mud, "XP Rate\n ", selector_x, selector_y);

    mud->control_appearance_onexp_left = onexp_buttons.left;
    mud->control_appearance_onexp_right = onexp_buttons.right;
    mud->appearance_onexp_box_x = selector_x;
    mud->appearance_onexp_box_y = selector_y;

    appearance_onexp_row_start = onexp_row_start;
    appearance_onexp_row_end = mud->panel_appearance->control_count;

    y += box_margin - (is_compact ? -1 : 3);

    panel_add_button_background(mud->panel_appearance, x, y,
                                APPEARANCE_ACCEPT_WIDTH,
                                APPEARANCE_ACCEPT_HEIGHT);

    panel_add_text_centre(mud->panel_appearance, x, y, "Accept", 4, 0);

    mud->control_appearance_accept =
        panel_add_button(mud->panel_appearance, x, y, APPEARANCE_ACCEPT_WIDTH,
                         APPEARANCE_ACCEPT_HEIGHT);
}

void mudclient_handle_appearance_panel_input(mudclient *mud) {
    // re-applies gating each call so a world switch takes effect
    int show_ironman, show_class, show_onexp;
    appearance_apply_creation_gating(mud, &show_ironman, &show_class,
                                     &show_onexp);

    panel_handle_mouse(mud->panel_appearance, mud->mouse_x, mud->mouse_y,
                       mud->last_mouse_button_down, mud->mouse_button_down,
                       mud->mouse_scroll_delta);

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_head_left)) {
        do {
            mud->appearance_head_type =
                (mud->appearance_head_type - 1 + game_data.animation_count) %
                game_data.animation_count;
        } while ((game_data.animations[mud->appearance_head_type].gender & 3) !=
                     1 ||
                 (game_data.animations[mud->appearance_head_type].gender &
                  (4 * mud->appearance_head_gender)) == 0);
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_head_right)) {
        do {
            mud->appearance_head_type =
                (mud->appearance_head_type + 1) % game_data.animation_count;
        } while ((game_data.animations[mud->appearance_head_type].gender & 3) !=
                     1 ||
                 (game_data.animations[mud->appearance_head_type].gender &
                  (4 * mud->appearance_head_gender)) == 0);
    }

    int hair_colours_length =
        sizeof(player_hair_colours) / sizeof(player_hair_colours[0]);

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_hair_left)) {
        mud->appearance_hair_colour =
            (mud->appearance_hair_colour - 1 + hair_colours_length) %
            hair_colours_length;
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_hair_right)) {
        mud->appearance_hair_colour =
            (mud->appearance_hair_colour + 1) % hair_colours_length;
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_gender_left) ||
        panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_gender_right)) {
        for (mud->appearance_head_gender = 3 - mud->appearance_head_gender;
             (game_data.animations[mud->appearance_head_type].gender & 3) !=
                 1 ||
             (game_data.animations[mud->appearance_head_type].gender &
              (4 * mud->appearance_head_gender)) == 0;
             mud->appearance_head_type =
                 (mud->appearance_head_type + 1) % game_data.animation_count)
            ;

        for (; (game_data.animations[mud->appearance_body_type].gender & 3) !=
                   2 ||
               (game_data.animations[mud->appearance_body_type].gender &
                (4 * mud->appearance_head_gender)) == 0;
             mud->appearance_body_type =
                 (mud->appearance_body_type + 1) % game_data.animation_count)
            ;
    }

    int top_bottom_colours_length = sizeof(player_top_bottom_colours) /
                                    sizeof(player_top_bottom_colours[0]);

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_top_left)) {
        mud->appearance_top_colour =
            (mud->appearance_top_colour - 1 + top_bottom_colours_length) %
            top_bottom_colours_length;
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_top_right)) {
        mud->appearance_top_colour =
            (mud->appearance_top_colour + 1) % top_bottom_colours_length;
    }

    /* The skin palette holds OpenRSC's unlockable colours beyond the original
     * five, but a colour is only SELECTABLE where the world has unlocked it --
     * their cycler steps over locked entries (mudclient.java's do/while on
     * unlockedSkinColours, whose defaults are exactly the first five). On
     * authentic/SP worlds nothing beyond those five is ever unlocked, so this
     * behaves identically to the old fixed-5 cycler. */
    int skin_colours_length = PLAYER_SKIN_COLOUR_COUNT;

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_skin_left)) {
        do {
            mud->appearance_skin_colour =
                (mud->appearance_skin_colour - 1 + skin_colours_length) %
                skin_colours_length;
        } while (!mudclient_is_skin_colour_unlocked(
            mud, mud->appearance_skin_colour));
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_skin_right)) {
        do {
            mud->appearance_skin_colour =
                (mud->appearance_skin_colour + 1) % skin_colours_length;
        } while (!mudclient_is_skin_colour_unlocked(
            mud, mud->appearance_skin_colour));
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_bottom_left)) {
        mud->appearance_bottom_colour =
            (mud->appearance_bottom_colour - 1 + top_bottom_colours_length) %
            top_bottom_colours_length;
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_bottom_right)) {
        mud->appearance_bottom_colour =
            (mud->appearance_bottom_colour + 1) % top_bottom_colours_length;
    }

    // game-mode cycler: regular/ironman/ultimate/hardcore
    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_ironman_left)) {
        mud->appearance_ironman_mode =
            (mud->appearance_ironman_mode - 1 + APPEARANCE_IRONMAN_MODE_COUNT) %
            APPEARANCE_IRONMAN_MODE_COUNT;
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_ironman_right)) {
        mud->appearance_ironman_mode =
            (mud->appearance_ironman_mode + 1) % APPEARANCE_IRONMAN_MODE_COUNT;
    }

    // character-class cycler (Classes ordinal 0..5)
    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_class_left)) {
        mud->appearance_class =
            (mud->appearance_class - 1 + APPEARANCE_CLASS_COUNT) %
            APPEARANCE_CLASS_COUNT;
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_class_right)) {
        mud->appearance_class =
            (mud->appearance_class + 1) % APPEARANCE_CLASS_COUNT;
    }

    // one-xp toggle (0 = world xp rate, 1 = original 1x)
    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_onexp_left) ||
        panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_onexp_right)) {
        mud->appearance_one_xp = mud->appearance_one_xp ? 0 : 1;
    }

    if (panel_is_clicked(mud->panel_appearance,
                         mud->control_appearance_accept)) {
        // Bot Manager reuse: editing a bot's look saves it onto the def, no create packet
        if (worldlist_bots_appearance_accept(mud)) {
            surface_black_screen(mud->surface);
            return;
        }
        packet_stream_new_packet(mud->packet_stream, CLIENT_APPEARANCE);
        packet_stream_put_byte(mud->packet_stream, mud->appearance_head_gender);
        packet_stream_put_byte(mud->packet_stream, mud->appearance_head_type);
        packet_stream_put_byte(mud->packet_stream, mud->appearance_body_type);
        packet_stream_put_byte(mud->packet_stream, 2);
        packet_stream_put_byte(mud->packet_stream, mud->appearance_hair_colour);
        packet_stream_put_byte(mud->packet_stream, mud->appearance_top_colour);

        packet_stream_put_byte(mud->packet_stream,
                               mud->appearance_bottom_colour);

        packet_stream_put_byte(mud->packet_stream, mud->appearance_skin_colour);

        // creation bytes: ironman mode then is-one-xp
        packet_stream_put_byte(mud->packet_stream, mud->appearance_ironman_mode);
        packet_stream_put_byte(mud->packet_stream, mud->appearance_one_xp);

        // class byte is an extension for the embedded sp server only, never sent to a custom world
        if (!mud->protocol_custom) {
            packet_stream_put_byte(mud->packet_stream, mud->appearance_class);
        }

        packet_stream_send_packet(mud->packet_stream);

        surface_black_screen(mud->surface);
        mud->show_appearance_change = 0;
    }
}

void mudclient_draw_appearance_panel(mudclient *mud) {
    mud->surface->interlace = 0;
    surface_black_screen(mud->surface);

    // hide the mode/class/xp-rate rows before the panel draws
    int show_ironman, show_class, show_onexp;
    appearance_apply_creation_gating(mud, &show_ironman, &show_class,
                                     &show_onexp);

    panel_draw_panel(mud->panel_appearance);

    // draw the selected mode/class/xp-rate text under each cycler
    int panel_off_x = mud->panel_appearance->offset_x;
    int panel_off_y = mud->panel_appearance->offset_y;

    if (show_ironman) {
        surface_draw_string_centre(
            mud->surface,
            appearance_ironman_mode_names[mud->appearance_ironman_mode],
            mud->appearance_ironman_box_x + panel_off_x,
            mud->appearance_ironman_box_y + panel_off_y + 9, FONT_BOLD_12,
            WHITE);
    }

    if (show_class) {
        surface_draw_string_centre(
            mud->surface, appearance_class_names[mud->appearance_class],
            mud->appearance_class_box_x + panel_off_x,
            mud->appearance_class_box_y + panel_off_y + 9, FONT_BOLD_12,
            WHITE);
    }

    if (show_onexp) {
        // the creation screen words the 1x question with the world's rate (config 72)
        char one_xp_rate_label[16];
        const char *one_xp_label =
            appearance_one_xp_names[mud->appearance_one_xp];

        if (mud->appearance_one_xp == 0 && mud->protocol_custom &&
            mud->orsc.skilling_exp_rate > 1) {
            snprintf(one_xp_rate_label, sizeof(one_xp_rate_label),
                     "World (%dx)", mud->orsc.skilling_exp_rate);
            one_xp_label = one_xp_rate_label;
        }

        surface_draw_string_centre(
            mud->surface, one_xp_label,
            mud->appearance_onexp_box_x + panel_off_x,
            mud->appearance_onexp_box_y + panel_off_y + 9, FONT_BOLD_12,
            WHITE);
    }

    int is_compact = mud->surface->width < MUD_VANILLA_WIDTH ||
                     mud->surface->height < MUD_VANILLA_HEIGHT;

    int x = mud->surface->width / 2;

    int y = (is_compact ? -7 : 25) +
            (mud->surface->height / 2 -
             (is_compact ? MUD_MIN_HEIGHT : MUD_VANILLA_HEIGHT) / 2);

    surface_draw_sprite_scale_mask(
        mud->surface, x - 32 - 55, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT,
        game_data.animations[ANIMATION_INDEX_LEGS].file_id,
        player_top_bottom_colours[mud->appearance_bottom_colour]);

    surface_draw_sprite_transform_mask(
        mud->surface, x - 32 - 55, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT,
        game_data.animations[mud->appearance_body_type].file_id,
        player_top_bottom_colours[mud->appearance_top_colour],
        player_skin_colours[mud->appearance_skin_colour], 0, 0);

    surface_draw_sprite_transform_mask(
        mud->surface, x - 32 - 55, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT,
        game_data.animations[mud->appearance_head_type].file_id,
        player_hair_colours[mud->appearance_hair_colour],
        player_skin_colours[mud->appearance_skin_colour], 0, 0);

    surface_draw_sprite_scale_mask(
        mud->surface, x - 32, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT, game_data.animations[2].file_id + 6,
        player_top_bottom_colours[mud->appearance_bottom_colour]);

    surface_draw_sprite_transform_mask(
        mud->surface, x - 32, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT,
        game_data.animations[mud->appearance_body_type].file_id + 6,
        player_top_bottom_colours[mud->appearance_top_colour],
        player_skin_colours[mud->appearance_skin_colour], 0, 0);

    surface_draw_sprite_transform_mask(
        mud->surface, x - 32, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT,
        game_data.animations[mud->appearance_head_type].file_id + 6,
        player_hair_colours[mud->appearance_hair_colour],
        player_skin_colours[mud->appearance_skin_colour], 0, 0);

    surface_draw_sprite_scale_mask(
        mud->surface, x - 32 + 55, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT, game_data.animations[2].file_id + 12,
        player_top_bottom_colours[mud->appearance_bottom_colour]);

    surface_draw_sprite_transform_mask(
        mud->surface, x - 32 + 55, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT,
        game_data.animations[mud->appearance_body_type].file_id + 12,
        player_top_bottom_colours[mud->appearance_top_colour],
        player_skin_colours[mud->appearance_skin_colour], 0, 0);

    surface_draw_sprite_transform_mask(
        mud->surface, x - 32 + 55, y, APPEARANCE_CHARACTER_WIDTH,
        APPEARANCE_CHARACTER_HEIGHT,
        game_data.animations[mud->appearance_head_type].file_id + 12,
        player_hair_colours[mud->appearance_hair_colour],
        player_skin_colours[mud->appearance_skin_colour], 0, 0);

    if (!mud->options->lowmem) {
        mudclient_draw_blue_bar(mud);
    }

    surface_draw(mud->surface);
}
