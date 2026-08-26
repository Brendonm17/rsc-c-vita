#ifndef _H_SINGLEPLAYER
#define _H_SINGLEPLAYER

// embedded offline RuneScape Classic server. runs the 2003scape/rsc-server bundle inside QuickJS-ng and exposes an
// in-memory loopback the client's PacketStream talks to as if it were a TCP socket. all functions are no-ops if the engine is unavailable

#include <stdint.h>

// boot the embedded server: load the bootstrap + bundle, post {type:'start'} and pump until the world is built and
// the server posts 'ready'. idempotent. blocks for the world load. returns 0 on success, -1 on failure
int singleplayer_start(void);

// true once the embedded server has booted
int singleplayer_is_ready(void);

// open / close the single loopback socket (one client)
void singleplayer_connect(void);
void singleplayer_disconnect(void);

// client -> server: hand raw packet bytes to the embedded server, queued for its next world tick
void singleplayer_client_send(const uint8_t *data, int length);

// server -> client: copy up to `max` queued response bytes into `buf`. returns the number copied, or -1 if none
// available yet
int singleplayer_client_recv(uint8_t *buf, int max);

// advance the embedded server one step: fire any due timers and drain pending JS jobs
void singleplayer_pump(void);

// optional callback invoked before and during the blocking world load to present a loading frame. `ctx` is passed
// back untouched
void singleplayer_set_progress_cb(void (*cb)(void *ctx), void *ctx);

// loading-screen pace in microseconds (16000=60fps, 33000=30fps)
void singleplayer_set_loading_sleep_us(int us);

// boot-progress snapshot for the loading screen. copies the current phase text into `text_out` and returns the
// percentage (0-100). 0 = no phase reported yet
int singleplayer_boot_progress(char *text_out, int n);

// select which single-player world's save folder is active. `id` is a safe identifier; "default" (or NULL/empty) uses
// the saves/ root
void singleplayer_set_world(const char *id);

// set the active single-player world's game rules. `xp_rate` multiplies experience gain (>=1), `members` selects
// members (P2P) content when non-zero. the trailing 16 params are the world's feature toggles, each mapping 1:1 to a server config.json key of the same name (tutorialIsland, wantSkillcapePerks, wantCombatOdyssey, wantPoisonNpcs, wantLeftclickWebs, wantMissingGuildGreetings, fasterYohnus, usesClasses, spawnIronMan, customFiremaking, wantBetterJewelryCrafting, wantCustomLeather, wantNewRareDropTables, npcKillMessages, wantEnchantedCrowns, wantBatchProgression)
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

// whether the active world's rules allow the character-creation Class / Ironman-mode selector to be shown.
// presentation-only; defaults to 1/shown
int singleplayer_world_uses_classes(void);
int singleplayer_world_spawns_ironman(void);

// tear down the engine (frees the world)
void singleplayer_stop(void);

// host left the world: end the embedded server completely: stop hosting, save the host's character, tear down the
// server thread
void singleplayer_leave_world(void);

// SP account management (world-editor "Players" tab). all operate directly on a world's players.json roster

// list up to `max` account usernames for `world_id` into `names` (each <=31 chars, NUL-terminated). returns count (0
// = none), or -1 on parse error
int singleplayer_players_list(const char *world_id, char (*names)[32], int max);

// remove one account from `world_id`. returns 0 on success (incl not-found), -1 on error
int singleplayer_players_delete(const char *world_id, const char *username);

// delete all save files for `world_id` (players + playerID) and rmdir its folder
void singleplayer_world_wipe(const char *world_id);

// enable/disable LAN or ad-hoc hosting so up to SPNET_MAX_GUESTS guests can join this single-player world as online
// clients. `display_name` is the advertised world name, ignored when disabling. disabling drops every connected guest
void singleplayer_host_set_enabled(int on, const char *display_name);

// number of guests currently connected (0 when hosting is disabled)
int singleplayer_host_guest_count(void);

#endif
