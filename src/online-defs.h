#ifndef _H_ONLINE_DEFS
#define _H_ONLINE_DEFS

typedef struct {
    int id;
    const char *name;
    const char *description;
    const char *command;
    int sprite; // openrsc sprite id
    int base_price;
    int stackable;
    int untradeable;
    int members;
    int wearable;
    // !stackable && (!untradeable || noteable); when 0, asNote() returns the item unchanged
    int has_note_type;
} OnlineItemDef;

typedef struct {
    int id;
    const char *name;
    const char *description;
    const char *command;
    // config-dependent command token: 0 = none/literal, 1 = shopOption, 2 = bankerOption1
    int command_token;
    // npc command2: a literal, or "" with token 3 = bankerOption2
    const char *command2;
    int command2_token;
} OnlineNpcDef;

extern const OnlineItemDef online_item_defs[];
extern const int online_item_def_count;
extern const int online_item_max_id;
extern const OnlineNpcDef online_npc_defs[];
extern const int online_npc_def_count;
extern const int online_npc_max_id;

// overlays these onto game_data by real id for a custom-online world
void game_data_load_online_defs(void);

// undoes the overlay, restoring base defs and counts
void game_data_unload_online_defs(void);

// resolves config-dependent npc commands once SEND_SERVER_CONFIGS has arrived
void game_data_resolve_online_npc_commands(int right_click_trade,
                                           int right_click_bank,
                                           int spawn_auction_npcs);

#endif
