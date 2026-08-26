#include "crowns.h"

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
#include "../gl/textures/custom.h"

// registers crown slot geometry in the gl custom atlas
void surface_setup_custom_crown_sprites(Surface *surface,
                                        int sprite_item_base) {
    for (int i = 0; i < GL_CUSTOM_CROWN_COUNT; i++) {
        int sid = sprite_item_base + gl_custom_crown_geoms[i].slot;

        surface->sprite_width[sid] = gl_custom_crown_geoms[i].width;
        surface->sprite_height[sid] = gl_custom_crown_geoms[i].height;
        surface->sprite_width_full[sid] = gl_custom_crown_geoms[i].width;
        surface->sprite_height_full[sid] = gl_custom_crown_geoms[i].height;
        surface->sprite_translate_x[sid] = 0;
        surface->sprite_translate_y[sid] = 0;
        surface->sprite_translate[sid] = 0;
    }

    for (int i = 0; i < GL_CUSTOM_EQUIPSLOT_COUNT; i++) {
        int sid = sprite_item_base + gl_custom_equipslot_geoms[i].slot;

        surface->sprite_width[sid] = gl_custom_equipslot_geoms[i].width;
        surface->sprite_height[sid] = gl_custom_equipslot_geoms[i].height;
        surface->sprite_width_full[sid] = gl_custom_equipslot_geoms[i].width;
        surface->sprite_height_full[sid] = gl_custom_equipslot_geoms[i].height;
        surface->sprite_translate_x[sid] = 0;
        surface->sprite_translate_y[sid] = 0;
        surface->sprite_translate[sid] = 0;
    }
}
#endif

// decodes wire crown int: high byte = crown index + 1, 0 = none
static int crown_index(int packed) {
    if (packed <= 0) {
        return -1;
    }

    int index = ((packed >> 24) & 0xff) - 1;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    if (index < 0 || index >= GL_CUSTOM_CROWN_COUNT) {
        return -1;
    }
#else
    // software build ships no crown sprites (GL-atlas only)
    (void)index;
    return -1;
#endif

    return index;
}

int mudclient_crown_width(mudclient *mud, int packed) {
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    int index = crown_index(packed);

    if (index < 0 || !mud->protocol_custom) {
        return 0;
    }

    return gl_custom_crown_geoms[index].width + 5;
#else
    (void)mud;
    (void)packed;
    return 0;
#endif
}

int mudclient_draw_crown(mudclient *mud, int x, int y, int packed) {
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    int index = crown_index(packed);

    if (index < 0 || !mud->protocol_custom) {
        return 0;
    }

    int width = gl_custom_crown_geoms[index].width;
    int height = gl_custom_crown_geoms[index].height;
    int sprite_id = mud->sprite_item + gl_custom_crown_geoms[index].slot;

    // low 24 bits are the recolour mask for the greyscale sprite
    surface_draw_sprite_transform_mask(mud->surface, x, y - height, width,
                                       height, sprite_id, packed & 0x00ffffff,
                                       0, 0, 0);

    return width + 5;
#else
    (void)mud;
    (void)x;
    (void)y;
    (void)packed;
    return 0;
#endif
}
