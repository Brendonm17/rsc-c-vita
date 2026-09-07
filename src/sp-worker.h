// bot-brain worker runtime for the embedded single-player server: a second
// QuickJS runtime on its own thread, connected by blob queues (transport only)
#ifndef _H_SP_WORKER
#define _H_SP_WORKER

#include <stddef.h>
#include <stdint.h>

#include "quickjs.h"

// embedder hook run on the worker thread after __host exists, before bootstrap; may be NULL
typedef void (*sp_worker_host_hook)(JSContext *ctx, JSValue host);

// start the worker thread; returns 0 on thread create, boots asynchronously (poll sp_worker_state())
int sp_worker_start(const char *bootstrap_path, const char *bundle_path,
                    sp_worker_host_hook hook);

// ask the worker to exit, join it, free its runtime, drop queued blobs
void sp_worker_stop(void);

#define SP_WORKER_STOPPED 0
#define SP_WORKER_BOOTING 1
#define SP_WORKER_READY 2
#define SP_WORKER_FAILED (-1)
int sp_worker_state(void);

// main-runtime side (server thread, server context)

// serialize obj and queue it for the worker; 0 = queued, -1 = full or unserializable
int sp_worker_post(JSContext *ctx, JSValueConst obj);

// take one message from the worker and rebuild it in ctx; JS_UNDEFINED when empty
JSValue sp_worker_poll(JSContext *ctx);

// number of worker->main messages waiting
int sp_worker_pending(void);

// register the worker* functions on the server context's __host
void sp_worker_register_main_host(JSContext *ctx, JSValue host,
                                  const char *bootstrap_path,
                                  const char *bundle_path,
                                  sp_worker_host_hook hook);

// counters for the trace line
typedef struct sp_worker_stats {
    uint64_t posted;         // main -> worker messages
    uint64_t received;       // worker -> main messages
    uint64_t bytes_to_worker;
    uint64_t bytes_to_main;
    uint64_t worker_messages; // messages the worker dispatched to JS
    uint64_t worker_busy_us;  // time the worker spent inside JS
    uint64_t worker_read_us;  // time the worker spent deserializing
    uint64_t main_write_us;   // time the main side spent serializing
    uint64_t main_read_us;    // time the main side spent deserializing
} sp_worker_stats;
void sp_worker_get_stats(sp_worker_stats *out);

#endif
