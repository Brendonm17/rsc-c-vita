#ifndef _H_BANK
#define _H_BANK

#include "../mudclient.h"

#define BANK_COLUMNS_MIN 6
#define BANK_ROWS_MIN 3

#define BANK_COLUMNS 8
#define BANK_ROWS 6

#define BANK_PAGE_BUTTON_WIDTH 65

#define BANK_MAGIC_DEPOSIT 0x87654321
#define BANK_MAGIC_WITHDRAW 0x12345678

#define BANK_OFFER_WITHDRAW 1
#define BANK_OFFER_DEPOSIT 2

// interface-options sub-op: u8 sub-op + i32 from + i32 to
#define INTERFACE_OPTION_BANK_SWAP 2
#define INTERFACE_OPTION_BANK_INSERT 3

/* milliseconds before scrolling using the buttons */
#define BANK_SCROLL_SPEED 120

void mudclient_bank_transaction(mudclient *mud, int item_id, int amount,
                                int opcode);
#ifndef REVISION_177
int mudclient_bank_item_is_cert(int item_id);
#endif
void mudclient_bank_deposit_all(mudclient *mud, int opcode);
// from_equipment: 0 = inventory deposit-all, 1 = equipment deposit-all
int mudclient_bank_can_deposit_all(mudclient *mud, int from_equipment);
void mudclient_add_bank_menus(mudclient *mud, int type, int item_id,
                              int item_amount, char *item_name);
void mudclient_draw_bank_amounts(mudclient *mud, int amount, int last_x, int x,
                                 int y);
void mudclient_handle_bank_amounts_input(mudclient *mud, int item_id,
                                         int amount, int last_x, int x, int y,
                                         int transaction_opcode);
void mudclient_draw_bank(mudclient *mud);

#endif
