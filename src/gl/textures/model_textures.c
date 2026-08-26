#include "model_textures.h"

gl_atlas_position gl_texture_atlas_positions[] = {
    {0.000000f, 0.125000f, 0.375000f, 0.500000f},
    {0.187500f, 0.250000f, 0.687500f, 0.750000f},
    {0.500000f, 0.625000f, 0.250000f, 0.375000f},
    {0.937500f, 1.000000f, 0.125000f, 0.187500f},
    {0.250000f, 0.375000f, 0.375000f, 0.500000f},
    {0.750000f, 0.875000f, 0.375000f, 0.500000f},
    {0.375000f, 0.500000f, 0.000000f, 0.125000f},
    {0.375000f, 0.500000f, 0.125000f, 0.250000f},
    {0.750000f, 0.875000f, 0.000000f, 0.125000f},
    {0.187500f, 0.250000f, 0.562500f, 0.625000f},
    {0.125000f, 0.187500f, 0.812500f, 0.875000f},
    {0.187500f, 0.250000f, 0.750000f, 0.812500f},
    {0.125000f, 0.250000f, 0.250000f, 0.375000f},
    {0.125000f, 0.187500f, 0.500000f, 0.562500f},
    {0.937500f, 1.000000f, 0.187500f, 0.250000f},
    {0.062500f, 0.125000f, 0.937500f, 1.000000f},
    {0.250000f, 0.375000f, 0.125000f, 0.250000f},
    {0.625000f, 0.687500f, 0.125000f, 0.250000f},
    {0.687500f, 0.812500f, 0.125000f, 0.250000f},
    {0.125000f, 0.250000f, 0.000000f, 0.125000f},
    {0.375000f, 0.500000f, 0.250000f, 0.375000f},
    {0.000000f, 0.125000f, 0.500000f, 0.625000f},
    {0.875000f, 1.000000f, 0.375000f, 0.500000f},
    {0.625000f, 0.750000f, 0.000000f, 0.125000f},
    {0.125000f, 0.187500f, 0.562500f, 0.625000f},
    {0.125000f, 0.187500f, 0.625000f, 0.687500f},
    {0.062500f, 0.125000f, 0.875000f, 0.937500f},
    {0.000000f, 0.125000f, 0.625000f, 0.750000f},
    {0.000000f, 0.125000f, 0.250000f, 0.375000f},
    {0.187500f, 0.250000f, 0.812500f, 0.875000f},
    {0.125000f, 0.187500f, 0.750000f, 0.812500f},
    {0.187500f, 0.250000f, 0.500000f, 0.562500f},
    {0.500000f, 0.625000f, 0.000000f, 0.125000f},
    {0.750000f, 0.875000f, 0.250000f, 0.375000f},
    {0.500000f, 0.625000f, 0.375000f, 0.500000f},
    {0.000000f, 0.062500f, 0.937500f, 1.000000f},
    {0.875000f, 1.000000f, 0.250000f, 0.375000f},
    {0.187500f, 0.250000f, 0.625000f, 0.687500f},
    {0.000000f, 0.062500f, 0.875000f, 0.937500f},
    {0.812500f, 0.937500f, 0.125000f, 0.250000f},
    {0.375000f, 0.500000f, 0.375000f, 0.500000f},
    {0.125000f, 0.250000f, 0.375000f, 0.500000f},
    {0.250000f, 0.375000f, 0.000000f, 0.125000f},
    {0.250000f, 0.375000f, 0.250000f, 0.375000f},
    {0.000000f, 0.125000f, 0.000000f, 0.125000f},
    {0.500000f, 0.625000f, 0.125000f, 0.250000f},
    {0.125000f, 0.187500f, 0.875000f, 0.937500f},
    {0.000000f, 0.125000f, 0.125000f, 0.250000f},
    {0.125000f, 0.187500f, 0.937500f, 1.000000f},
    {0.000000f, 0.125000f, 0.750000f, 0.875000f},
    {0.875000f, 1.000000f, 0.000000f, 0.125000f},
    {0.625000f, 0.750000f, 0.250000f, 0.375000f},
    {0.625000f, 0.750000f, 0.375000f, 0.500000f},
    {0.125000f, 0.250000f, 0.125000f, 0.250000f},
    {0.125000f, 0.187500f, 0.687500f, 0.750000f},
};

gl_atlas_position gl_white_model_atlas_position =
    {0.187500f, 0.218750f, 0.875000f, 0.906250f};

gl_atlas_position gl_transparent_model_atlas_position =
    {0.187500f, 0.218750f, 0.906250f, 0.937500f};

int model_textures_count(void) {
    return (int)(sizeof(gl_texture_atlas_positions) /
                 sizeof(gl_texture_atlas_positions[0]));
}

#if defined(__vita__) && defined(RENDER_GL)
// 1 = atlas cell has an alpha-transparent texel and needs the discard
#define MODEL_TEXTURE_MAX 256
static unsigned char gl_texture_has_alpha[MODEL_TEXTURE_MAX];

// shader discards only alpha==0 texels; flag only cells with a true alpha-0 texel
#define MODEL_TEXTURE_ALPHA_THRESHOLD 1

void model_textures_compute_alpha(SDL_Surface *atlas) {
    int count = model_textures_count();

    if (count > MODEL_TEXTURE_MAX) {
        count = MODEL_TEXTURE_MAX;
    }

    // default: an unscannable atlas cell is assumed to have alpha
    for (int i = 0; i < MODEL_TEXTURE_MAX; i++) {
        gl_texture_has_alpha[i] = 1;
    }

    if (atlas == NULL || atlas->pixels == NULL || atlas->format == NULL ||
        atlas->w <= 0 || atlas->h <= 0) {
        return;
    }

    // atlas is 32-bit ABGR8888
    if (atlas->format->BytesPerPixel != 4) {
        return;
    }

    int locked = 0;

    if (SDL_MUSTLOCK(atlas)) {
        if (SDL_LockSurface(atlas) != 0) {
            return;
        }

        locked = 1;
    }

    int surface_w = atlas->w;
    int surface_h = atlas->h;
    int pitch = atlas->pitch;
    uint8_t *base = (uint8_t *)atlas->pixels;

    for (int i = 0; i < count; i++) {
        gl_atlas_position pos = gl_texture_atlas_positions[i];

        // normalize UV cell to a pixel rect, clamped to the surface bounds
        int x0 = (int)(pos.left_u * surface_w);
        int x1 = (int)(pos.right_u * surface_w);
        int y0 = (int)(pos.top_v * surface_h);
        int y1 = (int)(pos.bottom_v * surface_h);

        if (x1 < x0) {
            int tmp = x0;
            x0 = x1;
            x1 = tmp;
        }

        if (y1 < y0) {
            int tmp = y0;
            y0 = y1;
            y1 = tmp;
        }

        if (x0 < 0) {
            x0 = 0;
        }

        if (y0 < 0) {
            y0 = 0;
        }

        if (x1 > surface_w) {
            x1 = surface_w;
        }

        if (y1 > surface_h) {
            y1 = surface_h;
        }

        // degenerate rect: assume alpha is present
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }

        int has_alpha = 0;

        for (int y = y0; y < y1 && !has_alpha; y++) {
            uint32_t *row = (uint32_t *)(base + (size_t)y * pitch);

            for (int x = x0; x < x1; x++) {
                uint8_t r, g, b, a;

                SDL_GetRGBA(row[x], atlas->format, &r, &g, &b, &a);

                if (a < MODEL_TEXTURE_ALPHA_THRESHOLD) {
                    has_alpha = 1;
                    break;
                }
            }
        }

        gl_texture_has_alpha[i] = (unsigned char)has_alpha;
    }

    if (locked) {
        SDL_UnlockSurface(atlas);
    }
}

int model_texture_has_alpha(int texture_index) {
    // out of range: assume alpha is present
    if (texture_index < 0 || texture_index >= MODEL_TEXTURE_MAX) {
        return 1;
    }

    return gl_texture_has_alpha[texture_index];
}
#endif
