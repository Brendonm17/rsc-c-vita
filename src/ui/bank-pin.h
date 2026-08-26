#ifndef _H_BANK_PIN
#define _H_BANK_PIN

#include "../mudclient.h"

#define BANK_PIN_DIGITS 4

// CLIENT_INTERFACE_OPTIONS sub-op (InterfaceOptions.java: BANK_PIN(8))
#define INTERFACE_OPTION_BANK_PIN 8

void mudclient_draw_bank_pin(mudclient *mud);

#endif
