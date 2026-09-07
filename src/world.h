#ifndef _H_WORLD
#define _H_WORLD

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef RENDER_GL
#if defined(__vita__)
#include <vitaGL.h>
#elif defined(GLAD)
#include <glad/glad.h>
#else
#include <GL/glew.h>
#include <GL/glu.h>
#endif
#if !defined(SDL12) && !defined(__SWITCH__) && !defined(__vita__)
#include <SDL_opengl.h>
#endif

#define CGLM_DEFINE_PRINTS
#include <cglm/cglm.h>
#endif

typedef struct World World;

// plane-0 quadrant arrays captured after decode, restored after the upper-floor
// passes overwrite them; per-world so worker and main-thread loads don't clash
// [4][48 * 48] == [PLANE_COUNT][TILE_COUNT]
typedef struct WorldGroundSnapshot {
    int8_t terrain_height[4][48 * 48];
    int8_t terrain_colour[4][48 * 48];
    int8_t walls_north_south[4][48 * 48];
    int8_t walls_east_west[4][48 * 48];
    uint16_t walls_diagonal[4][48 * 48];
    int8_t walls_roof[4][48 * 48];
    int8_t tile_decoration[4][48 * 48];
    int8_t tile_direction[4][48 * 48];
    int valid;
} WorldGroundSnapshot;

#include "mudclient.h"
#include "version.h"

#define REGION_WIDTH 96
#define REGION_HEIGHT 96

#define TILE_SIZE 128
#define PLANE_COUNT 4
#define TERRAIN_COUNT 64
#define TERRAIN_COLOUR_COUNT 256
#define LOCAL_COUNT 18432
#define REGION_SIZE 48
#define TILE_COUNT (REGION_SIZE * REGION_SIZE)
#define PLANE_HEIGHT 80000

#ifndef TERRAIN_MAX_FACES
#define TERRAIN_MAX_FACES 18688
#endif

#ifndef TERRAIN_MAX_VERTICES
#define TERRAIN_MAX_VERTICES 18688
#endif

/* length of the portion of the roof hanging over the building */
#define ROOF_SLOPE 16

/* https://github.com/2003scape/rsc-config/blob/master/res/types.json#L14 */
typedef enum TILE_TIPE {
    FLOOR_TILE_TYPE = 2,
    LIQUID_TILE_TYPE = 3,
    BRIDGE_TILE_TYPE = 4,
    HOLE_TILE_TYPE = 5
} TILE_TYPE;

/* used for everything except wallobjs/bounds */
typedef enum DIRECTION {
    DIR_NORTH = 0,
    DIR_NORTHWEST = 1,
    DIR_WEST = 2,
    DIR_SOUTHWEST = 3,
    DIR_SOUTH = 4,
    DIR_SOUTHEAST = 5,
    DIR_EAST = 6,
    DIR_NORTHEAST = 7,
    /*
     * attacker is shown on right, defender on left
     * except NPCs are always on the left
     */
    DIR_COMBAT_LEFT = 8,
    DIR_COMBAT_RIGHT = 9
} DIRECTION;

/* https://github.com/2003scape/rsc-config/blob/master/config-json/tiles.json */
#define BRIDGE_TILE_DECORATION 12

extern int16_t terrain_colours[TERRAIN_COLOUR_COUNT];

int rgb_to_texture_colour(int r, int g, int b);
void init_world_global(void);

struct World {
    Scene *scene;
    Surface *surface;
    int8_t player_alive;
    int version;
    int base_media_sprite;
    int8_t *landscape_pack;
    int8_t *map_pack;
    int8_t *member_landscape_pack;
    int8_t *member_map_pack;
    GameModel *parent_model;
    int object_adjacency[REGION_WIDTH][REGION_HEIGHT];
    int route_via[REGION_WIDTH][REGION_HEIGHT];
    int terrain_height_local[REGION_WIDTH][REGION_HEIGHT];
    uint16_t walls_diagonal[PLANE_COUNT][TILE_COUNT];
    GameModel *terrain_models[TERRAIN_COUNT];
    int8_t terrain_colour[PLANE_COUNT][TILE_COUNT];
    int8_t terrain_height[PLANE_COUNT][TILE_COUNT];
    int8_t tile_decoration[PLANE_COUNT][TILE_COUNT];
    int8_t tile_direction[PLANE_COUNT][TILE_COUNT];
    WorldGroundSnapshot ground_snapshot;
    GameModel *wall_models[PLANE_COUNT][TILE_COUNT];
    int8_t walls_east_west[PLANE_COUNT][TILE_COUNT];
    int8_t walls_north_south[PLANE_COUNT][TILE_COUNT];
    GameModel *roof_models[PLANE_COUNT][TILE_COUNT];
    int8_t walls_roof[PLANE_COUNT][TILE_COUNT];

    /* map face indexes to x, y coordinates for walking */
    int local_x[LOCAL_COUNT];
    int local_y[LOCAL_COUNT];

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    /* dynamically generated terrain, wall and roof models */
    GameModel **gl_world_models_buffer;
    int gl_world_models_offset;
#endif

    int8_t thick_walls;

    // whether to overlay the OpenRSC custom landscape on sector loads
    int8_t apply_custom_landscape;

    // while set, world_load_section skips live-side effects (scene adds, screen
    // clear, minimap upload); world_load_section_commit replays them
    int8_t defer_scene_adds;

    // detached world built by the prefetch worker; its models are in no scene, so
    // world_reset must not touch world->scene
    int8_t detached;

    // when non-NULL, minimap pixel writes go here (private worker scratch) instead
    // of the shared surface texture buffer
    uint8_t *minimap_buffer;
};

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void world_gl_create_world_models_buffer(World *world, int max_models);
void world_gl_buffer_world_models(World *world);
void world_gl_buffer_world_models_to(World *world,
                                     gl_vertex_buffer ***buffers,
                                     int *buffers_length, int deferred);
void world_gl_update_terrain_buffers(World *world);
void world_gl_update_terrain_patch(World *world);
void world_gl_update_terrain_flush(World *world);
#endif

void world_new(World *world, Scene *scene, Surface *surface, int version);
void world_load_section(World *world, int x, int y, int plane);
void world_load_section_commit(World *world);
int world_free_models_step(World *world, int start_index, int max_slots);
void world_free_models(World *world);
int world_route(World *world, int start_x, int start_y, int end_x1, int end_y1,
                int end_x2, int end_y2, int *route_x, int *route_y,
                int objects);
int world_is_under_roof(World *world, int x, int y);
int world_get_tile_direction(World *world, int x, int y);
void world_set_tile_direction(World *world, int x, int y, int direction);
int world_get_elevation(World *world, int x, int y);
int world_get_wall_roof(World *world, int x, int y);
void world_register_wall_object(World *world, int x, int y, int dir, int id);
void world_register_object(World *world, int x, int y, int id);
void world_remove_object(World *world, int x, int y, int id);
void world_remove_wall_object(World *world, int x, int y, int k, int id);
void world_add_models(World *world, GameModel **models);
void world_reset(World *world, int dispose);
#endif
