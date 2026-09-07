#ifndef _H_MUDCLIENT
#define _H_MUDCLIENT

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifdef WII
#include <asndlib.h>
#include <gccore.h>
#include <ogc/usbmouse.h>
#include <wiikeyboard/keyboard.h>
#include <wiiuse/wpad.h>

#include "config85_jag.h"
#include "entity24_jag.h"
#include "entity24_mem.h"
#include "filter2_jag.h"
#include "jagex_jag.h"
#include "land63_jag.h"
#include "land63_mem.h"
#include "maps63_jag.h"
#include "maps63_mem.h"
#include "media58_jag.h"
#include "models36_jag.h"
#include "sounds1_mem.h"
#include "textures17_jag.h"

#include "wii/arrow_yuv.h"
#include "wii/rsc_game_yuv.h"
#include "wii/rsc_keyboard_shift_yuv.h"
#include "wii/rsc_keyboard_yuv.h"

#define GAME_OFFSET_X 64
#define GAME_OFFSET_Y 54
#elif defined(__SWITCH__)
#include <switch.h>
#elif defined(_3DS)
#include <3ds.h>
#include <malloc.h>

#include "game_top_bgr.h"
#endif

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
#define CGLM_DEFINE_PRINTS
#include <cglm/cglm.h>
#endif

#ifdef RENDER_3DS_GL
#include <citro3d.h>
#include <tex3ds.h>

#define DISPLAY_TRANSFER_FLAGS                                                 \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) |                     \
     GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) |  \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |                            \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
#endif

#if !defined(WII) && !defined(_3DS)
#ifdef __SWITCH__
#include <SDL2/SDL.h>
#else
#include <SDL.h>
#endif

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define MUD_IS_BIG_ENDIAN
#endif

#ifdef RENDER_GL
#if defined(__vita__)
#include <vitaGL.h>
#elif defined(GLAD)
#include <glad/glad.h>
#else
#include <GL/glew.h>
#include <GL/glu.h>
#endif
#if !defined(SDL12) && !defined(__SWITCH__) && !defined(__vita__)
#include <SDL_opengl.h>
#endif

#include "gl/shader.h"
#endif
#endif

#define SAMPLE_RATE 8000
#define SAMPLE_BUFFER_SIZE 4096
#define BYTES_PER_SAMPLE 2
#define PCM_LENGTH (50 * 1024)

#define K_LEFT 37
#define K_RIGHT 39
#define K_UP 38
#define K_DOWN 40
#define K_PAGE_UP 33
#define K_PAGE_DOWN 34
#define K_HOME 36
#define K_F1 112
#define K_F2 113
#define K_F3 114
#define K_F4 115
#define K_F5 116
#define K_F6 117
#define K_F7 118
#define K_ENTER 13

#define K_BACKSPACE 8
#define K_ESCAPE 27
#define K_TAB 9

#define K_FWD_SLASH 47
#define K_ASTERISK 42
#define K_MINUS 45
#define K_PLUS 43
#define K_PERIOD 46

#define K_0 48
#define K_1 49
#define K_2 50
#define K_3 51
#define K_4 52
#define K_5 53
#define K_6 54
#define K_7 55
#define K_8 56
#define K_9 57

#ifdef REVISION_177
#define VERSION 177
#elif !defined(NO_RSA) && !defined(NO_ISAAC)
#define VERSION 203
#else
/*
 * 2003scape server compatiblity. Actual Jagex "204" clients identify
 * as 203.
 */
#define VERSION 204
#endif

// embedded 2003scape server expects protocol "204"; OpenRSC build sends VERSION (203)
// true when the client speaks the single-player wire shape: plaintext-204, no ISAAC, username-only, auto-register
#ifdef WITH_SINGLEPLAYER
#define MUD_SP_WIRE(mud) ((mud)->singleplayer || (mud)->spnet_guest)
#define LOGIN_VERSION(mud) (MUD_SP_WIRE(mud) ? 204 : VERSION)
#else
#define MUD_SP_WIRE(mud) (0)
#define LOGIN_VERSION(mud) (VERSION)
#endif

#ifndef MUD_DATADIR
#define MUD_DATADIR "/usr/local/share/rsc-c"
#endif

#define ZOOM_MIN 450
#define ZOOM_MAX 2250 // old 1250
#define ZOOM_INDOORS 550
#define ZOOM_OUTDOORS 750

#define MAGIC_LOC 128

#define FONT_FILES_LENGTH (sizeof(font_files) / sizeof(font_files[0]))

#define ANIMATED_MODELS_LENGTH 20

/* maximum amount of friends/ignores */
#define SOCIAL_LIST_MAX 100

// dialogue-option capacity; authentic caps menus at 5, custom sends up to 13; parse clamps
#define OPTION_MENU_MAX 16

// custom (10010) quest capacity; quests are a dynamic server-named list; parse clamps
#define ORSC_QUEST_MAX 64
#define ORSC_QUEST_NAME_MAX 48

// longest item name + " Certificate" + NUL, with headroom
#define ITEM_NAME_DISPLAY_MAX 96

// note sprite 438, certificate sprite 180; both from the stock item cache
#define ORSC_NOTE_SPRITE 438
#define ORSC_CERTIFICATE_SPRITE 180

// capped online-player store; the OpenRSC list is unbounded and scrolls
#define ORSC_ONLINE_LIST_MAX 256
#define ORSC_ONLINE_LOCATION_MAX 32
// their update(): listEndPoint = startComponentIndex + 49
#define ORSC_ONLINE_LIST_WINDOW 50

// bank preset count; SEND_BANK_PRESET (150) is per-slot
#define ORSC_PRESET_COUNT 2
// preset packet collapses 14 equipment entries to 11: 5->0, 6->1, 7->2, >7 -= 3
#define ORSC_PRESET_EQUIP_WIRE_COUNT 14

#define INPUT_TEXT_LENGTH 20
#define INPUT_PM_LENGTH 80
#define INPUT_DIGITS_LENGTH 14 /* 2,147,483,647m */

#define GAME_OBJECTS_MAX 1000
#define WALL_OBJECTS_MAX 500
#define OBJECTS_MAX 1500
#define PLAYERS_SERVER_MAX 2000
#define PLAYERS_MAX 500
#define NPCS_SERVER_MAX 5000
#define NPCS_MAX 500
#define GROUND_ITEMS_MAX 5000
#define PRAYER_COUNT 50
// PLAYER_SKILL_COUNT = authentic 18-skill baseline; PLAYER_SKILL_MAX = array capacity for custom extra skills (e.g. 19th Runecraft)
#define PLAYER_SKILL_COUNT 18
#define PLAYER_SKILL_MAX 24
#define PLAYER_STAT_EQUIPMENT_COUNT 5
#define PROJECTILE_RANGE_MAX 40

/* TODO overhead max? */
#define RECEIVED_MESSAGE_MAX 50
#define ACTION_BUBBLE_MAX 50
#define HEALTH_BAR_MAX 50
#define MAGIC_BUBBLE_MAX 50
#define OVERWORLD_TEXT_MAX 128

#define INVENTORY_ITEMS_MAX 30
#define PATH_STEPS_MAX 8000
// bank item capacity; custom member worlds hold up to ItemId.maxCustom (1592)
#define BANK_ITEMS_MAX 1592
#define SHOP_ITEMS_MAX 256 // TODO also just make this 40? (SHOP_GRID_MAX)
#define TRADE_ITEMS_MAX 14
#define DUEL_ITEMS_MAX 8

#define EXPERIENCE_DROPS_MAX 100

#define MOUSE_HISTORY_LENGTH 8192

#define MUD_VANILLA_WIDTH 512
#define MUD_VANILLA_HEIGHT 346

#define MUD_MIN_WIDTH 320
#define MUD_MIN_HEIGHT 240

#ifdef _3DS
#define MUD_WIDTH MUD_MIN_WIDTH
#define MUD_HEIGHT MUD_MIN_HEIGHT
#else
#define MUD_WIDTH MUD_VANILLA_WIDTH
#define MUD_HEIGHT MUD_VANILLA_HEIGHT
// #define MUD_WIDTH 320
// #define MUD_HEIGHT 240
#endif

// TODO make this a function
#define MUD_IS_COMPACT                                                         \
    (MUD_WIDTH < MUD_VANILLA_WIDTH || MUD_HEIGHT < MUD_VANILLA_HEIGHT)

/* npc IDs */
#define SHIFTY_MAN_ID 24
#define GIANT_BAT_ID 43

/* object IDs */
#define WINDMILL_SAILS_ID 74
#define FIRE_ID 97
#define FIREPLACE_ID 274
#define ODD_WELL_ID 466
#define LIGHTNING_ID 1031
#define FIRE_SPELL_ID 1036
#define SPELL_CHARGE_ID 1147
#define TORCH_ID 51
#define SKULL_TORCH_ID 143
#define CLAW_SPELL_ID 1142

/* boundary IDs */
#define ODD_LOOKING_WALL_ID 22

/* item IDs */
#define IRON_MACE_ID 0
#define COINS_ID 10

#define FIRE_RUNE_ID 31
#define FIRE_STAFF_ID 197
#define FIRE_BATTLESTAFF_ID 615
#define ENCHANTED_FIRE_BATTLESTAFF_ID 682

#define WATER_RUNE_ID 32
#define WATER_STAFF_ID 102
#define WATER_BATTLESTAFF_ID 616
#define ENCHANTED_WATER_BATTLESTAFF_ID 683

#define AIR_RUNE_ID 33
#define AIR_STAFF_ID 101
#define AIR_BATTLESTAFF_ID 617
#define ENCHANTED_AIR_BATTLESTAFF_ID 684

#define EARTH_RUNE_ID 34
#define EARTH_STAFF_ID 103
#define EARTH_BATTLESTAFF_ID 618
#define ENCHANTED_EARTH_BATTLESTAFF_ID 685

/* texture IDs */
#define FOUNTAIN_ID 17

/* skill IDs */
#define SKILL_ATTACK 0
#define SKILL_DEFENSE 1
#define SKILL_STRENGTH 2
#define SKILL_HITS 3
#define SKILL_PRAYER 5
#define SKILL_MAGIC 6

/* sprite stuff */
// sprite limit 8192: room for the full OpenRSC custom entity sprite range (custom_entities.png atlas)
// bumping this resizes the surface sprite arrays; needs a clean rebuild (delete .glo)
#define SPRITE_LIMIT 8192

/* jagex loading screen on startup */
#define LOADING_WIDTH 277
#define LOADING_HEIGHT 20

/* how many tiles away before objects stop animating */
#define OBJECT_ANIMATION_DISTANCE 7

typedef struct mudclient mudclient;

#include "chat-message.h"
#include "client-opcodes.h"
#include "colours.h"
#include "game-character.h"
#include "game-data.h"
#include "game-model.h"
#include "lib/bzip.h"
#include "lib/orsc-huffman.h"
#include "options.h"
#include "packet-handler.h"
#include "packet-stream.h"
#include "panel.h"
#include "scene.h"
#include "server-opcodes.h"
#include "surface.h"
#include "utility.h"
#include "version.h"
#include "world.h"

#include "ui/additional-options.h"
#include "ui/appearance.h"
#include "ui/auction.h"
#include "ui/bank-pin.h"
#include "ui/bank-preset.h"
#include "ui/online-list.h"
#include "ui/bank.h"
#include "ui/combat-style.h"
#include "ui/confirm.h"
#include "ui/crowns.h"
#include "ui/duel.h"
#include "ui/experience-drops.h"
#include "ui/ironman.h"
#include "ui/kill-feed.h"
#include "ui/login.h"
#include "ui/logout.h"
#include "ui/lost-connection.h"
#include "ui/menu.h"
#include "ui/message-tabs.h"
#include "ui/offer-x.h"
#include "ui/option-menu.h"
#include "ui/options-tab.h"
#include "ui/progress-bar.h"
#include "ui/server-message.h"
#include "ui/shop.h"
#include "ui/skill-guide.h"
#include "ui/sleep.h"
#include "ui/social-tab.h"
#include "ui/stats-tab.h"
#include "ui/status-bars.h"
#include "ui/trade.h"
#include "ui/ui-tabs.h"
#include "ui/welcome.h"
#include "ui/wilderness-warning.h"
#include "ui/worldlist.h"

#include "custom/clarify-herblaw-items.h"
#include "custom/diverse-npcs.h"
#include "custom/item-highlight.h"

#ifdef USE_TOONSCAPE
#include "custom/toonscape.h"
#endif

#ifdef WII
/* these are doubled for the wii */
#define KEY_WIDTH 23
#define KEY_HEIGHT 22

#define MUD_IS_BIG_ENDIAN

extern char keyboard_buttons[5][10];
extern char keyboard_shift_buttons[5][10];
extern int keyboard_offsets[];

void draw_background(uint8_t *framebuffer, int full);
void draw_arrow(uint8_t *framebuffer, int mouse_x, int mouse_y);
void draw_keyboard(uint8_t *framebuffer, int is_shift);

extern int wii_mouse_x;
extern int wii_mouse_y;
extern int wii_mouse_button;
#elif defined(_3DS)
#define SOC_ALIGN 0x1000
#define SOC_BUFFER_SIZE 0x100000

/* for keyboard thread */
#define STACK_SIZE (4 * 1024)

#define _3DS_KEYBOARD_NORMAL 0
#define _3DS_KEYBOARD_PASSWORD 1
#define _3DS_KEYBOARD_NUMPAD 2

extern u32 *SOC_buffer;

extern ndspWaveBuf wave_buf[2];
extern u32 *audio_buffer;
extern int fill_block;

extern Thread _3ds_keyboard_thread;
extern char _3ds_keyboard_buffer[255];
extern volatile int _3ds_keyboard_received_input;
extern SwkbdButton _3ds_keyboard_button;

extern char _3ds_option_buttons[5];

void _3ds_keyboard_thread_callback(void *arg);
void _3ds_toggle_top_screen(int is_off);

#ifdef RENDER_3DS_GL
void mudclient_3ds_gl_offscreen_frame_start(mudclient *mud);
void mudclient_3ds_gl_frame_start(mudclient *mud, int clear);
void mudclient_3ds_gl_frame_end();
#endif
#else
#ifdef SDL12
void get_sdl_keycodes(SDL_keysym *keysym, char *char_code, int *code);
#else
void get_sdl_keycodes(SDL_Keysym *keysym, char *char_code, int *code);
#endif
#endif

extern int mudclient_finger_1_x;
extern int mudclient_finger_1_y;
extern int mudclient_finger_1_down;

extern int mudclient_finger_2_x;
extern int mudclient_finger_2_y;
extern int mudclient_finger_2_down;

extern int mudclient_full_width;
extern int mudclient_full_height;

// TODO this was moved
extern const char *font_files[];
extern const char *animated_models[];
extern char login_screen_status[255];

/*
 * most walls are created by world.c and are non-interactive,
 * but those that are interactive or can change need to be
 * streamed from the server and are stored here.
 */
struct ServerBoundary {
    GameModel *model;
    int16_t x;
    int16_t y;
    uint16_t id;
    uint8_t direction;
    uint8_t already_in_menu;
};

struct ItemSpawn {
    GameModel *model; /* only used when 3D items enabled */
    int16_t x;
    int16_t y;
    int16_t z;
    uint16_t id;
    uint8_t already_in_menu;
    // per-item noted flag; custom (10010) with S_WANT_BANK_NOTES only, else 0
    uint8_t noted;
};

struct Scenery {
    int16_t x;
    int16_t y;
    uint16_t id;
    uint8_t direction;
    GameModel *model;
    uint8_t already_in_menu;
};

struct MagicBubble {
    uint16_t x;
    uint16_t y;
    uint8_t type;
    uint8_t time;
};

struct ActionBubble {
    uint16_t x;
    uint16_t y;
    uint16_t scale;
    uint16_t item;
};

struct HealthBar {
    uint16_t x;
    uint16_t y;
    uint8_t missing;
};

struct OverworldText {
    char *text;
    uint16_t x;
    uint16_t y;
    uint32_t colour;
};

struct MenuEntry {
    MenuType type;
    char action_text[64];
    char target_text[64];
    char wiki_page[64];
    /* data related to the target entity */
    int x, y;
    int16_t index;
    int16_t source_index;
    int32_t target_index;
};

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
// one prebuilt region held ready for an instant crossing; ~7MB each, two slots
#define REGION_CACHE_SLOTS 2

enum RegionCacheState {
    REGION_SLOT_EMPTY = 0,
    REGION_SLOT_BUILDING,
    REGION_SLOT_READY
};

struct RegionCacheEntry {
    int state; // enum RegionCacheState
    int sx;
    int sy;
    int plane;
    uint64_t last_touch;
    struct World *world;
    gl_vertex_buffer **buffers;
    int buffer_length;
    int realized; // 1 = GL objects fully uploaded and ready to swap in
    int realize_index; // buffer currently being uploaded incrementally, or 0
};
#endif

struct mudclient {
#ifdef WII
    /* store two for double-buffering */
    uint8_t **framebuffers;

    /* points to one of the two frame buffers in framebuffers */
    uint8_t *framebuffer;

    /* index of active framebuffer */
    int active_framebuffer;

    int last_wii_x;
    int last_wii_y;
    int last_wii_button;

    int8_t keyboard_open;
    int last_keyboard_button;
#elif defined(_3DS)
    uint8_t *_3ds_framebuffer_top;
    uint8_t *_3ds_framebuffer_bottom;

    int8_t _3ds_l_down;
    int8_t _3ds_r_down;
    int8_t _3ds_touch_down;
    int8_t keyboard_open;
    int8_t _3ds_gyro_down;
    int8_t _3ds_gyro_start;
    int8_t _3ds_top_screen_off;

    int _3ds_sound_position;
    int _3ds_sound_length;

#ifdef RENDER_3DS_GL
    C3D_RenderTarget *_3ds_gl_render_target;
    C3D_RenderTarget *_3ds_gl_offscreen_render_target;
#endif
#else
#ifndef SDL12
    SDL_Window *window;
#endif

    SDL_Surface *screen;
    SDL_Surface *pixel_surface;

#ifdef __vita__
    // Vita presents the software surface through the gxm SDL_Renderer; it also composites the system IME
    SDL_Renderer *vita_renderer;
    SDL_Texture *vita_texture;
#endif

#if defined(RENDER_GL) && !defined(SDL12)
    SDL_Window *gl_window;
#endif

    SDL_Cursor *default_cursor;
    SDL_Cursor *hand_cursor;
    int is_hand_cursor;
#endif

    Options *options;
    int game_width;
    int game_height;

    int8_t key_left;
    int8_t key_right;
    int8_t key_up;
    int8_t key_down;
    int8_t key_page_up;
    int8_t key_page_down;
    int8_t key_home;
    int8_t key_tab;
    int8_t key_1;
    int8_t key_2;
    int8_t key_3;
    int8_t key_4;
    int8_t key_5;

    /* for middle-click camera */
    int8_t middle_button_down;

    /* stores absolute mouse position and initial rotation for middle click
     * camera */
    int origin_mouse_x;
    int origin_rotation;

    /* make it spin */
    int last_mouse_sample_ticks;
    int last_mouse_sample_x;
    int camera_momentum;

    int mouse_scroll_delta;
    int mouse_action_timeout;
    int mouse_x;
    int mouse_y;
    int mouse_button_down;
    int last_mouse_button_down;
    int mouse_button_click;

    /* used for trade screen (holding for longer increases the amount) */
    int mouse_item_count_increment;
    int mouse_button_down_time;

    int mouse_click_x_history[MOUSE_HISTORY_LENGTH];
    int mouse_click_y_history[MOUSE_HISTORY_LENGTH];
    int mouse_click_count;

    /* yellow/red X sprite location and sprite cycle */
    int mouse_click_x_step;
    int mouse_click_x_x;
    int mouse_click_x_y;

    /* loading bar with jagex logo */
    int loading_step;
    int loading_progress_percent;
    char *loading_progess_text;
    int8_t error_loading_data;

    int timings[10];
    int stop_timeout;
    int fps;
    int target_fps;

    /* used for username boxes */
    char input_text_current[INPUT_TEXT_LENGTH + 1];
    char input_text_final[INPUT_TEXT_LENGTH + 1];

    /* used for private messaging */
    char input_pm_current[INPUT_PM_LENGTH + 1];
    char input_pm_final[INPUT_PM_LENGTH + 1];

    /* used for item amounts */
    char input_digits_current[INPUT_DIGITS_LENGTH + 1];
    int input_digits_final;

    int max_read_tries;
    int world_full_timeout;
    int moderator_level;
    int auto_login_attempts;

    /* ./ui/social-tab.c */
    Panel *panel_social_list;
    SocialInput show_dialog_social_input;
    int control_list_social;
    int ui_tab_social_sub_tab;
    int message_index;
    int message_tokens[SOCIAL_LIST_MAX];
    int friend_list_count;
    int64_t friend_list[SOCIAL_LIST_MAX * 2];
    int friend_list_online[SOCIAL_LIST_MAX * 2];
    int ignore_list_count;
    int64_t ignore_list[SOCIAL_LIST_MAX * 2];
    int64_t private_message_target;

    /* ./ui/options-tab.c */
    int8_t settings_camera_auto;
    int8_t settings_block_chat;
    int8_t settings_block_private;
    int8_t settings_block_trade;
    int8_t settings_block_duel;
    int8_t settings_mouse_button_one;
    int8_t settings_sound_disabled;

    // draw each player's name and clan tag above their head; custom-online, server-driven via SEND_GAME_SETTINGS, defaults on
    int8_t orsc_name_clan_tag_overlay;

    // per-player show flags for the side-menu HUD and kill feed, from the SEND_GAME_SETTINGS trailer (bytes 20/21);
    // default 1 shows both
    int8_t orsc_show_side_menu;
    int8_t orsc_show_kill_feed;
    // kill-counter lines in the side-menu HUD; per-account opt-in, default off; settings tail reads 28 and 34
    int8_t orsc_show_npc_kc;
    int8_t orsc_show_recent_npc_kc;

    // last bank certificate-deposit mode sent to the server; packet 199 sub-op 0, on change only
    int8_t orsc_bank_cert_mode;
    // the deposit being initiated is an "Uncert+Deposit" row
    int8_t bank_offer_uncert;

    // synced xp-counter state (settings-tail read 23): 0 never, 1 recent, 2 always
    int8_t orsc_xp_counter_synced;

    // session xp tracking for the Gained / xp-per-hour lines; fed only by the incremental experience packet
    int player_experience_gained[PLAYER_SKILL_MAX];
    uint64_t xp_gain_start_ms[PLAYER_SKILL_MAX];
    int64_t xp_gained_total;
    uint64_t xp_gained_total_start_ms;
    int xp_last_gain_skill;

    ChangePasswordStep show_change_password_step;
    char change_password_old[PASSWORD_LENGTH + 1];
    char change_password_new[PASSWORD_LENGTH + 1];

    PacketStream *packet_stream;
    uint64_t packet_last_read;
    int8_t incoming_packet[PACKET_BUFFER_LENGTH];

    char username[USERNAME_LENGTH + 1];
    char password[PASSWORD_LENGTH + 1];

    /* sprite indexes used for surface sprite drawing */
    int sprite_media;
    int sprite_util;
    int sprite_item;
    int sprite_logo;
    int sprite_projectile;
    int sprite_texture;
    int sprite_texture_world;

    Surface *surface;
    Scene *scene;
    World *world;

    /* amount of entity, action and teleport bubble sprites */
    int scene_sprite_count;

    /* created from cache and copied for each in-game instance */
    GameModel *game_models[GAME_OBJECTS_MAX];
    GameModel **item_models;

    /* ./ui/login.c */
    Panel *panel_login_welcome;
    Panel *panel_login_new_user;
    Panel *panel_login_existing_user;
    int control_welcome_new_user;
    int control_welcome_existing_user;
    int control_welcome_options;
    int control_welcome_worlds;
    int refer_id;
    int control_login_new_ok;
    int control_register_status;
    int control_register_status_bottom;
    int control_register_user;
    int control_register_password;
    int control_register_confirm_password;
    int control_register_checkbox;
    int control_register_submit;
    int control_register_cancel;
    int control_login_status;
    int control_login_status_bottom;
    int control_login_username;
    int control_login_password;
    int control_login_ok;
    int control_login_cancel;
    int control_login_recover;

    LOGIN_STAGE login_screen;
    char login_username[USERNAME_LENGTH + 1];
    char login_pass[PASSWORD_LENGTH + 1];
    char *login_prompt;
    char login_username_display[USERNAME_LENGTH + 3];

#ifdef REVISION_177
    int session_id;
#else
    int64_t session_id;
#endif

    int8_t logged_in;

    /* ./ui/message-tabs.c */
    Panel *panel_message_tabs;
    int control_text_list_all;
    int control_text_list_chat;
    int control_text_list_quest;
    int control_text_list_private;
    MessageTab message_tab_selected;
    int message_tab_flash_all;
    int message_tab_flash_history;
    int message_tab_flash_quest;
    int message_tab_flash_private;
    char message_history[MESSAGE_HISTORY_LENGTH][255];
    int message_history_timeout[MESSAGE_HISTORY_LENGTH];

    int login_timer;

#ifndef REVISION_177
    int system_update;
#endif

    /* ./ui/combat-style.c */
    int combat_style;

    int logout_timeout;
    int combat_timeout;

    int object_count;
    struct Scenery objects[OBJECTS_MAX];

    int wall_object_count;
    struct ServerBoundary wall_objects[WALL_OBJECTS_MAX];

    int player_server_indexes[PLAYERS_MAX];
    GameCharacter *player_server[PLAYERS_SERVER_MAX];

    int player_count;
    GameCharacter *players[PLAYERS_MAX];

    int known_player_count;
    GameCharacter *known_players[PLAYERS_MAX];

    /* the player we're controlling */
    int local_player_server_index;
    GameCharacter *local_player;
    GameCharacter *combat_target;

    GameCharacter *npcs_server[NPCS_SERVER_MAX];

    int npc_count;
    GameCharacter *npcs[NPCS_MAX];

    int known_npc_count;
    GameCharacter *known_npcs[NPCS_MAX];

    int ground_item_count;
    struct ItemSpawn ground_items[GROUND_ITEMS_MAX];

    /* ./ui/sleep.c */
    int8_t is_sleeping;
    int sleep_word_delay_timer;
    int sleep_word_delay;
    int fatigue_sleeping;
    char *sleeping_status_text;

    /* fade distant landscape */
    int8_t fog_of_war;

    /* used to keep track of model indexes to swap to in order to simulate
     * movement */
    int object_animation_count;
    int object_animation_cycle;
    int last_object_animation_cycle;
    int torch_animation_cycle;
    int last_torch_animation_cycle;
    int claw_animation_cycle;
    int last_claw_animation_cycle;

    /* (usually) yellow messages above player and NPC heads */
    int received_messages_count;
    int received_message_x[RECEIVED_MESSAGE_MAX];
    int received_message_y[RECEIVED_MESSAGE_MAX];
    int received_message_mid_point[RECEIVED_MESSAGE_MAX];
    int received_message_height[RECEIVED_MESSAGE_MAX];
    char *received_messages[RECEIVED_MESSAGE_MAX];

    int camera_angle;
    int camera_rotation;
    int camera_zoom;
    int camera_rotation_time;
    int camera_rotation_x;
    int camera_rotation_y;
    int camera_rotation_x_increment;
    int camera_rotation_y_increment;
    int camera_auto_rotate_player_x;
    int camera_auto_rotate_player_y;
    int camera_auto_counter;

    int8_t is_in_wilderness;
    int loading_area;
    int plane_width;
    int plane_height;
    int plane_index;
    int plane_multiplier;
    int region_x;
    int region_y;
    int local_region_x;
    int local_region_y;

    int last_plane_index;
    int local_lower_x;
    int local_lower_y;
    int local_upper_x;
    int local_upper_y;

    /* ./ui/wilderness-warning.c */
    int show_wilderness_warning;

    /* oh dear you are dead */
    int death_screen_timeout;

    /* bubbles with items above players' heads */
    int action_bubble_count;
    struct ActionBubble action_bubbles[ACTION_BUBBLE_MAX];

    /* green/red health bars displayed above characters' heads in combat */
    int health_bar_count;
    struct HealthBar health_bars[HEALTH_BAR_MAX];

    /* blue/red bubbles used for teleporting and telegrabbing */
    int magic_bubble_count;
    struct MagicBubble magic_bubbles[MAGIC_BUBBLE_MAX];

    int overworld_text_count;
    struct OverworldText overworld_text[OVERWORLD_TEXT_MAX];

    /*int8_t show_dialog_report_abuse_step;
    int report_abuse_offence;*/

    /* ./ui/ui-tabs.c */
    /* which UI tab is currently hovered over */
    int show_ui_tab;

    /* boundaries for when to close a tab UI */
    int ui_tab_min_x;
    int ui_tab_max_x;
    int ui_tab_min_y;
    int ui_tab_max_y;

    /* used to rotate minimap randomly on open for anti-macro */
    int minimap_random_rotation;
    int minimap_random_scale;

    /* ./ui/menu.c */
    int8_t show_right_click_menu;
    int16_t menu_items_count;
    int16_t menu_items_size;
    int16_t *menu_indices;
    struct MenuEntry *menu_items;
    int menu_width;
    int menu_height;
    int menu_x;
    int menu_y;

    /* ./ui/inventory-tab.c */
    int inventory_items_count;
    int inventory_item_id[INVENTORY_ITEMS_MAX];
    int inventory_item_stack_count[INVENTORY_ITEMS_MAX]; // TODO rename
    int inventory_equipped[INVENTORY_ITEMS_MAX];
    // per-slot noted flag in custom inventory packets; 0 on authentic/SP; a noted item always carries an amount
    int inventory_item_noted[INVENTORY_ITEMS_MAX];
    int selected_item_inventory_index;
    char *selected_item_name;

    /* ./ui/stats-tab.c */
    Panel *panel_quests;
    int control_list_quest;
    int8_t *quest_complete;

    // custom (10010) quest list: (id, stage, name) triples; stage < 0 complete, > 0 started, 0 not started;
    // empty on authentic/SP
    int orsc_quest_count;
    int orsc_quest_id[ORSC_QUEST_MAX];
    int orsc_quest_stage[ORSC_QUEST_MAX];
    char orsc_quest_name[ORSC_QUEST_MAX][ORSC_QUEST_NAME_MAX];

    // custom (10010) SEND_BANK_PRESET (150): stored loadout per preset slot; id -1 = empty slot
    int8_t orsc_preset_known[ORSC_PRESET_COUNT];
    int orsc_preset_inventory_id[ORSC_PRESET_COUNT][INVENTORY_ITEMS_MAX];
    int orsc_preset_inventory_amount[ORSC_PRESET_COUNT][INVENTORY_ITEMS_MAX];
    uint8_t orsc_preset_inventory_noted[ORSC_PRESET_COUNT][INVENTORY_ITEMS_MAX];
    int orsc_preset_equipment_id[ORSC_PRESET_COUNT][11];
    int orsc_preset_equipment_amount[ORSC_PRESET_COUNT][11];

    // preset viewer ("Assign Presets") state
    int8_t show_dialog_bank_preset;
    int bank_preset_selected_slot;

    // client-side note toggle; sets the noted byte on a custom BANK_WITHDRAW
    int8_t bank_swap_note_mode;

    // bank slot picked up for a reorder, or -1 (0 is a real slot)
    int bank_organize_slot;
    int8_t bank_organize_insert;

    // custom (10010) SEND_ONLINE_LIST (136); filled by the ::online reply
    int orsc_online_count;
    char orsc_online_name[ORSC_ONLINE_LIST_MAX][MAX_USER_LENGTH + 1];
    int orsc_online_crown[ORSC_ONLINE_LIST_MAX];
    char orsc_online_location[ORSC_ONLINE_LIST_MAX][ORSC_ONLINE_LOCATION_MAX];
    int8_t show_dialog_online_list;
    int online_list_scroll;
    // per-entry right-click menu: -1 = hidden, else the entry index
    int online_list_menu_entry;
    int online_list_menu_x;
    int online_list_menu_y;
    int ui_tab_stats_sub_tab;
    int player_skill_current[PLAYER_SKILL_MAX];
    int player_skill_base[PLAYER_SKILL_MAX];
    int player_experience[PLAYER_SKILL_MAX];
    // skills sent in the last stat-list packet: N = (len - 1) / 6; defaults to 18
    int player_skill_count;
    int player_quest_points;
    int stat_fatigue;
    int player_stat_equipment[PLAYER_STAT_EQUIPMENT_COUNT];

    /* ./ui/worldlist.c */
    Panel *panel_login_worldlist;
    int control_list_worlds;
    int control_worldlist_button;

    /* ./ui/magic-tab.c */
    Panel *panel_magic;
    int control_list_magic;
    int ui_tab_magic_sub_tab;
    int selected_spell;
    int8_t prayer_on[PRAYER_COUNT];

    /* decompressed archive of all 8-bit 8KHz ulaw samples */
    int8_t *sound_data;

    /* 100 kilobytes of 16-bit linear PCM */
    int16_t pcm_out[PCM_LENGTH];

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    int gl_is_walking;

    // set by the first packet tick of a rendered frame; later catch-up cycles skip the socket syscalls
    int gl_net_io_this_frame;

    // object set changed (SERVER_REGION_OBJECTS); scenery bake + terrain lighting refresh once in the next draw_game
    int gl_region_bake_pending;
    int gl_region_bake_last_ms;

    // deferred, coalesced wall + ground-item GL rebuilds; done at most once per frame in draw_game
    int gl_wall_update_pending;
    int gl_ground_item_update_pending;

    // async region loader: a worker builds a neighbouring region off-thread into an LRU cache;
    // a crossing becomes a pointer swap + GL realize
    struct World *region_next_world;
    gl_vertex_buffer **region_next_buffers;
    int region_next_buffer_length;
    int region_load_state; // 0 idle, 1 a build is in flight
    int region_load_lx;
    int region_load_ly;
    int region_load_plane;
    volatile int region_load_done;
    int region_load_force_sync;
    int region_building_index; // cache slot the in-flight build targets, or -1
    uint64_t region_touch_clock;

    // previous world after a swap; its models free a slice per frame
    struct World *region_old_world;
    int region_old_free_index;

    struct RegionCacheEntry region_cache[REGION_CACHE_SLOTS];

    // off-thread scenery bake: worker bakes geometry into a private arena, render thread realizes buffers and re-points models
    int region_bake_state; // 0 idle, 1 building
    volatile int region_bake_done;
    int16_t *bake_next_arena;
    struct GameModel **bake_next_copies;
    struct GameModel **bake_live_models;
    int bake_next_count;
    gl_vertex_buffer **bake_next_buffers;
    int bake_next_buffer_length;
#endif

    // roof-visibility state last applied to the scene; 0 = unknown, forces a re-apply
    int gl_roof_scene_state;

#ifdef RENDER_GL
    int gl_mouse_x;
    int gl_mouse_y;

    int gl_last_swap;
#endif

    int walk_path_x[PATH_STEPS_MAX];
    int walk_path_y[PATH_STEPS_MAX];

    /* ./ui/appearance.c */
    Panel *panel_appearance;
    int control_appearance_head_left;
    int control_appearance_head_right;
    int control_appearance_hair_left;
    int control_appearance_hair_right;
    int control_appearance_gender_left;
    int control_appearance_gender_right;
    int control_appearance_top_left;
    int control_appearance_top_right;
    int control_appearance_skin_left;
    int control_appearance_skin_right;
    int control_appearance_bottom_left;
    int control_appearance_bottom_right;
    int control_appearance_accept;

    // OpenRSC per-character game-mode (Ironman), one-xp toggle, and class, chosen on the appearance screen;
    // sent as extra bytes on CLIENT_APPEARANCE
    int control_appearance_ironman_left;
    int control_appearance_ironman_right;
    int control_appearance_onexp_left;
    int control_appearance_onexp_right;
    int control_appearance_class_left;
    int control_appearance_class_right;
    // box centres, so the draw pass can render the current selection text
    int appearance_ironman_box_x;
    int appearance_ironman_box_y;
    int appearance_onexp_box_x;
    int appearance_onexp_box_y;
    int appearance_class_box_x;
    int appearance_class_box_y;

    int8_t show_appearance_change;
    int appearance_head_type;
    int appearance_head_gender;
    int appearance_body_type;
    int appearance_hair_colour;
    int appearance_top_colour;
    int appearance_skin_colour;
    int appearance_bottom_colour;

    // 0 Regular(None) 1 Ironman 2 Ultimate 3 Hardcore  (IronmanMode ids)
    int appearance_ironman_mode;
    // 0 = use world xp rate, 1 = original 1x xp (OpenRSC isOneXp byte)
    int appearance_one_xp;
    // 0 ADVENTURER 1 WARRIOR 2 WIZARD 3 NECROMANCER 4 RANGER 5 MINER
    int appearance_class;

    /* ./ui/option-menu.c */
    int8_t show_option_menu;
    int option_menu_count;
    char option_menu_entry[OPTION_MENU_MAX][255];

    /* ./ui/welcome.c */
    int8_t show_dialog_welcome;
    int welcome_screen_already_shown;
    int welcome_last_ip;
    int welcome_days_ago;
    int welcome_recovery_set_days;
    int welcome_unread_messages;
    char *welcome_last_ip_string;

    /* ./ui/server-message.c */
    int8_t show_dialog_server_message;
    int server_message_box_top;
    char server_message[4096];

    /* extra page for compact mode */
    int server_message_page;
    char server_message_next[4096];

    /* ./ui/bank.c */
    int8_t show_dialog_bank;
    int new_bank_item_count;
    int new_bank_items[BANK_ITEMS_MAX];
    int new_bank_items_count[BANK_ITEMS_MAX];
    int bank_item_count;
    int bank_items[BANK_ITEMS_MAX];
    int bank_items_count[BANK_ITEMS_MAX];
    int bank_items_max;
    int bank_active_page;
    int bank_selected_item_slot;
    int bank_selected_item;
    int bank_offer_type;
    int bank_last_deposit_offer;
    int bank_last_withdraw_offer;
    int bank_scroll_row;
    int bank_last_scroll;
    int8_t bank_handle_dragged;
    int bank_visible_rows;
    int8_t bank_search_focus;

    /* ./ui/shop.c */
    int8_t show_dialog_shop;
    int shop_items[SHOP_ITEMS_MAX];
    int shop_items_count[SHOP_ITEMS_MAX];
    int shop_items_price[SHOP_ITEMS_MAX];
    int shop_selected_item_index;
    int shop_selected_item_type;
    int shop_buy_price_mod;
    int shop_sell_price_mod;

    /* ./ui/transaction.c */
    int transaction_item_count;
    int transaction_items[TRADE_ITEMS_MAX];
    int transaction_items_count[TRADE_ITEMS_MAX];
    int transaction_recipient_accepted;
    int transaction_accepted;
    char transaction_recipient_name[USERNAME_LENGTH + 1];
    int transaction_recipient_item_count;
    int transaction_recipient_items[TRADE_ITEMS_MAX];
    int transaction_recipient_items_count[TRADE_ITEMS_MAX];
    int transaction_selected_item;
    int transaction_last_offer;
    int transaction_offer_type;
    int transaction_tab; /* used for compact mode */

    int64_t transaction_recipient_confirm_name;
    // custom (10010) confirm windows send the opponent name as a string, not a base37 hash; the raw string is kept
    char transaction_recipient_confirm_name_str[USERNAME_LENGTH + 1];
    int transaction_confirm_item_count;
    int transaction_confirm_items[TRADE_ITEMS_MAX];
    int transaction_confirm_items_count[TRADE_ITEMS_MAX];
    int transaction_recipient_confirm_item_count;
    int transaction_recipient_confirm_items[TRADE_ITEMS_MAX];
    int transaction_recipient_confirm_items_count[TRADE_ITEMS_MAX];
    int transaction_confirm_accepted;

    // per-item noted flag in trade/duel item lists; custom (10010) with S_WANT_BANK_NOTES, else 0
    uint8_t transaction_items_noted[TRADE_ITEMS_MAX];
    uint8_t transaction_recipient_items_noted[TRADE_ITEMS_MAX];
    uint8_t transaction_confirm_items_noted[TRADE_ITEMS_MAX];
    uint8_t transaction_recipient_confirm_items_noted[TRADE_ITEMS_MAX];

    /* ./ui/trade.c */
    int8_t show_dialog_trade;
    int8_t show_dialog_trade_confirm;

    /* ./ui/duel.c */
    int8_t show_dialog_duel;
    int8_t show_dialog_duel_confirm;

    int8_t duel_option_retreat;
    int8_t duel_option_magic;
    int8_t duel_option_prayer;
    int8_t duel_option_weapons;

    /* ./ui/offer-x.c */
    int8_t show_dialog_offer_x;
    int offer_id;
    int offer_max;

    // OpenRSC want_drop_x: inventory slot awaiting a drop-X amount, or -1
    int drop_offer_index;

    /* ./ui/confirm.c */
    int8_t show_dialog_confirm;
    char *confirm_text_top;
    char *confirm_text_bottom;
    CONFIRM_TYPE confirm_type;

    /* ./ui/additional-options.c */
    int show_additional_options;
    int options_tab;

    Panel *panel_game_options;
    void *game_options[50];
    int game_option_types[50];

    Panel *panel_control_options;
    // three vita adjusters (8 controls each) plus the rows: 50 was one adjuster short
    void *control_options[64];
    int control_option_types[64];

    Panel *panel_ui_options;
    void *ui_options[50];
    int ui_option_types[50];

    Panel *panel_bank_options;
    void *bank_options[50];
    int bank_option_types[50];

    /* ./ui/experience-drops.c */
    int experience_drop_skill[EXPERIENCE_DROPS_MAX];
    int experience_drop_amount[EXPERIENCE_DROPS_MAX];
    float experience_drop_y[EXPERIENCE_DROPS_MAX];
    float experience_drop_speed[EXPERIENCE_DROPS_MAX];
    int experience_drop_count;

    /* wiki */
    int selected_wiki;

    char server[64];
    int port;

    char rsa_exponent[512];
    char rsa_modulus[512];

#ifdef WITH_SINGLEPLAYER
    int singleplayer; // play against the embedded offline server (loopback)
#endif

    // selected online world speaks the revision-177 dialect (OpenRSC); 0 = canonical 204 protocol
    int protocol177;

    // selected online world is an OpenRSC custom world (client_version 10010): 2-byte plaintext framing,
    // custom login, no ISAAC, native 204 opcodes
    int protocol_custom;

    // OpenRSC custom-world config flags from SEND_SERVER_CONFIGS (opcode 19); drive per-world sprites,
    // landscape, extra skills, custom UI
    struct {
        int custom_sprites;
        int custom_landscape;
        int want_runecraft;
        int want_harvesting;
        int want_equipment_tab;
        int want_clans;
        int want_parties;
        int want_bank_pins;
        // positions verified against Client_Base PacketHandler setServerConfiguration
        int spawn_auction_npcs;  // 4  (also gates a banker's "Collect")
        int floating_nametags;   // 6
        int want_kill_feed;      // 8
        int batch_progression;   // 12
        int experience_counter_toggle; // 18. gates the on-screen xp counter (and its settings row)
        // 25/26: gate the skill-guide and quest-guide windows
        int want_skill_menus;    // 25
        int want_quest_menus;    // 26
        int experience_drops_toggle; // 19: gates the xp-drops settings row
        // 28: dialogue-option hotkeys: 0 = off, 1 = number keys 1-5, 2 = keys + a "(N)" prefix
        int want_keyboard_shortcuts; // 28
        int want_exp_info;       // 35. adds the all-skills "Total xp" line to the stats tab
        // 32: certificate deposits; adds "Uncert+Deposit-X/All" rows to the bank deposit menu; wire is packet 199 sub-op 0 + mode byte
        int want_cert_deposit;   // 32
        // 41: colour-carry; re-apply the last @col@ code across a wrap in overhead chat
        int want_fixed_overhead_chat; // 41
        // 67: swaps the "web" wall-object commands to Slice/WalkTo (from WalkTo/Examine)
        int want_leftclick_webs; // 67
        // 1: world display name ("RSC Cabbage")
        char server_name[32];    // 1 (string)
        int side_menu;           // 13 (their sideMenuToggle HUD)
        int want_elixirs;        // 27
        int want_drop_x;         // 34 (gates the Drop-X inventory menu)
        int want_nature_rune_protection; // 90 (blocks alch on Nature-Rune)
        // 31: when set, custom trade/duel/ground-item payloads carry a per-item noted byte
        // 29: gates the custom bank interface (presets, equipment panel, drag-reorder, note toggles) and maxBankSize = ItemId.maxCustom
        int want_custom_banks;   // 29
        int want_bank_notes;     // 31
        // 79: noted-form wording; set = "<item>", clear = "<item> Certificate"
        int want_cert_as_notes;  // 79
        // 39: gates the staff colour prefix on a player's name
        int want_custom_rank_display; // 39
        int right_click_bank;    // 40
        int want_fatigue;        // 51
        int want_quest_started_indicator; // 57 (yellow "started" quest name)
        int want_bank_presets;   // 63
        // 71: appearance panel type: 0 = authentic (no extra selectors), 1 = ironman list + 1X-xp question, 2 = classes + global PK; gates the Mode/Class/XP-rate rows
        int character_creation_mode; // 71
        // 72: world skilling xp multiplier (Cabbage 5, Coleslaw 2)
        int skilling_exp_rate;   // 72
        int right_click_trade;   // 76
        int features_sleep;      // 77
        int want_openpk_points;  // 80
    } orsc;

    // OpenRSC worn-equipment snapshot (SEND_EQUIPMENT 254 / _UPDATE 255): 11 display slots;
    // 14 wield positions collapse 5->0, 6->1, 7->2, >7 -= 3
    int equipped_item_id[11];
    int equipped_item_amount[11];
    int tab_equipment_index; // 0 = inventory grid, 1 = equipment paperdoll

    // OpenRSC stat-overlay state from custom-only packets; *_seen flags double as display gates
    int orsc_npc_kills_total; // SEND_NPC_KILLS 147: 3x i32
    int orsc_npc_kills_recent_id;
    int orsc_npc_kills_recent_count;
    int orsc_npc_kills_seen;
    // SEND_ELIXIR 54: raw wire units, decremented once per engine tick, displayed as (timer/50) -> M:SS
    int orsc_elixir_timer;
    int orsc_experience_frozen; // SEND_EXPERIENCE_TOGGLE 34: u8
    int orsc_exp_shared; // SEND_EXPSHARED 98: u16 (party share)
    int64_t orsc_openpk_points; // SEND_OPENPK_POINTS 148: i64 (OpenPK)

    // OpenRSC custom bank PIN pad (SEND_BANK_PIN 135; see ui/bank-pin.c)
    int show_dialog_bank_pin;
    char bank_pin_input[5];
    int bank_pin_length;

    // OpenRSC custom batch progress bar (SEND_STATUS_PROGRESS_BAR 134)
    int orsc_progress_visible;
    int orsc_progress_total;
    int orsc_progress_current;
    int orsc_progress_delay; // ms per batch item (stored, not yet animated)

    // OpenRSC custom clans (SEND_CLAN 112): actionId 0 = roster snapshot, 1 = left/cleared, 2 = invite popup, 3 = settings, 4 = clan list;
    // ORSC_CLAN_MEMBERS_MAX caps the roster, overflow dropped
#define ORSC_CLAN_MEMBERS_MAX 50
    int orsc_clan_in;
    char orsc_clan_name[33];
    char orsc_clan_tag[9];
    char orsc_clan_leader[33];
    int orsc_clan_is_leader;
    int orsc_clan_size;
    char orsc_clan_member_names[ORSC_CLAN_MEMBERS_MAX][33];
    int orsc_clan_member_ranks[ORSC_CLAN_MEMBERS_MAX];
    int orsc_clan_member_online[ORSC_CLAN_MEMBERS_MAX];
    char orsc_clan_create_name[33]; // step-1 stash of the create flow
    char orsc_clan_pending_leader[33]; // leadership-transfer confirm target
    int orsc_clan_settings[5]; // kick/invite/searchJoin/allow0/allow1

    // clan browse results (SEND_CLAN actionId 4, requested via 199[11][8]); joining a listed clan sends ::joinclan <name>
#define ORSC_CLAN_BROWSE_MAX 32
    int orsc_clan_browse_count;
    char orsc_clan_browse_names[ORSC_CLAN_BROWSE_MAX][33];
    char orsc_clan_browse_tags[ORSC_CLAN_BROWSE_MAX][9];
    int orsc_clan_browse_members[ORSC_CLAN_BROWSE_MAX];
    int orsc_clan_browse_can_join[ORSC_CLAN_BROWSE_MAX];
    int orsc_clan_browse_points[ORSC_CLAN_BROWSE_MAX];
    // backing storage for the invite popup's confirm-dialog lines
    char orsc_clan_invite_top[80];
    char orsc_clan_invite_bottom[80];

    // OpenRSC custom parties (SEND_PARTY 116): actionId 0 = snapshot, 1 = left/cleared, 2 = invite popup;
    // snapshot keyed by leader name
#define ORSC_PARTY_MEMBERS_MAX 16
    int orsc_party_in;
    char orsc_party_leader[33];
    int orsc_party_is_leader;
    int orsc_party_size;
    char orsc_party_member_names[ORSC_PARTY_MEMBERS_MAX][33];
    int orsc_party_member_online[ORSC_PARTY_MEMBERS_MAX];
    int orsc_party_member_cur_hits[ORSC_PARTY_MEMBERS_MAX];
    int orsc_party_member_max_hits[ORSC_PARTY_MEMBERS_MAX];
    int orsc_party_member_combat[ORSC_PARTY_MEMBERS_MAX];
    // remaining party-snapshot status bytes for the party HUD: rank (1 = leader crown), skulled, in combat
    int orsc_party_member_rank[ORSC_PARTY_MEMBERS_MAX];
    int orsc_party_member_skull[ORSC_PARTY_MEMBERS_MAX];
    int orsc_party_member_in_combat[ORSC_PARTY_MEMBERS_MAX];
    // render frames left of the recent-damage flash; while set the HP bar shows red only
    int orsc_party_member_flash[ORSC_PARTY_MEMBERS_MAX];
    char orsc_party_invite_top[80];
    char orsc_party_invite_bottom[80];
    int orsc_party_settings[5]; // SEND_PARTY actionId 3: 3 settings + 2 allowed
    // party browse results (SEND_PARTY_LIST, actionId 4 on the party opcode, requested via 199[12][8]); informational
#define ORSC_PARTY_BROWSE_MAX 32
    int orsc_party_browse_count;
    int orsc_party_browse_ids[ORSC_PARTY_BROWSE_MAX];
    int orsc_party_browse_members[ORSC_PARTY_BROWSE_MAX];
    int orsc_party_browse_can_join[ORSC_PARTY_BROWSE_MAX];
    int orsc_party_browse_points[ORSC_PARTY_BROWSE_MAX];

    // small custom-only states: SEND_IRONMAN 113 (mode/restriction; actionId 1/2 shows/hides the selection UI)
    // + SEND_ON_TUTORIAL 111
    int orsc_ironman_type;
    int orsc_ironman_restriction;
    int orsc_on_tutorial;
    int show_dialog_ironman;

    // OpenRSC custom auction house (opcode 132); rows past the cap are parsed but dropped and counted in _overflow;
    // cap 512 rows
#define ORSC_AUCTION_MAX 512
    int orsc_auction_visible;
    int orsc_auction_count;
    int orsc_auction_overflow;
    int orsc_auction_scroll;
    int orsc_auction_selected;
    int orsc_auction_ids[ORSC_AUCTION_MAX];
    int orsc_auction_item_ids[ORSC_AUCTION_MAX];
    int orsc_auction_amounts[ORSC_AUCTION_MAX];
    int orsc_auction_prices[ORSC_AUCTION_MAX];
    int orsc_auction_mine[ORSC_AUCTION_MAX];
    char orsc_auction_sellers[ORSC_AUCTION_MAX][33];
    int orsc_auction_hours[ORSC_AUCTION_MAX];
    int orsc_auction_sell_mode; // armed by [Sell], next inventory right-click offers Auction
    int orsc_auction_create_item;
    int orsc_auction_create_amount;
    int orsc_auction_offer_stage; // AUCTION_OFFER_* in ui/auction.h

    // OpenRSC custom kill feed (SEND_KILL_ANNOUNCEMENT 118): top-right ticker, newest first, max 10 rows, 8s TTL;
    // killType = killing weapon item id (-1 ranged, -2 magic)
#define ORSC_KILL_FEED_MAX 10
#define ORSC_KILL_FEED_TTL 400 // 8s at 50 engine ticks/s
    struct {
        char killer[33];
        char victim[33];
        int kill_type;
        int ticks_left;
    } orsc_kill_feed[ORSC_KILL_FEED_MAX];
    int orsc_kill_feed_count;

    // SEND_ON_BLACK_HOLE 115: boolean; only effect is a taller chat-box backing, no interface
    int orsc_black_hole;

    // SEND_UNLOCKED_APPEARANCES 250: which skin colours the account may pick; only skin bits stored;
    // default = five original RSC skins unlocked, rest locked
    int8_t orsc_unlocked_skin[PLAYER_SKIN_COLOUR_COUNT];
    int orsc_unlocked_skin_known;

    // OpenRSC rank crown for the message being shown; set by the rich-chat handler (SERVER_MESSAGE 131),
    // consumed and cleared when the chat entry is added; first line only
    int orsc_pending_crown;

    // SEND_OPENPK_POINTS_TO_GP_RATIO 144: opens the points->GP exchange (OpenPK world)
    int orsc_show_points_to_gp;

    // SEND_FISHING_TRAWLER 133: action 0 show / 1 variables / 2 hide;
    // variables = water u16, fish u16, minutes u8, netRipped u8
    int orsc_trawler_visible;
    int orsc_trawler_water;
    int orsc_trawler_fish;
    int orsc_trawler_minutes;
    int orsc_trawler_net_ripped;

#ifdef WITH_SINGLEPLAYER
    // LAN/ad-hoc co-op join state; spnet_address holds the scanned co-op world's address for spnet_connect();
    // cleared when a preset or single-player world is selected
    int spnet_guest; // 1 = the selected world is a scanned co-op guest join
    char spnet_address[64]; // spnet_world_info.address of the selected world
#endif
};

void mudclient_new(mudclient *mud);
void mudclient_resize(mudclient *mud);
void mudclient_start_application(mudclient *mud, char *title);

void mudclient_handle_key_press(mudclient *mud, int key_code);
void mudclient_key_pressed(mudclient *mud, int code, int char_code);
void mudclient_key_released(mudclient *mud, int code);
void mudclient_handle_mouse_history(mudclient *mud, int x, int y);
void mudclient_mouse_moved(mudclient *mud, int x, int y);
void mudclient_mouse_released(mudclient *mud, int x, int y, int button);
void mudclient_mouse_pressed(mudclient *mud, int x, int y, int button);

void mudclient_set_target_fps(mudclient *mud, int fps);
void mudclient_reset_timings(mudclient *mud);
void mudclient_start(mudclient *mud);
void mudclient_stop(mudclient *mud);

void mudclient_draw_loading_progress(mudclient *mud, int percent, char *text);
int8_t *mudclient_read_data_file(mudclient *mud, char *file, char *description,
                                 int percent);
void mudclient_load_jagex_tga_sprite(mudclient *mud, int8_t *buffer);
void mudclient_load_jagex(mudclient *mud);
void mudclient_load_game_config(mudclient *mud);
void mudclient_load_media(mudclient *mud);
void mudclient_load_entities(mudclient *mud);
void mudclient_load_textures(mudclient *mud);
void mudclient_load_models(mudclient *mud);
void mudclient_load_maps(mudclient *mud);
void mudclient_load_sounds(mudclient *mud);

GameModel *mudclient_create_wall_object(mudclient *mud, int x, int y,
                                        int direction, int id, int count);
int mudclient_load_next_region(mudclient *mud, int lx, int ly);
void mudclient_region_proximity_check(mudclient *mud);
void mudclient_gl_bake_cancel(mudclient *mud);

GameCharacter *mudclient_add_character(mudclient *mud,
                                       GameCharacter **character_server,
                                       GameCharacter **known_characters,
                                       int known_character_count,
                                       int server_index, int x, int y,
                                       int animation, int npc_id);
GameCharacter *mudclient_add_player(mudclient *mud, int server_index, int x,
                                    int y, int animation);
GameCharacter *mudclient_add_npc(mudclient *mud, int server_index, int x, int y,
                                 int animation, int npc_id);

void mudclient_update_bank_items(mudclient *mud);
void mudclient_close_connection(mudclient *mud);
void mudclient_lost_connection(mudclient *mud);
int mudclient_is_valid_camera_angle(mudclient *mud, int angle);
void mudclient_auto_rotate_camera(mudclient *mud);
void mudclient_handle_camera_zoom(mudclient *mud);
void mudclient_handle_game_input(mudclient *mud);
void mudclient_handle_inputs(mudclient *mud);
void mudclient_update_object_animation(mudclient *mud, int object_index,
                                       char *model_name);
#ifndef REVISION_177
// OpenRSC floating name/clan-tag overlay (custom-online, players only)
void mudclient_draw_character_nametag(mudclient *mud, GameCharacter *player,
                                      int x, int y, int width);
#endif
void mudclient_draw_character_message(mudclient *mud, GameCharacter *character,
                                      int x, int y, int width);
void mudclient_draw_character_damage(mudclient *mud, GameCharacter *character,
                                     int x, int y, int ty, int width,
                                     int height, int is_npc, float depth);
int mudclient_should_chop_head(mudclient *mud, GameCharacter *character,
                               ANIMATION_INDEX animation_index);
void mudclient_draw_player(mudclient *mud, int x, int y, int width, int height,
                           int id, int skew_x, int ty, float depth_top,
                           float depth_bottom);
void mudclient_draw_npc(mudclient *mud, int x, int y, int width, int height,
                        int id, int skew_x, int ty, float depth_top,
                        float depth_bottom);
void mudclient_draw_blue_bar(mudclient *mud);
int mudclient_is_in_combat(mudclient *mud);
GameCharacter *mudclient_get_opponent(mudclient *mud);
void mudclient_draw_ui(mudclient *mud);
int mudclient_compare_text(const void *v1, const void *v2);
void mudclient_draw_overhead(mudclient *mud);
void mudclient_animate_objects(mudclient *mud);
#ifdef RENDER_GL
void mudclient_gl_bake_region_objects(mudclient *mud);
#endif
void mudclient_draw_entity_sprites(mudclient *mud);
void mudclient_draw_game(mudclient *mud);
void mudclient_reset_game(mudclient *mud);
void mudclient_login(mudclient *mud, char *username, char *password,
                     int reconnecting);
void mudclient_registration_login(mudclient *mud);
void mudclient_register(mudclient *mud, char *username, char *password);
void mudclient_change_password(mudclient *mud, char *old_password,
                               char *new_password);
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void mudclient_update_fov(mudclient *mud);
#endif
void mudclient_start_game(mudclient *mud);
void mudclient_draw(mudclient *mud);

#ifdef SDL12
void mudclient_sdl1_on_resize(mudclient *mud, int width, int height);
#endif
void mudclient_on_resize(mudclient *mud);
void mudclient_poll_events(mudclient *mud);
int mudclient_is_touch(mudclient *mud);
// may this account pick skin colour `index`? five original RSC skins always, rest only via SEND_UNLOCKED_APPEARANCES (250)
int mudclient_is_skin_colour_unlocked(mudclient *mud, int index);
void mudclient_trigger_keyboard(mudclient *mud, char *text, int is_password,
                                int x, int y, int width, int height, int font,
                                int is_centred, int submit_on_enter);
#ifdef __vita__
void vita_ime_open(const char *title, const char *initial, int is_password,
                   int initial_length, int submit_on_enter);
void vita_ime_poll(mudclient *mud);
int vita_ime_is_active(void);
#endif
#ifdef _3DS
void mudclient_3ds_flush_audio(mudclient *mud);
void mudclient_3ds_open_keyboard(mudclient *mud);
void mudclient_3ds_handle_keyboard(mudclient *mud);
void mudclient_3ds_draw_top_background(mudclient *mud);
#endif
void mudclient_run(mudclient *mud);
void mudclient_remove_ignore(mudclient *mud, int64_t encoded_username);
void mudclient_draw_magic_bubble(mudclient *mud, int x, int y, int width,
                                 int height, int id, float depth);
void mudclient_draw_ground_item(mudclient *mud, int x, int y, int width,
                                int height, int id, float depth_top,
                                float depth_bottom);
int mudclient_is_item_equipped(mudclient *mud, int id);
int mudclient_get_inventory_count(mudclient *mud, int id);
int mudclient_has_inventory_item(mudclient *mud, int id, int minimum);
void mudclient_send_logout(mudclient *mud);
void mudclient_play_sound(mudclient *mud, char *name);
int mudclient_walk_to(mudclient *mud, int start_x, int start_y, int x1, int y1,
                      int x2, int y2, int check_objects, int walk_to_action,
                      int first_step);
void mudclient_walk_to_action_source(mudclient *mud, int start_x, int start_y,
                                     int dest_x, int dest_y, int action);
void mudclient_walk_to_ground_item(mudclient *mud, int start_x, int start_y,
                                   int dest_x, int dest_y, int walk_to_action);
void mudclient_walk_to_wall_object(mudclient *mud, int dest_x, int dest_y,
                                   int direction);
void mudclient_walk_to_object(mudclient *mud, int x, int y, int direction,
                              int id);
int mudclient_is_ui_scaled(mudclient *mud);
void mudclient_format_number_commas(mudclient *mud, int number, char *dest);
int mudclient_option_numbers_level(mudclient *mud);
void mudclient_format_item_amount(mudclient *mud, int item_amount, char *dest);

// note helpers: mudclient_item_noted() is slot N a note (0 unless custom + S_WANT_BANK_NOTES);
// mudclient_item_display_name() applies want_cert_as_notes
int mudclient_item_noted(mudclient *mud, int inventory_slot);
void mudclient_item_display_name(mudclient *mud, int item_id, int noted,
                                 char *out, int out_size);
// draw a noted item: certificate/note sprite, not the item icon; inset_y = 4 inventory, 7 preset
void mudclient_draw_noted_item(mudclient *mud, int x, int y, int slot_width,
                               int slot_height, int item_id, int inset_y);
int mudclient_get_wilderness_depth(mudclient *mud);
void mudclient_draw_item(mudclient *mud, int x, int y, int slot_width,
                         int slot_height, int item_id);
int main(int argc, char **argv);
#endif
#ifdef EMSCRIPTEN
void browser_mouse_moved(int x, int y);
void browser_key_pressed(int code, int char_code);
#endif
