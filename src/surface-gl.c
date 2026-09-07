#include "surface.h"

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
gl_atlas_position gl_white_atlas_position = {
    .left_u = 0.0f,
    .right_u = 1.0f / GL_TEXTURE_SIZE,
    .top_v = (GL_TEXTURE_SIZE - 1.0f) / GL_TEXTURE_SIZE,
    .bottom_v = (GL_TEXTURE_SIZE) / GL_TEXTURE_SIZE};

gl_atlas_position gl_transparent_atlas_position = {
    .left_u = 2.0f / GL_TEXTURE_SIZE,
    .right_u = 3.0f / GL_TEXTURE_SIZE,
    .top_v = (GL_TEXTURE_SIZE - 1.0f) / GL_TEXTURE_SIZE,
    .bottom_v = (GL_TEXTURE_SIZE) / GL_TEXTURE_SIZE};

// base-texture binding cache is per flush now, see surface_gl_draw

#ifdef RENDER_GL
// combined 2D atlas: 3 x 2 cells of 1024px (sprite sheet + five entity sheets)
#define GL_COMBINED_ATLAS_WIDTH 3072
#define GL_COMBINED_ATLAS_HEIGHT 2048
#define GL_COMBINED_CELL 1024

// custom-entity sheet (worn-equipment layers + no-body NPC bodies) is its own 5551 texture;
// an absent sheet returns 0 and the draw branch is skipped
static GLuint surface_gl_load_custom_entity_texture(const char *file) {
    SDL_Surface *loaded = IMG_Load(file);

    if (loaded == NULL) {
        mud_error("unable to load optional %s texture\n%s\n", file,
                  IMG_GetError());
        return 0;
    }

    SDL_Surface *image = loaded;

#ifndef SDL12
    // the Vita loader already hands back RGBA32; desktop SDL_image may not
    if (loaded->format->format != SDL_PIXELFORMAT_RGBA32) {
        image = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(loaded);

        if (image == NULL) {
            mud_error("unable to convert %s to RGBA\n", file);
            return 0;
        }
    }
#endif

    int width = image->w;
    int height = image->h;
    uint16_t *packed = malloc((size_t)width * height * sizeof(uint16_t));

    if (packed == NULL) {
        SDL_FreeSurface(image);
        return 0;
    }

    for (int y = 0; y < height; y++) {
        const uint8_t *row = (const uint8_t *)image->pixels + (size_t)y * image->pitch;
        uint16_t *out = packed + (size_t)y * width;

        for (int x = 0; x < width; x++) {
            const uint8_t *p = row + x * 4;

            // GL_UNSIGNED_SHORT_5_5_5_1: R in the top five bits, A in bit 0
            out[x] = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 3) << 6) |
                                ((p[2] >> 3) << 1) | (p[3] >= 128 ? 1 : 0));
        }
    }

    SDL_FreeSurface(image);

    GLuint texture = 0;
    gl_create_texture(&texture);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_SHORT_5_5_5_1, packed);

    free(packed);

#ifdef RENDER_GL
    GLenum upload_error = glGetError();

    if (upload_error != GL_NO_ERROR) {
        mud_error("texture upload %s GL error 0x%x\n", file,
                  (unsigned int)upload_error);
    }
#endif


    return texture;
}

// upload one source sheet into its combined-atlas cell (currently bound GL_TEXTURE_2D);
// returns 1 on success; a missing required sheet is fatal, an optional one returns 0
static int surface_gl_load_into_combined(const char *file, int x, int y,
                                         int required) {
    SDL_Surface *texture_image = IMG_Load(file);

    if (!texture_image) {
        if (required) {
            mud_error("unable to load %s texture\n%s\n", file, IMG_GetError());
            exit(1);
        }

        // an optional sheet must not fail silently: a missed load turns every custom worn layer and no-body NPC invisible
        mud_error("unable to load optional %s texture\n%s\n", file,
                  IMG_GetError());
        return 0;
    }

    // upload in <=1024-row bands
    int uploaded = 0;

    while (uploaded < texture_image->h) {
        int band = texture_image->h - uploaded;

        if (band > 1024) {
            band = 1024;
        }

        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y + uploaded, texture_image->w,
                        band, GL_RGBA, GL_UNSIGNED_BYTE,
                        (uint8_t *)texture_image->pixels +
                            (size_t)uploaded * texture_image->pitch);

        uploaded += band;
    }

#ifdef RENDER_GL
    GLenum upload_error = glGetError();

    if (upload_error != GL_NO_ERROR) {
        mud_error("texture upload %s -> combined (%d,%d) GL error 0x%x\n",
                  file, x, y, (unsigned int)upload_error);
    }
#endif

    SDL_FreeSurface(texture_image);

    return 1;
}

// remap an atlas position from a source sheet's [0,1] UV into its combined-atlas cell;
// result clamped half a texel inside the cell so a border UV can't cross into the next cell; row_span 2 = full-height cell
static gl_atlas_position surface_gl_combined_position(gl_atlas_position p,
                                                      int col, int row,
                                                      int row_span) {
    float cell_u = (float)GL_COMBINED_CELL / GL_COMBINED_ATLAS_WIDTH;
    float cell_v = (float)GL_COMBINED_CELL / GL_COMBINED_ATLAS_HEIGHT;

    float offset_u = col * cell_u;
    float offset_v = row * cell_v;
    float span_v = row_span * cell_v;

    float min_u = offset_u + 0.5f / GL_COMBINED_ATLAS_WIDTH;
    float max_u = offset_u + cell_u - 0.5f / GL_COMBINED_ATLAS_WIDTH;
    float min_v = offset_v + 0.5f / GL_COMBINED_ATLAS_HEIGHT;
    float max_v = offset_v + span_v - 0.5f / GL_COMBINED_ATLAS_HEIGHT;

#define GL_COMBINED_CLAMP(value, lo, hi)                                       \
    ((value) < (lo) ? (lo) : ((value) > (hi) ? (hi) : (value)))

    p.left_u = GL_COMBINED_CLAMP(offset_u + p.left_u * cell_u, min_u, max_u);
    p.right_u = GL_COMBINED_CLAMP(offset_u + p.right_u * cell_u, min_u, max_u);
    p.top_v = GL_COMBINED_CLAMP(offset_v + p.top_v * span_v, min_v, max_v);
    p.bottom_v =
        GL_COMBINED_CLAMP(offset_v + p.bottom_v * span_v, min_v, max_v);

#undef GL_COMBINED_CLAMP

    return p;
}

// cell coordinates of each entity sheet in the combined atlas
static const int gl_combined_entity_col[ENTITY_TEXTURE_LENGTH] = {1, 2, 2, 0,
                                                                  1};
static const int gl_combined_entity_row[ENTITY_TEXTURE_LENGTH] = {0, 0, 1, 1,
                                                                  1};
#endif // RENDER_GL

static void surface_gl_quad_new(Surface *surface, gl_quad *quad, int x, int y,
                                int width, int height);
#endif

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void surface_gl_new(Surface *surface, int width, int height, int limit,
                    mudclient *mud) {
#ifdef RENDER_GL
    surface_gl_create_framebuffer(surface);

#ifdef EMSCRIPTEN
    shader_new(&surface->gl_flat_shader, "./cache/flat.webgl.vs",
               "./cache/flat.webgl.fs");
#elif defined(__vita__)
    // Vita: precompiled GXP (offline Cg via psp2cgc), no runtime compilation
    shader_new(&surface->gl_flat_shader, "app0:/cache/flat.vs.gxp",
               "app0:/cache/flat.fs.gxp");
#elif defined(OPENGL15) || defined(OPENGL20)
    shader_new(&surface->gl_flat_shader, "./cache/flat.gl2.vs",
               "./cache/flat.gl2.fs");
#elif defined(__SWITCH__)
    shader_new(&surface->gl_flat_shader, "romfs:/flat.vs", "romfs:/flat.fs");
#else
    shader_new(&surface->gl_flat_shader, "./cache/flat.vs", "./cache/flat.fs");
#endif

    shader_use(&surface->gl_flat_shader);

    shader_set_int(&surface->gl_flat_shader, "sprite_texture", 0);
    shader_set_int(&surface->gl_flat_shader, "sprite_base_texture", 1);
#elif defined(RENDER_3DS_GL)
    surface->_3ds_gl_flat_shader_dvlb =
        DVLB_ParseFile((u32 *)flat_shbin, flat_shbin_size);

    shaderProgramInit(&surface->_3ds_gl_flat_shader);

    shaderProgramSetVsh(&surface->_3ds_gl_flat_shader,
                        &surface->_3ds_gl_flat_shader_dvlb->DVLE[0]);

    C3D_BindProgram(&surface->_3ds_gl_flat_shader);
#endif

    // the Surface is malloc'd: this embedded struct starts as garbage, and
    // vertex_buffer_gl_new's re-create check reads it
    memset(&surface->gl_flat_buffer, 0, sizeof(surface->gl_flat_buffer));

    vertex_buffer_gl_new(&surface->gl_flat_buffer, sizeof(gl_quad_vertex),
                         GL_MAX_QUADS * 4, GL_MAX_QUADS * 6);

#ifdef RENDER_GL
    // client-side staging; see the field's comment in surface.h.
    surface->gl_flat_staging = calloc(GL_MAX_QUADS, sizeof(gl_quad));
    surface->gl_flat_uploaded = 0;

    // quad->triangle index pattern (0,1,2, 0,2,3 per quad) never changes: upload the whole element buffer once;
    // 16-bit indices fit (max vertex index GL_MAX_QUADS*4 - 1 = 8191)
    {
        GLushort *static_indices = malloc(GL_MAX_QUADS * 6 * sizeof(GLushort));

        if (static_indices != NULL) {
            for (int i = 0; i < GL_MAX_QUADS; i++) {
                GLushort base = i * 4;

                static_indices[i * 6] = base;
                static_indices[i * 6 + 1] = base + 1;
                static_indices[i * 6 + 2] = base + 2;
                static_indices[i * 6 + 3] = base;
                static_indices[i * 6 + 4] = base + 2;
                static_indices[i * 6 + 5] = base + 3;
            }

            vertex_buffer_gl_bind(&surface->gl_flat_buffer);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                            GL_MAX_QUADS * 6 * sizeof(GLushort), static_indices);

            free(static_indices);
        }
    }
#endif

    int attribute_offset = 0;

    /* vertex { x, y, z } */
    vertex_buffer_gl_add_attribute(&surface->gl_flat_buffer, &attribute_offset,
                                   3);

    /* colour { r, g, b, a } */
    vertex_buffer_gl_add_attribute(&surface->gl_flat_buffer, &attribute_offset,
                                   4);

    /* greyscale texture { u, v } */
    vertex_buffer_gl_add_attribute(&surface->gl_flat_buffer, &attribute_offset,
                                   2);

    /* base texture { u, v } */
    vertex_buffer_gl_add_attribute(&surface->gl_flat_buffer, &attribute_offset,
                                   2);

#ifdef RENDER_GL
#ifdef __SWITCH__
#define GL_TEXTURE_DIR "romfs:/textures/"
#elif defined(__vita__)
#define GL_TEXTURE_DIR "app0:/cache/textures/"
#else
#define GL_TEXTURE_DIR "./cache/textures/"
#endif

    // combined 2D atlas: sprite sheet + five entity sheets in one 3072x2048 texture (1024px cells), so nearly every 2D quad batches into one draw call.
    // cell (col,row): sprites (0,0) entities_0 (1,0) entities_1 (2,0) entities_3 (0,1) entities_4 (1,1) entities_2 (2,1)
    {
        GLuint combined = 0;
        gl_create_texture(&combined);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GL_COMBINED_ATLAS_WIDTH,
                     GL_COMBINED_ATLAS_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     NULL);

        surface_gl_load_into_combined(GL_TEXTURE_DIR "sprites.png", 0, 0, 1);

        static const int entity_cell_x[ENTITY_TEXTURE_LENGTH] = {1024, 2048,
                                                                 2048, 0, 1024};
        static const int entity_cell_y[ENTITY_TEXTURE_LENGTH] = {0, 0, 1024,
                                                                 1024, 1024};

        for (int i = 0; i < ENTITY_TEXTURE_LENGTH; i++) {
            char filename[64] = {0};
            sprintf(filename, GL_TEXTURE_DIR "entities_%d.png", i);

            surface_gl_load_into_combined(filename, entity_cell_x[i],
                                          entity_cell_y[i], 1);

            surface->gl_entity_textures[i] = combined;
        }

        surface->gl_sprite_texture = combined;

        // optional custom worn-equipment layers + no-body NPC bodies; absent sheet leaves the id 0 and skips the draw branch
        surface->gl_custom_entity_texture = 0;
    }

    // separate 16-bit texture; created after the atlas so the atlas keeps the lower texture id
    surface->gl_custom_entity_texture = surface_gl_load_custom_entity_texture(
        GL_TEXTURE_DIR "custom_entities.png");

    // optional separate atlas for custom item icons (1024x512, does not fit the combined atlas); absent leaves id 0 and skips the draw branch
    surface->gl_custom_texture = 0;
    {
        const char *custom_path = GL_TEXTURE_DIR "custom_sprites.png";

        FILE *custom_fp = fopen(custom_path, "rb");
        if (custom_fp != NULL) {
            fclose(custom_fp);
            gl_load_texture(&surface->gl_custom_texture, (char *)custom_path);
        }
    }

    surface->gl_dynamic_texture_buffer =
        calloc(1024 * 1024 * 3, sizeof(uint8_t));

    gl_create_texture(&surface->gl_dynamic_texture);

    // size the storage once so updates can use glTexSubImage2D instead of a full glTexImage2D reallocation
    glBindTexture(GL_TEXTURE_2D, surface->gl_dynamic_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1024, 1024, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, NULL);
#elif defined(RENDER_3DS_GL)
    surface->_3ds_gl_projection_uniform = shaderInstanceGetUniformLocation(
        (&surface->_3ds_gl_flat_shader)->vertexShader, "projection");

    BufInfo_Add(&surface->gl_flat_buffer.buf_info, surface->gl_flat_buffer.vbo,
                sizeof(gl_quad_vertex), 4, 0x3210);

    _3ds_gl_load_tex(sprites_t3x, sprites_t3x_size,
                     &surface->gl_sprite_texture);

    _3ds_gl_load_tex(entities_0_t3x, entities_0_t3x_size,
                     &surface->gl_entity_textures[0]);

    _3ds_gl_load_tex(entities_1_t3x, entities_1_t3x_size,
                     &surface->gl_entity_textures[1]);

    _3ds_gl_load_tex(entities_2_t3x, entities_2_t3x_size,
                     &surface->gl_entity_textures[2]);

    _3ds_gl_load_tex(entities_3_t3x, entities_3_t3x_size,
                     &surface->gl_entity_textures[3]);

    _3ds_gl_load_tex(entities_4_t3x, entities_4_t3x_size,
                     &surface->gl_entity_textures[4]);

    Mtx_OrthoTilt(&surface->_3ds_gl_projection, 0.0, 320.0, 0.0, 240.0, 0.0,
                  1.0, true);
#endif

    surface_gl_reset_context(surface);
}

float surface_gl_translate_x(Surface *surface, int x) {
    return gl_translate_x(x, surface->width);
}

float surface_gl_translate_y(Surface *surface, int y) {
    return gl_translate_y(y, surface->height);
}

void surface_gl_reset_context(Surface *surface) {
    surface->gl_flat_count = 0;
    surface->gl_flat_uploaded = 0;

#ifdef RENDER_GL
    surface->gl_contexts[0].texture = surface->gl_sprite_texture;
    surface->gl_contexts[0].base_texture = surface->gl_sprite_texture;
#elif defined(RENDER_3DS_GL)
    surface->gl_contexts[0].texture = &surface->gl_sprite_texture;
    surface->gl_contexts[0].base_texture = &surface->gl_sprite_texture;
#endif

    surface->gl_contexts[0].quad_count = 0;

    surface->gl_contexts[0].min_x = surface->bounds_min_x;
    surface->gl_contexts[0].max_x = surface->bounds_max_x;
    surface->gl_contexts[0].min_y = surface->bounds_min_y;
    surface->gl_contexts[0].max_y = surface->bounds_max_y;

    surface->gl_contexts[0].use_depth = 0;

#ifdef RENDER_GL
    surface->gl_contexts[0].scissored = 0;
#endif

    surface->gl_context_count = 1;
}

static void surface_gl_quad_new(Surface *surface, gl_quad *quad, int x, int y,
                                int width, int height) {
#ifdef RENDER_GL
    float left_x = surface_gl_translate_x(surface, x);
    float right_x = surface_gl_translate_x(surface, x + width);

    float top_y = surface_gl_translate_y(surface, y);
    float bottom_y = surface_gl_translate_y(surface, y + height);
#elif defined(RENDER_3DS_GL)
    float left_x = x;
    float right_x = x + width;

    float top_y = 240 - y - height;
    float bottom_y = 240 - y;
#endif

    quad->bottom_left.x = left_x;
    quad->bottom_left.y = bottom_y;

    quad->bottom_right.x = right_x;
    quad->bottom_right.y = bottom_y;

    quad->top_right.x = right_x;
    quad->top_right.y = top_y;

    quad->top_left.x = left_x;
    quad->top_left.y = top_y;
}

void surface_gl_quad_apply_atlas(gl_quad *quad,
                                 gl_atlas_position atlas_position, int flip) {
    if (flip) {
        quad->bottom_left.u = atlas_position.right_u;
        quad->bottom_right.u = atlas_position.left_u;

        quad->top_left.u = atlas_position.right_u;
        quad->top_right.u = atlas_position.left_u;
    } else {
        quad->bottom_left.u = atlas_position.left_u;
        quad->bottom_right.u = atlas_position.right_u;

        quad->top_left.u = atlas_position.left_u;
        quad->top_right.u = atlas_position.right_u;
    }

#ifdef RENDER_GL
    quad->bottom_left.v = atlas_position.bottom_v;
    quad->bottom_right.v = atlas_position.bottom_v;

    quad->top_right.v = atlas_position.top_v;
    quad->top_left.v = atlas_position.top_v;
#elif defined(RENDER_3DS_GL)
    quad->bottom_left.v = 1.0f - atlas_position.top_v;
    quad->bottom_right.v = 1.0f - atlas_position.top_v;

    quad->top_right.v = 1.0f - atlas_position.bottom_v;
    quad->top_left.v = 1.0f - atlas_position.bottom_v;
#endif
}

void surface_gl_quad_apply_base_atlas(gl_quad *quad,
                                      gl_atlas_position atlas_position,
                                      int flip) {
    if (flip) {
        quad->bottom_left.base_u = atlas_position.right_u;
        quad->bottom_right.base_u = atlas_position.left_u;

        quad->top_left.base_u = atlas_position.right_u;
        quad->top_right.base_u = atlas_position.left_u;
    } else {
        quad->bottom_left.base_u = atlas_position.left_u;
        quad->bottom_right.base_u = atlas_position.right_u;

        quad->top_left.base_u = atlas_position.left_u;
        quad->top_right.base_u = atlas_position.right_u;
    }

#ifdef RENDER_GL
    quad->bottom_left.base_v = atlas_position.bottom_v;
    quad->bottom_right.base_v = atlas_position.bottom_v;

    quad->top_right.base_v = atlas_position.top_v;
    quad->top_left.base_v = atlas_position.top_v;
#elif defined(RENDER_3DS_GL)
    quad->bottom_left.base_v = 1.0f - atlas_position.top_v;
    quad->bottom_right.base_v = 1.0f - atlas_position.top_v;

    quad->top_right.base_v = 1.0f - atlas_position.bottom_v;
    quad->top_left.base_v = 1.0f - atlas_position.bottom_v;
#endif
}

#if defined(__vita__) && defined(RENDER_GL)
// inset an atlas cell's UV rect by half a texel per edge so GL_NEAREST never samples the shared boundary;
// insets symmetrically so it stays correct before the flip swap; ~1-texel cells collapse to centre; Vita/RENDER_GL only
static gl_atlas_position
surface_gl_inset_atlas_position(gl_atlas_position position) {
    const float inset = 0.5f / 1024.0f;

    // horizontal
    if (fabsf(position.right_u - position.left_u) > 2.0f * inset) {
        if (position.left_u <= position.right_u) {
            position.left_u += inset;
            position.right_u -= inset;
        } else {
            position.left_u -= inset;
            position.right_u += inset;
        }
    } else {
        float centre_u = (position.left_u + position.right_u) * 0.5f;
        position.left_u = centre_u;
        position.right_u = centre_u;
    }

    // vertical
    if (fabsf(position.bottom_v - position.top_v) > 2.0f * inset) {
        if (position.top_v <= position.bottom_v) {
            position.top_v += inset;
            position.bottom_v -= inset;
        } else {
            position.top_v -= inset;
            position.bottom_v += inset;
        }
    } else {
        float centre_v = (position.top_v + position.bottom_v) * 0.5f;
        position.top_v = centre_v;
        position.bottom_v = centre_v;
    }

    return position;
}

// nudge only the top+left edges of a glyph cell inward by a fraction of a texel,
// leaving bottom/right on the cell boundary; Vita/RENDER_GL only
static gl_atlas_position
surface_gl_inset_glyph_top_left(gl_atlas_position position) {
    // inset all four edges toward the cell centre by a quarter-texel
    const float nudge = (1.0f / 4.0f) / 1024.0f;

    if (position.left_u < position.right_u) {
        position.left_u += nudge;
        position.right_u -= nudge;
    } else {
        position.left_u -= nudge;
        position.right_u += nudge;
    }

    if (position.top_v < position.bottom_v) {
        position.top_v += nudge;
        position.bottom_v -= nudge;
    } else {
        position.top_v -= nudge;
        position.bottom_v += nudge;
    }

    return position;
}
#endif

#ifdef RENDER_GL
// UV rect of a captured login background sprite in the 1024x1024 atlas
gl_atlas_position surface_gl_login_atlas_position(Surface *surface,
                                                  int sprite_id) {
    int width = surface->sprite_width[sprite_id];
    int height = surface->sprite_height[sprite_id];
    int offset_x = MINIMAP_SPRITE_WIDTH;
    int offset_y = (sprite_id - surface->mud->sprite_logo) * height;

    gl_atlas_position position = {
        offset_x / 1024.0f, (offset_x + width) / 1024.0f, offset_y / 1024.0f,
        (offset_y + height) / 1024.0f};

    return position;
}
#endif

void surface_gl_vertex_apply_colour(gl_quad_vertex *vertices, int length,
                                    int colour, int alpha) {
    float r = ((colour >> 16) & 0xff) / 255.0f;
    float g = ((colour >> 8) & 0xff) / 255.0f;
    float b = (colour & 0xff) / 255.0f;
    float a = alpha / 255.0f;

    for (int i = 0; i < length; i++) {
        vertices[i].r = r;
        vertices[i].g = g;
        vertices[i].b = b;
        vertices[i].a = a;
    }
}

void surface_gl_vertex_apply_depth(gl_quad_vertex *vertices, int length,
                                   float depth) {
    for (int i = 0; i < length; i++) {
        vertices[i].z = depth;
    }
}

void gl_vertex_apply_rotation(float *x, float *y, float centre_x,
                              float centre_y, float angle) {
    *x -= centre_x;
    *y -= centre_y;

    float sine = sin(angle);
    float cosine = cos(angle);

    float x_new = (*x) * cosine - (*y) * sine;
    float y_new = (*x) * sine + (*y) * cosine;

    *x = x_new + centre_x;
    *y = y_new + centre_y;
}

#ifdef RENDER_GL
// clip an axis-aligned quad against the current surface bounds on the CPU; returns 0 when the quad is entirely outside
static int surface_gl_clip_quad_to_bounds(Surface *surface, gl_quad *quad) {
    float bx0 = surface_gl_translate_x(surface, surface->bounds_min_x);
    float bx1 = surface_gl_translate_x(surface, surface->bounds_max_x);
    float by0 = surface_gl_translate_y(surface, surface->bounds_min_y);
    float by1 = surface_gl_translate_y(surface, surface->bounds_max_y);

    // order the clip window per axis (translate_y flips the y direction)
    float cx0 = bx0 < bx1 ? bx0 : bx1;
    float cx1 = bx0 < bx1 ? bx1 : bx0;
    float cy0 = by0 < by1 ? by0 : by1;
    float cy1 = by0 < by1 ? by1 : by0;

    float qxa = quad->top_left.x, qxb = quad->top_right.x;
    float qya = quad->top_left.y, qyb = quad->bottom_left.y;

    float qx0 = qxa < qxb ? qxa : qxb, qx1 = qxa < qxb ? qxb : qxa;
    float qy0 = qya < qyb ? qya : qyb, qy1 = qya < qyb ? qyb : qya;

    // fully inside (the overwhelmingly common case): untouched
    if (qx0 >= cx0 && qx1 <= cx1 && qy0 >= cy0 && qy1 <= cy1) {
        return 1;
    }

    // fully outside: nothing of it would have been drawn
    if (qx1 <= cx0 || qx0 >= cx1 || qy1 <= cy0 || qy0 >= cy1) {
        return 0;
    }

    // partial overlap: clamp each vertex and re-derive u/v from the original edges
    gl_quad orig = *quad;

    float inv_w = (orig.top_right.x != orig.top_left.x)
                      ? 1.0f / (orig.top_right.x - orig.top_left.x)
                      : 0.0f;
    float inv_h = (orig.bottom_left.y != orig.top_left.y)
                      ? 1.0f / (orig.bottom_left.y - orig.top_left.y)
                      : 0.0f;

    gl_quad_vertex *vertices = (gl_quad_vertex *)quad;

    for (int i = 0; i < 4; i++) {
        float nx = vertices[i].x;
        float ny = vertices[i].y;

        if (nx < cx0) {
            nx = cx0;
        } else if (nx > cx1) {
            nx = cx1;
        }

        if (ny < cy0) {
            ny = cy0;
        } else if (ny > cy1) {
            ny = cy1;
        }

        if (nx != vertices[i].x) {
            float t = (nx - orig.top_left.x) * inv_w;

            vertices[i].u =
                orig.top_left.u + (orig.top_right.u - orig.top_left.u) * t;

            vertices[i].base_u =
                orig.top_left.base_u +
                (orig.top_right.base_u - orig.top_left.base_u) * t;

            vertices[i].r =
                orig.top_left.r + (orig.top_right.r - orig.top_left.r) * t;
            vertices[i].g =
                orig.top_left.g + (orig.top_right.g - orig.top_left.g) * t;
            vertices[i].b =
                orig.top_left.b + (orig.top_right.b - orig.top_left.b) * t;
            vertices[i].a =
                orig.top_left.a + (orig.top_right.a - orig.top_left.a) * t;

            vertices[i].x = nx;
        }

        if (ny != vertices[i].y) {
            float t = (ny - orig.top_left.y) * inv_h;

            vertices[i].v =
                orig.top_left.v + (orig.bottom_left.v - orig.top_left.v) * t;

            vertices[i].base_v =
                orig.top_left.base_v +
                (orig.bottom_left.base_v - orig.top_left.base_v) * t;

            // per-axis colour lerp: exact for gradient quads, no-op for constant-colour quads
            vertices[i].r =
                orig.top_left.r + (orig.bottom_left.r - orig.top_left.r) * t;
            vertices[i].g =
                orig.top_left.g + (orig.bottom_left.g - orig.top_left.g) * t;
            vertices[i].b =
                orig.top_left.b + (orig.bottom_left.b - orig.top_left.b) * t;
            vertices[i].a =
                orig.top_left.a + (orig.bottom_left.a - orig.top_left.a) * t;

            vertices[i].y = ny;
        }
    }

    return 1;
}
#endif

#ifdef RENDER_GL
void surface_gl_buffer_quad(Surface *surface, gl_quad *quad, GLuint texture,
                            GLuint base_texture) {
#elif defined(RENDER_3DS_GL)
void surface_gl_buffer_quad(Surface *surface, gl_quad *quad, C3D_Tex *texture,
                            C3D_Tex *base_texture) {
#endif
    // an overflow drops the quad; the log line is rate-limited
    static int overflow_dropped = 0;

    if (surface->gl_context_count >= GL_MAX_QUADS) {
        if ((overflow_dropped++ % 2000) == 0) {
            mud_error("too many context (texture/boundary) switches!\n");
        }
        return;
    }

    if (surface->gl_flat_count >= GL_MAX_QUADS) {
        if ((overflow_dropped++ % 2000) == 0) {
            mud_error("too many quads (%d dropped so far)!\n", overflow_dropped);
        }
        return;
    }

    int vertex_offset = surface->gl_flat_count * sizeof(gl_quad);
    int ebo_index = surface->gl_flat_count * 4;

#ifdef RENDER_GL
    (void)ebo_index;
    (void)vertex_offset;

    // axis-aligned quads (sprites, glyphs, boxes) get clipped against the current bounds here;
    // rotated and skewed quads keep the scissored path
    int axis_aligned = quad->top_left.x == quad->bottom_left.x &&
                       quad->top_right.x == quad->bottom_right.x &&
                       quad->top_left.y == quad->top_right.y &&
                       quad->bottom_left.y == quad->bottom_right.y;

    int needs_scissor = !axis_aligned;

    if (axis_aligned && !surface_gl_clip_quad_to_bounds(surface, quad)) {
        return; // fully outside the bounds
    }

    // accumulate into the client-side staging buffer only
    memcpy(&surface->gl_flat_staging[surface->gl_flat_count], quad,
           sizeof(gl_quad));

#elif defined(RENDER_3DS_GL)
    memcpy(surface->gl_flat_buffer.vbo + vertex_offset, quad, sizeof(gl_quad));

    uint16_t indices[] = {ebo_index, ebo_index + 1, ebo_index + 2,
                          ebo_index, ebo_index + 2, ebo_index + 3};

    memcpy(surface->gl_flat_buffer.ebo +
               (surface->gl_flat_count * sizeof(indices)),
           indices, sizeof(indices));
#endif

    surface->gl_flat_count++;

    int context_index = surface->gl_context_count - 1;
    SurfaceGlContext *context = &surface->gl_contexts[context_index];

#ifdef RENDER_GL
    // a context = one glDrawElements at flush; splits on a state change: texture, base texture, depth mode, or entering/leaving the scissored path
    int use_depth = quad->bottom_left.z != 0;

    if (context->use_depth == use_depth &&
        context->scissored == needs_scissor && context->texture == texture &&
        context->base_texture == base_texture &&
        (!needs_scissor || (context->min_x == surface->bounds_min_x &&
                            context->max_x == surface->bounds_max_x &&
                            context->min_y == surface->bounds_min_y &&
                            context->max_y == surface->bounds_max_y))) {
        context->quad_count++;
    } else {
        context = &surface->gl_contexts[context_index + 1];

        context->min_x = surface->bounds_min_x;
        context->max_x = surface->bounds_max_x;
        context->min_y = surface->bounds_min_y;
        context->max_y = surface->bounds_max_y;

        context->texture = texture;
        context->base_texture = base_texture;

        context->use_depth = use_depth;
        context->scissored = needs_scissor;

        context->quad_count = 1;

        surface->gl_context_count++;
    }
#else
    if (context->use_depth == 0 && quad->bottom_left.z == 0 &&
        context->min_x == surface->bounds_min_x &&
        context->max_x == surface->bounds_max_x &&
        context->min_y == surface->bounds_min_y &&
        context->max_y == surface->bounds_max_y &&
        context->texture == texture && context->base_texture == base_texture) {
        context->quad_count++;
    } else {
        context = &surface->gl_contexts[context_index + 1];

        context->min_x = surface->bounds_min_x;
        context->max_x = surface->bounds_max_x;
        context->min_y = surface->bounds_min_y;
        context->max_y = surface->bounds_max_y;

        context->texture = texture;
        context->base_texture = base_texture;

        context->use_depth = quad->bottom_left.z != 0;

        context->quad_count = 1;

        surface->gl_context_count++;
    }
#endif
}

void surface_gl_buffer_box(Surface *surface, int x, int y, int width,
                           int height, int colour, int alpha) {
    gl_quad quad = {0};

    surface_gl_quad_new(surface, &quad, x, y, width, height);

#ifdef RENDER_GL
    surface_gl_quad_apply_atlas(
        &quad, surface_gl_combined_position(gl_white_atlas_position, 0, 0, 1),
        0);
    surface_gl_quad_apply_base_atlas(
        &quad,
        surface_gl_combined_position(gl_transparent_atlas_position, 0, 0, 1),
        0);
#else
    surface_gl_quad_apply_atlas(&quad, gl_white_atlas_position, 0);
    surface_gl_quad_apply_base_atlas(&quad, gl_transparent_atlas_position, 0);
#endif
    surface_gl_vertex_apply_colour((gl_quad_vertex *)(&quad), 4, colour, alpha);

#ifdef RENDER_GL
    surface_gl_buffer_quad(surface, &quad, surface->gl_sprite_texture,
                           surface->gl_sprite_texture);
#elif defined(RENDER_3DS_GL)
    surface_gl_buffer_quad(surface, &quad, &surface->gl_sprite_texture,
                           &surface->gl_sprite_texture);
#endif
}

void surface_gl_buffer_character(Surface *surface, char character, int x, int y,
                                 int colour, int font_id, int draw_shadow,
                                 float depth) {
    if (character == ' ') {
        return;
    }

    int8_t *font_data = game_fonts[font_id];
    int char_set_index = -1;

    for (int i = 0; i < CHAR_SET_LENGTH; i++) {
        if (character == CHAR_SET[i]) {
            char_set_index = i;
            break;
        }
    }

    if (char_set_index == -1) {
        return;
    }

    int character_offset = character_width[(unsigned)CHAR_SET[char_set_index]];
    int width = font_data[character_offset + 3] + (draw_shadow ? 1 : 0);
    int height = font_data[character_offset + 4] + (draw_shadow ? 1 : 0);

    x += font_data[character_offset + 5];
    y -= font_data[character_offset + 6];

    gl_quad quad = {0};

    surface_gl_quad_new(surface, &quad, x, y, width, height);

    gl_atlas_position atlas_position =
        draw_shadow ? gl_font_shadow_atlas_positions[font_id][char_set_index]
                    : gl_font_atlas_positions[font_id][char_set_index];

#if defined(__vita__) && defined(RENDER_GL)
    // pull the top+left edges in by ~1/16 texel
    atlas_position = surface_gl_inset_glyph_top_left(atlas_position);
#endif

#ifdef RENDER_GL
    surface_gl_quad_apply_atlas(
        &quad, surface_gl_combined_position(atlas_position, 0, 0, 1), 0);
    surface_gl_quad_apply_base_atlas(
        &quad,
        surface_gl_combined_position(gl_transparent_atlas_position, 0, 0, 1),
        0);
#else
    surface_gl_quad_apply_atlas(&quad, atlas_position, 0);
    surface_gl_quad_apply_base_atlas(&quad, gl_transparent_atlas_position, 0);
#endif

    surface_gl_vertex_apply_colour((gl_quad_vertex *)(&quad), 4, colour, 255);
    surface_gl_vertex_apply_depth((gl_quad_vertex *)(&quad), 4, depth);

#ifdef RENDER_GL
    surface_gl_buffer_quad(surface, &quad, surface->gl_sprite_texture,
                           surface->gl_sprite_texture);
#elif defined(RENDER_3DS_GL)
    surface_gl_buffer_quad(surface, &quad, &surface->gl_sprite_texture,
                           &surface->gl_sprite_texture);
#endif
}

void surface_gl_buffer_sprite(Surface *surface, int sprite_id, int x, int y,
                              int scale_width, int scale_height, int skew_x,
                              int mask_colour, int skin_colour, int alpha,
                              int flip, int rotation, float depth_top,
                              float depth_bottom) {
#ifdef RENDER_GL
    GLuint texture = surface->gl_sprite_texture;
    GLuint base_texture = surface->gl_sprite_texture;
#elif defined(RENDER_3DS_GL)
    C3D_Tex *texture = &surface->gl_sprite_texture;
    C3D_Tex *base_texture = &surface->gl_sprite_texture;
#endif

    gl_atlas_position atlas_position = gl_transparent_atlas_position;
    gl_atlas_position base_atlas_position = gl_transparent_atlas_position;

#ifdef RENDER_GL
    // combined-atlas cell for each side of the quad; col -1 = handle outside the atlas, UVs stay as-is
    int tex_col = 0, tex_row = 0, tex_span = 1;
    int base_col = 0, base_row = 0, base_span = 1;
#endif

    if (sprite_id == SPRITE_LIMIT - 1) {
        atlas_position = gl_logo_atlas_position;
    } else if (sprite_id == surface->mud->sprite_logo) {
#ifdef RENDER_GL
        atlas_position =
            surface_gl_login_atlas_position(surface, sprite_id);
        texture = surface->gl_dynamic_texture;
        tex_col = -1;
#elif defined(RENDER_3DS_GL)
        atlas_position = gl_login_atlas_positions[0];
#endif
    } else if (sprite_id == surface->mud->sprite_logo + 1) {
#ifdef RENDER_GL
        atlas_position =
            surface_gl_login_atlas_position(surface, sprite_id);
        texture = surface->gl_dynamic_texture;
        tex_col = -1;
#elif defined(RENDER_3DS_GL)
        atlas_position = gl_login_atlas_positions[1];
#endif
    } else if (sprite_id == surface->mud->sprite_logo + 2) {
#ifdef RENDER_GL
        atlas_position =
            surface_gl_login_atlas_position(surface, sprite_id);
        texture = surface->gl_dynamic_texture;
        tex_col = -1;
#elif defined(RENDER_3DS_GL)
        atlas_position = gl_login_atlas_positions[2];
#endif
    } else if (sprite_id == surface->mud->sprite_media - 1) {
#ifdef RENDER_GL
        gl_atlas_position test = {
            0.0f, (float)MINIMAP_SPRITE_WIDTH / 1024.0f,
            (float)SLEEP_HEIGHT / 1024.0f,
            (float)(SLEEP_HEIGHT + MINIMAP_SPRITE_HEIGHT) / 1024.0f};

        base_texture = surface->gl_dynamic_texture;
        base_col = -1;
        base_atlas_position = test;
#elif defined(RENDER_3DS_GL)
        atlas_position = gl_map_atlas_position;
#endif
    } else if (sprite_id >= 0 && sprite_id < surface->mud->sprite_media) {
        gl_entity_texture texture_position =
            gl_entities_texture_positions[sprite_id];

        int texture_index = texture_position.texture_index;

        if (texture_index >= 0) {
#ifdef RENDER_GL
            texture = surface->gl_entity_textures[texture_index];
            tex_col = gl_combined_entity_col[texture_index];
            tex_row = gl_combined_entity_row[texture_index];
#elif defined(RENDER_3DS_GL)
            texture = &surface->gl_entity_textures[texture_index];
#endif
            atlas_position = texture_position.atlas_position;
        }

        gl_entity_texture base_texture_position =
            gl_entities_base_texture_positions[sprite_id];

        if (skin_colour != 0) {
            int skin_index = gl_get_entity_skin_index(skin_colour);
            int skin_sprite_index = gl_get_entity_sprite_index(sprite_id);

            if (skin_sprite_index != -1 && skin_index != -1) {
                base_texture_position =
                    gl_entities_skin_texture_positions[skin_index]
                                                      [skin_sprite_index];
            }
        }

        int base_texture_index = base_texture_position.texture_index;

        if (base_texture_index >= 0) {
#ifdef RENDER_GL
            base_texture = surface->gl_entity_textures[base_texture_index];
            base_col = gl_combined_entity_col[base_texture_index];
            base_row = gl_combined_entity_row[base_texture_index];
#elif defined(RENDER_3DS_GL)
            base_texture = &surface->gl_entity_textures[base_texture_index];
#endif
            base_atlas_position = base_texture_position.atlas_position;
        }
    } else if (surface->gl_custom_texture != 0 &&
               sprite_id >= surface->mud->sprite_item + GL_CUSTOM_SPRITE_BASE &&
               sprite_id < surface->mud->sprite_item + GL_CUSTOM_SPRITE_BASE +
                               GL_CUSTOM_SPRITE_COUNT) {
        // OpenRSC custom item icon: sampled from the separate custom atlas.
        int custom_index =
            sprite_id - (surface->mud->sprite_item + GL_CUSTOM_SPRITE_BASE);

        texture = surface->gl_custom_texture;
        base_texture = surface->gl_custom_texture;
        atlas_position = gl_custom_atlas_positions[custom_index];
        base_atlas_position = gl_transparent_atlas_position;
#ifdef RENDER_GL
        tex_col = -1;
        base_col = -1;
#endif
    } else if (surface->gl_custom_entity_texture != 0 &&
               sprite_id >= GL_CUSTOM_ENTITY_FILE_BASE &&
               sprite_id < GL_CUSTOM_ENTITY_FILE_BASE +
                               GL_CUSTOM_ENTITY_SLOT_COUNT) {
        // OpenRSC custom worn-equipment layer / no-body NPC body: a layered animation frame in the custom-entity range,
        // sampled from the custom-entity atlas
        int custom_entity_index = sprite_id - GL_CUSTOM_ENTITY_FILE_BASE;

        texture = surface->gl_custom_entity_texture;
        base_texture = surface->gl_custom_entity_texture;
        atlas_position = gl_custom_entity_atlas_positions[custom_entity_index];
        base_atlas_position = gl_transparent_atlas_position;
#ifdef RENDER_GL
        // its own texture: UVs are sheet-local, no combined-cell remap
        tex_col = -1;
        base_col = -1;
#endif
    } else if (sprite_id >= surface->mud->sprite_media &&
               sprite_id < surface->mud->sprite_projectile +
                               game_data.projectile_sprite) {
        int atlas_index = sprite_id - surface->mud->sprite_media;

        atlas_position = gl_media_atlas_positions[atlas_index];
        base_atlas_position = gl_media_base_atlas_positions[atlas_index];
    } else if (sprite_id == surface->mud->sprite_texture + 1) {
#ifdef RENDER_GL
        base_texture = surface->gl_dynamic_texture;
        base_col = -1;

        gl_atlas_position test = {0.0f, (float)SLEEP_WIDTH / 1024.0f, 0.0f,
                                  (float)SLEEP_HEIGHT / 1024.0f};

        base_atlas_position = test;
#elif defined(RENDER_3DS_GL)
        atlas_position = gl_sleep_atlas_position;
#else
        return;
#endif
    } else {
        return;
    }

    float ratio_x = 1.0;
    float ratio_y = 1.0;
    float width_full = surface->sprite_width_full[sprite_id];
    float height_full = surface->sprite_height_full[sprite_id];

    if (scale_width != -1) {
        ratio_x = scale_width / width_full;
        ratio_y = scale_height / height_full;
    }

    float gl_width = surface->sprite_width[sprite_id] * ratio_x;
    float gl_height = surface->sprite_height[sprite_id] * ratio_y;

    float translate_x = surface->sprite_translate_x[sprite_id];

    if (flip) {
        translate_x =
            width_full - surface->sprite_width[sprite_id] - translate_x;
    }

    translate_x *= ratio_x;

    float translate_y = surface->sprite_translate_y[sprite_id] * ratio_y;

    if (surface->sprite_translate[sprite_id]) {
        x += floorf(translate_x);
        y += floorf(translate_y);
    }

    gl_quad quad = {0};

    surface_gl_quad_new(surface, &quad, x, y, gl_width, gl_height);

    if (skew_x != 0) {
#ifdef RENDER_GL
        float top_left_skew =
            (1.0f - (translate_y / (height_full * ratio_y))) * (float)skew_x;

        float bottom_left_skew =
            (1.0f - ((translate_y + gl_height) / (height_full * ratio_y))) *
            (float)skew_x;
#elif defined(RENDER_3DS_GL)
        float bottom_left_skew =
            (1.0f - (translate_y / (height_full * ratio_y))) * (float)skew_x;

        float top_left_skew =
            (1.0f - ((translate_y + gl_height) / (height_full * ratio_y))) *
            (float)skew_x;
#endif

        quad.bottom_left.x =
            surface_gl_translate_x(surface, x + bottom_left_skew);

        quad.bottom_right.x =
            surface_gl_translate_x(surface, x + gl_width + bottom_left_skew);

        quad.top_right.x =
            surface_gl_translate_x(surface, x + gl_width + top_left_skew);

        quad.top_left.x = surface_gl_translate_x(surface, x + top_left_skew);
    } else if (rotation != 0) {
        float centre_x = (gl_width - 1) / 2.0f;
        float centre_y = (gl_height - 1) / 2.0f;
        float angle = TABLE_TO_RADIANS(-rotation, 512);

#ifdef RENDER_GL
        float points[][4] = {
            {0, gl_height},        /* bottom left */
            {gl_width, gl_height}, /* bottom right */
            {gl_width, 0},         /* top right */
            {0, 0},                /* top left */
        };

        for (int i = 0; i < 4; i++) {
            gl_quad_vertex *vertex = ((gl_quad_vertex *)(&quad) + i);

            gl_vertex_apply_rotation(&points[i][0], &points[i][1], centre_x,
                                     centre_y, angle);

            vertex->x = surface_gl_translate_x(surface, x + points[i][0]);
            vertex->y = surface_gl_translate_y(surface, y + points[i][1]);
        }
#elif defined(RENDER_3DS_GL)
        float points[][4] = {
            {gl_width, gl_height}, /* bottom right */
            {0, gl_height},        /* bottom left */
            {0, 0},                /* top left */
            {gl_width, 0},         /* top right */
        };

        for (int i = 0; i < 4; i++) {
            gl_quad_vertex *vertex = ((gl_quad_vertex *)(&quad) + i);

            gl_vertex_apply_rotation(&points[i][0], &points[i][1], centre_x,
                                     centre_y, angle);

            vertex->x = x + points[i][0];
            vertex->y = 240 - y - points[i][1];
        }
#endif
    }

#if defined(__vita__) && defined(RENDER_GL)
    // half-texel inset applied to both the greyscale and base UVs
    atlas_position = surface_gl_inset_atlas_position(atlas_position);
    base_atlas_position = surface_gl_inset_atlas_position(base_atlas_position);
#endif

#ifdef RENDER_GL
    // remap sheet-local UVs into the sheet's combined-atlas cell
    if (tex_col >= 0) {
        atlas_position = surface_gl_combined_position(atlas_position, tex_col,
                                                      tex_row, tex_span);
    }

    if (base_col >= 0) {
        base_atlas_position = surface_gl_combined_position(
            base_atlas_position, base_col, base_row, base_span);
    }
#endif

    surface_gl_quad_apply_atlas(&quad, atlas_position, flip);
    surface_gl_quad_apply_base_atlas(&quad, base_atlas_position, flip);

    /* bald head sprites - TODO magic #s */
    if (sprite_id >= 189 && sprite_id <= 216) {
        mask_colour = skin_colour;
    }

    if (mask_colour == 0) {
        mask_colour = WHITE;
    }

    surface_gl_vertex_apply_colour((gl_quad_vertex *)(&quad), 4, mask_colour,
                                   alpha);

    surface_gl_vertex_apply_depth((gl_quad_vertex *)(&quad), 2, depth_bottom);
    surface_gl_vertex_apply_depth((gl_quad_vertex *)(&quad) + 2, 2, depth_top);

    surface_gl_buffer_quad(surface, &quad, texture, base_texture);
}

void surface_gl_buffer_circle(Surface *surface, int x, int y, int radius,
                              int colour, int alpha, float depth) {
    int diameter = radius * 2;

    x -= radius;
    y -= radius;

    gl_quad quad = {0};

    surface_gl_quad_new(surface, &quad, x, y, diameter, diameter);

#ifdef RENDER_GL
    surface_gl_quad_apply_atlas(
        &quad, surface_gl_combined_position(gl_circle_atlas_position, 0, 0, 1),
        0);
    surface_gl_quad_apply_base_atlas(
        &quad,
        surface_gl_combined_position(gl_transparent_atlas_position, 0, 0, 1),
        0);
#else
    surface_gl_quad_apply_atlas(&quad, gl_circle_atlas_position, 0);
    surface_gl_quad_apply_base_atlas(&quad, gl_transparent_atlas_position, 0);
#endif

    surface_gl_vertex_apply_colour((gl_quad_vertex *)(&quad), 4, colour, alpha);
    surface_gl_vertex_apply_depth((gl_quad_vertex *)(&quad), 4, depth);

#ifdef RENDER_GL
    surface_gl_buffer_quad(surface, &quad, surface->gl_sprite_texture,
                           surface->gl_sprite_texture);
#elif defined(RENDER_3DS_GL)
    surface_gl_buffer_quad(surface, &quad, &surface->gl_sprite_texture,
                           &surface->gl_sprite_texture);
#endif
}

void surface_gl_buffer_gradient(Surface *surface, int x, int y, int width,
                                int height, int top_colour, int bottom_colour) {
    gl_quad quad = {0};

    surface_gl_quad_new(surface, &quad, x, y, width, height);

#ifdef RENDER_GL
    surface_gl_quad_apply_atlas(
        &quad, surface_gl_combined_position(gl_white_atlas_position, 0, 0, 1),
        0);
    surface_gl_quad_apply_base_atlas(
        &quad,
        surface_gl_combined_position(gl_transparent_atlas_position, 0, 0, 1),
        0);
#else
    surface_gl_quad_apply_atlas(&quad, gl_white_atlas_position, 0);
    surface_gl_quad_apply_base_atlas(&quad, gl_transparent_atlas_position, 0);
#endif

    surface_gl_vertex_apply_colour((gl_quad_vertex *)(&quad), 2, bottom_colour,
                                   255);

    surface_gl_vertex_apply_colour((gl_quad_vertex *)(&quad) + 2, 2, top_colour,
                                   255);

#ifdef RENDER_GL
    surface_gl_buffer_quad(surface, &quad, surface->gl_sprite_texture,
                           surface->gl_sprite_texture);
#elif defined(RENDER_3DS_GL)
    surface_gl_buffer_quad(surface, &quad, &surface->gl_sprite_texture,
                           &surface->gl_sprite_texture);
#endif
}

void surface_gl_blur_texture(Surface *surface, int sprite_id, int blur_height,
                             int x, int y, int height) {
#ifdef RENDER_GL
    int offset_x = MINIMAP_SPRITE_WIDTH;

    int offset_y = (sprite_id - surface->mud->sprite_logo) *
                   surface->sprite_height[surface->mud->sprite_logo];

    uint8_t *texture_data = surface->gl_dynamic_texture_buffer;
#elif defined(RENDER_3DS_GL)
    int offset_x = 0;
    int offset_y = 0;

    if (!surface_3ds_gl_get_sprite_texture_offsets(surface, sprite_id,
                                                   &offset_x, &offset_y)) {
        return;
    }

    uint16_t *texture_data = (uint16_t *)surface->gl_sprite_texture.data;
#endif

    for (int xx = x; xx < x + surface->sprite_width[sprite_id]; xx++) {
        for (int yy = y; yy < y + height; yy++) {
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 0;

            for (int x2 = xx; x2 <= xx; x2++) {
                if (x2 >= 0 && x2 < surface->sprite_width[sprite_id]) {
                    for (int y2 = yy - blur_height; y2 <= yy + blur_height;
                         y2++) {
                        if (y2 >= 0 && y2 < surface->sprite_height[sprite_id]) {
#ifdef RENDER_GL
                            int texture_offset =
                                (((offset_y + y2) * 1024) + (offset_x + x2)) *
                                3;

                            r += texture_data[texture_offset];
                            g += texture_data[texture_offset + 1];
                            b += texture_data[texture_offset + 2];
#elif defined(RENDER_3DS_GL)
                            int32_t pixel = _3ds_gl_rgba5551_to_rgb32(
                                texture_data[_3ds_gl_translate_texture_index(
                                                 x2 + offset_x, y2 + offset_y,
                                                 1024) /
                                             2]);

                            r += (pixel >> 16) & 0xff;
                            g += (pixel >> 8) & 0xff;
                            b += pixel & 0xff;
#endif
                            a++;
                        }
                    }
                }
            }

#ifdef RENDER_GL
            int texture_offset =
                (((offset_y + yy) * 1024) + (offset_x + xx)) * 3;

            texture_data[texture_offset] = r / a;
            texture_data[texture_offset + 1] = g / a;
            texture_data[texture_offset + 2] = b / a;
#elif defined(RENDER_3DS_GL)
            int texture_offset = _3ds_gl_translate_texture_index(
                                     xx + offset_x, yy + offset_y, 1024) /
                                 2;

            texture_data[texture_offset] = _3ds_gl_rgb32_to_rgba5551(
                ((r / a) << 16) + ((g / a) << 8) + (b / a));

#endif
        }
    }

#ifdef RENDER_GL
    surface_gl_update_dynamic_texture(surface);
#endif
}

void surface_gl_apply_login_filter(Surface *surface, int sprite_id) {
    for (int i = 6; i >= 1; i--) {
        surface_gl_blur_texture(surface, sprite_id, i, 0, i, 8);
    }

    int sprite_height = surface->sprite_height[sprite_id];

    for (int i = 6; i >= 1; i--) {
        surface_gl_blur_texture(surface, sprite_id, i, 0, sprite_height - 6 - i,
                                8);
    }
}

#endif

#if defined(RENDER_GL) && !defined(RENDER_3DS_GL)
void surface_gl_draw(Surface *surface, GL_DEPTH_MODE depth_mode) {
    // scissor test stays disabled except around contexts that need a rect (rotated/skewed quads, the minimap)
    glDisable(GL_CULL_FACE);

    shader_use(&surface->gl_flat_shader);

    vertex_buffer_gl_bind(&surface->gl_flat_buffer);

    // upload every quad staged since the last flush in one batch
    if (surface->gl_flat_count > surface->gl_flat_uploaded) {
        int upload_count = surface->gl_flat_count - surface->gl_flat_uploaded;

        glBufferSubData(GL_ARRAY_BUFFER,
                        surface->gl_flat_uploaded * sizeof(gl_quad),
                        upload_count * sizeof(gl_quad),
                        &surface->gl_flat_staging[surface->gl_flat_uploaded]);


        surface->gl_flat_uploaded = surface->gl_flat_count;
    }

    int drawn_quads = 0;
    int scissor_on = 0;

    GLuint last_texture = 0;

    // both bindings are re-established on the first context of every flush;
    // a static base-texture cache went stale when anything else bound unit 1
    GLuint last_base_texture = 0;

    for (int i = 0; i < surface->gl_context_count; i++) {
        SurfaceGlContext *context = &surface->gl_contexts[i];

        if (context->quad_count <= 0) {
            continue;
        }

        if (context->scissored) {
            // don't apply UI scaling to entities drawn within the world
            int is_ui_scaled =
                context->use_depth ? 0 : mudclient_is_ui_scaled(surface->mud);

            int min_y = context->min_y * (is_ui_scaled + 1);
            int max_y = context->max_y * (is_ui_scaled + 1);
            int min_x = context->min_x * (is_ui_scaled + 1);
            int max_x = context->max_x * (is_ui_scaled + 1);

            int bounds_width = max_x - min_x;
            int bounds_height = max_y - min_y;

#ifdef __vita__
            // scale the scissor rect from game pixels to the 960x544 panel;
            // while capturing into the login FBO the target is 1:1 so use unscaled coords
            if (surface->gl_capture_active) {
                glScissor(min_x,
                          surface->mud->game_height - min_y - bounds_height,
                          bounds_width, bounds_height);
            } else {
                int gw = surface->mud->game_width,
                    gh = surface->mud->game_height;
                int sx0 = min_x * 960 / gw;
                int sy0 = (gh - min_y - bounds_height) * 544 / gh;
                glScissor(sx0, sy0, bounds_width * 960 / gw,
                          bounds_height * 544 / gh);
            }
#else
            glScissor(min_x, surface->mud->game_height - min_y - bounds_height,
                      bounds_width, bounds_height);
#endif

            if (!scissor_on) {
                glEnable(GL_SCISSOR_TEST);
                scissor_on = 1;
            }
        } else if (scissor_on) {
            glDisable(GL_SCISSOR_TEST);
            scissor_on = 0;
        }

        GLuint texture = context->texture;

        if (texture != last_texture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);

            last_texture = texture;
        }

        GLuint base_texture = context->base_texture;

        if (base_texture != last_base_texture) {
            glActiveTexture(GL_TEXTURE0 + 1);
            glBindTexture(GL_TEXTURE_2D, base_texture);

            // never leave unit 1 active: every other glBindTexture assumes unit 0
            glActiveTexture(GL_TEXTURE0);

            last_base_texture = base_texture;
        }

        int quad_count = context->quad_count;

        glDrawElements(GL_TRIANGLES, quad_count * 6, GL_UNSIGNED_SHORT,
                       (void *)(drawn_quads * 6 * sizeof(GLushort)));


        drawn_quads += quad_count;
    }

    if (scissor_on) {
        glDisable(GL_SCISSOR_TEST);
    }

    if (depth_mode == GL_DEPTH_BOTH) {
        surface_gl_reset_context(surface);
    }
}

void surface_gl_raster_to_sprite(Surface *surface, int sprite_id, int x,
                                 int y, int width, int height) {
    (void)x;
    (void)y;

    glReadPixels(0, 0, surface->mud->game_width, surface->mud->game_height,
                 GL_RGBA, GL_UNSIGNED_BYTE, surface->gl_screen_pixels);

    int offset_y = (sprite_id - surface->mud->sprite_logo) * height;
    int offset_x = MINIMAP_SPRITE_WIDTH;

    // login backdrop is a banner `width` px wide captured from the centre of the game_width-wide FBO; no-op when game_width == width
    int src_x0 = (surface->mud->game_width - width) / 2;

    if (src_x0 < 0) {
        src_x0 = 0;
    }

    // glReadPixels returns bottom-up GL data, so the source row is flipped here
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            uint32_t colour =
                surface
                    ->gl_screen_pixels[(src_x0 + x) +
                                       (surface->mud->game_height - y - 1) *
                                           surface->mud->game_width];

            int texture_offset = ((offset_y + y) * 1024 + (offset_x + x)) * 3;

            surface->gl_dynamic_texture_buffer[texture_offset + 2] =
                (colour >> 16) & 255;

            surface->gl_dynamic_texture_buffer[texture_offset + 1] =
                (colour >> 8) & 255;

            surface->gl_dynamic_texture_buffer[texture_offset] = colour & 255;
        }
    }

#if defined(__vita__)
    // release the off-screen login-capture FBO so subsequent reads/draws target the display
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
#endif

    surface_gl_update_dynamic_texture(surface);
}

void surface_gl_create_framebuffer(Surface *surface) {
    free(surface->gl_screen_pixels);

    surface->gl_screen_pixels = calloc(
        surface->mud->game_width * surface->mud->game_height, sizeof(uint32_t));

#if defined(__vita__)
    // create the off-screen render target for the login background capture:
    // game_width x game_height RGBA8 colour texture + depth renderbuffer
    int gw = surface->mud->game_width;
    int gh = surface->mud->game_height;

    glGenTextures(1, &surface->gl_login_color_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surface->gl_login_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gw, gh, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenRenderbuffers(1, &surface->gl_login_depth_rb);
    glBindRenderbuffer(GL_RENDERBUFFER, surface->gl_login_depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, gw, gh);

    glGenFramebuffers(1, &surface->gl_login_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, surface->gl_login_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           surface->gl_login_color_tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, surface->gl_login_depth_rb);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        mud_error("[gl] login capture FBO incomplete!\n");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    surface->gl_capture_active = 0;
#endif
}

#if defined(__vita__)
// bind the off-screen login-capture FBO as the active draw+read target; sets gl_capture_active for 1:1 scissor coords
void surface_gl_capture_begin(Surface *surface) {
    surface->gl_capture_active = 1;

    glBindFramebuffer(GL_FRAMEBUFFER, surface->gl_login_fbo);

    glViewport(0, 0, surface->mud->game_width, surface->mud->game_height);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// end the login-capture FBO scene and make its colour texture safe to read; FBO stays bound for read
void surface_gl_capture_end(Surface *surface) {
    // keep the FBO bound for READ, switch WRITE back to the display
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // a draw op on the default write target forces the FBO scene to end
    glDisable(GL_SCISSOR_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);

    // block until the GPU has finished writing the FBO colour texture
    glFinish();

    // restore the full 960x544 panel viewport (capture_begin set it to the FBO size)
    glViewport(0, 0, 960, 544);

    surface->gl_capture_active = 0;
}
#endif

void surface_gl_update_dynamic_texture(Surface *surface) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surface->gl_dynamic_texture);

    // storage was allocated at creation; update texels in place
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1024, GL_RGB,
                    GL_UNSIGNED_BYTE, surface->gl_dynamic_texture_buffer);
}

// upload only rows [y, y+height) of the 1024x1024 dynamic texture
void surface_gl_update_dynamic_texture_rows(Surface *surface, int y,
                                            int height) {
    if (y < 0) {
        y = 0;
    }
    if (y + height > 1024) {
        height = 1024 - y;
    }
    if (height <= 0) {
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surface->gl_dynamic_texture);

    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, 1024, height, GL_RGB,
                    GL_UNSIGNED_BYTE,
                    surface->gl_dynamic_texture_buffer + (size_t)y * 1024 * 3);
}
#endif
