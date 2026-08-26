// generated, see custom-maps-landscape.c
#ifndef _H_CUSTOM_MAPS_LANDSCAPE
#define _H_CUSTOM_MAPS_LANDSCAPE
#include <stdint.h>

struct custom_map_sector {
    int plane, sx, sy;
    const unsigned char *height;
    const unsigned char *colour;
    const unsigned char *decoration;
    const unsigned char *walls_ns; // NULL if all-zero
    const unsigned char *walls_ew; // NULL if all-zero
    const unsigned char *walls_roof; // NULL if all-zero
    const unsigned short *walls_diag; // NULL if all-zero
};

// fill the client World chunk arrays for a custom-map terrain sector
int custom_maps_landscape_fill(int plane, int sx, int sy,
                               int8_t *terrain_height, int8_t *terrain_colour,
                               int8_t *walls_north_south, int8_t *walls_east_west,
                               uint16_t *walls_diagonal, int8_t *walls_roof,
                               int8_t *tile_decoration, int8_t *tile_direction);

#endif
