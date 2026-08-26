// lan/ad-hoc transport for co-op behind one api: LAN over wifi (desktop+vita), ADHOC over the vita ad-hoc radio (vita
// only)

#ifndef _H_SP_NET
#define _H_SP_NET

typedef enum {
    SPNET_MODE_OFF = 0,
    SPNET_MODE_LAN,
    SPNET_MODE_ADHOC
} spnet_mode;

typedef enum {
    SPNET_STATUS_OFF = 0, // no mode started
    SPNET_STATUS_STARTING, // net stack / adhocctl coming up
    SPNET_STATUS_NO_NETWORK, // LAN: no wifi/ethernet connectivity
    SPNET_STATUS_READY, // up, idle (joinable state for presets/scans)
    SPNET_STATUS_SCANNING, // discovery in progress
    SPNET_STATUS_HOSTING, // advertising + accepting guests
    SPNET_STATUS_ERROR // unrecoverable; see spnet_status_text()
} spnet_status;

#define SPNET_MAX_GUESTS 7 // + the host = the 8-peer ad-hoc group ceiling

typedef struct {
    char name[64]; // display label, e.g. "bren's world (2 playing)"
    char address[64]; // opaque handle for spnet_connect
    int player_count; // -1 = unknown
} spnet_world_info;

// switch net mode; returns 0 on success
int spnet_init(spnet_mode mode);
void spnet_shutdown(void);

spnet_mode spnet_get_mode(void);
spnet_status spnet_get_status(void);
// short ui status label, e.g. "connected", "hosting (2 guests)"
const char *spnet_status_text(void);

// drives net state machines and dialogs; call once per frame
void spnet_pump(void);

// nonzero while the vita netcheck dialog is on screen
int spnet_dialog_active(void);

// discovery (guest side)
int spnet_scan_start(void); // async; results accumulate until next start
int spnet_scan_results(spnet_world_info *out, int max); // snapshot count

// hosting
int spnet_host_start(const char *display_name, int max_guests);
void spnet_host_stop(void);
// Next pending guest connection, or -1. Server thread.
int spnet_host_accept(void);

// begins connecting to a scanned world; returns a conn handle
int spnet_connect(const char *address);
int spnet_conn_state(int conn); // 0 = connecting, 1 = established, -1 = dead
int spnet_send(int conn, const void *buf, int len);
int spnet_recv(int conn, void *buf, int len);
void spnet_close(int conn);

#endif
