#ifndef _H_AUCTION
#define _H_AUCTION

#include "../mudclient.h"

#define AUCTION_DIALOG_WIDTH 470
#define AUCTION_DIALOG_HEIGHT 300
#define AUCTION_ROW_HEIGHT 14
#define AUCTION_VISIBLE_ROWS 14

// interface options sub-op: auction(10) and its action ids
#define INTERFACE_OPTION_AUCTION 10
#define AUCTION_OPTION_BUY 0
#define AUCTION_OPTION_CREATE 1
#define AUCTION_OPTION_ABORT 2
#define AUCTION_OPTION_REFRESH 3
#define AUCTION_OPTION_CLOSE 4
#define AUCTION_OPTION_DELETE 5

// offer-x consumer stages (mud->orsc_auction_offer_stage)
#define AUCTION_OFFER_NONE 0
#define AUCTION_OFFER_BUY_AMOUNT 1
#define AUCTION_OFFER_SELL_AMOUNT 2
#define AUCTION_OFFER_SELL_PRICE 3

void mudclient_draw_auction(mudclient *mud);
int mudclient_handle_auction_offer(mudclient *mud);
void mudclient_auction_start_sell(mudclient *mud, int item_id);

#endif
