#ifndef _H_SINGLEPLAYER
#define _H_SINGLEPLAYER

// embedded offline runescape classic server: runs the rsc-server bundle in
// quickjs and exposes an in-memory loopback socket to the client

#include <stdint.h>

// boot the embedded server; blocks until the world is built. idempotent. returns 0 on success, -1 on failure
int singleplayer_start(void);

// true once the embedded server has booted
int singleplayer_is_ready(void);

// open / close the single loopback socket (one client)
void singleplayer_connect(void);
void singleplayer_disconnect(void);

// client -> server: hand raw packet bytes to the embedded server, queued for the next world tick
void singleplayer_client_send(const uint8_t *data, int length);

// server -> client: copy up to max queued response bytes into buf. returns the count copied, or -1 if none yet
int singleplayer_client_recv(uint8_t *buf, int max);

// advance the embedded server one step: fire due timers and drain pending js jobs
void singleplayer_pump(void);

// callback invoked during the blocking world load to draw a loading frame; ctx is passed back untouched
void singleplayer_set_progress_cb(void (*cb)(void *ctx), void *ctx);

// loading-screen pace in microseconds (16000=60fps, 33000=30fps)
void singleplayer_set_loading_sleep_us(int us);

// boot-progress snapshot: copies the current phase text into text_out and
// returns the percentage 0-100 (0 = no phase reported yet)
int singleplayer_boot_progress(char *text_out, int n);

// select which single-player world's save folder is active. "default" (or null/empty) uses the saves/ root
void singleplayer_set_world(const char *id);

// set the active world's game rules. xp_rate multiplies xp gain (>=1), members selects p2p content when nonzero
// the trailing 16 params are feature toggles, each mapping 1:1 to a server config.json key
void singleplayer_set_world_rules(int xp_rate, int members, int fatigue,
                                  int remember_style, int game_speed,
                                  int custom_quests, int holiday_events,
                                  int tutorial_island, int want_skillcape_perks,
                                  int want_combat_odyssey, int want_poison_npcs,
                                  int want_leftclick_webs,
                                  int want_missing_guild_greetings,
                                  int faster_yohnus, int uses_classes,
                                  int spawn_ironman, int custom_firemaking,
                                  int want_better_jewelry_crafting,
                                  int want_custom_leather,
                                  int want_new_rare_drop_tables,
                                  int npc_kill_messages,
                                  int want_enchanted_crowns,
                                  int want_batch_progression);

// whether the active world's rules show the character-creation class / ironman
// selector; presentation-only, the server enforces it authoritatively
int singleplayer_world_uses_classes(void);
int singleplayer_world_spawns_ironman(void);

// tear down the engine (frees the world)
void singleplayer_stop(void);

// host left the world: end the embedded server, save the host's character, and
// tear down the server thread. no-op for guests / online clients
void singleplayer_leave_world(void);

// sp account management (world-editor "Players" tab); all operate directly on
// a world's players.json roster
int singleplayer_players_list(const char *world_id, char (*names)[32], int max);

// remove one account from world_id. returns 0 on success (incl. not-found), -1 on error
int singleplayer_players_delete(const char *world_id, const char *username);

// delete all save files for world_id (players + playerID) and rmdir its folder
void singleplayer_world_wipe(const char *world_id);

// sp bot management (world-editor "Bots" tab); ops edit bot-defs.json directly,
// one def per bot. personality weights are 0-100 here, written 0.0-1.0 to json
// roster cap
#define SP_BOT_MAX 100

// the server's skill keys, in its order; a def's levels[i] is the starting
// level for SP_BOT_SKILL_KEYS[i], or 0 for the default
#define SP_BOT_SKILL_COUNT 20
extern const char *const SP_BOT_SKILL_KEYS[SP_BOT_SKILL_COUNT];

struct sp_bot_def {
    int enabled;        // 0 = kept on the roster but never spawned
    int levels[SP_BOT_SKILL_COUNT];
    char name[32];      // unique roster name (server lowercases it)
    char archetype[16]; // preset: warrior/skiller/merchant/wanderer/loner/quester/casual
    // every bot runs the "career" brain; no brain selector
    int money;          // money lean: 0 smart (default), 1 bank, 2 sell, 3 alch
    int combat_style;   // 0 controlled, 1 aggressive, 2 accurate, 3 defensive
    int focus;          // combat focus: 0 auto, 1 melee, 2 magic, 3 ranged
    int pvp;            // wilderness pvp appetite: 0 off, 1 rarely, 2 sometimes, 3 often
    int party;          // party appetite: how often it invites others to team up (0 off .. 3 often)
    int trade;          // trade appetite: how often it trades with others (0 off .. 3 often)
    int faction_join;   // faction-join appetite: 0 lone wolf .. 3 joiner
    int paced;          // per-bot paced-levelling opt-in
    // personality sliders, 0-100 (server 0.0-1.0)
    int aggression, risk, greed, sociability, diligence, curiosity, patience;
    // appearance indices -> server hairColour/topColour/trouserColour/skinColour/headSprite/bodySprite
    int hair_colour, top_colour, trouser_colour, skin_colour, head_sprite,
        body_sprite;
};

// option-cycler label counts for money lean / combat style / focus / pvp appetite
#define SP_BOT_MONEY_COUNT 4
#define SP_BOT_STYLE_COUNT 4
#define SP_BOT_PVP_COUNT 4
#define SP_BOT_FOCUS_COUNT 4 // auto/melee/magic/ranged
const char *singleplayer_bot_money_name(int index);
const char *singleplayer_bot_style_name(int index);
const char *singleplayer_bot_pvp_name(int index);
const char *singleplayer_bot_focus_name(int index);

// load a world's bot defs into out (up to max). returns the count (0 = none), or -1 on parse error
int singleplayer_bots_load(const char *world_id, struct sp_bot_def *out, int max);

// a bot's live skill levels from its save record: out_levels[i][skill] = base
// level, out_found[i] = 1 when the record exists
void singleplayer_bots_live_levels(const char *world_id,
                                   const struct sp_bot_def *defs, int count,
                                   int (*out_levels)[SP_BOT_SKILL_COUNT],
                                   int *out_found);

// overwrite a world's bot-defs.json with count defs. returns 0 ok, -1 error
int singleplayer_bots_save(const char *world_id, const struct sp_bot_def *defs,
                           int count);

// fill def's personality sliders + archetype string from an archetype preset. unknown -> casual
void singleplayer_bot_archetype_preset(const char *archetype,
                                       struct sp_bot_def *def);

// archetype cycler support for the editor
int singleplayer_bot_archetype_count(void);
const char *singleplayer_bot_archetype_name(int index);

// enable/disable LAN or ad-hoc hosting so guests can join as online clients.
// display_name is the advertised world name (ignored when disabling); must be called from the main/ui thread
void singleplayer_host_set_enabled(int on, const char *display_name);

// number of guests currently connected (0 when hosting is disabled)
int singleplayer_host_guest_count(void);

#endif
