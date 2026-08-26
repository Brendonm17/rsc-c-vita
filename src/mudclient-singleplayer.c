// mudclient-singleplayer.c: embedded offline RSC server on a dedicated thread. rsc-server bundle runs in QuickJS;
// client and server threads share two lock-free SPSC byte rings plus atomic command flags
#include "singleplayer.h"

#ifdef WITH_SINGLEPLAYER

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#else
#include <pthread.h>
#endif

#include "quickjs.h"
#include "sp-net.h"

#ifndef SP_BOOTSTRAP_PATH
#ifdef __vita__
#define SP_BOOTSTRAP_PATH "app0:/sp/sp-bootstrap.js"
#else
#define SP_BOOTSTRAP_PATH "sp/sp-bootstrap.js"
#endif
#endif

#ifndef SP_BUNDLE_PATH
#ifdef __vita__
#define SP_BUNDLE_PATH "app0:/sp/browser.bundle.js"
#else
#define SP_BUNDLE_PATH "sp/browser.bundle.js"
#endif
#endif

#ifndef SP_SAVE_DIR
#ifdef __vita__
#define SP_SAVE_DIR "ux0:data/RuneScape/saves"
#else
#define SP_SAVE_DIR "sp/saves"
#endif
#endif

// precomputed pathfinder obstacle map
#ifndef SP_PATHFINDER_CACHE_PATH
#ifdef __vita__
#define SP_PATHFINDER_CACHE_PATH "app0:/sp/pathfinder.cache"
#else
#define SP_PATHFINDER_CACHE_PATH "sp/pathfinder.cache"
#endif
#endif

// precomputed parsed landscape (sectors + region bounds)
#ifndef SP_LANDSCAPE_CACHE_PATH
#ifdef __vita__
#define SP_LANDSCAPE_CACHE_PATH "app0:/sp/landscape.cache"
#else
#define SP_LANDSCAPE_CACHE_PATH "sp/landscape.cache"
#endif
#endif

// compiled-bytecode cache of the browser bundle in writable storage; invalidated when the bundle byte length changes
#ifndef SP_BUNDLE_BC_PATH
#ifdef __vita__
#define SP_BUNDLE_BC_PATH "ux0:data/RuneScape/rsc-sp-bundle.bc"
#else
#define SP_BUNDLE_BC_PATH "sp/bundle.bc"
#endif
#endif

// precompiled bundle bytecode shipped with the app; format = u64 LE source length + JS_WriteObject bytes; tried
// first, a length/read mismatch falls through to the ux0: cache, then to compiling
#ifndef SP_BUNDLE_BC_SHIPPED_PATH
#ifdef __vita__
#define SP_BUNDLE_BC_SHIPPED_PATH "app0:/sp/browser.bundle.bc"
#else
#define SP_BUNDLE_BC_SHIPPED_PATH "sp/browser.bundle.bc"
#endif
#endif

#define SP_BOOT_TIMEOUT_MS 600000

// lock-free SPSC byte ring (one writer thread, one reader thread)
#define SP_RING_CAP (1 << 18) // 256 KB, must be a power of two
#define SP_RING_MASK (SP_RING_CAP - 1)

typedef struct {
    uint8_t buf[SP_RING_CAP];
    _Atomic unsigned head; // consumer index
    _Atomic unsigned tail; // producer index
} SpRing;

// producer side: append up to n bytes, returns how many fit
static int sp_ring_write(SpRing *r, const uint8_t *d, int n) {
    unsigned head = atomic_load_explicit(&r->head, memory_order_acquire);
    unsigned tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    int w = 0;
    while (w < n) {
        unsigned next = (tail + 1) & SP_RING_MASK;
        if (next == head) break; // full
        r->buf[tail] = d[w++];
        tail = next;
    }
    atomic_store_explicit(&r->tail, tail, memory_order_release);
    return w;
}

// consumer side: read up to max bytes, returns how many
static int sp_ring_read(SpRing *r, uint8_t *out, int max) {
    unsigned tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    unsigned head = atomic_load_explicit(&r->head, memory_order_relaxed);
    int n = 0;
    while (n < max && head != tail) {
        out[n++] = r->buf[head];
        head = (head + 1) & SP_RING_MASK;
    }
    atomic_store_explicit(&r->head, head, memory_order_release);
    return n;
}

static SpRing sp_in;   // client -> server
static SpRing sp_recv; // server -> client

// guest table (LAN co-op): up to SPNET_MAX_GUESTS JS socket ids "g0".."g6", each backed by a real spnet
// connection; fields are server-thread-only except sp_hosting_enabled and sp_guest_count
#define SP_GUEST_ID_LEN 4 // "g0".."g6" + NUL

typedef struct {
    int active;               // slot in use
    int conn;                 // spnet connection handle, -1 when inactive
    char id[SP_GUEST_ID_LEN]; // "g0".."g6", for logging
    JSValue id_val;           // cached JS string; valid once sp_ctx exists
    SpRing tx;                // server -> guest bytes, drained via spnet_send
    int pending_disconnect;   // set by a recv error or tx overflow, torn down next sp_guests_pump iteration
} SpGuest;

static SpGuest sp_guests[SPNET_MAX_GUESTS];
static _Atomic int sp_hosting_enabled = 0; // main thread writes, server thread reads
static _Atomic int sp_guest_count = 0;     // server thread writes, main thread reads
static int sp_guest_hosting_prev = 0;      // server-thread-only: on/off edge detect

// shared atomics
static _Atomic int sp_ready = 0;       // server booted + world loaded
static _Atomic int sp_failed = 0;      // boot failed
static _Atomic int sp_thread_quit = 0; // ask the server thread to exit
static _Atomic int sp_cmd_connect = 0;
static _Atomic int sp_cmd_disconnect = 0;
static _Atomic int sp_client_connected = 0; // host's own "sp" loopback is live
static int sp_thread_started = 0; // main-thread only
#ifdef __vita__
static SceUID sp_tid = -1;
#else
static pthread_t sp_pth;
#endif

static void (*sp_progress_cb)(void *) = NULL; // main-thread loading render
static void *sp_progress_ctx = NULL;
// loading-screen frame pace: 16ms=60, 33ms=30
static int sp_loading_sleep_us = 16000;

void singleplayer_set_loading_sleep_us(int us) { sp_loading_sleep_us = us; }

// QuickJS state, server thread only
static JSRuntime *sp_rt = NULL;
static JSContext *sp_ctx = NULL;
static JSValue sp_obj, sp_fn_pump, sp_fn_start, sp_fn_connect, sp_fn_send,
    sp_fn_disconnect, sp_socket_id;

static uint64_t sp_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// boot-progress display state; server thread writes, main thread reads via singleplayer_boot_progress()
static volatile int sp_boot_pct = 0;
static char sp_boot_text[48] = {0};
static uint64_t sp_boot_t0 = 0;          // server-thread-only: boot start time
static int sp_boot_last_logged_pct = -1; // server-thread-only: log de-dupe

// report boot progress (server thread only); ignores backwards updates; `text` may be NULL (pct-only update)
static void sp_set_boot_progress(int pct, const char *text) {
    if (pct < sp_boot_pct) return;
    if (pct > 100) pct = 100;

    if (text) snprintf(sp_boot_text, sizeof(sp_boot_text), "%s", text);
    sp_boot_pct = pct;

    if (pct != sp_boot_last_logged_pct) {
        sp_boot_last_logged_pct = pct;
    }
}

// dedicated PRNG for crypto.getRandomValues (server thread only)
static uint32_t sp_rng = 0x9e3779b9;
static uint8_t sp_rand_byte(void) {
    sp_rng ^= sp_rng << 13;
    sp_rng ^= sp_rng >> 17;
    sp_rng ^= sp_rng << 5;
    return (uint8_t)(sp_rng & 0xff);
}

static char *sp_read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[sp] cannot open %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)n;
    return buf;
}

static void sp_dump_exception(JSContext *ctx) {
    JSValue exc = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, exc);
    fprintf(stderr, "[sp][exception] %s\n", s ? s : "(null)");
    if (s) JS_FreeCString(ctx, s);
    JSValue stk = JS_GetPropertyStr(ctx, exc, "stack");
    if (!JS_IsUndefined(stk)) {
        const char *st = JS_ToCString(ctx, stk);
        if (st) {
            fprintf(stderr, "%s\n", st);
            JS_FreeCString(ctx, st);
        }
    }
    JS_FreeValue(ctx, stk);
    JS_FreeValue(ctx, exc);
}

static int sp_eval(JSContext *ctx, const char *buf, size_t len, const char *name) {
    JSValue r = JS_Eval(ctx, buf, len, name, JS_EVAL_TYPE_GLOBAL);
    int rc = 0;
    if (JS_IsException(r)) {
        sp_dump_exception(ctx);
        rc = -1;
    }
    JS_FreeValue(ctx, r);
    return rc;
}

// evaluate the bundle via a compiled-bytecode cache; rejected if bundle length changed or bytecode incompatible
// try one bytecode-cache file; returns 1 and stores the function in *fn_out on success, else 0
static int sp_try_bc_file(JSContext *ctx, const char *path, size_t src_len,
                          JSValue *fn_out) {
    size_t bclen = 0;
    char *bc = sp_read_file(path, &bclen);
    int ok = 0;
    if (bc) {
        if (bclen > 8) {
            uint64_t cached_src_len = 0;
            memcpy(&cached_src_len, bc, 8);
            if (cached_src_len == (uint64_t)src_len) {
                JSValue fn = JS_ReadObject(ctx, (const uint8_t *)bc + 8,
                                           bclen - 8, JS_READ_OBJ_BYTECODE);
                if (JS_IsException(fn)) {
                    JS_FreeValue(ctx, fn);
                } else {
                    *fn_out = fn;
                    ok = 1;
                }
            }
        }
        free(bc);
    }
    return ok;
}

static int sp_eval_bundle(JSContext *ctx, const char *buf, size_t len,
                          const char *name) {
    JSValue fn = JS_UNDEFINED;
    int from_cache = 0;

    // bytecode shipped with the app, precompiled on the PC
    sp_set_boot_progress(4, "Loading compiled server");
    if (sp_try_bc_file(ctx, SP_BUNDLE_BC_SHIPPED_PATH, len, &fn)) {
        from_cache = 1;
    }

    // bytecode this device compiled + cached on an earlier boot
    if (!from_cache && sp_try_bc_file(ctx, SP_BUNDLE_BC_PATH, len, &fn)) {
        from_cache = 1;
    }

    if (!from_cache) {
        sp_set_boot_progress(5, "Compiling server - one time");
        fn = JS_Eval(ctx, buf, len, name,
                     JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(fn)) {
            sp_dump_exception(ctx);
            JS_FreeValue(ctx, fn);
            return -1;
        }

        size_t out_len = 0;
        // strip the embedded source text + debug line info
        uint8_t *out = JS_WriteObject(
            ctx, &out_len, fn,
            JS_WRITE_OBJ_BYTECODE | JS_WRITE_OBJ_STRIP_SOURCE |
                JS_WRITE_OBJ_STRIP_DEBUG);
        if (out) {
            FILE *f = fopen(SP_BUNDLE_BC_PATH, "wb");
            if (f) {
                uint64_t src_len = (uint64_t)len;
                fwrite(&src_len, 1, 8, f);
                fwrite(out, 1, out_len, f);
                fclose(f);
            }
            js_free(ctx, out);
        }
    } else {
        sp_set_boot_progress(8, "Loading compiled server");
    }

    sp_set_boot_progress(12, "Starting server");
    JSValue r = JS_EvalFunction(ctx, fn); // consumes fn
    int rc = 0;
    if (JS_IsException(r)) {
        sp_dump_exception(ctx);
        rc = -1;
    }
    JS_FreeValue(ctx, r);
    return rc;
}

// host primitives (globalThis.__host.*), all called on the server thread

static JSValue host_print(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    if (argc >= 1) {
        const char *s = JS_ToCString(ctx, argv[0]);
        if (s) {
            fputs(s, stderr);
            fputc('\n', stderr);
            fflush(stderr);
            JS_FreeCString(ctx, s);
        }
    }
    return JS_UNDEFINED;
}

static JSValue host_now(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
    return JS_NewFloat64(ctx, (double)sp_mono_ms());
}

// __host.progress(pct, text): JS boot milestones feed the loading display
static JSValue host_progress(JSContext *ctx, JSValueConst t, int argc,
                             JSValueConst *argv) {
    if (argc >= 2) {
        int32_t pct = 0;
        JS_ToInt32(ctx, &pct, argv[0]);
        const char *s = JS_ToCString(ctx, argv[1]);
        if (s) {
            sp_set_boot_progress((int)pct, s);
            JS_FreeCString(ctx, s);
        }
    }
    return JS_UNDEFINED;
}

static JSValue host_fill_random(JSContext *ctx, JSValueConst t, int argc,
                                JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    size_t size = 0;
    uint8_t *p = JS_GetUint8Array(ctx, &size, argv[0]);
    if (p) {
        for (size_t i = 0; i < size; i++) p[i] = sp_rand_byte();
    }
    return JS_UNDEFINED;
}

// find the active guest slot for JS id `id` ("g0".."g6"), or -1
static int sp_guest_find(const char *id) {
    if (!id) return -1;
    for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
        if (sp_guests[i].active && strcmp(sp_guests[i].id, id) == 0) return i;
    }
    return -1;
}

// server -> client bytes: producer side of sp_recv (id "sp") or a guest tx ring (id "gN"); called from JS
static JSValue host_to_client(JSContext *ctx, JSValueConst t, int argc,
                              JSValueConst *argv) {
    if (argc < 2) return JS_UNDEFINED;
    size_t size = 0;
    uint8_t *p = JS_GetUint8Array(ctx, &size, argv[1]);
    if (!p || size == 0) return JS_UNDEFINED;

    const char *id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_UNDEFINED;

    if (strcmp(id, "sp") == 0) {
        int off = 0;
        while (off < (int)size) {
            int w = sp_ring_write(&sp_recv, p + off, (int)size - off);
            if (w == 0) { // ring full: client not draining, drop and log once
                fprintf(stderr, "[sp] server->client ring full\n");
                break;
            }
            off += w;
        }
    } else {
        int gi = sp_guest_find(id);
        if (gi >= 0) {
            SpGuest *g = &sp_guests[gi];
            int w = sp_ring_write(&g->tx, p, (int)size);
            if (w < (int)size) {
                // overflow: drop this guest, torn down next sp_guests_pump()
                fprintf(stderr, "[sp] guest %s tx ring full, disconnecting\n",
                        g->id);
                g->pending_disconnect = 1;
            }
        } // stale id: drop silently
    }

    JS_FreeCString(ctx, id);
    return JS_UNDEFINED;
}

// file-backed storage (server thread)

// active world's save-folder id; "default" = saves/ root, else saves/<id>/
static char sp_active_world[64] = "default";
static int sp_active_xp = 1;       // experienceRate for the active world
static int sp_active_members = 1;  // 1 = members world, 0 = free (f2p)
static int sp_active_fatigue = 1;  // 1 = fatigue on (classic), 0 = no fatigue
static int sp_active_remember = 0; // 1 = remember last combat style
static int sp_active_speed = 1;    // tick-speed multiplier (1 = authentic)
static int sp_active_quests = 1;   // 1 = OpenRSC custom quests enabled
static int sp_active_holiday = 1;  // 1 = OpenRSC holiday events enabled

// per-world feature toggles; real values set by singleplayer_set_world_rules() at world-select
static int sp_active_tutorial_island = 0;      // tutorialIsland
static int sp_active_skillcape_perks = 1;      // wantSkillcapePerks
static int sp_active_combat_odyssey = 1;       // wantCombatOdyssey
static int sp_active_poison_npcs = 0;          // wantPoisonNpcs
static int sp_active_leftclick_webs = 0;       // wantLeftclickWebs
static int sp_active_guild_greetings = 1;      // wantMissingGuildGreetings
static int sp_active_faster_yohnus = 0;        // fasterYohnus
static int sp_active_uses_classes = 1;         // usesClasses
static int sp_active_spawn_ironman = 1;        // spawnIronMan

// second feature-toggle wave, all default on
static int sp_active_custom_firemaking = 1;         // customFiremaking
static int sp_active_better_jewelry_crafting = 1;   // wantBetterJewelryCrafting
static int sp_active_custom_leather = 1;            // wantCustomLeather
static int sp_active_new_rare_drop_tables = 1;      // wantNewRareDropTables
static int sp_active_npc_kill_messages = 1;         // npcKillMessages
static int sp_active_enchanted_crowns = 1;          // wantEnchantedCrowns
static int sp_active_batch_progression = 1;         // wantBatchProgression

void singleplayer_set_world(const char *id) {
    char new_id[64];
    if (id == NULL || id[0] == '\0') {
        snprintf(new_id, sizeof(new_id), "default");
    } else {
        snprintf(new_id, sizeof(new_id), "%s", id);
    }

    // switching worlds after boot: tear down; the next login boots fresh
    if (sp_thread_started && strcmp(new_id, sp_active_world) != 0) {
        singleplayer_stop();
    }

    snprintf(sp_active_world, sizeof(sp_active_world), "%s", new_id);
}

// set the active world's game rules; call before singleplayer_start()
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
                                  int want_batch_progression) {
    sp_active_xp = xp_rate > 0 ? xp_rate : 1;
    sp_active_members = members ? 1 : 0;
    sp_active_fatigue = fatigue ? 1 : 0;
    sp_active_remember = remember_style ? 1 : 0;
    sp_active_speed = game_speed > 0 ? game_speed : 1;
    sp_active_quests = custom_quests ? 1 : 0;
    sp_active_holiday = holiday_events ? 1 : 0;
    sp_active_tutorial_island = tutorial_island ? 1 : 0;
    sp_active_skillcape_perks = want_skillcape_perks ? 1 : 0;
    sp_active_combat_odyssey = want_combat_odyssey ? 1 : 0;
    sp_active_poison_npcs = want_poison_npcs ? 1 : 0;
    sp_active_leftclick_webs = want_leftclick_webs ? 1 : 0;
    sp_active_guild_greetings = want_missing_guild_greetings ? 1 : 0;
    sp_active_faster_yohnus = faster_yohnus ? 1 : 0;
    sp_active_uses_classes = uses_classes ? 1 : 0;
    sp_active_spawn_ironman = spawn_ironman ? 1 : 0;
    sp_active_custom_firemaking = custom_firemaking ? 1 : 0;
    sp_active_better_jewelry_crafting = want_better_jewelry_crafting ? 1 : 0;
    sp_active_custom_leather = want_custom_leather ? 1 : 0;
    sp_active_new_rare_drop_tables = want_new_rare_drop_tables ? 1 : 0;
    sp_active_npc_kill_messages = npc_kill_messages ? 1 : 0;
    sp_active_enchanted_crowns = want_enchanted_crowns ? 1 : 0;
    sp_active_batch_progression = want_batch_progression ? 1 : 0;
}

// presentation-only reads for the appearance/creation UI
int singleplayer_world_uses_classes(void) { return sp_active_uses_classes; }
int singleplayer_world_spawns_ironman(void) { return sp_active_spawn_ironman; }

static int sp_world_is_root(void) {
    return sp_active_world[0] == '\0' || strcmp(sp_active_world, "default") == 0;
}

static void sp_key_path(const char *key, char *out, size_t outsz) {
    char safe[64];
    size_t j = 0;
    for (size_t i = 0; key[i] && j < sizeof(safe) - 1; i++) {
        char c = key[i];
        int ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        safe[j++] = (char)(ok ? c : '_');
    }
    safe[j] = '\0';

    if (sp_world_is_root()) {
        snprintf(out, outsz, "%s/%s.json", SP_SAVE_DIR, safe);
    } else {
        snprintf(out, outsz, "%s/%s/%s.json", SP_SAVE_DIR, sp_active_world, safe);
    }
}

static void sp_ensure_save_dir(void) {
#ifdef __vita__
    mkdir("ux0:data", 0777);
    mkdir("ux0:data/RuneScape", 0777);
#endif
    mkdir(SP_SAVE_DIR, 0777);

    if (!sp_world_is_root()) {
        char dir[256];
        snprintf(dir, sizeof(dir), "%s/%s", SP_SAVE_DIR, sp_active_world);
        mkdir(dir, 0777);
    }
}

static JSValue host_storage_get(JSContext *ctx, JSValueConst t, int argc,
                                JSValueConst *argv) {
    if (argc < 1) return JS_NULL;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) return JS_NULL;
    char path[512];
    sp_key_path(key, path, sizeof(path));
    JS_FreeCString(ctx, key);
    size_t len = 0;
    char *data = sp_read_file(path, &len);
    if (!data) return JS_NULL;
    JSValue r = JS_NewStringLen(ctx, data, len);
    free(data);
    return r;
}

static JSValue host_storage_set(JSContext *ctx, JSValueConst t, int argc,
                                JSValueConst *argv) {
    if (argc < 2) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    const char *val = JS_ToCString(ctx, argv[1]);
    if (key && val) {
        sp_ensure_save_dir();
        char path[512];
        sp_key_path(key, path, sizeof(path));
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(val, 1, strlen(val), f);
            fclose(f);
        } else {
            fprintf(stderr, "[sp] cannot write save %s\n", path);
        }
    }
    if (key) JS_FreeCString(ctx, key);
    if (val) JS_FreeCString(ctx, val);
    return JS_UNDEFINED;
}

static JSValue host_storage_del(JSContext *ctx, JSValueConst t, int argc,
                                JSValueConst *argv) {
    if (argc < 1) return JS_UNDEFINED;
    const char *key = JS_ToCString(ctx, argv[0]);
    if (key) {
        char path[512];
        sp_key_path(key, path, sizeof(path));
        remove(path);
        JS_FreeCString(ctx, key);
    }
    return JS_UNDEFINED;
}

// SP account management (world-editor "Players" tab); roster in players.json = JSON text of a Map's entries
// [[username, playerObject], ...]; list/delete = parse/rewrite that array via a throwaway QuickJS runtime

// build the on-disk path of a storage key for an arbitrary world id
static void sp_world_file_path(const char *world_id, const char *key,
                               char *out, size_t outsz) {
    int is_root = !world_id || world_id[0] == '\0' ||
                  strcmp(world_id, "default") == 0;
    if (is_root) {
        snprintf(out, outsz, "%s/%s.json", SP_SAVE_DIR, key);
    } else {
        snprintf(out, outsz, "%s/%s/%s.json", SP_SAVE_DIR, world_id, key);
    }
}

// read a file, no stderr noise on a missing file
static char *sp_read_file_quiet(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)n;
    return buf;
}

// parse a players.json, unwrapping the double-JSON encoding (file on disk is a JSON string of the array);
// returns the array or JS_EXCEPTION
static JSValue sp_players_parse(JSContext *ctx, const char *data, size_t len,
                                const char *path) {
    JSValue v = JS_ParseJSON(ctx, data, len, path);
    if (!JS_IsException(v) && JS_IsString(v)) {
        const char *inner = JS_ToCString(ctx, v);
        if (inner) {
            JSValue a = JS_ParseJSON(ctx, inner, strlen(inner), path);
            JS_FreeCString(ctx, inner);
            JS_FreeValue(ctx, v);
            v = a;
        }
    }
    return v;
}

// list up to `max` account usernames for world `world_id` into `names`, each NUL-terminated, <=31 chars;
// returns count (0 = none/no file), or -1 on parse error
int singleplayer_players_list(const char *world_id, char (*names)[32], int max) {
    if (max <= 0) return 0;
    char path[512];
    sp_world_file_path(world_id, "players", path, sizeof(path));
    size_t len = 0;
    char *data = sp_read_file_quiet(path, &len);
    if (!data) return 0;

    JSRuntime *rt = JS_NewRuntime();
    if (!rt) { free(data); return -1; }
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); free(data); return -1; }

    int count = 0;
    JSValue arr = sp_players_parse(ctx, data, len, path);
    free(data);

    if (!JS_IsException(arr) && JS_IsArray(arr)) {
        JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
        uint32_t n = 0;
        JS_ToUint32(ctx, &n, lenv);
        JS_FreeValue(ctx, lenv);
        for (uint32_t i = 0; i < n && count < max; i++) {
            JSValue entry = JS_GetPropertyUint32(ctx, arr, i); // [name, obj]
            JSValue uname = JS_GetPropertyUint32(ctx, entry, 0);
            const char *s = JS_ToCString(ctx, uname);
            if (s) {
                snprintf(names[count], 32, "%.31s", s);
                JS_FreeCString(ctx, s);
                count++;
            }
            JS_FreeValue(ctx, uname);
            JS_FreeValue(ctx, entry);
        }
    } else {
        count = -1;
    }
    JS_FreeValue(ctx, arr);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return count;
}

// delete account `username` from world `world_id`'s players.json; returns 0 on success (incl not-found),
// -1 on read/parse/write error
int singleplayer_players_delete(const char *world_id, const char *username) {
    if (!username || !username[0]) return -1;
    char path[512];
    sp_world_file_path(world_id, "players", path, sizeof(path));
    size_t len = 0;
    char *data = sp_read_file_quiet(path, &len);
    if (!data) return -1;

    JSRuntime *rt = JS_NewRuntime();
    if (!rt) { free(data); return -1; }
    JSContext *ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); free(data); return -1; }

    int rc = -1;
    JSValue arr = sp_players_parse(ctx, data, len, path);
    free(data);

    if (!JS_IsException(arr) && JS_IsArray(arr)) {
        JSValue out = JS_NewArray(ctx);
        JSValue lenv = JS_GetPropertyStr(ctx, arr, "length");
        uint32_t n = 0;
        JS_ToUint32(ctx, &n, lenv);
        JS_FreeValue(ctx, lenv);
        uint32_t oi = 0;
        for (uint32_t i = 0; i < n; i++) {
            JSValue entry = JS_GetPropertyUint32(ctx, arr, i);
            JSValue uname = JS_GetPropertyUint32(ctx, entry, 0);
            const char *s = JS_ToCString(ctx, uname);
            int drop = (s && strcasecmp(s, username) == 0);
            if (s) JS_FreeCString(ctx, s);
            JS_FreeValue(ctx, uname);
            if (drop) {
                JS_FreeValue(ctx, entry); // drop this entry
            } else {
                JS_SetPropertyUint32(ctx, out, oi++, entry); // moves ref
            }
        }
        // write back in double-encoded form: stringify the array, then stringify that string again
        JSValue inner = JS_JSONStringify(ctx, out, JS_UNDEFINED, JS_UNDEFINED);
        JSValue json = JS_JSONStringify(ctx, inner, JS_UNDEFINED, JS_UNDEFINED);
        JS_FreeValue(ctx, inner);
        const char *js = JS_ToCString(ctx, json);
        if (js) {
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(js, 1, strlen(js), f);
                fclose(f);
                rc = 0;
            }
            JS_FreeCString(ctx, js);
        }
        JS_FreeValue(ctx, json);
        JS_FreeValue(ctx, out);
    }
    JS_FreeValue(ctx, arr);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return rc;
}

// delete every save file for a world: unlink players + playerID, rmdir the per-world folder; never the shared root
void singleplayer_world_wipe(const char *world_id) {
    char path[512];
    sp_world_file_path(world_id, "players", path, sizeof(path));
    remove(path);
    sp_world_file_path(world_id, "playerID", path, sizeof(path));
    remove(path);
    int is_root = !world_id || world_id[0] == '\0' ||
                  strcmp(world_id, "default") == 0;
    if (!is_root) {
        char dir[512];
        snprintf(dir, sizeof(dir), "%s/%s", SP_SAVE_DIR, world_id);
        rmdir(dir);
    }
}

// precomputed pathfinder obstacle map, or null if absent
static JSValue host_pathfinder_cache(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv) {
    size_t len = 0;
    char *data = sp_read_file(SP_PATHFINDER_CACHE_PATH, &len);
    if (!data) return JS_NULL;
    JSValue r = JS_NewUint8ArrayCopy(ctx, (const uint8_t *)data, len);
    free(data);
    return r;
}

// precomputed parsed landscape (sectors + bounds), or null if absent
static JSValue host_landscape_cache(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv) {
    size_t len = 0;
    char *data = sp_read_file(SP_LANDSCAPE_CACHE_PATH, &len);
    if (!data) return JS_NULL;
    JSValue r = JS_NewUint8ArrayCopy(ctx, (const uint8_t *)data, len);
    free(data);
    return r;
}

static void sp_drain_jobs(void) {
    JSContext *jc;
    for (;;) {
        int n = JS_ExecutePendingJob(sp_rt, &jc);
        if (n == 0) break;
        if (n < 0) {
            sp_dump_exception(jc);
            break;
        }
    }
}

static void sp_pump(void) {
    JSValue now = JS_NewFloat64(sp_ctx, (double)sp_mono_ms());
    JSValue r = JS_Call(sp_ctx, sp_fn_pump, sp_obj, 1, &now);
    JS_FreeValue(sp_ctx, now);
    if (JS_IsException(r)) sp_dump_exception(sp_ctx);
    JS_FreeValue(sp_ctx, r);
    sp_drain_jobs();
}

// boot the engine + load the world (server thread). returns 0 on success
static int sp_boot(void) {
    // fresh boot-progress state
    sp_boot_pct = 0;
    sp_boot_text[0] = '\0';
    sp_boot_t0 = sp_mono_ms();
    sp_boot_last_logged_pct = -1;

    sp_rt = JS_NewRuntime();
    if (!sp_rt) return -1;
    JS_SetGCThreshold(sp_rt, 128 * 1024 * 1024);
    JS_SetMaxStackSize(sp_rt, 1024 * 1024);
    sp_ctx = JS_NewContext(sp_rt);
    if (!sp_ctx) return -1;

    JSValue global = JS_GetGlobalObject(sp_ctx);
    JSValue host = JS_NewObject(sp_ctx);
    JS_SetPropertyStr(sp_ctx, host, "print",
                      JS_NewCFunction(sp_ctx, host_print, "print", 1));
    JS_SetPropertyStr(sp_ctx, host, "now",
                      JS_NewCFunction(sp_ctx, host_now, "now", 0));
    JS_SetPropertyStr(sp_ctx, host, "progress",
                      JS_NewCFunction(sp_ctx, host_progress, "progress", 2));
    JS_SetPropertyStr(sp_ctx, host, "fillRandom",
                      JS_NewCFunction(sp_ctx, host_fill_random, "fillRandom", 1));
    JS_SetPropertyStr(sp_ctx, host, "toClient",
                      JS_NewCFunction(sp_ctx, host_to_client, "toClient", 2));
    JS_SetPropertyStr(sp_ctx, host, "storageGet",
                      JS_NewCFunction(sp_ctx, host_storage_get, "storageGet", 1));
    JS_SetPropertyStr(sp_ctx, host, "storageSet",
                      JS_NewCFunction(sp_ctx, host_storage_set, "storageSet", 2));
    JS_SetPropertyStr(sp_ctx, host, "storageDel",
                      JS_NewCFunction(sp_ctx, host_storage_del, "storageDel", 1));
    JS_SetPropertyStr(
        sp_ctx, host, "pathfinderCache",
        JS_NewCFunction(sp_ctx, host_pathfinder_cache, "pathfinderCache", 0));
    JS_SetPropertyStr(
        sp_ctx, host, "landscapeCache",
        JS_NewCFunction(sp_ctx, host_landscape_cache, "landscapeCache", 0));
    JS_SetPropertyStr(sp_ctx, global, "__host", host);
    JS_FreeValue(sp_ctx, global);

    size_t blen = 0;
    char *boot = sp_read_file(SP_BOOTSTRAP_PATH, &blen);
    if (!boot) return -1;
    if (sp_eval(sp_ctx, boot, blen, "sp-bootstrap.js")) {
        free(boot);
        return -1;
    }
    free(boot);

    sp_set_boot_progress(2, "Reading server");
    size_t ulen = 0;
    char *bundle = sp_read_file(SP_BUNDLE_PATH, &ulen);
    if (!bundle) return -1;
    unsigned long sp_eval_t0 = (unsigned long)sp_mono_ms();
    if (sp_eval_bundle(sp_ctx, bundle, ulen, "browser.bundle.js")) {
        free(bundle);
        return -1;
    }
    free(bundle);

    global = JS_GetGlobalObject(sp_ctx);
    sp_obj = JS_GetPropertyStr(sp_ctx, global, "__sp");
    JS_FreeValue(sp_ctx, global);
    if (JS_IsUndefined(sp_obj)) {
        fprintf(stderr, "[sp] __sp missing\n");
        return -1;
    }
    sp_fn_pump = JS_GetPropertyStr(sp_ctx, sp_obj, "pump");
    sp_fn_start = JS_GetPropertyStr(sp_ctx, sp_obj, "start");
    sp_fn_connect = JS_GetPropertyStr(sp_ctx, sp_obj, "connect");
    sp_fn_send = JS_GetPropertyStr(sp_ctx, sp_obj, "send");
    sp_fn_disconnect = JS_GetPropertyStr(sp_ctx, sp_obj, "disconnect");
    sp_socket_id = JS_NewString(sp_ctx, "sp");

    // fresh guest-table state
    for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
        snprintf(sp_guests[i].id, sizeof(sp_guests[i].id), "g%d", i);
        sp_guests[i].id_val = JS_NewString(sp_ctx, sp_guests[i].id);
        sp_guests[i].active = 0;
        sp_guests[i].conn = -1;
        sp_guests[i].pending_disconnect = 0;
        atomic_store(&sp_guests[i].tx.head, 0);
        atomic_store(&sp_guests[i].tx.tail, 0);
    }
    atomic_store(&sp_guest_count, 0);
    sp_guest_hosting_prev = 0;

    // build the start config in C. config.experienceRate multiplies XP gain; config.members gates P2P content
    JSValue cfg = JS_NewObject(sp_ctx);
    JS_SetPropertyStr(sp_ctx, cfg, "worldID", JS_NewInt32(sp_ctx, 1));
    JS_SetPropertyStr(sp_ctx, cfg, "version", JS_NewInt32(sp_ctx, 204));
    JS_SetPropertyStr(sp_ctx, cfg, "members",
                      JS_NewBool(sp_ctx, sp_active_members));
    JS_SetPropertyStr(sp_ctx, cfg, "experienceRate",
                      JS_NewInt32(sp_ctx, sp_active_xp));
    JS_SetPropertyStr(sp_ctx, cfg, "fatigue",
                      JS_NewBool(sp_ctx, sp_active_fatigue));
    JS_SetPropertyStr(sp_ctx, cfg, "rememberCombatStyle",
                      JS_NewBool(sp_ctx, sp_active_remember));
    JS_SetPropertyStr(sp_ctx, cfg, "gameSpeed",
                      JS_NewInt32(sp_ctx, sp_active_speed));
    JS_SetPropertyStr(sp_ctx, cfg, "customQuests",
                      JS_NewBool(sp_ctx, sp_active_quests));
    JS_SetPropertyStr(sp_ctx, cfg, "holidayEvents",
                      JS_NewBool(sp_ctx, sp_active_holiday));

    // per-world feature toggles. key names are the exact server config.json keys
    JS_SetPropertyStr(sp_ctx, cfg, "tutorialIsland",
                      JS_NewBool(sp_ctx, sp_active_tutorial_island));
    JS_SetPropertyStr(sp_ctx, cfg, "wantSkillcapePerks",
                      JS_NewBool(sp_ctx, sp_active_skillcape_perks));
    JS_SetPropertyStr(sp_ctx, cfg, "wantCombatOdyssey",
                      JS_NewBool(sp_ctx, sp_active_combat_odyssey));
    JS_SetPropertyStr(sp_ctx, cfg, "wantPoisonNpcs",
                      JS_NewBool(sp_ctx, sp_active_poison_npcs));
    JS_SetPropertyStr(sp_ctx, cfg, "wantLeftclickWebs",
                      JS_NewBool(sp_ctx, sp_active_leftclick_webs));
    JS_SetPropertyStr(sp_ctx, cfg, "wantMissingGuildGreetings",
                      JS_NewBool(sp_ctx, sp_active_guild_greetings));
    JS_SetPropertyStr(sp_ctx, cfg, "fasterYohnus",
                      JS_NewBool(sp_ctx, sp_active_faster_yohnus));
    JS_SetPropertyStr(sp_ctx, cfg, "usesClasses",
                      JS_NewBool(sp_ctx, sp_active_uses_classes));
    JS_SetPropertyStr(sp_ctx, cfg, "spawnIronMan",
                      JS_NewBool(sp_ctx, sp_active_spawn_ironman));

    // second feature-toggle wave
    JS_SetPropertyStr(sp_ctx, cfg, "customFiremaking",
                      JS_NewBool(sp_ctx, sp_active_custom_firemaking));
    JS_SetPropertyStr(sp_ctx, cfg, "wantBetterJewelryCrafting",
                      JS_NewBool(sp_ctx, sp_active_better_jewelry_crafting));
    JS_SetPropertyStr(sp_ctx, cfg, "wantCustomLeather",
                      JS_NewBool(sp_ctx, sp_active_custom_leather));
    JS_SetPropertyStr(sp_ctx, cfg, "wantNewRareDropTables",
                      JS_NewBool(sp_ctx, sp_active_new_rare_drop_tables));
    JS_SetPropertyStr(sp_ctx, cfg, "npcKillMessages",
                      JS_NewBool(sp_ctx, sp_active_npc_kill_messages));
    JS_SetPropertyStr(sp_ctx, cfg, "wantEnchantedCrowns",
                      JS_NewBool(sp_ctx, sp_active_enchanted_crowns));
    JS_SetPropertyStr(sp_ctx, cfg, "wantBatchProgression",
                      JS_NewBool(sp_ctx, sp_active_batch_progression));

    fprintf(stderr,
            "[sp] posting start (xp x%d, %s, fatigue %s, remember %s, "
            "speed x%d, quests %s, holiday %s, tutorial %s, skillcape %s, "
            "odyssey %s, poison %s, webs %s, greetings %s, yohnus %s, "
            "classes %s, ironman %s, firemaking %s, jewelry %s, leather %s, "
            "raredrops %s, killmsgs %s, crowns %s, batch %s); loading "
            "world...\n",
            sp_active_xp, sp_active_members ? "members" : "f2p",
            sp_active_fatigue ? "on" : "off", sp_active_remember ? "yes" : "no",
            sp_active_speed, sp_active_quests ? "on" : "off",
            sp_active_holiday ? "on" : "off",
            sp_active_tutorial_island ? "on" : "off",
            sp_active_skillcape_perks ? "on" : "off",
            sp_active_combat_odyssey ? "on" : "off",
            sp_active_poison_npcs ? "on" : "off",
            sp_active_leftclick_webs ? "on" : "off",
            sp_active_guild_greetings ? "on" : "off",
            sp_active_faster_yohnus ? "on" : "off",
            sp_active_uses_classes ? "on" : "off",
            sp_active_spawn_ironman ? "on" : "off",
            sp_active_custom_firemaking ? "on" : "off",
            sp_active_better_jewelry_crafting ? "on" : "off",
            sp_active_custom_leather ? "on" : "off",
            sp_active_new_rare_drop_tables ? "on" : "off",
            sp_active_npc_kill_messages ? "on" : "off",
            sp_active_enchanted_crowns ? "on" : "off",
            sp_active_batch_progression ? "on" : "off");
    JSValue r = JS_Call(sp_ctx, sp_fn_start, sp_obj, 1, &cfg);
    JS_FreeValue(sp_ctx, cfg);
    if (JS_IsException(r)) sp_dump_exception(sp_ctx);
    JS_FreeValue(sp_ctx, r);

    uint64_t t0 = sp_mono_ms();
    uint64_t last_log = t0;
    while (sp_mono_ms() - t0 < SP_BOOT_TIMEOUT_MS) {
        sp_pump();

        // mirror entry.js's milestone hooks into the C-side shared state for per-phase loading progress
        JSValue pv = JS_GetPropertyStr(sp_ctx, sp_obj, "progressPct");
        if (!JS_IsUndefined(pv)) {
            int32_t jpct = 0;
            JS_ToInt32(sp_ctx, &jpct, pv);
            JSValue tv = JS_GetPropertyStr(sp_ctx, sp_obj, "progressText");
            const char *jtext = JS_ToCString(sp_ctx, tv);
            sp_set_boot_progress(jpct, jtext);
            if (jtext) JS_FreeCString(sp_ctx, jtext);
            JS_FreeValue(sp_ctx, tv);
        }
        JS_FreeValue(sp_ctx, pv);

        JSValue rv = JS_GetPropertyStr(sp_ctx, sp_obj, "ready");
        int ready = JS_ToBool(sp_ctx, rv);
        JS_FreeValue(sp_ctx, rv);
        if (ready) {
            return 0;
        }
        // periodic heartbeat
        if (sp_mono_ms() - last_log >= 15000) {
            last_log = sp_mono_ms();
        }
    }
    fprintf(stderr, "[sp] boot TIMEOUT after %lu ms\n",
            (unsigned long)(sp_mono_ms() - t0));
    return -1;
}

// the server thread: boot, then pump forever
static void sp_free_engine(void) {
    if (!sp_ctx) return;
    JS_FreeValue(sp_ctx, sp_fn_pump);
    JS_FreeValue(sp_ctx, sp_fn_start);
    JS_FreeValue(sp_ctx, sp_fn_connect);
    JS_FreeValue(sp_ctx, sp_fn_send);
    JS_FreeValue(sp_ctx, sp_fn_disconnect);
    JS_FreeValue(sp_ctx, sp_socket_id);
    for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
        JS_FreeValue(sp_ctx, sp_guests[i].id_val);
    }
    JS_FreeValue(sp_ctx, sp_obj);
    JS_FreeContext(sp_ctx);
    JS_FreeRuntime(sp_rt);
    sp_ctx = NULL;
    sp_rt = NULL;
}

// guest connection pump (server thread)

// tear down guest slot `i`: tell the JS server the socket is gone, close its transport, free the slot
static void sp_guest_disconnect(int i) {
    SpGuest *g = &sp_guests[i];
    if (!g->active) return;
    JSValue id = JS_DupValue(sp_ctx, g->id_val);
    JSValue r = JS_Call(sp_ctx, sp_fn_disconnect, sp_obj, 1, &id);
    JS_FreeValue(sp_ctx, id);
    JS_FreeValue(sp_ctx, r);
    sp_drain_jobs();
    spnet_close(g->conn);
    g->active = 0;
    g->conn = -1;
    g->pending_disconnect = 0;
    atomic_store(&g->tx.head, 0);
    atomic_store(&g->tx.tail, 0);
    atomic_fetch_sub(&sp_guest_count, 1);
}

// drain up to sizeof(buf) bytes from ring `r` to spnet_send, non-blocking; commits only the bytes the transport
// accepted. returns bytes sent, or -1 on transport error
static int sp_ring_drain_to_conn(SpRing *r, int conn) {
    uint8_t buf[8192];
    unsigned tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    unsigned head = atomic_load_explicit(&r->head, memory_order_relaxed);
    int avail = 0;
    unsigned h = head;
    while (h != tail && avail < (int)sizeof(buf)) {
        buf[avail++] = r->buf[h];
        h = (h + 1) & SP_RING_MASK;
    }
    if (avail == 0) return 0;
    int sent = spnet_send(conn, buf, avail);
    if (sent < 0) return -1;
    if (sent > 0) {
        unsigned newhead = (head + (unsigned)sent) & SP_RING_MASK;
        atomic_store_explicit(&r->head, newhead, memory_order_release);
    }
    return sent;
}

// accept new guests, shuttle bytes both ways over spnet transports, process disconnects; no-op when not hosting
static void sp_guests_pump(void) {
    int hosting = atomic_load(&sp_hosting_enabled);

    if (!hosting && sp_guest_hosting_prev) {
        // hosting was just turned off: drop every connected guest
        for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
            if (sp_guests[i].active) sp_guest_disconnect(i);
        }
    }
    sp_guest_hosting_prev = hosting;

    if (!hosting) return;

    // accept any pending guests
    for (;;) {
        int conn = spnet_host_accept();
        if (conn < 0) break;
        int slot = -1;
        for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
            if (!sp_guests[i].active) { slot = i; break; }
        }
        if (slot < 0) {
            fprintf(stderr, "[sp] guest table full, refusing connection\n");
            spnet_close(conn);
            continue;
        }
        SpGuest *g = &sp_guests[slot];
        g->active = 1;
        g->conn = conn;
        g->pending_disconnect = 0;
        atomic_store(&g->tx.head, 0);
        atomic_store(&g->tx.tail, 0);
        atomic_fetch_add(&sp_guest_count, 1);
        JSValue id = JS_DupValue(sp_ctx, g->id_val);
        JSValue r = JS_Call(sp_ctx, sp_fn_connect, sp_obj, 1, &id);
        JS_FreeValue(sp_ctx, id);
        JS_FreeValue(sp_ctx, r);
        sp_drain_jobs();
    }

    // client -> server: pull bytes off each guest's transport and feed to __sp.send(id, bytes)
    uint8_t inbuf[8192];
    for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
        SpGuest *g = &sp_guests[i];
        if (!g->active) continue;
        int n = spnet_recv(g->conn, inbuf, sizeof(inbuf));
        if (n > 0) {
            JSValue u8 = JS_NewUint8ArrayCopy(sp_ctx, inbuf, (size_t)n);
            JSValue args[2];
            args[0] = JS_DupValue(sp_ctx, g->id_val);
            args[1] = u8;
            JSValue r = JS_Call(sp_ctx, sp_fn_send, sp_obj, 2, args);
            JS_FreeValue(sp_ctx, args[0]);
            JS_FreeValue(sp_ctx, u8);
            JS_FreeValue(sp_ctx, r);
            sp_drain_jobs();
        } else if (n < 0) {
            g->pending_disconnect = 1; // peer gone
        }
    }

    // server -> client: drain each guest's tx ring over its transport, non-blocking
    for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
        SpGuest *g = &sp_guests[i];
        if (!g->active) continue;
        int sent = sp_ring_drain_to_conn(&g->tx, g->conn);
        if (sent < 0) g->pending_disconnect = 1;
    }

    // process disconnects flagged earlier (recv/send error or tx overflow)
    for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
        if (sp_guests[i].pending_disconnect) sp_guest_disconnect(i);
    }
}

static void sp_thread_run(void) {
    if (sp_boot() != 0) {
        fprintf(stderr, "[sp] boot failed\n");
        if (sp_ctx) {
            JS_FreeContext(sp_ctx);
            sp_ctx = NULL;
        }
        if (sp_rt) {
            JS_FreeRuntime(sp_rt);
            sp_rt = NULL;
        }
        atomic_store(&sp_failed, 1);
        return;
    }
    atomic_store(&sp_ready, 1);

    uint8_t inbuf[8192];
    while (!atomic_load(&sp_thread_quit)) {
        if (atomic_load(&sp_cmd_connect)) {
            // fresh socket: drop any stale loopback bytes
            atomic_store(&sp_in.head, 0);
            atomic_store(&sp_in.tail, 0);
            atomic_store(&sp_recv.head, 0);
            atomic_store(&sp_recv.tail, 0);
            JSValue id = JS_DupValue(sp_ctx, sp_socket_id);
            JSValue r = JS_Call(sp_ctx, sp_fn_connect, sp_obj, 1, &id);
            JS_FreeValue(sp_ctx, id);
            JS_FreeValue(sp_ctx, r);
            sp_drain_jobs();
            atomic_store(&sp_cmd_connect, 0);
        }

        int n = sp_ring_read(&sp_in, inbuf, sizeof(inbuf));
        if (n > 0) {
            JSValue u8 = JS_NewUint8ArrayCopy(sp_ctx, inbuf, (size_t)n);
            JSValue args[2];
            args[0] = JS_DupValue(sp_ctx, sp_socket_id);
            args[1] = u8;
            JSValue r = JS_Call(sp_ctx, sp_fn_send, sp_obj, 2, args);
            JS_FreeValue(sp_ctx, args[0]);
            JS_FreeValue(sp_ctx, u8);
            JS_FreeValue(sp_ctx, r);
            sp_drain_jobs();
        }

        if (atomic_load(&sp_cmd_disconnect)) {
            JSValue id = JS_DupValue(sp_ctx, sp_socket_id);
            JSValue r = JS_Call(sp_ctx, sp_fn_disconnect, sp_obj, 1, &id);
            JS_FreeValue(sp_ctx, id);
            JS_FreeValue(sp_ctx, r);
            sp_drain_jobs();
            atomic_store(&sp_cmd_disconnect, 0);
        }

        sp_guests_pump();

        sp_pump();
        usleep(3000); // ~3ms; 640ms world tick fires here when due
    }

    // singleplayer_stop() requested exit: drop connected guests, then free the world + runtime
    for (int i = 0; i < SPNET_MAX_GUESTS; i++) {
        if (sp_guests[i].active) sp_guest_disconnect(i);
    }
    sp_free_engine();
}

#ifdef __vita__
static int sp_thread_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    sp_thread_run();
    return 0;
}
#else
static void *sp_thread_entry(void *arg) {
    (void)arg;
    sp_thread_run();
    return NULL;
}
#endif

// public API (client / main thread)

void singleplayer_set_progress_cb(void (*cb)(void *ctx), void *ctx) {
    sp_progress_cb = cb;
    sp_progress_ctx = ctx;
}

int singleplayer_boot_progress(char *text_out, int n) {
    if (text_out && n > 0) {
        snprintf(text_out, (size_t)n, "%s", sp_boot_text);
    }
    return sp_boot_pct;
}

int singleplayer_is_ready(void) { return atomic_load(&sp_ready); }

int singleplayer_start(void) {
    if (atomic_load(&sp_ready)) return 0;

    if (!sp_thread_started) {
        sp_thread_started = 1;
#ifdef __vita__
        // keep the render/main thread on core 0 and run the server tick on core 2
        sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(),
                                             SCE_KERNEL_CPU_MASK_USER_0);
        sp_tid = sceKernelCreateThread("rsc_sp_server", sp_thread_entry,
                                       0x10000100, 2 * 1024 * 1024, 0,
                                       SCE_KERNEL_CPU_MASK_USER_2, NULL);
        if (sp_tid < 0) {
            fprintf(stderr, "[sp] sceKernelCreateThread failed: 0x%08X\n", sp_tid);
            atomic_store(&sp_failed, 1);
            return -1;
        }
        sceKernelStartThread(sp_tid, 0, NULL);
#else
        if (pthread_create(&sp_pth, NULL, sp_thread_entry, NULL) != 0) {
            fprintf(stderr, "[sp] pthread_create failed\n");
            atomic_store(&sp_failed, 1);
            return -1;
        }
#endif
    }

    // wait for the server thread to finish loading the world, animating the loading screen meanwhile
    uint64_t t0 = sp_mono_ms();
    while (!atomic_load(&sp_ready) && !atomic_load(&sp_failed)) {
        if (sp_progress_cb) sp_progress_cb(sp_progress_ctx);
        usleep(sp_loading_sleep_us); // 16ms=60fps / 33ms=30fps per settings
        if (sp_mono_ms() - t0 > SP_BOOT_TIMEOUT_MS) break;
    }
    return atomic_load(&sp_ready) ? 0 : -1;
}

// wait briefly for the server thread to acknowledge a command flag
static void sp_wait_flag(_Atomic int *flag) {
    uint64_t t0 = sp_mono_ms();
    while (atomic_load(flag) && sp_mono_ms() - t0 < 5000) {
        usleep(1000);
    }
}

void singleplayer_connect(void) {
    if (!atomic_load(&sp_ready)) return;
    atomic_store(&sp_cmd_connect, 1);
    sp_wait_flag(&sp_cmd_connect); // socket exists before the first send
    atomic_store(&sp_client_connected, 1);
}

void singleplayer_disconnect(void) {
    if (!atomic_load(&sp_ready)) return;
    // idempotent: the host's "sp" loopback is disconnected exactly once
    if (!atomic_exchange(&sp_client_connected, 0)) return;
    atomic_store(&sp_cmd_disconnect, 1);
    sp_wait_flag(&sp_cmd_disconnect);
}

// main/UI thread: drives spnet_host_start/stop directly, flags the server thread to begin or stop accepting
void singleplayer_host_set_enabled(int on, const char *display_name) {
    if (on) {
        spnet_host_start(display_name, SPNET_MAX_GUESTS);
        atomic_store(&sp_hosting_enabled, 1);
    } else {
        atomic_store(&sp_hosting_enabled, 0);
        spnet_host_stop();
        // connected guests are dropped by the server thread's off-edge check in sp_guests_pump
    }
}

int singleplayer_host_guest_count(void) { return atomic_load(&sp_guest_count); }

void singleplayer_client_send(const uint8_t *data, int length) {
    if (length <= 0) return;
    int off = 0;
    while (off < length) {
        int w = sp_ring_write(&sp_in, data + off, length - off);
        if (w == 0) {
            usleep(1000); // ring momentarily full: let the server drain
            continue;
        }
        off += w;
    }
}

int singleplayer_client_recv(uint8_t *buf, int max) {
    int n = sp_ring_read(&sp_recv, buf, max);
    return n > 0 ? n : -1;
}

// no-op for the loopback read path / test harnesses
void singleplayer_pump(void) {}

// host left the world: stop hosting, save the host's character, tear down the server thread
void singleplayer_leave_world(void) {
    if (atomic_load(&sp_hosting_enabled)) {
        singleplayer_host_set_enabled(0, NULL);
    }
    singleplayer_disconnect(); // idempotent; guarantees the host save
    singleplayer_stop();
}

void singleplayer_stop(void) {
    if (!sp_thread_started) return; // never started, nothing to free

    // signal the server thread to exit; it frees the JSRuntime on exit
    atomic_store(&sp_thread_quit, 1);
#ifdef __vita__
    sceKernelWaitThreadEnd(sp_tid, NULL, NULL);
    sceKernelDeleteThread(sp_tid);
    sp_tid = -1;
#else
    pthread_join(sp_pth, NULL);
#endif

    // reset all state; a later singleplayer_start() boots a fresh server
    atomic_store(&sp_ready, 0);
    atomic_store(&sp_failed, 0);
    atomic_store(&sp_thread_quit, 0);
    atomic_store(&sp_cmd_connect, 0);
    atomic_store(&sp_cmd_disconnect, 0);
    atomic_store(&sp_client_connected, 0);
    atomic_store(&sp_in.head, 0);
    atomic_store(&sp_in.tail, 0);
    atomic_store(&sp_recv.head, 0);
    atomic_store(&sp_recv.tail, 0);
    sp_thread_started = 0;
}

#endif // WITH_SINGLEPLAYER
