#ifndef _H_GAME_CHARACTER
#define _H_GAME_CHARACTER

#include <stdint.h>
#include <string.h>

/* animation order indices */
#define ANIMATION_COUNT 12

typedef enum {
    ANIMATION_INDEX_HEAD = 0,
    ANIMATION_INDEX_BODY = 1,
    ANIMATION_INDEX_LEGS = 2,
    ANIMATION_INDEX_LEFT_HAND = 3,    /* shields */
    ANIMATION_INDEX_RIGHT_HAND = 4,   /* swords */
    ANIMATION_INDEX_HEAD_OVERLAY = 5, /* med helms */
    ANIMATION_INDEX_BODY_OVERLAY = 6, /* chainmail */
    ANIMATION_INDEX_LEGS_OVERLAY = 7, /* skirts */
    ANIMATION_INDEX_8 = 8,
    ANIMATION_INDEX_BOOTS = 9,
    ANIMATION_INDEX_NECK = 10, /* amulets */
    ANIMATION_INDEX_CAPE = 11
} ANIMATION_INDEX;

extern ANIMATION_INDEX character_animation_array[8][ANIMATION_COUNT];

extern int character_walk_model[4];
extern int character_combat_model_array1[8];
extern int character_combat_model_array2[8];

#define PLAYER_HAIR_COLOUR_COUNT 10
#define PLAYER_TOP_BOTTOM_COLOUR_COUNT 15
#define PLAYER_SKIN_COLOUR_COUNT 43
// default unlocked skin colours: the 5 original RSC skins
#define PLAYER_SKIN_COLOUR_BASE_COUNT 5

extern int player_hair_colours[PLAYER_HAIR_COLOUR_COUNT];
extern int player_top_bottom_colours[PLAYER_TOP_BOTTOM_COLOUR_COUNT];
extern int player_skin_colours[PLAYER_SKIN_COLOUR_COUNT];

typedef struct GameCharacter GameCharacter;

#include "mudclient.h"
#include "utility.h"

#define WAYPOINT_COUNT 10

struct GameCharacter {
    int64_t encoded_username;
    char name[MAX_USER_LENGTH + 1];

    uint16_t server_index;
    uint16_t server_id;

    /* set to -1 for players */
    int16_t npc_id;

    uint8_t step_count;
    uint8_t moving_step;
    uint8_t waypoint_current;

    int32_t current_x;
    int32_t current_y;
    int32_t waypoints_x[WAYPOINT_COUNT];
    int32_t waypoints_y[WAYPOINT_COUNT];

    int8_t current_animation;
    int8_t next_animation;

    /* chat message above head */
    uint8_t message_timeout;
    char message[255];

    uint8_t damage_taken;
    uint8_t current_hits;
    uint8_t max_hits;

    uint8_t combat_timer;

    uint8_t hair_colour;
    uint8_t top_colour;
    uint8_t bottom_colour;
    uint8_t skin_colour;
    uint8_t skull_visible;

    /* action bubble */
    uint8_t bubble_timeout;
    uint16_t bubble_item;

    int16_t level;

    ANIMATION_INDEX animations[ANIMATION_COUNT];

    uint16_t incoming_projectile_sprite;
    uint8_t projectile_range;

    int32_t attacking_player_server_index;
    int32_t attacking_npc_server_index;

    // custom appearance update trailer, zeroed on authentic
    char clan_tag[16]; // empty when the player is in no clan
    int32_t icon; // (crownIndex + 1) << 24 | recolourMask24
    uint8_t group_id;
    uint8_t is_invisible;
    uint8_t is_invulnerable;
    // OpenRSC update type 8: a heal, the mirror of type 2's damage
    uint8_t heal_taken;
    uint8_t heal_timer;
    // npc update type 6: npc weapon/wield, custom-only
    uint8_t wield;
    uint8_t wield2;
};

void game_character_new(GameCharacter *game_character);
void game_character_move(GameCharacter *character);

#endif
