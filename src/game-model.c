#include "game-model.h"

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
float gl_tri_face_us[] = {0.0f, 1.0f, 0.0f};
float gl_tri_face_vs[] = {1.0f, 1.0f, 0.0f};

float gl_quad_face_us[] = {0.0f, 1.0f, 1.0f, 0.0f};
float gl_quad_face_vs[] = {1.0f, 1.0f, 0.0f, 0.0f};
#endif

static void vertex_hash_drop(GameModel *game_model);

// face index buffer: pooled slab slice or per-face malloc
static uint16_t *game_model_alloc_face_vertices(GameModel *game_model,
                                                int count) {
    if (!game_model->faces_pooled) {
        return malloc(count * sizeof(uint16_t));
    }

    if (game_model->face_pool == NULL) {
        game_model->face_pool =
            malloc((size_t)game_model->max_faces * 4 * sizeof(uint16_t));
        game_model->face_pool_used = 0;

        if (game_model->face_pool == NULL) {
            game_model->faces_pooled = 0;
            return malloc(count * sizeof(uint16_t));
        }
    }

    if (game_model->face_pool_used + count > game_model->max_faces * 4) {
        mud_error("face pool overflow\n");
        exit(1);
    }

    uint16_t *face_vertices =
        game_model->face_pool + game_model->face_pool_used;

    game_model->face_pool_used += count;

    return face_vertices;
}

static void face_pool_drop(GameModel *game_model) {
    if (game_model->face_pool != NULL) {
        free(game_model->face_pool);
        game_model->face_pool = NULL;
    }

    game_model->face_pool_used = 0;
}

void game_model_new(GameModel *game_model) {
    memset(game_model, 0, sizeof(GameModel));

    game_model->transform_state = GAME_MODEL_TRANSFORM_BEGIN;
    game_model->visible = 1;
    game_model->key = -1;
    game_model->light_ambience = 32; /* 256 is the maximum */
    game_model->light_diffuse = 512;
    game_model->light_direction_x = 180;
    game_model->light_direction_y = 155;
    game_model->light_direction_z = 95;
    game_model->light_direction_magnitude = 256;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    game_model->gl_ebo_offset = -1;
    glm_mat4_identity(game_model->transform);
#endif
}

void game_model_new_alloc(GameModel *game_model, int vertex_count,
                          int face_count) {
    game_model_new(game_model);
    game_model_allocate(game_model, vertex_count, face_count);
}

void game_model_new_merge(GameModel *game_model, GameModel **pieces,
                          int count) {
    game_model_new(game_model);
    game_model_merge(game_model, pieces, count);
}

void game_model_new_merge_flags(GameModel *game_model, GameModel **pieces,
                                int count, int autocommit, int isolated,
                                int unlit, int unpickable) {
    game_model_new(game_model);

    game_model->autocommit = autocommit;
    game_model->isolated = isolated;
    game_model->unlit = unlit;
    game_model->unpickable = unpickable;

    game_model_merge(game_model, pieces, count);
}

void game_model_new_alloc_flags(GameModel *game_model, int vertex_count,
                                int face_count, int autocommit, int isolated,
                                int unlit, int unpickable, int projected) {
    game_model_new(game_model);

    game_model->autocommit = autocommit;
    game_model->isolated = isolated;
    game_model->unlit = unlit;
    game_model->unpickable = unpickable;
    game_model->projected = projected;

    game_model_allocate(game_model, vertex_count, face_count);
}

void game_model_new_ob3(GameModel *game_model, int8_t *data, size_t len) {
    size_t offset = 0;

    game_model_new(game_model);

    int vertex_count = get_unsigned_short(data, offset, len);
    offset += 2;

    int face_count = get_unsigned_short(data, offset, len);
    offset += 2;

    game_model_allocate(game_model, vertex_count, face_count);

    for (int i = 0; i < vertex_count; i++) {
        game_model->vertex_x[i] = get_signed_short(data, offset, len);
        offset += 2;
    }

    for (int i = 0; i < vertex_count; i++) {
        game_model->vertex_y[i] = get_signed_short(data, offset, len);
        offset += 2;
    }

    for (int i = 0; i < vertex_count; i++) {
        game_model->vertex_z[i] = get_signed_short(data, offset, len);
        offset += 2;
    }

    game_model->vertex_count = vertex_count;

    for (int i = 0; i < face_count; i++) {
        game_model->face_vertex_count[i] =
            get_unsigned_byte(data, offset++, len);
    }

    for (int i = 0; i < face_count; i++) {
        game_model->face_fill_front[i] = get_signed_short(data, offset, len);
        offset += 2;

        if (game_model->face_fill_front[i] == 32767) {
            game_model->face_fill_front[i] = COLOUR_TRANSPARENT;
        }
    }

    for (int i = 0; i < face_count; i++) {
        game_model->face_fill_back[i] = get_signed_short(data, offset, len);
        offset += 2;

        if (game_model->face_fill_back[i] == 32767) {
            game_model->face_fill_back[i] = COLOUR_TRANSPARENT;
        }
    }

    for (int i = 0; i < face_count; i++) {
        int is_gouraud = get_unsigned_byte(data, offset++, len);
        game_model->face_intensity[i] = is_gouraud ? GAME_MODEL_USE_GOURAUD : 0;
    }

    for (int i = 0; i < face_count; i++) {
        game_model->face_vertices[i] =
            calloc(game_model->face_vertex_count[i], sizeof(uint16_t));

        for (int j = 0; j < game_model->face_vertex_count[i]; j++) {
            if (vertex_count < 256) {
                game_model->face_vertices[i][j] =
                    get_unsigned_byte(data, offset++, len);
            } else {
                game_model->face_vertices[i][j] =
                    get_unsigned_short(data, offset, len);

                offset += 2;
            }
        }
    }

    game_model->face_count = face_count;
}

void game_model_reset(GameModel *game_model) {
    game_model->base_x = 0;
    game_model->base_y = 0;
    game_model->base_z = 0;
    game_model->orientation_yaw = 0;
    game_model->orientation_pitch = 0;
    game_model->orientation_roll = 0;
    game_model->transform_type = 0;
}

void game_model_allocate(GameModel *game_model, int vertex_count,
                         int face_count) {
    /* each terrain, wall and roof location gets a model, even if empty */
    if (vertex_count == 0) {
        return;
    }

    vertex_hash_drop(game_model);
    face_pool_drop(game_model);

    game_model->vertex_x = calloc(vertex_count, sizeof(int16_t));
    game_model->vertex_y = calloc(vertex_count, sizeof(int16_t));
    game_model->vertex_z = calloc(vertex_count, sizeof(int16_t));
    game_model->vertex_intensity = calloc(vertex_count, sizeof(int16_t));
    game_model->vertex_ambience = calloc(vertex_count, sizeof(int8_t));
    game_model->face_vertex_count = calloc(face_count, sizeof(uint8_t));
    game_model->face_vertices = calloc(face_count, sizeof(uint16_t *));
    game_model->face_fill_front = calloc(face_count, sizeof(int16_t));
    game_model->face_fill_back = calloc(face_count, sizeof(int16_t));
    game_model->face_intensity = calloc(face_count, sizeof(int16_t));
    game_model->normal_scale = calloc(face_count, sizeof(int16_t));
    game_model->normal_magnitude = calloc(face_count, sizeof(int32_t));

    // TODO only scene->view needs this
    // #ifdef RENDER_SW
    if (!game_model->projected) {
        game_model->project_vertex_x = calloc(vertex_count, sizeof(int16_t));
        game_model->project_vertex_y = calloc(vertex_count, sizeof(int16_t));
        game_model->project_vertex_z = calloc(vertex_count, sizeof(int16_t));
        game_model->vertex_view_x = calloc(vertex_count, sizeof(int32_t));
        game_model->vertex_view_y = calloc(vertex_count, sizeof(int32_t));
    }
    // #endif

    if (!game_model->unpickable) {
        game_model->is_local_player = calloc(face_count, sizeof(int8_t));
        game_model->face_tag = calloc(face_count, sizeof(int));
    }

    if (game_model->autocommit) {
        game_model->vertex_transformed_x = game_model->vertex_x;
        game_model->vertex_transformed_y = game_model->vertex_y;
        game_model->vertex_transformed_z = game_model->vertex_z;
    } else {
        // TODO only scene->view needs this
        // #ifdef RENDER_SW
        game_model->vertex_transformed_x =
            calloc(vertex_count, sizeof(int16_t));

        game_model->vertex_transformed_y =
            calloc(vertex_count, sizeof(int16_t));

        game_model->vertex_transformed_z =
            calloc(vertex_count, sizeof(int16_t));
        // #endif
    }

    if (!game_model->unlit || !game_model->isolated) {
        game_model->face_normal_x = calloc(face_count, sizeof(int16_t));
        game_model->face_normal_y = calloc(face_count, sizeof(int16_t));
        game_model->face_normal_z = calloc(face_count, sizeof(int16_t));
    }

    game_model->face_count = 0;
    game_model->vertex_count = 0;
    game_model->max_vertices = vertex_count;
    game_model->max_faces = face_count;

    game_model_reset(game_model);
}

void game_model_projection_prepare(GameModel *game_model) {
    if (game_model->vertex_count == 0) {
        return;
    }

    game_model->project_vertex_x =
        calloc(game_model->vertex_count, sizeof(int));

    game_model->project_vertex_y =
        calloc(game_model->vertex_count, sizeof(int));

    game_model->project_vertex_z =
        calloc(game_model->vertex_count, sizeof(int));

    game_model->vertex_view_x = calloc(game_model->vertex_count, sizeof(int));
    game_model->vertex_view_y = calloc(game_model->vertex_count, sizeof(int));
}

void game_model_clear(GameModel *game_model) {
    game_model->face_count = 0;
    game_model->vertex_count = 0;

    vertex_hash_drop(game_model);
}

void game_model_reduce(GameModel *game_model, int delta_faces,
                       int delta_vertices) {
    if (game_model->face_count - delta_faces < 0) {
        delta_faces = game_model->face_count;
    }

    if (!game_model->faces_pooled) {
        for (int i = 1; i <= delta_faces; i++) {
            free(game_model->face_vertices[game_model->face_count - i]);
        }
    }

    game_model->face_count -= delta_faces;

    if (game_model->vertex_count - delta_vertices < 0) {
        delta_vertices = game_model->vertex_count;
    }

    game_model->vertex_count -= delta_vertices;

    vertex_hash_drop(game_model);
}

void game_model_merge(GameModel *game_model, GameModel **pieces, int count) {
    int face_count = 0;
    int vertex_count = 0;

    for (int i = 0; i < count; i++) {
        face_count += pieces[i]->face_count;
        vertex_count += pieces[i]->vertex_count;
    }

    game_model_allocate(game_model, vertex_count, face_count);

    for (int i = 0; i < count; i++) {
        GameModel *source = pieces[i];
        game_model_commit(source);

        game_model->light_ambience = source->light_ambience;
        game_model->light_diffuse = source->light_diffuse;
        game_model->light_direction_x = source->light_direction_x;
        game_model->light_direction_y = source->light_direction_y;
        game_model->light_direction_z = source->light_direction_z;

        game_model->light_direction_magnitude =
            source->light_direction_magnitude;

        for (int src_f = 0; src_f < source->face_count; src_f++) {
            uint16_t *dst_vs =
                calloc(source->face_vertex_count[src_f], sizeof(uint16_t));

            uint16_t *src_vs = source->face_vertices[src_f];

            for (int v = 0; v < source->face_vertex_count[src_f]; v++) {
                dst_vs[v] = game_model_vertex_at(
                    game_model, source->vertex_x[src_vs[v]],
                    source->vertex_y[src_vs[v]], source->vertex_z[src_vs[v]]);
            }

            int dst_f = game_model_create_face(
                game_model, source->face_vertex_count[src_f], dst_vs,
                source->face_fill_front[src_f], source->face_fill_back[src_f]);

            game_model->face_intensity[dst_f] = source->face_intensity[src_f];
            game_model->normal_scale[dst_f] = source->normal_scale[src_f];

            game_model->normal_magnitude[dst_f] =
                source->normal_magnitude[src_f];
        }
    }

    game_model->transform_state = GAME_MODEL_TRANSFORM_BEGIN;
}

static uint32_t vertex_hash_mix(int x, int y, int z) {
    uint32_t hash = (uint32_t)x * 0x8da6b343u;

    hash ^= (uint32_t)y * 0xd8163841u;
    hash ^= (uint32_t)z * 0xcb1ab31fu;

    return hash;
}

// keeps the first index stored for a key
static void vertex_hash_insert(GameModel *game_model, int x, int y, int z,
                               int index) {
    uint32_t mask = (uint32_t)game_model->vertex_hash_mask;
    uint32_t i = vertex_hash_mix(x, y, z) & mask;

    while (game_model->vertex_hash[i] >= 0) {
        int existing = game_model->vertex_hash[i];

        if (game_model->vertex_x[existing] == x &&
            game_model->vertex_y[existing] == y &&
            game_model->vertex_z[existing] == z) {
            return;
        }

        i = (i + 1) & mask;
    }

    game_model->vertex_hash[i] = index;
}

static void vertex_hash_build(GameModel *game_model) {
    int capacity = 16;

    // stays under half full: vertex_count never exceeds max_vertices
    while (capacity < game_model->max_vertices * 2) {
        capacity <<= 1;
    }

    game_model->vertex_hash = malloc(capacity * sizeof(int32_t));

    if (game_model->vertex_hash == NULL) {
        return; // fall back to the linear scan
    }

    game_model->vertex_hash_mask = capacity - 1;
    memset(game_model->vertex_hash, 0xff, capacity * sizeof(int32_t));

    for (int i = 0; i < game_model->vertex_count; i++) {
        vertex_hash_insert(game_model, game_model->vertex_x[i],
                           game_model->vertex_y[i], game_model->vertex_z[i], i);
    }
}

// dropped when vertex coordinates may change
static void vertex_hash_drop(GameModel *game_model) {
    if (game_model->vertex_hash != NULL) {
        free(game_model->vertex_hash);
        game_model->vertex_hash = NULL;
        game_model->vertex_hash_mask = 0;
    }
}

int game_model_vertex_at(GameModel *game_model, int x, int y, int z) {
    if (game_model->vertex_hash == NULL) {
        vertex_hash_build(game_model);
    }

    if (game_model->vertex_hash != NULL) {
        uint32_t mask = (uint32_t)game_model->vertex_hash_mask;
        uint32_t i = vertex_hash_mix(x, y, z) & mask;

        while (game_model->vertex_hash[i] >= 0) {
            int existing = game_model->vertex_hash[i];

            if (game_model->vertex_x[existing] == x &&
                game_model->vertex_y[existing] == y &&
                game_model->vertex_z[existing] == z) {
                return existing;
            }

            i = (i + 1) & mask;
        }

        return game_model_create_vertex(game_model, x, y, z);
    }

    for (int i = 0; i < game_model->vertex_count; i++) {
        if (game_model->vertex_x[i] == x && game_model->vertex_y[i] == y &&
            game_model->vertex_z[i] == z) {
            return i;
        }
    }

    return game_model_create_vertex(game_model, x, y, z);
}

int game_model_create_vertex(GameModel *game_model, int x, int y, int z) {
    if (game_model->vertex_count >= game_model->max_vertices) {
        return -1;
    }

    game_model->vertex_x[game_model->vertex_count] = x;
    game_model->vertex_y[game_model->vertex_count] = y;
    game_model->vertex_z[game_model->vertex_count] = z;

    if (game_model->vertex_hash != NULL) {
        vertex_hash_insert(game_model, x, y, z, game_model->vertex_count);
    }

    return game_model->vertex_count++;
}

int game_model_create_face(GameModel *game_model, int number,
                           uint16_t *vertices, int fill_front, int fill_back) {
    if (game_model->face_count >= game_model->max_faces) {
        return -1;
    }

    game_model->face_vertex_count[game_model->face_count] = number;
    game_model->face_vertices[game_model->face_count] = vertices;
    game_model->face_fill_front[game_model->face_count] = fill_front;
    game_model->face_fill_back[game_model->face_count] = fill_back;

    game_model->transform_state = GAME_MODEL_TRANSFORM_BEGIN;

    return game_model->face_count++;
}

void game_model_split(GameModel *game_model, GameModel **pieces, int piece_dx,
                      int piece_dz, int rows, int count, int piece_max_vertices,
                      int pickable) {
    // commit on the identity-transform world parents boils down to the lighting pass the pieces copy
    if (game_model->autocommit && game_model->transform_type == 0) {
        game_model_relight(game_model);
    } else {
        game_model_commit(game_model);
    }

    int *piece_vertex_count = calloc(count, sizeof(int));
    int *piece_face_count = calloc(count, sizeof(int));

    for (int i = 0; i < game_model->face_count; i++) {
        int sum_x = 0;
        int sum_z = 0;
        int face_vertex_count = game_model->face_vertex_count[i];
        uint16_t *vertices = game_model->face_vertices[i];

        for (int j = 0; j < face_vertex_count; j++) {
            sum_x += game_model->vertex_x[vertices[j]];
            sum_z += game_model->vertex_z[vertices[j]];
        }

        int piece_index =
            ((int)(sum_x / (face_vertex_count * piece_dx))) +
            ((int)(sum_z / (face_vertex_count * piece_dz))) * rows;

        piece_vertex_count[piece_index] += face_vertex_count;
        piece_face_count[piece_index]++;
    }

    for (int i = 0; i < count; i++) {
        if (piece_vertex_count[i] > piece_max_vertices) {
            piece_vertex_count[i] = piece_max_vertices;
        }

        pieces[i] = malloc(sizeof(GameModel));

        game_model_new_alloc_flags(pieces[i], piece_vertex_count[i],
                                   piece_face_count[i], 1, 1, 1, pickable, 1);

        // one face-index slab per piece instead of a malloc per face
        pieces[i]->faces_pooled = 1;

        pieces[i]->light_diffuse = game_model->light_diffuse;
        pieces[i]->light_ambience = game_model->light_ambience;
    }

    free(piece_vertex_count);
    free(piece_face_count);

    for (int i = 0; i < game_model->face_count; i++) {
        int sum_x = 0;
        int sum_z = 0;
        int face_vertex_count = game_model->face_vertex_count[i];
        uint16_t *vertices = game_model->face_vertices[i];

        for (int j = 0; j < face_vertex_count; j++) {
            sum_x += game_model->vertex_x[vertices[j]];
            sum_z += game_model->vertex_z[vertices[j]];
        }

        int piece_index =
            ((int)(sum_x / (face_vertex_count * piece_dx))) +
            ((int)(sum_z / (face_vertex_count * piece_dz))) * rows;

        game_model_copy_lighting(game_model, pieces[piece_index], vertices,
                                 face_vertex_count, i);
    }

    for (int i = 0; i < count; i++) {
        game_model_projection_prepare(pieces[i]);
    }
}

void game_model_copy_lighting(GameModel *game_model, GameModel *model,
                              uint16_t *src_vertices, int vertex_count,
                              int in_face) {
    uint16_t *dest_vertices =
        game_model_alloc_face_vertices(model, vertex_count);

    for (int i = 0; i < vertex_count; i++) {
        int vertex =
            game_model_vertex_at(model, game_model->vertex_x[src_vertices[i]],
                                 game_model->vertex_y[src_vertices[i]],
                                 game_model->vertex_z[src_vertices[i]]);

        dest_vertices[i] = vertex;

        model->vertex_intensity[vertex] =
            game_model->vertex_intensity[src_vertices[i]];

        model->vertex_ambience[vertex] =
            game_model->vertex_ambience[src_vertices[i]];
    }

    int out_face = game_model_create_face(model, vertex_count, dest_vertices,
                                          game_model->face_fill_front[in_face],
                                          game_model->face_fill_back[in_face]);

    if (!model->unpickable && !game_model->unpickable) {
        model->face_tag[out_face] = game_model->face_tag[in_face];
    }

    model->face_intensity[out_face] = game_model->face_intensity[in_face];
    model->normal_scale[out_face] = game_model->normal_scale[in_face];
    model->normal_magnitude[out_face] = game_model->normal_magnitude[in_face];
}

void game_model_set_light_dir(GameModel *game_model, int x, int y, int z) {
    if (game_model->unlit) {
        return;
    }

    game_model->light_direction_x = x;
    game_model->light_direction_y = y;
    game_model->light_direction_z = z;
    game_model->light_direction_magnitude = (int)sqrt(x * x + y * y + z * z);

    game_model_light(game_model);
}

void game_model_set_light_intensity(GameModel *game_model, int ambience,
                                    int diffuse, int x, int y, int z) {
    game_model->light_ambience = 256 - ambience * 4;
    game_model->light_diffuse = (64 - diffuse) * 16 + 128;

    game_model_set_light_dir(game_model, x, y, z);
}

void game_model_set_light(GameModel *game_model, int gouraud, int ambience,
                          int diffuse, int x, int y, int z) {
    if (game_model->unlit) {
        return;
    }

    for (int i = 0; i < game_model->face_count; i++) {
        game_model->face_intensity[i] = gouraud ? GAME_MODEL_USE_GOURAUD : 0;
    }

    game_model_set_light_intensity(game_model, ambience, diffuse, x, y, z);
}

void game_model_set_vertex_ambience(GameModel *game_model, int vertex_index,
                                    int ambience) {
    game_model->vertex_ambience[vertex_index] = ambience & 0xff;
}

void game_model_orient(GameModel *game_model, int yaw, int pitch, int roll) {
    game_model->orientation_yaw = yaw & 255;
    game_model->orientation_pitch = pitch & 255;
    game_model->orientation_roll = roll & 255;
    game_model_determine_transform_type(game_model);
    game_model->transform_state = GAME_MODEL_TRANSFORM_BEGIN;
}

void game_model_rotate(GameModel *game_model, int yaw, int pitch, int roll) {
    game_model_orient(game_model, game_model->orientation_yaw + yaw,
                      game_model->orientation_pitch + pitch,
                      game_model->orientation_roll + roll);
}

void game_model_place(GameModel *game_model, int x, int y, int z) {
    game_model->base_x = x;
    game_model->base_y = y;
    game_model->base_z = z;
    game_model_determine_transform_type(game_model);
    game_model->transform_state = GAME_MODEL_TRANSFORM_BEGIN;
}

void game_model_translate(GameModel *game_model, int x, int y, int z) {
    game_model_place(game_model, game_model->base_x + x, game_model->base_y + y,
                     game_model->base_z + z);
}

void game_model_determine_transform_type(GameModel *game_model) {
    if (game_model->orientation_yaw != 0 ||
        game_model->orientation_pitch != 0 ||
        game_model->orientation_roll != 0) {
        game_model->transform_type = GAME_MODEL_TRANSFORM_ROTATE;
    } else if (game_model->base_x != 0 || game_model->base_y != 0 ||
               game_model->base_z != 0) {
        game_model->transform_type = GAME_MODEL_TRANSFORM_TRANSLATE;
    } else {
        game_model->transform_type = 0;
    }
}

void game_model_apply_translate(GameModel *game_model, int dx, int dy, int dz) {
    for (int i = 0; i < game_model->vertex_count; i++) {
        game_model->vertex_transformed_x[i] += dx;
        game_model->vertex_transformed_y[i] += dy;
        game_model->vertex_transformed_z[i] += dz;
    }
}

void game_model_apply_rotation(GameModel *game_model, int yaw, int roll,
                               int pitch) {
    for (int i = 0; i < game_model->vertex_count; i++) {
        if (pitch != 0) {
            int sin = sin_cos_512[pitch];
            int cos = sin_cos_512[pitch + 256];

            int x = (game_model->vertex_transformed_y[i] * sin +
                     game_model->vertex_transformed_x[i] * cos) >>
                    15;

            game_model->vertex_transformed_y[i] =
                (game_model->vertex_transformed_y[i] * cos -
                 game_model->vertex_transformed_x[i] * sin) >>
                15;

            game_model->vertex_transformed_x[i] = x;
        }

        if (yaw != 0) {
            int sin = sin_cos_512[yaw];
            int cos = sin_cos_512[yaw + 256];

            int y = (game_model->vertex_transformed_y[i] * cos -
                     game_model->vertex_transformed_z[i] * sin) >>
                    15;

            game_model->vertex_transformed_z[i] =
                (game_model->vertex_transformed_y[i] * sin +
                 game_model->vertex_transformed_z[i] * cos) >>
                15;

            game_model->vertex_transformed_y[i] = y;
        }

        if (roll != 0) {
            int sin = sin_cos_512[roll];
            int cos = sin_cos_512[roll + 256];

            int x = (game_model->vertex_transformed_z[i] * sin +
                     game_model->vertex_transformed_x[i] * cos) >>
                    15;

            game_model->vertex_transformed_z[i] =
                (game_model->vertex_transformed_z[i] * cos -
                 game_model->vertex_transformed_x[i] * sin) >>
                15;

            game_model->vertex_transformed_x[i] = x;
        }
    }
}

void game_model_compute_bounds(GameModel *game_model) {
    game_model->min_x = 999999;
    game_model->min_y = 999999;
    game_model->min_z = 999999;
    game_model->max_x = -999999;
    game_model->max_y = -999999;
    game_model->max_z = -999999;

    for (int i = 0; i < game_model->face_count; i++) {
        uint16_t *face_vertices = game_model->face_vertices[i];
        int vertex_index = face_vertices[0];
        int face_vertex_count = game_model->face_vertex_count[i];

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        vec3 vertex = {VERTEX_TO_FLOAT(game_model->vertex_x[vertex_index]),
                       VERTEX_TO_FLOAT(game_model->vertex_y[vertex_index]),
                       VERTEX_TO_FLOAT(game_model->vertex_z[vertex_index])};

        vec3 transformed_vertex = {0};
        glm_mat4_mulv3(game_model->transform, vertex, 1, transformed_vertex);

        int min_x = FLOAT_TO_VERTEX(transformed_vertex[0]);
        int min_y = FLOAT_TO_VERTEX(transformed_vertex[1]);
        int min_z = FLOAT_TO_VERTEX(transformed_vertex[2]);
#else
        int min_x = game_model->vertex_transformed_x[vertex_index];
        int min_y = game_model->vertex_transformed_y[vertex_index];
        int min_z = game_model->vertex_transformed_z[vertex_index];
#endif
        int max_x = min_x;
        int max_y = min_y;
        int max_z = min_z;

        for (int j = 0; j < face_vertex_count; j++) {
            vertex_index = face_vertices[j];

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
            vec3 vertex = {0};
            vertex[0] = VERTEX_TO_FLOAT(game_model->vertex_x[vertex_index]);
            vertex[1] = VERTEX_TO_FLOAT(game_model->vertex_y[vertex_index]);
            vertex[2] = VERTEX_TO_FLOAT(game_model->vertex_z[vertex_index]);

            vec3 transformed_vertex = {0};

            glm_mat4_mulv3(game_model->transform, vertex, 1,
                           transformed_vertex);

            int vertex_transformed_x = FLOAT_TO_VERTEX(transformed_vertex[0]);
            int vertex_transformed_y = FLOAT_TO_VERTEX(transformed_vertex[1]);
            int vertex_transformed_z = FLOAT_TO_VERTEX(transformed_vertex[2]);
#else
            int vertex_transformed_x =
                game_model->vertex_transformed_x[vertex_index];

            int vertex_transformed_y =
                game_model->vertex_transformed_y[vertex_index];

            int vertex_transformed_z =
                game_model->vertex_transformed_z[vertex_index];
#endif

            if (vertex_transformed_x < min_x) {
                min_x = vertex_transformed_x;
            } else if (vertex_transformed_x > max_x) {
                max_x = vertex_transformed_x;
            }

            if (vertex_transformed_y < min_y) {
                min_y = vertex_transformed_y;
            } else if (vertex_transformed_y > max_y) {
                max_y = vertex_transformed_y;
            }

            if (vertex_transformed_z < min_z) {
                min_z = vertex_transformed_z;
            } else if (vertex_transformed_z > max_z) {
                max_z = vertex_transformed_z;
            }
        }

        if (min_x < game_model->min_x) {
            game_model->min_x = min_x;
        }

        if (max_x > game_model->max_x) {
            game_model->max_x = max_x;
        }

        if (min_y < game_model->min_y) {
            game_model->min_y = min_y;
        }

        if (max_y > game_model->max_y) {
            game_model->max_y = max_y;
        }

        if (min_z < game_model->min_z) {
            game_model->min_z = min_z;
        }

        if (max_z > game_model->max_z) {
            game_model->max_z = max_z;
        }
    }
}

void game_model_get_face_normals(GameModel *game_model, int16_t *vertex_x,
                                 int16_t *vertex_y, int16_t *vertex_z,
                                 int16_t *face_normal_x, int16_t *face_normal_y,
                                 int16_t *face_normal_z, int reset_scale) {
    for (int i = 0; i < game_model->face_count; i++) {
        uint16_t *face_vertices = game_model->face_vertices[i];

        int vertex_x_a = vertex_x[face_vertices[0]];
        int vertex_y_a = vertex_y[face_vertices[0]];
        int vertex_z_a = vertex_z[face_vertices[0]];
        int vertex_x_delta_ba = vertex_x[face_vertices[1]] - vertex_x_a;
        int vertex_y_delta_ba = vertex_y[face_vertices[1]] - vertex_y_a;
        int vertex_z_delta_ba = vertex_z[face_vertices[1]] - vertex_z_a;
        int vertex_x_delta_ca = vertex_x[face_vertices[2]] - vertex_x_a;
        int vertex_y_delta_ca = vertex_y[face_vertices[2]] - vertex_y_a;
        int vertex_z_delta_ca = vertex_z[face_vertices[2]] - vertex_z_a;

        int normal_x = vertex_y_delta_ba * vertex_z_delta_ca -
                       vertex_y_delta_ca * vertex_z_delta_ba;

        int normal_y = vertex_z_delta_ba * vertex_x_delta_ca -
                       vertex_z_delta_ca * vertex_x_delta_ba;

        int normal_z = 0;

        for (normal_z = vertex_x_delta_ba * vertex_y_delta_ca -
                        vertex_x_delta_ca * vertex_y_delta_ba;
             normal_x > 8192 || normal_y > 8192 || normal_z > 8192 ||
             normal_x < -8192 || normal_y < -8192 || normal_z < -8192;
             normal_z /= 2) {
            normal_x /= 2;
            normal_y /= 2;
        }

        int normal_magnitude =
            256 * sqrt(normal_x * normal_x + normal_y * normal_y +
                       normal_z * normal_z);

        if (normal_magnitude <= 0) {
            normal_magnitude = 1;
        }

        // << 16
        face_normal_x[i] = (normal_x * 65536) / normal_magnitude;
        face_normal_y[i] = (normal_y * 65536) / normal_magnitude;
        face_normal_z[i] = (normal_z * 65535) / normal_magnitude;

        if (reset_scale) {
            game_model->normal_scale[i] = -1;
        }
    }
}

void game_model_get_vertex_normals(GameModel *game_model,
                                   int16_t *face_normal_x,
                                   int16_t *face_normal_y,
                                   int16_t *face_normal_z, int16_t *normal_x,
                                   int16_t *normal_y, int16_t *normal_z,
                                   int32_t *normal_magnitude) {
    for (int i = 0; i < game_model->face_count; i++) {
        if (game_model->face_intensity[i] == GAME_MODEL_USE_GOURAUD) {
            for (int j = 0; j < game_model->face_vertex_count[i]; j++) {
                uint16_t vertex_index = game_model->face_vertices[i][j];

                normal_x[vertex_index] += face_normal_x[i];
                normal_y[vertex_index] += face_normal_y[i];
                normal_z[vertex_index] += face_normal_z[i];

                normal_magnitude[vertex_index]++;
            }
        }
    }
}

void game_model_light(GameModel *game_model) {
    if (game_model->unlit) {
        return;
    }

    int divisor =
        (game_model->light_diffuse * game_model->light_direction_magnitude) >>
        8; // >> 8 is / 256

    for (int i = 0; i < game_model->face_count; i++) {
        if (game_model->face_intensity[i] != GAME_MODEL_USE_GOURAUD) {
            game_model->face_intensity[i] =
                (game_model->face_normal_x[i] * game_model->light_direction_x +
                 game_model->face_normal_y[i] * game_model->light_direction_y +
                 game_model->face_normal_z[i] * game_model->light_direction_z) /
                divisor;
        }
    }

    // TODO we could probably re-use a global variable for this instead of
    // allocating and freeing so many arrays
    int16_t *normal_x = calloc(game_model->vertex_count, sizeof(int16_t));
    int16_t *normal_y = calloc(game_model->vertex_count, sizeof(int16_t));
    int16_t *normal_z = calloc(game_model->vertex_count, sizeof(int16_t));

    int32_t *normal_magnitude =
        calloc(game_model->vertex_count, sizeof(int32_t));

    game_model_get_vertex_normals(game_model, game_model->face_normal_x,
                                  game_model->face_normal_y,
                                  game_model->face_normal_z, normal_x, normal_y,
                                  normal_z, normal_magnitude);

    for (int i = 0; i < game_model->vertex_count; i++) {
        if (normal_magnitude[i] > 0) {
            game_model->vertex_intensity[i] =
                (normal_x[i] * game_model->light_direction_x +
                 normal_y[i] * game_model->light_direction_y +
                 normal_z[i] * game_model->light_direction_z) /
                (divisor * normal_magnitude[i]);
        }
    }

    free(normal_x);
    free(normal_y);
    free(normal_z);
    free(normal_magnitude);
}

void game_model_relight(GameModel *game_model) {
    if (game_model->unlit && game_model->isolated) {
        return;
    }

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    if (!game_model->autocommit) {
        return;
    }
#endif

    game_model_get_face_normals(
        game_model, game_model->vertex_transformed_x,
        game_model->vertex_transformed_y, game_model->vertex_transformed_z,
        game_model->face_normal_x, game_model->face_normal_y,
        game_model->face_normal_z, 1);

    game_model_light(game_model);
}

void game_model_reset_transform(GameModel *game_model) {
    game_model->transform_state = 0;

    // TODO only scene->view needs this
    // #ifdef RENDER_SW
    if (!game_model->autocommit) {
        memcpy(game_model->vertex_transformed_x, game_model->vertex_x,
               game_model->vertex_count * sizeof(int16_t));

        memcpy(game_model->vertex_transformed_y, game_model->vertex_y,
               game_model->vertex_count * sizeof(int16_t));

        memcpy(game_model->vertex_transformed_z, game_model->vertex_z,
               game_model->vertex_count * sizeof(int16_t));
    }
    // #endif

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    glm_mat4_identity(game_model->transform);
#endif
}

void game_model_apply(GameModel *game_model) {
    // transforms can rewrite the vertex arrays, so any cached (x,y,z) index goes stale
    vertex_hash_drop(game_model);

    if (game_model->transform_state == GAME_MODEL_TRANSFORM_RESET) {
        game_model_reset_transform(game_model);

        game_model->min_x = -9999999;
        game_model->min_y = -9999999;
        game_model->min_z = -9999999;
        game_model->max_x = 9999999;
        game_model->max_y = 9999999;
        game_model->max_z = 9999999;
    } else if (game_model->transform_state == GAME_MODEL_TRANSFORM_BEGIN) {
        game_model_reset_transform(game_model);

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        if (game_model->transform_type >= GAME_MODEL_TRANSFORM_TRANSLATE) {
            glm_translate(game_model->transform,
                          (vec3){VERTEX_TO_FLOAT(game_model->base_x),
                                 VERTEX_TO_FLOAT(game_model->base_y),
                                 VERTEX_TO_FLOAT(game_model->base_z)});
        }

        if (game_model->transform_type >= GAME_MODEL_TRANSFORM_ROTATE) {
            glm_rotate(game_model->transform,
                       TABLE_TO_RADIANS(game_model->orientation_pitch, 512),
                       (vec3){0.0f, 1.0f, 0.0f});

            glm_rotate(game_model->transform,
                       TABLE_TO_RADIANS(game_model->orientation_yaw, 512),
                       (vec3){1.0f, 0.0f, 0.0f});

            glm_rotate(game_model->transform,
                       TABLE_TO_RADIANS(game_model->orientation_roll, 512),
                       (vec3){0.0f, 0.0f, -1.0f});
        }

        /* fixes the z-fighting with the walls. check if faces > 1 so it
         * doesn't break wallobjects */
        if (!game_model->autocommit && game_model->face_count > 1) {
            glm_scale_uni(game_model->transform, 0.990f);
        }
#endif

        // TODO only scene->view needs this
        // #ifdef RENDER_SW
        if (game_model->transform_type >= GAME_MODEL_TRANSFORM_ROTATE) {
            game_model_apply_rotation(game_model, game_model->orientation_yaw,
                                      game_model->orientation_pitch,
                                      game_model->orientation_roll);
        }

        if (game_model->transform_type >= GAME_MODEL_TRANSFORM_TRANSLATE) {
            game_model_apply_translate(game_model, game_model->base_x,
                                       game_model->base_y, game_model->base_z);
        }
        // #endif

        game_model_compute_bounds(game_model);

        game_model_relight(game_model);
    }
}

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
// build the world-space transform matrix from game_model_apply's BEGIN case without mutating the model; already-baked
// models keep their identity transform
void game_model_gl_bake_transform(GameModel *game_model, mat4 out) {
    glm_mat4_identity(out);

    if (game_model->transform_state != GAME_MODEL_TRANSFORM_BEGIN) {
        return;
    }

    if (game_model->transform_type >= GAME_MODEL_TRANSFORM_TRANSLATE) {
        glm_translate(out, (vec3){VERTEX_TO_FLOAT(game_model->base_x),
                                  VERTEX_TO_FLOAT(game_model->base_y),
                                  VERTEX_TO_FLOAT(game_model->base_z)});
    }

    if (game_model->transform_type >= GAME_MODEL_TRANSFORM_ROTATE) {
        glm_rotate(out, TABLE_TO_RADIANS(game_model->orientation_pitch, 512),
                   (vec3){0.0f, 1.0f, 0.0f});
        glm_rotate(out, TABLE_TO_RADIANS(game_model->orientation_yaw, 512),
                   (vec3){1.0f, 0.0f, 0.0f});
        glm_rotate(out, TABLE_TO_RADIANS(game_model->orientation_roll, 512),
                   (vec3){0.0f, 0.0f, -1.0f});
    }

    if (!game_model->autocommit && game_model->face_count > 1) {
        glm_scale_uni(out, 0.990f);
    }
}
#endif

void game_model_project_view(GameModel *game_model, int camera_x, int camera_y,
                             int camera_z, int camera_pitch, int camera_roll,
                             int camera_yaw, int view_distance, int clip_near) {
    int yaw_sin = 0;
    int yaw_cos = 0;
    int pitch_sin = 0;
    int pitch_cos = 0;
    int roll_sin = 0;
    int roll_cos = 0;

    if (camera_yaw != 0) {
        yaw_sin = sin_cos_2048[camera_yaw];
        yaw_cos = sin_cos_2048[camera_yaw + 1024];
    }

    if (camera_roll != 0) {
        roll_sin = sin_cos_2048[camera_roll];
        roll_cos = sin_cos_2048[camera_roll + 1024];
    }

    if (camera_pitch != 0) {
        pitch_sin = sin_cos_2048[camera_pitch];
        pitch_cos = sin_cos_2048[camera_pitch + 1024];
    }

    for (int i = 0; i < game_model->vertex_count; i++) {
        int x = game_model->vertex_transformed_x[i] - camera_x;
        int y = game_model->vertex_transformed_y[i] - camera_y;
        int z = game_model->vertex_transformed_z[i] - camera_z;

        // TODO can probably be commented out since yaw isn't used.
        if (camera_yaw != 0) {
            int X = (y * yaw_sin + x * yaw_cos) >> 15;
            y = (y * yaw_cos - x * yaw_sin) >> 15;
            x = X;
        }

        if (camera_roll != 0) {
            int X = (z * roll_sin + x * roll_cos) >> 15;
            z = (z * roll_cos - x * roll_sin) >> 15;
            x = X;
        }

        if (camera_pitch != 0) {
            int Y = (y * pitch_cos - z * pitch_sin) >> 15;
            z = (y * pitch_sin + z * pitch_cos) >> 15;
            y = Y;
        }

        if (z >= clip_near) {
            // game_model->vertex_view_x[i] = (int)((x << view_distance) / z);
            // game_model->vertex_view_y[i] = (int)((y << view_distance) / z);
            game_model->vertex_view_x[i] = (x * view_distance) / z;
            game_model->vertex_view_y[i] = (y * view_distance) / z;
        } else {
            // game_model->vertex_view_x[i] = x << view_distance;
            // game_model->vertex_view_y[i] = y << view_distance;
            game_model->vertex_view_x[i] = x * view_distance;
            game_model->vertex_view_y[i] = y * view_distance;
        }

        game_model->project_vertex_x[i] = x;
        game_model->project_vertex_y[i] = y;
        game_model->project_vertex_z[i] = z;
    }
}

void game_model_project(GameModel *game_model, int camera_x, int camera_y,
                        int camera_z, int camera_pitch, int camera_roll,
                        int camera_yaw, int view_distance, int clip_near) {
    game_model_apply(game_model);

    if (game_model->min_z > scene_frustum_near_z ||
        game_model->max_z < scene_frustum_far_z ||
        game_model->min_x > scene_frustum_min_x ||
        game_model->max_x < scene_frustum_max_x ||
        game_model->min_y > scene_frustum_min_y ||
        game_model->max_y < scene_frustum_max_y) {
        game_model->visible = 0;
        return;
    }

    game_model->visible = 1;

#ifdef RENDER_SW
    game_model_project_view(game_model, camera_x, camera_y, camera_z,
                            camera_pitch, camera_roll, camera_yaw,
                            view_distance, clip_near);
#else
    (void)camera_x;
    (void)camera_y;
    (void)camera_z;
    (void)camera_pitch;
    (void)camera_roll;
    (void)camera_yaw;
    (void)view_distance;
    (void)clip_near;
#endif
}

void game_model_commit(GameModel *game_model) {
    game_model_apply(game_model);

    // TODO only scene->view needs this
    // #ifdef RENDER_SW
    if (!game_model->autocommit) {
        memcpy(game_model->vertex_x, game_model->vertex_transformed_x,
               game_model->vertex_count * sizeof(int16_t));
        memcpy(game_model->vertex_y, game_model->vertex_transformed_y,
               game_model->vertex_count * sizeof(int16_t));
        memcpy(game_model->vertex_z, game_model->vertex_transformed_z,
               game_model->vertex_count * sizeof(int16_t));
    }
    // #endif

    game_model_reset(game_model);
}

GameModel *game_model_copy(GameModel *game_model) {
    GameModel *copy = calloc(1, sizeof(GameModel));

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    copy->vertex_count = game_model->vertex_count;
    copy->face_count = game_model->face_count;

    copy->vertex_x = game_model->vertex_x;
    copy->vertex_y = game_model->vertex_y;
    copy->vertex_z = game_model->vertex_z;

    copy->vertex_intensity = game_model->vertex_intensity;
    copy->vertex_ambience = game_model->vertex_ambience;
    copy->face_vertex_count = game_model->face_vertex_count;
    copy->face_vertices = game_model->face_vertices;
    copy->face_fill_front = game_model->face_fill_front;
    copy->face_fill_back = game_model->face_fill_back;
    copy->face_intensity = game_model->face_intensity;
    copy->normal_scale = game_model->normal_scale;
    copy->normal_magnitude = game_model->normal_magnitude;

    copy->light_ambience = game_model->light_ambience;
    copy->light_diffuse = game_model->light_diffuse;
    copy->light_direction_x = game_model->light_direction_x;
    copy->light_direction_y = game_model->light_direction_y;
    copy->light_direction_z = game_model->light_direction_z;

    copy->face_normal_x = game_model->face_normal_x;
    copy->face_normal_y = game_model->face_normal_y;
    copy->face_normal_z = game_model->face_normal_z;

    // TODO remove?
    copy->vertex_transformed_x = game_model->vertex_transformed_x;
    copy->vertex_transformed_y = game_model->vertex_transformed_y;
    copy->vertex_transformed_z = game_model->vertex_transformed_z;

    copy->light_direction_magnitude = game_model->light_direction_magnitude;

    copy->gl_ebo_offset = game_model->gl_ebo_offset;
    copy->gl_ebo_length = game_model->gl_ebo_length;
    copy->gl_noclip_ebo_length = game_model->gl_noclip_ebo_length;
    copy->gl_clip_ebo_offset = game_model->gl_clip_ebo_offset;
    copy->gl_clip_ebo_length = game_model->gl_clip_ebo_length;
    copy->gl_buffer = game_model->gl_buffer;
#else
    GameModel **pieces = malloc(sizeof(GameModel *));
    pieces[0] = game_model;

    game_model_new_merge(copy, pieces, 1);

    copy->depth = game_model->depth;

    free(pieces);
#endif

    copy->transparent = game_model->transparent;

    return copy;
}

GameModel *game_model_copy_flags(GameModel *game_model, int autocommit,
                                 int isolated, int unlit, int pickable) {
    GameModel **pieces = malloc(sizeof(GameModel *));
    pieces[0] = game_model;

    GameModel *copy = malloc(sizeof(GameModel));
    game_model_new_merge_flags(copy, pieces, 1, autocommit, isolated, unlit,
                               pickable);

    copy->depth = game_model->depth;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    copy->gl_vbo_offset = game_model->gl_vbo_offset;
    copy->gl_ebo_offset = game_model->gl_ebo_offset;
    copy->gl_ebo_length = game_model->gl_ebo_length;
    copy->gl_noclip_ebo_length = game_model->gl_noclip_ebo_length;
    copy->gl_clip_ebo_offset = game_model->gl_clip_ebo_offset;
    copy->gl_clip_ebo_length = game_model->gl_clip_ebo_length;
    copy->gl_buffer = game_model->gl_buffer;
#endif

    free(pieces);

    return copy;
}

void game_model_copy_position(GameModel *game_model, GameModel *source) {
    game_model->orientation_yaw = source->orientation_yaw;
    game_model->orientation_pitch = source->orientation_pitch;
    game_model->orientation_roll = source->orientation_roll;
    game_model->base_x = source->base_x;
    game_model->base_y = source->base_y;
    game_model->base_z = source->base_z;

    game_model_determine_transform_type(game_model);

    game_model->transform_state = GAME_MODEL_TRANSFORM_BEGIN;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    glm_mat4_copy(source->transform, game_model->transform);
#endif
}

void game_model_destroy(GameModel *game_model) {
    if (game_model == NULL) {
        return;
    }

    vertex_hash_drop(game_model);

    game_model->vertex_count = 0;

    if (game_model->faces_pooled) {
        for (int i = 0; i < game_model->face_count; i++) {
            game_model->face_vertices[i] = NULL;
        }

        face_pool_drop(game_model);
        game_model->faces_pooled = 0;
    } else {
        for (int i = 0; i < game_model->face_count; i++) {
            free(game_model->face_vertices[i]);
            game_model->face_vertices[i] = NULL;
        }
    }

    game_model->face_count = 0;

    free(game_model->face_vertices);
    game_model->face_vertices = NULL;

    if (game_model->vertex_x != game_model->vertex_transformed_x) {
        free(game_model->vertex_transformed_x);
    }

    free(game_model->vertex_x);
    game_model->vertex_x = NULL;
    game_model->vertex_transformed_x = NULL;

    if (game_model->vertex_y != game_model->vertex_transformed_y) {
        free(game_model->vertex_transformed_y);
    }

    free(game_model->vertex_y);
    game_model->vertex_y = NULL;
    game_model->vertex_transformed_y = NULL;

    if (game_model->vertex_z != game_model->vertex_transformed_z) {
        free(game_model->vertex_transformed_z);
    }

    free(game_model->vertex_z);
    game_model->vertex_z = NULL;
    game_model->vertex_transformed_z = NULL;

    free(game_model->vertex_intensity);
    game_model->vertex_intensity = NULL;

    free(game_model->vertex_ambience);
    game_model->vertex_ambience = NULL;

    free(game_model->face_vertex_count);
    game_model->face_vertex_count = NULL;

    free(game_model->face_vertices);
    game_model->face_vertices = NULL;

    free(game_model->face_fill_front);
    game_model->face_fill_front = NULL;

    free(game_model->face_fill_back);
    game_model->face_fill_back = NULL;

    free(game_model->face_intensity);
    game_model->face_intensity = NULL;

    free(game_model->normal_scale);
    game_model->normal_scale = NULL;

    free(game_model->normal_magnitude);
    game_model->normal_magnitude = NULL;

    free(game_model->project_vertex_x);
    game_model->project_vertex_x = NULL;

    free(game_model->project_vertex_y);
    game_model->project_vertex_y = NULL;

    free(game_model->project_vertex_z);
    game_model->project_vertex_z = NULL;

    free(game_model->vertex_view_x);
    game_model->vertex_view_x = NULL;

    free(game_model->vertex_view_y);
    game_model->vertex_view_y = NULL;

    free(game_model->is_local_player);
    game_model->is_local_player = NULL;

    free(game_model->face_tag);
    game_model->face_tag = NULL;

    free(game_model->face_normal_x);
    game_model->face_normal_x = NULL;

    free(game_model->face_normal_y);
    game_model->face_normal_y = NULL;

    free(game_model->face_normal_z);
    game_model->face_normal_z = NULL;
}

void game_model_dump(GameModel *game_model, char *file_name) {
    char name[255];

    sprintf(name, "./dump-%s.obj", file_name);

    FILE *obj_file = fopen(name, "w");

    for (int i = 0; i < game_model->vertex_count; i++) {
        float vertex_x = (((float)game_model->vertex_x[i]) / 1000.0f);
        float vertex_y = ((float)game_model->vertex_y[i]) / 1000.0f;
        float vertex_z = (((float)game_model->vertex_z[i]) / 1000.0f);

        fprintf(obj_file, "v %f %f %f\n", vertex_x, -vertex_y, vertex_z);
    }

    for (int i = 0; i < game_model->face_count; i++) {
        fprintf(obj_file, "f ");

        for (int j = 0; j < game_model->face_vertex_count[i]; j++) {
            fprintf(obj_file, "%d ", game_model->face_vertices[i][j] + 1);
        }

        fprintf(obj_file, "\n");
    }

    fclose(obj_file);
}

/* use the sprite masking technique on model faces */
void game_model_mask_faces(GameModel *game_model, int16_t *face_fill,
                           int mask_colour) {
    for (int j = 0; j < game_model->face_count; j++) {
        int fill_colour = -1 - face_fill[j];
        int r = ((fill_colour >> 10) & 31) * 8;
        int g = ((fill_colour >> 5) & 31) * 8;
        int b = (fill_colour & 31) * 8;

        if (r == g && g == b) {
            float mask_r = ((mask_colour >> 16) & 0xff) / 255.0f;
            float mask_g = ((mask_colour >> 8) & 0xff) / 255.0f;
            float mask_b = (mask_colour & 0xff) / 255.0f;

            int new_r = (int)((float)r * mask_r) & 0xff;
            int new_g = (int)((float)g * mask_g) & 0xff;
            int new_b = (int)((float)b * mask_b) & 0xff;

            face_fill[j] =
                -(((new_r / 8) << 10) | ((new_g / 8) << 5) | (new_b / 8)) - 1;
        }
    }
}

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void game_model_gl_decode_face_fill(int face_fill,
                                    gl_face_fill *vbo_face_fill) {
    vbo_face_fill->r = 1.0f;
    vbo_face_fill->g = 1.0f;
    vbo_face_fill->b = 1.0f;

    vbo_face_fill->texture_index = -1;

    if (face_fill != COLOUR_TRANSPARENT) {
        if (face_fill < 0) {
            face_fill = -1 - face_fill;

            vbo_face_fill->r = (((face_fill >> 10) & 31) * 8) / 255.0f;
            vbo_face_fill->g = (((face_fill >> 5) & 31) * 8) / 255.0f;
            vbo_face_fill->b = ((face_fill & 31) * 8) / 255.0f;
        } else if (face_fill >= 0) {
            vbo_face_fill->texture_index = face_fill;
        }
    }
}

void game_model_gl_unwrap_uvs(GameModel *game_model, uint16_t *face_vertices,
                              int face_vertex_count, float *us, float *vs) {
    if (face_vertex_count <= 4) {
        float *face_us = NULL;
        float *face_vs = NULL;

        if (face_vertex_count == 3) {
            face_us = gl_tri_face_us;
            face_vs = gl_tri_face_vs;
        } else if (face_vertex_count == 4) {
            face_us = gl_quad_face_us;
            face_vs = gl_quad_face_vs;
        }

        for (int i = 0; i < face_vertex_count; i++) {
            us[i] = face_us[i];
#ifdef RENDER_3DS_GL
            vs[i] = 1.0f - face_vs[i];
#else
            vs[i] = face_vs[i];
#endif
        }

        return;
    }

    vec3 vertices[face_vertex_count];

    for (int i = 0; i < face_vertex_count; i++) {
        uint16_t vertex_index = face_vertices[i];

        vertices[i][0] = VERTEX_TO_FLOAT(game_model->vertex_x[vertex_index]);
        vertices[i][1] = VERTEX_TO_FLOAT(game_model->vertex_y[vertex_index]);
        vertices[i][2] = VERTEX_TO_FLOAT(game_model->vertex_z[vertex_index]);
    }

    vec3 location_x = {0};
    glm_vec3_sub(vertices[1], vertices[0], location_x);

    vec3 delta = {0};
    glm_vec3_sub(vertices[2], vertices[0], delta);

    // TODO get face normal
    vec3 normal = {0};
    glm_vec3_cross(location_x, delta, normal);

    vec3 location_y = {0};
    glm_vec3_cross(normal, location_x, location_y);

    glm_vec3_normalize(location_x);
    glm_vec3_normalize(location_y);

    float max_x = 0;
    float min_x = 0;

    float max_y = 0;
    float min_y = 0;

    for (int i = 0; i < face_vertex_count; i++) {
        vec3 vertex = {0};

        glm_vec3_sub(vertices[i], vertices[0], vertex);

        float x = glm_vec3_dot(vertex, location_x);
        float y = glm_vec3_dot(vertex, location_y);

        if (i == 0 || x > max_x) {
            max_x = x;
        }

        if (i == 0 || x < min_x) {
            min_x = x;
        }

        if (i == 0 || y > max_y) {
            max_y = y;
        }

        if (i == 0 || y < min_y) {
            min_y = y;
        }

        us[i] = x;
        vs[i] = y;
    }

    for (int i = 0; i < face_vertex_count; i++) {
        float x = us[i];
        float y = vs[i];

        us[i] = (x - min_x) / (max_x - min_x);
#ifdef RENDER_3DS_GL
        vs[i] = (y - min_y) / (max_y - min_y);
#else
        vs[i] = 1.0f - (y - min_y) / (max_y - min_y);
#endif
    }
}

/* offset UVs for atlas */
void gl_offset_texture_uvs_atlas(gl_atlas_position texture_position,
                                 float *texture_x, float *texture_y) {
    float texture_width =
        fabs(texture_position.left_u - texture_position.right_u);

    float texture_height =
        fabs(texture_position.top_v - texture_position.bottom_v);

    /* for fountain texture */
    if (texture_width * 1024 == 64 && texture_height * 1024 == 128) {
        texture_height /= 2;
    }

    // inset the atlas UVs by half a texel so NEAREST sampling never rounds onto an adjacent cell
    const float inset = 0.5f / 1024.0f;

    *texture_x = texture_position.left_u + inset +
                 (*texture_x) * (texture_width - 2.0f * inset);

#ifdef RENDER_GL
    *texture_y = texture_position.top_v + inset +
                 (*texture_y) * (texture_height - 2.0f * inset);
#elif defined(RENDER_3DS_GL)
    *texture_y = (1.0f - texture_position.bottom_v) + inset +
                 (*texture_y) * (texture_height - 2.0f * inset);
#endif
}

/* add a game model to VBO and EBO arrays at the specified offsets, then update
 * those offsets to new ones */
// classify a model's shader route and pass structure: sidedness flags, gl_all_flat, and on Vita the noclip-safety of
// the model and each face. returns the count of EBO indices the noclip-safe faces emit (0 off-Vita and for transparent models)
int game_model_gl_classify(GameModel *game_model, int *face_noclip) {
    int noclip_index_count = 0;

    // classify the model as one-sided: if no face has a visible front draw back only, symmetrically for front;
    // mixed/two-sided models keep both passes
    int has_front_visible = 0;
    int has_back_visible = 0;
    // fill is a TEXTURE when fill >= 0 && fill != COLOUR_TRANSPARENT (negative = RGB colour, COLOUR_TRANSPARENT =
    // INT16_MAX). no textured face on either side = flat model, uses the no-discard early-Z shader
    int has_texture = 0;
#if defined(__vita__) && defined(RENDER_GL)
    // a model is noclip-safe unless some drawn face uses an alpha-transparent texture on either side
    int needs_clip = 0;
    // a drawn COLOUR_TRANSPARENT side samples the alpha-0 transparent texel only the clip discards; track per side
    int has_transparent_front = 0;
    int has_transparent_back = 0;
#endif
    for (int i = 0; i < game_model->face_count; i++) {
        int fill_front = game_model->face_fill_front[i];
        int fill_back = game_model->face_fill_back[i];
        if (fill_front != COLOUR_TRANSPARENT) {
            has_front_visible = 1;
        }
        if (fill_back != COLOUR_TRANSPARENT) {
            has_back_visible = 1;
        }
        if ((fill_front >= 0 && fill_front != COLOUR_TRANSPARENT) ||
            (fill_back >= 0 && fill_back != COLOUR_TRANSPARENT)) {
            has_texture = 1;
        }
#if defined(__vita__) && defined(RENDER_GL)
        if (fill_front == COLOUR_TRANSPARENT) {
            has_transparent_front = 1;
        }
        if (fill_back == COLOUR_TRANSPARENT) {
            has_transparent_back = 1;
        }
        if ((fill_front >= 0 && fill_front != COLOUR_TRANSPARENT &&
             model_texture_has_alpha(fill_front)) ||
            (fill_back >= 0 && fill_back != COLOUR_TRANSPARENT &&
             model_texture_has_alpha(fill_back))) {
            needs_clip = 1;
        }
#endif
    }
    game_model->gl_all_back_only = !has_front_visible;
    game_model->gl_all_front_only = !has_back_visible;
    game_model->gl_all_flat = !has_texture;
#if defined(__vita__) && defined(RENDER_GL)
    // the no-discard shader is safe only if no drawn fragment can sample an alpha-0 texel: (a) no alpha-transparent
    // texture, (b) no drawn COLOUR_TRANSPARENT side
    int draws_transparent_texel;
    if (game_model->gl_all_back_only) {
        draws_transparent_texel = has_transparent_back;
    } else if (game_model->gl_all_front_only) {
        draws_transparent_texel = has_transparent_front;
    } else {
        draws_transparent_texel = has_transparent_front || has_transparent_back;
    }
    game_model->gl_noclip_safe = !needs_clip && !draws_transparent_texel;

    // per-face partition predicate for the EBO split. a side is opaque iff it is not the transparent marker and is
    // either a flat RGB colour (fill < 0) or a texture with no alpha-0 texels. a face is noclip-safe iff all its drawn sides are opaque; the drawn sides follow the model's one-sided flags. transparent models force every face to the clip range
    if (!game_model->transparent) {
        for (int i = 0; i < game_model->face_count; i++) {
            int fill_front = game_model->face_fill_front[i];
            int fill_back = game_model->face_fill_back[i];

            int front_opaque =
                (fill_front != COLOUR_TRANSPARENT) &&
                (fill_front < 0 || !model_texture_has_alpha(fill_front));

            int back_opaque =
                (fill_back != COLOUR_TRANSPARENT) &&
                (fill_back < 0 || !model_texture_has_alpha(fill_back));

            int face_is_noclip_safe;
            if (game_model->gl_all_back_only) {
                face_is_noclip_safe = back_opaque;
            } else if (game_model->gl_all_front_only) {
                face_is_noclip_safe = front_opaque;
            } else {
                face_is_noclip_safe = front_opaque && back_opaque;
            }

            if (face_noclip != NULL) {
                face_noclip[i] = face_is_noclip_safe;
            }

            if (face_is_noclip_safe) {
                noclip_index_count +=
                    (game_model->face_vertex_count[i] - 2) * 3;
            }
        }
    } else if (face_noclip != NULL) {
        for (int i = 0; i < game_model->face_count; i++) {
            face_noclip[i] = 0;
        }
    }
#else
    (void)face_noclip;
#endif

    return noclip_index_count;
}

// quantize a texcoord to S16-normalized: round(clamp(v,-1,1) * 32767), inverse of the GPU's v/32767. RSC model UVs
// live in [-1,1]
static inline int16_t gl_pack_snorm16(float v) {
    if (v < -1.0f) {
        v = -1.0f;
    } else if (v > 1.0f) {
        v = 1.0f;
    }
    return (int16_t)roundf(v * 32767.0f);
}

void game_model_gl_buffer_arrays(GameModel *game_model, int *vertex_offset,
                                 int *ebo_offset) {
    if (!game_model->gl_buffer) {
        return;
    }

    int16_t *face_normal_x = calloc(game_model->face_count, sizeof(int16_t));
    int16_t *face_normal_y = calloc(game_model->face_count, sizeof(int16_t));
    int16_t *face_normal_z = calloc(game_model->face_count, sizeof(int16_t));

    game_model_get_face_normals(game_model, game_model->vertex_x,
                                game_model->vertex_y, game_model->vertex_z,
                                face_normal_x, face_normal_y, face_normal_z, 0);

    // smoothed per-vertex normals are only read for gouraud faces; flat-lit models skip the accumulation pass
    int any_gouraud = 0;
    int total_face_vertices = 0;

    for (int i = 0; i < game_model->face_count; i++) {
        total_face_vertices += game_model->face_vertex_count[i];

        if (game_model->face_intensity[i] == GAME_MODEL_USE_GOURAUD) {
            any_gouraud = 1;
        }
    }

    int16_t *vertex_normal_x = NULL;
    int16_t *vertex_normal_y = NULL;
    int16_t *vertex_normal_z = NULL;
    int32_t *vertex_normal_magnitude = NULL;

    if (any_gouraud) {
        vertex_normal_x = calloc(game_model->vertex_count, sizeof(int16_t));
        vertex_normal_y = calloc(game_model->vertex_count, sizeof(int16_t));
        vertex_normal_z = calloc(game_model->vertex_count, sizeof(int16_t));

        vertex_normal_magnitude =
            calloc(game_model->vertex_count, sizeof(int32_t));

        game_model_get_vertex_normals(game_model, face_normal_x, face_normal_y,
                                      face_normal_z, vertex_normal_x,
                                      vertex_normal_y, vertex_normal_z,
                                      vertex_normal_magnitude);
    }

    vertex_buffer_gl_bind(game_model->gl_buffer);

#ifdef RENDER_GL
    // reserve the model's whole VBO slice once and write records through the pointer
    gl_model_vertex *vbo_out = vertex_buffer_gl_stage_vbo(
        game_model->gl_buffer,
        (*vertex_offset) * (int)sizeof(gl_model_vertex),
        total_face_vertices * (int)sizeof(gl_model_vertex));
    int vbo_out_index = 0;
#endif

    // count of leading EBO indices belonging to opaque/no-clip faces
    game_model->gl_noclip_ebo_length = 0;
#if defined(__vita__) && defined(RENDER_GL)
    // per-face "all drawn sides opaque" flags. face_vbo_base records each face's VBO base index (Vita/RENDER_GL only)
    int *face_noclip = NULL;
    int *face_vbo_base = NULL;
    if (game_model->face_count > 0) {
        face_noclip = calloc(game_model->face_count, sizeof(int));
        face_vbo_base = calloc(game_model->face_count, sizeof(int));
        if (face_noclip == NULL || face_vbo_base == NULL) {
            mud_error("out of memory buffering model faces\n");
            exit(1);
        }
    }
#endif

#if defined(__vita__) && defined(RENDER_GL)
    game_model_gl_classify(game_model, face_noclip);
#else
    game_model_gl_classify(game_model, NULL);
#endif

    for (int i = 0; i < game_model->face_count; i++) {
        uint16_t *face_vertices = game_model->face_vertices[i];
        int face_vertex_count = game_model->face_vertex_count[i];
        int face_intensity = game_model->face_intensity[i];
        int fill_front = game_model->face_fill_front[i];
        int fill_back = game_model->face_fill_back[i];

        /* -2 is the bridge in the barbarian agility course. fixes
         * https://github.com/2003scape/rsc-c/issues/76 */
        /*if (fill_front == -2) {
            fill_front = COLOUR_TRANSPARENT;
        }

        if (fill_back == -2) {
            fill_back = COLOUR_TRANSPARENT;
        }*/

        gl_face_fill face_fill_front = {0};
        game_model_gl_decode_face_fill(fill_front, &face_fill_front);

        gl_face_fill face_fill_back = {0};
        game_model_gl_decode_face_fill(fill_back, &face_fill_back);

        // front and back unwraps take identical inputs
        float front_face_us[face_vertex_count];
        float front_face_vs[face_vertex_count];

        game_model_gl_unwrap_uvs(game_model, face_vertices, face_vertex_count,
                                 front_face_us, front_face_vs);

        float *back_face_us = front_face_us;
        float *back_face_vs = front_face_vs;

        for (int j = 0; j < face_vertex_count; j++) {
            uint16_t vertex_index = face_vertices[j];

            float vertex_x =
                VERTEX_TO_FLOAT(game_model->vertex_x[vertex_index]);

            float vertex_y =
                VERTEX_TO_FLOAT(game_model->vertex_y[vertex_index]);

            float vertex_z =
                VERTEX_TO_FLOAT(game_model->vertex_z[vertex_index]);

            // pack the normal raw as SHORT (sources are already int16). normal[3] = magnitude; flat faces default to
            // 1
            int16_t normal[4] = {0, 0, 0, 1};

            if (face_intensity == GAME_MODEL_USE_GOURAUD) {
                normal[0] = (int16_t)vertex_normal_x[vertex_index];
                normal[1] = (int16_t)vertex_normal_y[vertex_index];
                normal[2] = (int16_t)vertex_normal_z[vertex_index];
                normal[3] = (int16_t)vertex_normal_magnitude[vertex_index];
            } else {
                normal[0] = (int16_t)face_normal_x[i];
                normal[1] = (int16_t)face_normal_y[i];
                normal[2] = (int16_t)face_normal_z[i];
            }

            int vertex_intensity = game_model->vertex_intensity[vertex_index] +
                                   game_model->vertex_ambience[vertex_index];

            float front_texture_x = front_face_us[j];
            float front_texture_y = 1.0f - front_face_vs[j];

            float back_texture_x = back_face_us[j];
            float back_texture_y = 1.0f - back_face_vs[j];

            gl_atlas_position front_atlas_position =
                gl_transparent_model_atlas_position;

            if (fill_front != COLOUR_TRANSPARENT) {
                if (face_fill_front.texture_index == -1) {
                    front_atlas_position = gl_white_model_atlas_position;
                } else {
                    front_atlas_position =
                        gl_texture_atlas_positions[face_fill_front
                                                       .texture_index];
                }
            }

            gl_offset_texture_uvs_atlas(front_atlas_position, &front_texture_x,
                                        &front_texture_y);

            gl_atlas_position back_atlas_position =
                gl_transparent_model_atlas_position;

            if (fill_back != COLOUR_TRANSPARENT) {
                if (face_fill_back.texture_index == -1) {
                    back_atlas_position = gl_white_model_atlas_position;
                } else {
                    back_atlas_position =
                        gl_texture_atlas_positions[face_fill_back
                                                       .texture_index];
                }
            }

            gl_offset_texture_uvs_atlas(back_atlas_position, &back_texture_x,
                                        &back_texture_y);

            if (face_fill_front.texture_index == 17) {
                front_texture_x *= -1;
                front_texture_y *= -1;
            }

            if (face_fill_back.texture_index == 17) {
                back_texture_x *= -1;
                back_texture_y *= -1;
            }

            gl_model_vertex vertex = {
                /* vertex */
                vertex_x, vertex_y, vertex_z, //

                // normal { x, y, z, magnitude }: raw int16
                {normal[0], normal[1], normal[2], normal[3]}, //

                // lighting { face_intensity, vertex_intensity }: int16; USE_GOURAUD sentinel (INT16_MAX) carried in
                // face_intensity
                {(int16_t)(face_intensity), (int16_t)(vertex_intensity)}, //

                // front colour: quantize [0,1] floats to normalized bytes. [3] = opaque padding
                {(unsigned char)(face_fill_front.r * 255.0f + 0.5f),
                 (unsigned char)(face_fill_front.g * 255.0f + 0.5f),
                 (unsigned char)(face_fill_front.b * 255.0f + 0.5f), 255},

                // front texture: S16-normalized
                {gl_pack_snorm16(front_texture_x),
                 gl_pack_snorm16(front_texture_y)},

                /* back colour */
                {(unsigned char)(face_fill_back.r * 255.0f + 0.5f),
                 (unsigned char)(face_fill_back.g * 255.0f + 0.5f),
                 (unsigned char)(face_fill_back.b * 255.0f + 0.5f), 255},

                // back texture: S16-normalized
                {gl_pack_snorm16(back_texture_x),
                 gl_pack_snorm16(back_texture_y)}};

#ifdef RENDER_GL
            if (vbo_out != NULL) {
                vbo_out[vbo_out_index + j] = vertex;
            }
#elif defined(RENDER_3DS_GL)
            memcpy(game_model->gl_buffer->vbo +
                       (((*vertex_offset) + j) * sizeof(vertex)),
                   &vertex, sizeof(vertex));
#endif
        }

#if defined(__vita__) && defined(RENDER_GL)
        if (face_vbo_base != NULL) {
            // defer the triangle-fan EBO writes to the two sweeps below (noclip faces into segment-A slice, clip
            // faces into segment-B). record this face's VBO base
            face_vbo_base[i] = (*vertex_offset);
        }
#else
        for (int j = 0; j < face_vertex_count - 2; j++) {
#ifdef RENDER_GL
            // 16-bit indices: the buffer split (MAX_VERTEX_INDEX = 65535) keeps every vertex_offset below 65535, so
            // each index fits a GLushort
            GLushort indices[] = {(*vertex_offset), (*vertex_offset) + j + 1,
                                  (*vertex_offset) + j + 2};

            vertex_buffer_gl_write_ebo(game_model->gl_buffer,
                                       (*ebo_offset) * sizeof(GLushort),
                                       sizeof(indices), indices);
#elif defined(RENDER_3DS_GL)
            uint16_t indices[] = {(*vertex_offset), (*vertex_offset) + j + 1,
                                  (*vertex_offset) + j + 2};

            memcpy(game_model->gl_buffer->ebo +
                       ((*ebo_offset) * sizeof(uint16_t)),
                   indices, sizeof(indices));
#endif

            (*ebo_offset) += 3;
        }
#endif

        (*vertex_offset) += face_vertex_count;
#ifdef RENDER_GL
        vbo_out_index += face_vertex_count;
#endif
    }

#if defined(__vita__) && defined(RENDER_GL)
    // two-sweep EBO emission into the family's two segments. sweep 1 emits noclip-safe faces' fan indices into
    // segment A [gl_ebo_offset], counting into gl_noclip_ebo_length; sweep 2 emits the rest into segment B [gl_clip_ebo_offset]. transparent models put everything in the clip segment
    if (face_vbo_base != NULL || game_model->face_count == 0) {
        int noclip_written = 0;
        int clip_written = 0;

        // both segment slices staged once, the sweeps write through pointers. 16-bit indices: each face's VBO base
        // plus fan offset stays < 65535, fitting a GLushort
        GLushort *noclip_out = NULL;
        GLushort *clip_out = NULL;

        if (game_model->face_count > 0) {
            // the family planner already stored the segment split
            int noclip_length =
                game_model->gl_ebo_length - game_model->gl_clip_ebo_length;

            noclip_out = vertex_buffer_gl_stage_ebo(
                game_model->gl_buffer,
                game_model->gl_ebo_offset * (int)sizeof(GLushort),
                noclip_length * (int)sizeof(GLushort));

            clip_out = vertex_buffer_gl_stage_ebo(
                game_model->gl_buffer,
                game_model->gl_clip_ebo_offset * (int)sizeof(GLushort),
                (game_model->gl_ebo_length - noclip_length) *
                    (int)sizeof(GLushort));
        }

        // sweep 1: noclip-safe faces -> this model's segment-A slice
        for (int i = 0; i < game_model->face_count; i++) {
            if (!face_noclip[i]) {
                continue;
            }

            int face_vertex_count = game_model->face_vertex_count[i];
            int base = face_vbo_base[i];

            for (int j = 0; j < face_vertex_count - 2; j++) {
                if (noclip_out != NULL) {
                    noclip_out[noclip_written] = base;
                    noclip_out[noclip_written + 1] = base + j + 1;
                    noclip_out[noclip_written + 2] = base + j + 2;
                }

                noclip_written += 3;
                game_model->gl_noclip_ebo_length += 3;
            }
        }

        // sweep 2: clip-needed faces -> this model's segment-B slice
        for (int i = 0; i < game_model->face_count; i++) {
            if (face_noclip[i]) {
                continue;
            }

            int face_vertex_count = game_model->face_vertex_count[i];
            int base = face_vbo_base[i];

            for (int j = 0; j < face_vertex_count - 2; j++) {
                if (clip_out != NULL) {
                    clip_out[clip_written] = base;
                    clip_out[clip_written + 1] = base + j + 1;
                    clip_out[clip_written + 2] = base + j + 2;
                }

                clip_written += 3;
            }
        }

        game_model->gl_clip_ebo_length = clip_written;
    }

    free(face_noclip);
    free(face_vbo_base);
#endif

    free(face_normal_x);
    free(face_normal_y);
    free(face_normal_z);

    free(vertex_normal_x);
    free(vertex_normal_y);
    free(vertex_normal_z);
    free(vertex_normal_magnitude);
}

void game_model_get_vertex_ebo_lengths(GameModel **game_models, int length,
                                       int *vertex_count, int *ebo_length) {
    for (int i = 0; i < length; i++) {
        GameModel *game_model = game_models[i];

        if (game_model == NULL) {
            continue;
        }

        game_model->gl_ebo_length = 0;

        for (int j = 0; j < game_model->face_count; j++) {
            int face_vertex_count = game_model->face_vertex_count[j];
            *vertex_count += face_vertex_count;
            game_model->gl_ebo_length += (face_vertex_count - 2) * 3;
        }

        *ebo_length += game_model->gl_ebo_length;
    }
}

float game_model_gl_intersects(GameModel *game_model, vec3 ray_direction,
                               vec3 ray_position) {
    float t[10] = {0};

    float min_vertex_x = VERTEX_TO_FLOAT(game_model->min_x);
    float max_vertex_x = VERTEX_TO_FLOAT(game_model->max_x);

    float min_vertex_y = VERTEX_TO_FLOAT(game_model->min_y);
    float max_vertex_y = VERTEX_TO_FLOAT(game_model->max_y);

    float min_vertex_z = VERTEX_TO_FLOAT(game_model->min_z);
    float max_vertex_z = VERTEX_TO_FLOAT(game_model->max_z);

    t[1] = (min_vertex_x - ray_position[0]) / ray_direction[0];
    t[2] = (max_vertex_x - ray_position[0]) / ray_direction[0];

    t[3] = (min_vertex_y - ray_position[1]) / ray_direction[1];
    t[4] = (max_vertex_y - ray_position[1]) / ray_direction[1];

    t[5] = (min_vertex_z - ray_position[2]) / ray_direction[2];
    t[6] = (max_vertex_z - ray_position[2]) / ray_direction[2];

    t[7] = fmax(fmax(fmin(t[1], t[2]), fmin(t[3], t[4])), fmin(t[5], t[6]));
    t[8] = fmin(fmin(fmax(t[1], t[2]), fmax(t[3], t[4])), fmax(t[5], t[6]));
    t[9] = (t[8] < 0 || t[7] > t[8]) ? -1 : t[7];

    return t[9];
}

static void game_model_gl_buffer_attributes(gl_vertex_buffer *vertex_buffer);

// deferred = create without GL objects; game_model_gl_realize_buffer supplies them on the render thread
void game_model_gl_create_buffer(gl_vertex_buffer *vertex_buffer,
                                 int vbo_length, int ebo_length,
                                 int deferred) {
#ifdef RENDER_GL
    if (deferred) {
        vertex_buffer_gl_new_deferred(vertex_buffer, sizeof(gl_model_vertex),
                                      vbo_length, ebo_length);
        return;
    }
#else
    (void)deferred;
#endif

    // TODO terrain buffer should be dynamic, add a flag
    vertex_buffer_gl_new(vertex_buffer, sizeof(gl_model_vertex), vbo_length,
                         ebo_length);

    game_model_gl_buffer_attributes(vertex_buffer);
}

#ifdef RENDER_GL
void game_model_gl_realize_buffer(gl_vertex_buffer *vertex_buffer,
                                  int keep_mirror) {
    if (!vertex_buffer->gl_deferred) {
        return;
    }

    vertex_buffer_gl_realize(vertex_buffer, keep_mirror);
    game_model_gl_buffer_attributes(vertex_buffer);
}

// incremental realize: begin creates the GL objects + attributes, step uploads
// the mirror in chunks over frames (returns 1 when done)
void game_model_gl_realize_buffer_begin(gl_vertex_buffer *vertex_buffer) {
    if (!vertex_buffer->gl_deferred) {
        return;
    }

    vertex_buffer_gl_realize_begin(vertex_buffer);
    game_model_gl_buffer_attributes(vertex_buffer);
}

int game_model_gl_realize_buffer_step(gl_vertex_buffer *vertex_buffer,
                                      int max_bytes, int keep_mirror) {
    return vertex_buffer_gl_realize_step(vertex_buffer, max_bytes, keep_mirror);
}
#endif

static void game_model_gl_buffer_attributes(gl_vertex_buffer *vertex_buffer) {
    // colours packed as 4 normalized bytes each (front_colour[4] / back_colour[4]), so the vertex mixes float and
    // byte fields; every attribute uses an explicit offsetof() byte offset and component type. on 3DS citro3d derives the offset from cumulative loader sizes, so the loader types/counts below tile the struct exactly (3f, 4f, 2f, 4ub, 2f, 4ub, 2f)
#ifdef RENDER_GL
    // vertex { x, y, z }: full float range (world coords)
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 3, GL_FLOAT, GL_FALSE,
                                      offsetof(gl_model_vertex, x));

    // normal { x, y, z, magnitude }: int16, widened SHORT -> float (raw integer components / face count, not
    // normalized)
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 4, GL_SHORT, GL_FALSE,
                                      offsetof(gl_model_vertex, normal));

    // lighting { face_intensity, vertex_intensity }: int16, SHORT -> float
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 2, GL_SHORT, GL_FALSE,
                                      offsetof(gl_model_vertex, lighting));

    // front colour { r, g, b }: normalized bytes -> float3 in the shader
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 3, GL_UNSIGNED_BYTE,
                                      GL_TRUE,
                                      offsetof(gl_model_vertex, front_colour));

    // front texture { s, t }: S16-normalized SHORT -> [-1,1] float
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 2, GL_SHORT, GL_TRUE,
                                      offsetof(gl_model_vertex, front_tex));

    // back colour { r, g, b }: normalized bytes -> float3 in the shader
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 3, GL_UNSIGNED_BYTE,
                                      GL_TRUE,
                                      offsetof(gl_model_vertex, back_colour));

    // back texture { s, t }: S16-normalized SHORT -> [-1,1] float
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 2, GL_SHORT, GL_TRUE,
                                      offsetof(gl_model_vertex, back_tex));
#elif defined(RENDER_3DS_GL)
    // attribute types mirror the packed 40-byte gl_model_vertex so the citro3d loader sizes tile the struct exactly:
    // 3f,4s,2s,4ub,2s,4ub,2s = 12+8+4+4+4+4+4 = 40

    /* vertex { x, y, z } */
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 3, GPU_FLOAT, 3,
                                      offsetof(gl_model_vertex, x));

    // normal { x, y, z, magnitude }: raw int16
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 4, GPU_SHORT, 4,
                                      offsetof(gl_model_vertex, normal));

    // lighting { face_intensity, vertex_intensity }: int16
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 2, GPU_SHORT, 2,
                                      offsetof(gl_model_vertex, lighting));

    // front colour { r, g, b, a }: 4 normalized bytes, shader reads .rgb
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 4, GPU_UNSIGNED_BYTE, 4,
                                      offsetof(gl_model_vertex, front_colour));

    // front texture { s, t }: S16-normalized int16
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 2, GPU_SHORT, 2,
                                      offsetof(gl_model_vertex, front_tex));

    // back colour { r, g, b, a }: 4 normalized bytes, shader reads .rgb
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 4, GPU_UNSIGNED_BYTE, 4,
                                      offsetof(gl_model_vertex, back_colour));

    // back texture { s, t }: S16-normalized int16
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, 2, GPU_SHORT, 2,
                                      offsetof(gl_model_vertex, back_tex));

    BufInfo_Add(&vertex_buffer->buf_info, vertex_buffer->vbo,
                sizeof(gl_model_vertex), 7, 0x6543210);
#endif
}

/* calculate the length of the VBO and EBO arrays for a list of game models,
 * then populate them */
int game_model_gl_buffer_models(gl_vertex_buffer ***vertex_buffers,
                                int *vertex_buffers_length,
                                GameModel **game_models,
                                int game_models_length, int keep_mirror,
                                int deferred) {
    int vertex_offset = 0;
    int ebo_offset = 0;

    game_model_get_vertex_ebo_lengths(game_models, game_models_length,
                                      &vertex_offset, &ebo_offset);

    for (int i = 0; i < *vertex_buffers_length; i++) {
        vertex_buffer_gl_destroy((*vertex_buffers)[i]);
        free((*vertex_buffers)[i]);
    }

    free(*vertex_buffers);

    // TODO move this to header
    int MAX_VERTEX_INDEX = 65535;
    // int MAX_VERTEX_INDEX = 2147483647;
    int total_buffers = ceil((float)ebo_offset / (float)MAX_VERTEX_INDEX);

    if (total_buffers == 0) {
        *vertex_buffers_length = 0;
        return 0;
    }

    *vertex_buffers = calloc(total_buffers, sizeof(gl_vertex_buffer *));

    for (int i = 0; i < total_buffers; i++) {
        (*vertex_buffers)[i] = calloc(1, sizeof(gl_vertex_buffer));
    }

    int vertex_buffer_index = 0;

    vertex_offset = 0;
    ebo_offset = 0;

    int next_vertex_offset = vertex_offset;
    int next_ebo_offset = ebo_offset;

    gl_vertex_buffer *vertex_buffer = (*vertex_buffers)[vertex_buffer_index];

    // dupe_of[i] = index of the model whose buffer slices model i aliases (shared vertex arrays), or -1
    int *dupe_of = malloc(game_models_length * sizeof(int));

    for (int i = 0; i < game_models_length; i++) {
        dupe_of[i] = -1;
    }

    for (int i = 0; i < game_models_length; i++) {
        GameModel *game_model = game_models[i];

        if (game_model == NULL) {
            continue;
        }

        GameModel *dupe = NULL;

        for (int j = 0; j < i; j++) {
            GameModel *game_model_b = game_models[j];

            if (game_model_b != NULL && game_model_b->vertex_count > 0 &&
                game_model != game_model_b &&
                game_model->vertex_x == game_model_b->vertex_x) {
                dupe = game_model_b;
                dupe_of[i] = j;
                break;
            }
        }

        if (dupe) {
            game_model->gl_buffer = dupe->gl_buffer;
            game_model->gl_vbo_offset = dupe->gl_vbo_offset;
            game_model->gl_ebo_offset = dupe->gl_ebo_offset;
            continue;
        }

        next_vertex_offset = vertex_offset;
        next_ebo_offset = ebo_offset;

        for (int j = 0; j < game_model->face_count; j++) {
            int face_vertex_count = game_model->face_vertex_count[j];
            next_vertex_offset += face_vertex_count;
            next_ebo_offset += (face_vertex_count - 2) * 3;
        }

        if (next_ebo_offset >= MAX_VERTEX_INDEX ||
            next_vertex_offset >= MAX_VERTEX_INDEX) {
            game_model_gl_create_buffer(vertex_buffer, vertex_offset,
                                        ebo_offset, deferred);

            vertex_buffer_index++;
            vertex_buffer = (*vertex_buffers)[vertex_buffer_index];

            game_model->gl_vbo_offset = 0;
            game_model->gl_ebo_offset = 0;

            vertex_offset = next_vertex_offset - vertex_offset;
            ebo_offset = next_ebo_offset - ebo_offset;
        } else {
            game_model->gl_vbo_offset = vertex_offset;
            game_model->gl_ebo_offset = ebo_offset;

            vertex_offset = next_vertex_offset;
            ebo_offset = next_ebo_offset;
        }

        game_model->gl_buffer = vertex_buffer;
    }

    game_model_gl_create_buffer(vertex_buffer, vertex_offset, ebo_offset,
                                deferred);

#if defined(__vita__) && defined(RENDER_GL)
    // plan the two-segment EBO layout per buffer: segment A packs every model's noclip-safe indices back to back,
    // segment B every model's clip-needed indices. per-model draw offsets are re-based here
    for (int b = 0; b < total_buffers; b++) {
        gl_vertex_buffer *segment_buffer = (*vertex_buffers)[b];

        int noclip_total = 0;

        for (int i = 0; i < game_models_length; i++) {
            GameModel *game_model = game_models[i];

            if (game_model == NULL || dupe_of[i] >= 0 ||
                game_model->gl_buffer != segment_buffer) {
                continue;
            }

            noclip_total += game_model_gl_classify(game_model, NULL);
        }

        int a_cursor = 0;
        int b_cursor = noclip_total;

        for (int i = 0; i < game_models_length; i++) {
            GameModel *game_model = game_models[i];

            if (game_model == NULL || dupe_of[i] >= 0 ||
                game_model->gl_buffer != segment_buffer) {
                continue;
            }

            int noclip_length = game_model_gl_classify(game_model, NULL);
            int clip_length = game_model->gl_ebo_length - noclip_length;

            game_model->gl_ebo_offset = a_cursor;
            game_model->gl_noclip_ebo_length = noclip_length;
            game_model->gl_clip_ebo_offset = b_cursor;
            game_model->gl_clip_ebo_length = clip_length;

            a_cursor += noclip_length;
            b_cursor += clip_length;
        }
    }

    // dupes alias their source's slices
    for (int i = 0; i < game_models_length; i++) {
        if (game_models[i] != NULL && dupe_of[i] >= 0) {
            GameModel *source = game_models[dupe_of[i]];

            game_models[i]->gl_ebo_offset = source->gl_ebo_offset;
            game_models[i]->gl_noclip_ebo_length =
                source->gl_noclip_ebo_length;
            game_models[i]->gl_clip_ebo_offset = source->gl_clip_ebo_offset;
            game_models[i]->gl_clip_ebo_length = source->gl_clip_ebo_length;
        }
    }
#endif

    free(dupe_of);

    for (int i = 0; i < game_models_length; i++) {
        GameModel *game_model = game_models[i];

        if (game_model == NULL) {
            continue;
        }

        vertex_offset = game_model->gl_vbo_offset;
        ebo_offset = game_model->gl_ebo_offset;

        game_model_gl_buffer_arrays(game_model, &vertex_offset, &ebo_offset);

        // game_model_destroy(game_model);
    }

#ifdef RENDER_GL
    // one ranged upload per buffer instead of one call per face-vertex
    for (int i = 0; i < total_buffers; i++) {
        vertex_buffer_gl_flush((*vertex_buffers)[i], keep_mirror);
    }
#else
    (void)keep_mirror;
#endif

    *vertex_buffers_length = total_buffers;

    return total_buffers;
}

#if defined(RENDER_GL) && (defined(EMSCRIPTEN) || defined(__vita__))
void game_model_gl_create_pick_buffer(gl_vertex_buffer *pick_buffer,
                                      int vbo_length, int ebo_length) {
    vertex_buffer_gl_new(pick_buffer, sizeof(gl_pick_vertex), vbo_length,
                         ebo_length);

    /*glBufferData(GL_ARRAY_BUFFER, vbo_length * sizeof(gl_pick_vertex), NULL,
                 GL_DYNAMIC_DRAW);*/

    int attribute_offset = 0;

    /* vertex { x, y, z } */
    vertex_buffer_gl_add_attribute(pick_buffer, &attribute_offset, 3);

    /* colour { r, g } */
    vertex_buffer_gl_add_attribute(pick_buffer, &attribute_offset, 2);
}

void game_model_gl_buffer_pick_arrays(GameModel *game_model, int *vertex_offset,
                                      int *ebo_offset) {
    for (int i = 0; i < game_model->face_count; i++) {
        uint16_t *face_vertices = game_model->face_vertices[i];
        int face_vertex_count = game_model->face_vertex_count[i];

        int face_tag = game_model->face_tag[i] - TILE_FACE_TAG;

        float face_tag_r = (face_tag & 0xff) / 255.0f;
        float face_tag_g = ((face_tag >> 8) & 0xff) / 255.0f;

        for (int j = 0; j < face_vertex_count; j++) {
            uint16_t vertex_index = face_vertices[j];

            GLfloat vertex_x =
                VERTEX_TO_FLOAT(game_model->vertex_x[vertex_index]);

            GLfloat vertex_y =
                VERTEX_TO_FLOAT(game_model->vertex_y[vertex_index]);

            GLfloat vertex_z =
                VERTEX_TO_FLOAT(game_model->vertex_z[vertex_index]);

            gl_pick_vertex vertex = {
                /* vertex */
                vertex_x, vertex_y, vertex_z, //

                /* face tag */
                face_tag_r, face_tag_g //
            };

            glBufferSubData(GL_ARRAY_BUFFER,
                            ((*vertex_offset) + j) * sizeof(gl_pick_vertex),
                            sizeof(gl_pick_vertex), (void *)&vertex);
        }

        for (int j = 0; j < face_vertex_count - 2; j++) {
            // 16-bit indices: the pick buffer holds only the terrain models under the cursor, vertex_offset stays
            // well below 65535
            GLushort indices[] = {(*vertex_offset), (*vertex_offset) + j + 1,
                                  (*vertex_offset) + j + 2};

            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                            (*ebo_offset) * sizeof(GLushort), sizeof(indices),
                            indices);

            (*ebo_offset) += 3;
        }

        (*vertex_offset) += face_vertex_count;
    }
}

void game_model_gl_buffer_pick_models(gl_vertex_buffer *pick_buffer,
                                      GameModel **game_models, int length) {
    int vertex_offset = 0;
    int ebo_offset = 0;

    game_model_get_vertex_ebo_lengths(game_models, length, &vertex_offset,
                                      &ebo_offset);

    game_model_gl_create_pick_buffer(pick_buffer, vertex_offset, ebo_offset);

    vertex_offset = 0;
    ebo_offset = 0;

    for (int i = 0; i < length; i++) {
        GameModel *game_model = game_models[i];

        if (game_model == NULL) {
            continue;
        }

        game_model->gl_pick_vbo_offset = vertex_offset;
        game_model->gl_pick_ebo_offset = ebo_offset;

        game_model_gl_buffer_pick_arrays(game_model, &vertex_offset,
                                         &ebo_offset);
    }
}
#endif
#endif
