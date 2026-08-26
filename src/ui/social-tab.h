#ifndef _H_SOCIAL_TAB
#define _H_SOCIAL_TAB

typedef enum {
    SOCIAL_ADD_FRIEND = 1,
    SOCIAL_MESSAGE_FRIEND,
    SOCIAL_ADD_IGNORE,
    // custom clans: two-step create (name then tag), invite by name
    SOCIAL_CLAN_CREATE_NAME,
    SOCIAL_CLAN_CREATE_TAG,
    SOCIAL_CLAN_INVITE,
    // custom parties: inviting also creates the party server-side
    SOCIAL_PARTY_INVITE
} SocialInput;

#include "../mudclient.h"

// custom clans: interface-options sub-op and its action ids
#define INTERFACE_OPTION_CLAN 11
#define CLAN_OPTION_CREATE 0
#define CLAN_OPTION_LEAVE 1
#define CLAN_OPTION_INVITE_PLAYER 2
#define CLAN_OPTION_ACCEPT_INVITE 3
#define CLAN_OPTION_DECLINE_INVITE 4
#define CLAN_OPTION_KICK_PLAYER 5
#define CLAN_OPTION_RANK_PLAYER 6
#define CLAN_OPTION_CLAN_SETTINGS 7
#define CLAN_OPTION_SEND_CLAN_INFO 8

// custom parties: interface-options sub-op and action ids
#define INTERFACE_OPTION_PARTY 12
#define PARTY_OPTION_INIT 0
#define PARTY_OPTION_LEAVE 1
#define PARTY_OPTION_CREATE_OR_INVITE 2
#define PARTY_OPTION_ACCEPT_INVITE 3
#define PARTY_OPTION_DECLINE_INVITE 4
#define PARTY_OPTION_KICK_PLAYER 5
#define PARTY_OPTION_RANK_PLAYER 6
#define PARTY_OPTION_PARTY_SETTINGS 7
#define PARTY_OPTION_SEND_PARTY_INFO 8
#define PARTY_OPTION_INVITE_PLAYER_OR_MAKE 9

/* width and height of tab window */
#define SOCIAL_WIDTH 196
#define SOCIAL_HEIGHT 182
#define SOCIAL_TAB_HEIGHT 24

/* width of add friend/ignore dialog box */
#define SOCIAL_DIALOG_ADD_WIDTH 300

/* width of message friend dialog box */
#define SOCIAL_DIALOG_MESSAGE_WIDTH (MUD_WIDTH - 12)

/* height of add, ignore and message dialog boxes */
#define SOCIAL_DIALOG_HEIGHT 70

/* width and height of clickable cancel button */
#define SOCIAL_CANCEL_SIZE 40

#ifdef REVISION_177
#define FRIEND_ONLINE 99
#else
#define FRIEND_ONLINE 255
#endif

// online friends report as 99 on 177 worlds, 255 on 204
#define MUD_FRIEND_ONLINE(mud) ((mud)->protocol177 ? 99 : FRIEND_ONLINE)

void mudclient_sort_friends(mudclient *mud);
void mudclient_add_friend(mudclient *mud, char *username);
void mudclient_remove_friend(mudclient *mud, int64_t encoded_username);
void mudclient_add_ignore(mudclient *mud, char *username);
void mudclient_remove_ignore(mudclient *mud, int64_t encoded_username);
#ifndef REVISION_177
// custom: name string recipient, smart-length rs2-huffman body
void mudclient_send_private_message_custom(mudclient *mud, const char *username,
                                           const char *message);
// custom friend/ignore add-remove: one name string, no base37 long
void mudclient_send_social_custom(mudclient *mud, int opcode,
                                  const char *username);
#endif
void mudclient_send_private_message(mudclient *mud, int64_t username,
                                    int8_t *message, int length);

void mudclient_draw_ui_tab_social(mudclient *mud, int no_menus);
void mudclient_draw_social_input(mudclient *mud);

#ifndef REVISION_177
// sends a clan action on the custom wire with optional strings
void mudclient_orsc_send_clan_action(mudclient *mud, int action,
                                     const char *str1, const char *str2);

// sends a party action on the custom wire
void mudclient_orsc_send_party_action(mudclient *mud, int action,
                                      const char *str1);
// the always-on-screen party box
void mudclient_draw_party_hud(mudclient *mud);
#endif

#endif
