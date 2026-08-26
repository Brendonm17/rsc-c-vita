#ifndef _H_MENU
#define _H_MENU

/* higher number is higher priority in the context menu */
typedef enum {
    MENU_CAST_GROUNDITEM = 200,
    MENU_USEWITH_GROUNDITEM = 210,
    MENU_GROUNDITEM_TAKE = 220,
    MENU_GROUNDITEM_EXAMINE = 3200,

    MENU_CAST_WALLOBJECT = 300,
    MENU_USEWITH_WALLOBJECT = 310,
    MENU_WALL_OBJECT_COMMAND1 = 320,
    MENU_WALL_OBJECT_COMMAND2 = 2300,
    MENU_WALL_OBJECT_EXAMINE = 3300,

    MENU_CAST_OBJECT = 400,
    MENU_USEWITH_OBJECT = 410,
    MENU_OBJECT_COMMAND1 = 420,
    MENU_OBJECT_COMMAND2 = 2400,
    MENU_OBJECT_EXAMINE = 3400,

    MENU_CAST_INVITEM = 600,
    MENU_USEWITH_INVITEM = 610,
    MENU_INVENTORY_UNEQUIP = 620,
    MENU_INVENTORY_WEAR = 630,
    MENU_INVENTORY_COMMAND = 640,
    MENU_INVENTORY_USE = 650,
    MENU_INVENTORY_DROP = 660,
    MENU_INVENTORY_DROP_X = 661,
    MENU_INVENTORY_EXAMINE = 3600,

    // equipment tab: remove a worn item from the paperdoll
    MENU_EQUIP_UNEQUIP = 621,

    // auction sell mode: auction an inventory item
    MENU_INVENTORY_AUCTION = 622,

    // clan/party leader admin menu actions
    MENU_CLAN_RANK = 623,
    MENU_CLAN_LEADERSHIP = 624,
    MENU_CLAN_SETTING = 625,
    MENU_PARTY_SETTING = 626,

    // per-player share toggles: 0 = shareloot, 1 = shareexp
    MENU_PARTY_SHARE = 627,

    // unequip to bank: remove gear while the bank is open
    MENU_EQUIP_REMOVE_TO_BANK = 628,

    MENU_CAST_NPC = 700,
    MENU_USEWITH_NPC = 710,
    MENU_NPC_TALK = 720,
    MENU_NPC_COMMAND = 725,
    // npc command2 (banker collect, auction clerk teleport)
    MENU_NPC_COMMAND2 = 726,
    MENU_NPC_ATTACK1 = 715,
    MENU_NPC_ATTACK2 = 2715,
    MENU_NPC_EXAMINE = 3700,

    MENU_CAST_PLAYER = 800,
    MENU_USEWITH_PLAYER = 810,
    MENU_PLAYER_ATTACK1 = 805,
    MENU_PLAYER_ATTACK2 = 2805,
    MENU_PLAYER_DUEL = 2806,
    MENU_PLAYER_TRADE = 2810,
    MENU_PLAYER_FOLLOW = 2820,
    // want_parties worlds: the in-world player menu's "Invite to party" (199[12][2] + u16 server index)
    MENU_PLAYER_PARTY_INVITE = 2821,

    MENU_CAST_GROUND = 900,
    MENU_WALK = 920,
    MENU_CAST_SELF = 1000,

    MENU_BANK_WITHDRAW = 601,
    MENU_BANK_DEPOSIT = 602,

    // equip an item straight from the bank
    MENU_BANK_EQUIP = 605,

    // custom bank reordering: swap or insert by slot
    MENU_BANK_ORGANIZE_SWAP = 606,
    MENU_BANK_ORGANIZE_INSERT = 607,

    // want_cert_deposit: Uncert+Deposit-X/All on the 27 certificate items; preceded by sendCertMode (199[0][mode])
    MENU_BANK_DEPOSIT_UNCERT = 608,

    // "Bury All" (want_drop_x): ITEM_COMMAND with the whole inventory count instead of 1
    MENU_INVENTORY_COMMAND_ALL = 609,

    // equipment-tab rows: command/Drop use the 0xFFFF equipped sentinel + item id; Use selects the virtual
    // inventory index slot + 30, decoded server-side as the equipped slot
    MENU_EQUIP_COMMAND = 4101,
    MENU_EQUIP_USE = 4102,
    MENU_EQUIP_DROP = 4103,

    /* trade and duel */
    MENU_TRANSACTION_OFFER = 603,
    MENU_TRANSACTION_REMOVE = 604,

    /* compass directions */
    MENU_MAP_LOOK = 101,

    MENU_WIKI_LOOKUP = 102,

    MENU_CANCEL = 4000
} MenuType;

#include "../mudclient.h"
#include "transaction.h"

/* for mouse picking */
#define PLAYER_FACE_TAG 10000
#define GROUND_ITEM_FACE_TAG 20000
#define NPC_FACE_TAG 30000
#define TILE_FACE_TAG 200000

#define WIKI_TYPE_PAGE "Special:Lookup?type=%s&id=%d&name=%s"

void mudclient_menu_item_click(mudclient *mud, int i);
void mudclient_create_top_mouse_menu(mudclient *mud);
void mudclient_menu_add_wiki(mudclient *mud, const char *display,
                             const char *page);
void mudclient_menu_add_id_wiki(mudclient *mud, const char *display,
                                const char *type, int id);
void mudclient_menu_add_ground_item(mudclient *mud, int index);
void mudclient_create_right_click_menu(mudclient *mud);
void mudclient_draw_right_click_menu(mudclient *mud);
void mudclient_draw_hover_tooltip(mudclient *mud);

#endif
