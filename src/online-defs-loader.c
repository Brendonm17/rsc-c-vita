// overlays openrsc item/npc def tables onto game_data for custom online worlds
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "game-data.h"
#include "online-defs.h"

static int online_defs_loaded = 0;

// snapshot of the base def arrays, restored on unload
static struct ItemConfig *snap_items = NULL;
static struct NpcConfig *snap_npcs = NULL;
static int snap_base_item_count = 0;
static int snap_base_npc_count = 0;
static int snap_base_item_sprite_count = 0;

// grow a def array to `need` entries, zeroing the new tail
static void *grow_defs(void *arr, int *count, int need, size_t elem) {
    if (*count >= need) {
        return arr;
    }
    void *grown = realloc(arr, (size_t)need * elem);
    if (grown == NULL) {
        return arr;
    }
    memset((char *)grown + (size_t)(*count) * elem, 0,
           (size_t)(need - *count) * elem);
    *count = need;
    return arr = grown;
}

void game_data_load_online_defs(void) {
    if (online_defs_loaded) {
        return;
    }
    online_defs_loaded = 1;

    // snapshot the base regions before growing
    snap_base_item_count = game_data.item_count;
    snap_base_npc_count = game_data.npc_count;
    snap_base_item_sprite_count = game_data.item_sprite_count;
    snap_items = malloc((size_t)snap_base_item_count * sizeof(struct ItemConfig));
    if (snap_items != NULL) {
        memcpy(snap_items, game_data.items,
               (size_t)snap_base_item_count * sizeof(struct ItemConfig));
    }
    snap_npcs = malloc((size_t)snap_base_npc_count * sizeof(struct NpcConfig));
    if (snap_npcs != NULL) {
        memcpy(snap_npcs, game_data.npcs,
               (size_t)snap_base_npc_count * sizeof(struct NpcConfig));
    }

    // items
    game_data.items = grow_defs(game_data.items, &game_data.item_count,
                                online_item_max_id + 1, sizeof(struct ItemConfig));
    for (int i = 0; i < online_item_def_count; i++) {
        const OnlineItemDef *d = &online_item_defs[i];
        if (d->id < 0 || d->id >= game_data.item_count) {
            continue;
        }
        struct ItemConfig *it = &game_data.items[d->id];
        // base strings live in the jag buffer, never freed
        it->name = strdup(d->name);
        it->description = strdup(d->description);
        it->command = strdup(d->command);
        // config85 polarity: 0 = stackable
        it->stackable = d->stackable ? 0 : 1;
        it->base_price = (uint32_t)d->base_price;
        it->wearable = (uint16_t)d->wearable;
        it->special = (uint8_t)d->untradeable; // struct field: untradable
        it->members = (uint8_t)d->members;
        it->has_note_type = (uint8_t)d->has_note_type;
        // custom items carry an atlas slot >= 450, -1 keeps the base sprite
        if (d->sprite >= 0) {
            it->sprite = (uint16_t)d->sprite;
            if ((int)it->sprite + 1 > game_data.item_sprite_count) {
                game_data.item_sprite_count = it->sprite + 1;
            }
        }
    }

    // npcs
    game_data.npcs = grow_defs(game_data.npcs, &game_data.npc_count,
                               online_npc_max_id + 1, sizeof(struct NpcConfig));
    for (int i = 0; i < online_npc_def_count; i++) {
        const OnlineNpcDef *d = &online_npc_defs[i];
        if (d->id < 0 || d->id >= game_data.npc_count) {
            continue;
        }
        struct NpcConfig *n = &game_data.npcs[d->id];
        n->name = strdup(d->name);
        n->description = strdup(d->description);
        n->command = strdup(d->command);
        // literal command2 only, token-backed ones fill in via the resolver
        n->command2 = d->command2[0] != '\0' ? strdup(d->command2) : NULL;
    }
}

// restore the base def region and counts
void game_data_unload_online_defs(void) {
    if (!online_defs_loaded) {
        return;
    }

    if (snap_items != NULL) {
        memcpy(game_data.items, snap_items,
               (size_t)snap_base_item_count * sizeof(struct ItemConfig));
        game_data.item_count = snap_base_item_count;
        game_data.item_sprite_count = snap_base_item_sprite_count;
        free(snap_items);
        snap_items = NULL;
    }

    if (snap_npcs != NULL) {
        memcpy(game_data.npcs, snap_npcs,
               (size_t)snap_base_npc_count * sizeof(struct NpcConfig));
        game_data.npc_count = snap_base_npc_count;
        free(snap_npcs);
        snap_npcs = NULL;
    }

    online_defs_loaded = 0;
}

void game_data_resolve_online_npc_commands(int right_click_trade,
                                           int right_click_bank,
                                           int spawn_auction_npcs) {
    if (!online_defs_loaded) {
        return;
    }

    for (int i = 0; i < online_npc_def_count; i++) {
        const OnlineNpcDef *d = &online_npc_defs[i];

        if (d->id < 0 || d->id >= game_data.npc_count) {
            continue;
        }

        // shop "Trade" / banker "Bank" / auction "Collect" per server config
        if (d->command_token != 0) {
            char *command = "";

            if (d->command_token == 1 && right_click_trade) {
                command = "Trade";
            } else if (d->command_token == 2 && right_click_bank) {
                command = "Bank";
            }

            game_data.npcs[d->id].command = strdup(command);
        }

        if (d->command2_token == 3) {
            game_data.npcs[d->id].command2 =
                spawn_auction_npcs ? strdup("Collect") : NULL;
        }
    }
}
