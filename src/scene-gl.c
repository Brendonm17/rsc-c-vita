#include "scene.h"

// per-face early-Z split: routes the opaque faces of mixed models onto the no-discard shader. default (0) is
// per-model routing, each model on a single shader chosen by gl_noclip_safe. Vita/RENDER_GL only
#define GL_PER_FACE_EARLYZ 0

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
int scene_gl_model_time_compare(const void *a, const void *b) {
    GlModelTime model_time_a = *(GlModelTime *)a;
    GlModelTime model_time_b = *(GlModelTime *)b;

    if (model_time_a.time == model_time_b.time) {
        return 0;
    }

    return model_time_a.time < model_time_b.time ? -1 : 1;
}

#if defined(RENDER_GL) && !GL_PER_FACE_EARLYZ
// buffer order: sort by (vertex buffer, index range start) so models back-to-back in the shared VBO/EBO become
// mergeable runs (one glDrawElements for many models)
static int scene_gl_opaque_buffer_order_compare(const void *a, const void *b) {
    const GameModel *ma = ((const GlOpaqueSortEntry *)a)->model;
    const GameModel *mb = ((const GlOpaqueSortEntry *)b)->model;

    if (ma->gl_buffer != mb->gl_buffer) {
        return (uintptr_t)ma->gl_buffer < (uintptr_t)mb->gl_buffer ? -1 : 1;
    }

    if (ma->gl_ebo_offset != mb->gl_ebo_offset) {
        return ma->gl_ebo_offset - mb->gl_ebo_offset;
    }

    // zero-length segment-A slices share an offset; keep the segment-B order deterministic too
    return ma->gl_clip_ebo_offset - mb->gl_clip_ebo_offset;
}
#else
// front-to-back: smaller squared distance (nearer) sorts first
static int scene_gl_opaque_depth_compare(const void *a, const void *b) {
    int64_t da = ((const GlOpaqueSortEntry *)a)->dist2;
    int64_t db = ((const GlOpaqueSortEntry *)b)->dist2;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}
#endif

void scene_gl_update_camera(Scene *scene) {
    vec3 camera_position = {VERTEX_TO_FLOAT(scene->camera_x),
                            VERTEX_TO_FLOAT(scene->camera_y),
                            VERTEX_TO_FLOAT(scene->camera_z)};

    vec3 camera_front = {0.0, 0.0, -1.0};
    vec3 camera_up = {0.0, -1.0, 0.0};

    // ??
    float yaw = 1.571051f + TABLE_TO_RADIANS(scene->camera_pitch, 2048);
    float pitch = 1.338493f - TABLE_TO_RADIANS(scene->camera_yaw, 2048);

    vec3 front = {cos(yaw) * cos(pitch), pitch, sin(yaw) * cos(pitch)};

    glm_normalize_to(front, camera_front);

    vec3 camera_centre = {0};
    glm_vec3_add(camera_position, camera_front, camera_centre);

    glm_lookat(camera_position, camera_centre, camera_up, scene->gl_view);

    glm_mat4_inv(scene->gl_view, scene->gl_inverse_view);

    // this GL far plane is also the frustum-cull far distance (cull planes are extracted from this projection
    // matrix). cap the far margin just past the fog floor plus one model's reach (~1536). margin is 256 (one fog ramp). fog-of-war-off (fog_z_distance 20000) keeps an effectively-infinite far via the min()
    int clip_far_margin =
        scene->fog_z_distance < 256 ? scene->fog_z_distance : 256;
    float clip_far = VERTEX_TO_FLOAT(scene->clip_far_3d + clip_far_margin);

#ifdef RENDER_GL
    glm_perspective(scene->gl_fov,
                    (float)(scene->width) / (float)(scene->gl_height - 1),
                    VERTEX_TO_FLOAT(scene->clip_near),
                    VERTEX_TO_FLOAT(clip_far), scene->gl_projection);

    glm_mat4_inv(scene->gl_projection, scene->gl_inverse_projection);
#elif defined(RENDER_3DS_GL)
    _3ds_gl_perspective(scene->gl_fov,
                        (float)(scene->width) / (float)(scene->gl_height - 1),
                        VERTEX_TO_FLOAT(scene->clip_near),
                        VERTEX_TO_FLOAT(clip_far), scene->gl_projection);

    glm_perspective(scene->gl_fov,
                    (float)(scene->width) / (float)(scene->gl_height - 1),
                    VERTEX_TO_FLOAT(scene->clip_near),
                    VERTEX_TO_FLOAT(clip_far), scene->gl_original_projection);

    glm_mat4_inv(scene->gl_original_projection, scene->gl_inverse_projection);
#endif

    glm_mat4_mul(scene->gl_projection, scene->gl_view,
                 scene->gl_projection_view);
}
#endif /* end shared between 3DS and normal GL */

/* normal GL only */
#ifdef RENDER_GL

#if defined(__vita__) && defined(RENDER_GL) && GL_PER_FACE_EARLYZ
// per-face early-Z: set the full per-model uniform set on one shader program. called once per shader a model's draw
// uses (no-discard range and/or clip range), with projection_view_model precomputed by the caller
static void scene_gl_set_model_uniforms(Shader *shader, GameModel *game_model,
                                        mat4 projection_view_model) {
    shader_use(shader);

    shader_set_mat4_loc(shader->loc_model, game_model->transform);

    shader_set_mat4_loc(shader->loc_projection_view_model,
                        projection_view_model);

    vec3 light_direction = {
        VERTEX_TO_FLOAT(game_model->light_direction_x) * VERTEX_SCALE,
        VERTEX_TO_FLOAT(game_model->light_direction_y) * VERTEX_SCALE,
        VERTEX_TO_FLOAT(game_model->light_direction_z) * VERTEX_SCALE};

    shader_set_float_loc(shader->loc_light_ambience,
                         game_model->light_ambience);

    shader_set_int_loc(shader->loc_unlit, game_model->unlit);

    if (!game_model->unlit) {
        shader_set_vec3_loc(shader->loc_light_direction, light_direction);

        shader_set_float_loc(shader->loc_light_diffuse,
                             ((float)game_model->light_diffuse *
                              (float)game_model->light_direction_magnitude) /
                                 256.0f);
    }

    shader_set_float_loc(shader->loc_opacity,
                         game_model->transparent ? TRANSLUCENT_MODEL_OPACITY
                                                 : 1.0f);
}
#endif

void scene_gl_draw_game_model(Scene *scene, GameModel *game_model) {
    if (game_model->gl_ebo_offset == -1 || !game_model->visible) {
        return;
    }

    vertex_buffer_gl_bind(game_model->gl_buffer);

#if defined(__vita__) && defined(RENDER_GL) && GL_PER_FACE_EARLYZ
    // per-face early-Z: the model's EBO is partitioned into an opaque/no-clip range [gl_ebo_offset, +noclip_len)
    // drawn with the no-discard shader, and a clip-needed range [+noclip_len, +gl_ebo_length) drawn with the clip shader. transparent models force noclip_len == 0 at buffer time, drawing entirely on the clip shader
    int noclip_len = game_model->gl_noclip_ebo_length;
    int clip_len = game_model->gl_ebo_length - noclip_len;

    int noclip_off = game_model->gl_ebo_offset;
    int clip_off = game_model->gl_ebo_offset + noclip_len;

    // Compute the matrices once and feed both shaders the same values.
    mat4 view_model = {0};
    glm_mat4_mul(scene->gl_view, game_model->transform, view_model);

    mat4 projection_view_model = {0};
    glm_mat4_mul(scene->gl_projection, view_model, projection_view_model);

    Shader *noclip_shader = &scene->game_model_shader_noclip;
    Shader *clip_shader = &scene->game_model_shader;

    if (noclip_len > 0) {
        scene_gl_set_model_uniforms(noclip_shader, game_model,
                                    projection_view_model);
    }

    if (clip_len > 0) {
        scene_gl_set_model_uniforms(clip_shader, game_model,
                                    projection_view_model);
    }

    glEnable(GL_CULL_FACE);

    // same pass structure as legacy (one-sided -> one pass, two-sided -> two passes), but each pass draws up to two
    // ranges: the opaque range on the no-discard shader and the clip range on the clip shader
#define GL_DRAW_SPLIT_PASS(cull_face_value, cull_front_value)                   \
    do {                                                                        \
        glCullFace(cull_face_value);                                            \
        if (noclip_len > 0) {                                                   \
            shader_use(noclip_shader);                                          \
            shader_set_int_loc(noclip_shader->loc_cull_front,                   \
                               (cull_front_value));                             \
            glDrawElements(GL_TRIANGLES, noclip_len, GL_UNSIGNED_SHORT,           \
                           (void *)(noclip_off * sizeof(GLushort)));              \
        }                                                                       \
        if (clip_len > 0) {                                                     \
            shader_use(clip_shader);                                            \
            shader_set_int_loc(clip_shader->loc_cull_front,                     \
                               (cull_front_value));                             \
            glDrawElements(GL_TRIANGLES, clip_len, GL_UNSIGNED_SHORT,             \
                           (void *)(clip_off * sizeof(GLushort)));                \
        }                                                                       \
    } while (0)

    if (game_model->gl_all_back_only) {
        GL_DRAW_SPLIT_PASS(GL_FRONT, 1);
    } else if (game_model->gl_all_front_only) {
        GL_DRAW_SPLIT_PASS(GL_BACK, 0);
    } else {
        GL_DRAW_SPLIT_PASS(GL_BACK, 0);
        GL_DRAW_SPLIT_PASS(GL_FRONT, 1);
    }

#undef GL_DRAW_SPLIT_PASS

#else // legacy single-shader path (GL_PER_FACE_EARLYZ == 0, or non-Vita)

#if defined(__vita__) && defined(RENDER_GL)
    // opaque models (no face samples an alpha-transparent texture, e.g. terrain) route to the no-discard shader.
    // models with alpha-transparent textures (fences, windows, foliage) keep the clip shader
    // transparent (alpha-blended) models (shadows, water, glass) keep the clip shader. two-segment draw: noclip-safe
    // faces in segment A (no-discard early-Z shader, gl_ebo_offset + gl_noclip_ebo_length), clip-needed faces in segment B (clip shader, gl_clip_ebo_offset + gl_clip_ebo_length). mixed models draw both ranges; fully-opaque, fully-cutout and transparent draw one
    mat4 view_model = {0};
    glm_mat4_mul(scene->gl_view, game_model->transform, view_model);

    mat4 projection_view_model = {0};
    glm_mat4_mul(scene->gl_projection, view_model, projection_view_model);

    vec3 light_direction = {
        VERTEX_TO_FLOAT(game_model->light_direction_x) * VERTEX_SCALE,
        VERTEX_TO_FLOAT(game_model->light_direction_y) * VERTEX_SCALE,
        VERTEX_TO_FLOAT(game_model->light_direction_z) * VERTEX_SCALE};

    glEnable(GL_CULL_FACE);

    for (int segment = 0; segment < 2; segment++) {
        int segment_offset = segment == 0 ? game_model->gl_ebo_offset
                                          : game_model->gl_clip_ebo_offset;
        int segment_length = segment == 0 ? game_model->gl_noclip_ebo_length
                                          : game_model->gl_clip_ebo_length;

        if (segment_length <= 0) {
            continue;
        }

        Shader *gm = segment == 0 ? &scene->game_model_shader_noclip
                                  : &scene->game_model_shader;
        shader_use(gm);

        shader_set_mat4_loc(gm->loc_model, game_model->transform);
        shader_set_mat4_loc(gm->loc_projection_view_model,
                            projection_view_model);

        shader_set_float_loc(gm->loc_light_ambience,
                             game_model->light_ambience);

        shader_set_int_loc(gm->loc_unlit, game_model->unlit);

        if (!game_model->unlit) {
            shader_set_vec3_loc(gm->loc_light_direction, light_direction);

            shader_set_float_loc(gm->loc_light_diffuse,
                                 ((float)game_model->light_diffuse *
                                  (float)game_model->light_direction_magnitude) /
                                     256.0f);
        }

        shader_set_float_loc(gm->loc_opacity,
                             game_model->transparent ? TRANSLUCENT_MODEL_OPACITY
                                                     : 1.0f);

        // one-sided models (all faces transparent on one side: terrain, most decorations) only need the visible cull
        // pass. mixed / two-sided models keep both passes
        if (game_model->gl_all_back_only) {
            glCullFace(GL_FRONT);
            shader_set_int_loc(gm->loc_cull_front, 1);
            glDrawElements(GL_TRIANGLES, segment_length, GL_UNSIGNED_SHORT,
                           (void *)(segment_offset * sizeof(GLushort)));
        } else if (game_model->gl_all_front_only) {
            glCullFace(GL_BACK);
            shader_set_int_loc(gm->loc_cull_front, 0);
            glDrawElements(GL_TRIANGLES, segment_length, GL_UNSIGNED_SHORT,
                           (void *)(segment_offset * sizeof(GLushort)));
        } else {
            glCullFace(GL_BACK);
            shader_set_int_loc(gm->loc_cull_front, 0);
            glDrawElements(GL_TRIANGLES, segment_length, GL_UNSIGNED_SHORT,
                           (void *)(segment_offset * sizeof(GLushort)));
            glCullFace(GL_FRONT);
            shader_set_int_loc(gm->loc_cull_front, 1);
            glDrawElements(GL_TRIANGLES, segment_length, GL_UNSIGNED_SHORT,
                           (void *)(segment_offset * sizeof(GLushort)));
        }
    }
#else
    Shader *gm = &scene->game_model_shader;

    shader_set_mat4_loc(gm->loc_model, game_model->transform);

    mat4 view_model = {0};
    glm_mat4_mul(scene->gl_view, game_model->transform, view_model);

    mat4 projection_view_model = {0};
    glm_mat4_mul(scene->gl_projection, view_model, projection_view_model);

    shader_set_mat4_loc(gm->loc_projection_view_model, projection_view_model);

    vec3 light_direction = {
        VERTEX_TO_FLOAT(game_model->light_direction_x) * VERTEX_SCALE,
        VERTEX_TO_FLOAT(game_model->light_direction_y) * VERTEX_SCALE,
        VERTEX_TO_FLOAT(game_model->light_direction_z) * VERTEX_SCALE};

    shader_set_float_loc(gm->loc_light_ambience, game_model->light_ambience);

    shader_set_int_loc(gm->loc_unlit, game_model->unlit);

    if (!game_model->unlit) {
        shader_set_vec3_loc(gm->loc_light_direction, light_direction);

        shader_set_float_loc(gm->loc_light_diffuse,
                             ((float)game_model->light_diffuse *
                              (float)game_model->light_direction_magnitude) /
                                 256.0f);
    }

    shader_set_float_loc(gm->loc_opacity,
                         game_model->transparent ? TRANSLUCENT_MODEL_OPACITY
                                                 : 1.0f);

    glEnable(GL_CULL_FACE);

    // desktop / WebGL / 3DS: the original two passes (winding-independent)
    glCullFace(GL_BACK);
    shader_set_int_loc(gm->loc_cull_front, 0);
    glDrawElements(GL_TRIANGLES, game_model->gl_ebo_length, GL_UNSIGNED_SHORT,
                   (void *)(game_model->gl_ebo_offset * sizeof(GLushort)));

    glCullFace(GL_FRONT);
    shader_set_int_loc(gm->loc_cull_front, 1);
    glDrawElements(GL_TRIANGLES, game_model->gl_ebo_length, GL_UNSIGNED_SHORT,
                   (void *)(game_model->gl_ebo_offset * sizeof(GLushort)));
#endif

#endif // GL_PER_FACE_EARLYZ split
}

#if defined(RENDER_GL) && !GL_PER_FACE_EARLYZ
// two buffer-adjacent opaque models share one glDrawElements when every piece of GL state is bit-identical: same
// vertex buffer, shader route, transform, lighting, opacity, cull-pass structure. static world geometry (terrain, walls, roofs, scenery) shares all of this almost universally: identity transforms and the global scene lighting
static int scene_gl_models_merge_compatible(const GameModel *a,
                                            const GameModel *b) {
    if (a->gl_buffer != b->gl_buffer) {
        return 0;
    }

    // no shader-route check: the two-segment layout gives every run a single shader per segment, and off-Vita there
    // is only one model shader

    // same opacity uniform (both 0 in the opaque pass)
    if (a->transparent != b->transparent) {
        return 0;
    }

    // same cull-pass structure + cull_front uniform sequence
    if (a->gl_all_back_only != b->gl_all_back_only ||
        a->gl_all_front_only != b->gl_all_front_only) {
        return 0;
    }

    // same lighting uniforms
    if (a->unlit != b->unlit || a->light_ambience != b->light_ambience) {
        return 0;
    }

    if (!a->unlit &&
        (a->light_direction_x != b->light_direction_x ||
         a->light_direction_y != b->light_direction_y ||
         a->light_direction_z != b->light_direction_z ||
         a->light_diffuse != b->light_diffuse ||
         a->light_direction_magnitude != b->light_direction_magnitude)) {
        return 0;
    }

    // same model matrix (bit compare: equal bits -> identical transform)
    if (memcmp(a->transform, b->transform, sizeof(mat4)) != 0) {
        return 0;
    }

    return 1;
}

// per-pass submission cache: every GL call between draws makes vitaGL re-dirty and re-upload the shader's entire
// uniform buffer. valid within one opaque pass; scene_gl_render resets it each frame
static struct {
    int valid; // buffer/shader fields hold real state
    int values_valid; // uniform value fields match the GPU side
    Shader *shader;
    gl_vertex_buffer *buffer;
    mat4 model;
    mat4 pvm;
    float ambience;
    int unlit;
    vec3 light_direction;
    float diffuse;
    float opacity;
    int cull_front;
    int cull_gl;
} gl_run_cache;

static const mat4 gl_identity_mat4 = GLM_MAT4_IDENTITY_INIT;

// draw one merged run [ebo_offset, ebo_offset + ebo_length) of the shared buffer, taking every uniform from the run's
// head model. redundant binds/uploads are skipped via gl_run_cache
static void scene_gl_draw_opaque_run(Scene *scene, GameModel *game_model,
                                     Shader *gm, int ebo_offset,
                                     int ebo_length) {
    if (!gl_run_cache.valid || gl_run_cache.buffer != game_model->gl_buffer) {
        vertex_buffer_gl_bind(game_model->gl_buffer);
        gl_run_cache.buffer = game_model->gl_buffer;
    }

    if (!gl_run_cache.valid || gl_run_cache.shader != gm) {
        shader_use(gm);
        gl_run_cache.shader = gm;

        // different program = different uniform store: forget every value
        gl_run_cache.values_valid = 0;
    }

    if (!gl_run_cache.values_valid ||
        memcmp(gl_run_cache.model, game_model->transform, sizeof(mat4)) != 0) {
        shader_set_mat4_loc(gm->loc_model, game_model->transform);
        memcpy(gl_run_cache.model, game_model->transform, sizeof(mat4));
    }

    // world geometry is baked to world space (identity transform), so its PVM is exactly the per-frame
    // projection_view: skip both mat4 multiplies
    mat4 projection_view_model = {0};

    if (memcmp(game_model->transform, gl_identity_mat4, sizeof(mat4)) == 0) {
        glm_mat4_copy(scene->gl_projection_view, projection_view_model);
    } else {
        mat4 view_model = {0};
        glm_mat4_mul(scene->gl_view, game_model->transform, view_model);
        glm_mat4_mul(scene->gl_projection, view_model, projection_view_model);
    }

    if (!gl_run_cache.values_valid ||
        memcmp(gl_run_cache.pvm, projection_view_model, sizeof(mat4)) != 0) {
        shader_set_mat4_loc(gm->loc_projection_view_model,
                            projection_view_model);
        memcpy(gl_run_cache.pvm, projection_view_model, sizeof(mat4));
    }

    if (!gl_run_cache.values_valid ||
        gl_run_cache.ambience != (float)game_model->light_ambience) {
        shader_set_float_loc(gm->loc_light_ambience,
                             game_model->light_ambience);
        gl_run_cache.ambience = (float)game_model->light_ambience;
    }

    if (!gl_run_cache.values_valid || gl_run_cache.unlit != game_model->unlit) {
        shader_set_int_loc(gm->loc_unlit, game_model->unlit);
        gl_run_cache.unlit = game_model->unlit;
    }

    if (!game_model->unlit) {
        vec3 light_direction = {
            VERTEX_TO_FLOAT(game_model->light_direction_x) * VERTEX_SCALE,
            VERTEX_TO_FLOAT(game_model->light_direction_y) * VERTEX_SCALE,
            VERTEX_TO_FLOAT(game_model->light_direction_z) * VERTEX_SCALE};

        float diffuse = ((float)game_model->light_diffuse *
                         (float)game_model->light_direction_magnitude) /
                        256.0f;

        if (!gl_run_cache.values_valid ||
            memcmp(gl_run_cache.light_direction, light_direction,
                   sizeof(vec3)) != 0) {
            shader_set_vec3_loc(gm->loc_light_direction, light_direction);
            memcpy(gl_run_cache.light_direction, light_direction,
                   sizeof(vec3));
        }

        if (!gl_run_cache.values_valid || gl_run_cache.diffuse != diffuse) {
            shader_set_float_loc(gm->loc_light_diffuse, diffuse);
            gl_run_cache.diffuse = diffuse;
        }
    }

    float opacity =
        game_model->transparent ? TRANSLUCENT_MODEL_OPACITY : 1.0f;

    if (!gl_run_cache.values_valid || gl_run_cache.opacity != opacity) {
        shader_set_float_loc(gm->loc_opacity, opacity);
        gl_run_cache.opacity = opacity;
    }

#define GL_RUN_CULL(mode)                                                      \
    do {                                                                       \
        if (!gl_run_cache.valid || gl_run_cache.cull_gl != (int)(mode)) {      \
            glCullFace(mode);                                                  \
            gl_run_cache.cull_gl = (int)(mode);                                \
        }                                                                      \
    } while (0)

#define GL_RUN_CULL_FRONT(value)                                               \
    do {                                                                       \
        if (!gl_run_cache.values_valid ||                                      \
            gl_run_cache.cull_front != (value)) {                              \
            shader_set_int_loc(gm->loc_cull_front, (value));                   \
            gl_run_cache.cull_front = (value);                                 \
        }                                                                      \
    } while (0)

#if defined(__vita__)
    if (game_model->gl_all_back_only) {
        GL_RUN_CULL(GL_FRONT);
        GL_RUN_CULL_FRONT(1);
        glDrawElements(GL_TRIANGLES, ebo_length, GL_UNSIGNED_SHORT,
                       (void *)(ebo_offset * sizeof(GLushort)));
    } else if (game_model->gl_all_front_only) {
        GL_RUN_CULL(GL_BACK);
        GL_RUN_CULL_FRONT(0);
        glDrawElements(GL_TRIANGLES, ebo_length, GL_UNSIGNED_SHORT,
                       (void *)(ebo_offset * sizeof(GLushort)));
    } else {
        GL_RUN_CULL(GL_BACK);
        GL_RUN_CULL_FRONT(0);
        glDrawElements(GL_TRIANGLES, ebo_length, GL_UNSIGNED_SHORT,
                       (void *)(ebo_offset * sizeof(GLushort)));
        GL_RUN_CULL(GL_FRONT);
        GL_RUN_CULL_FRONT(1);
        glDrawElements(GL_TRIANGLES, ebo_length, GL_UNSIGNED_SHORT,
                       (void *)(ebo_offset * sizeof(GLushort)));
    }
#else
    GL_RUN_CULL(GL_BACK);
    GL_RUN_CULL_FRONT(0);
    glDrawElements(GL_TRIANGLES, ebo_length, GL_UNSIGNED_SHORT,
                   (void *)(ebo_offset * sizeof(GLushort)));

    GL_RUN_CULL(GL_FRONT);
    GL_RUN_CULL_FRONT(1);
    glDrawElements(GL_TRIANGLES, ebo_length, GL_UNSIGNED_SHORT,
                   (void *)(ebo_offset * sizeof(GLushort)));
#endif

#undef GL_RUN_CULL
#undef GL_RUN_CULL_FRONT

    gl_run_cache.valid = 1;
    gl_run_cache.values_valid = 1;
}
#endif // RENDER_GL && !GL_PER_FACE_EARLYZ

#if defined(__vita__) && defined(RENDER_GL)
// CPU-side conservative frustum cull (Gribb-Hartmann): rejects models that pass ->visible but sit off the real
// frustum pyramid. planes are extracted from gl_projection_view and the AABB is scaled by VERTEX_TO_FLOAT to match. cglm mat4 is column-major (m[col][row]). rows: left = row3+row0, right = row3-row0, bottom = row3+row1, top = row3-row1, near = row3+row2, far = row3-row2. plane (a,b,c,d): point inside when a*x+b*y+c*z+d >= 0. normalization skipped
static void scene_gl_extract_frustum_planes(mat4 m, float planes[6][4]) {
    // logical M[row][col] == m[col][row]
#define M(row, col) (m[col][row])
    // left = row3 + row0
    planes[0][0] = M(3, 0) + M(0, 0);
    planes[0][1] = M(3, 1) + M(0, 1);
    planes[0][2] = M(3, 2) + M(0, 2);
    planes[0][3] = M(3, 3) + M(0, 3);
    // right = row3 - row0
    planes[1][0] = M(3, 0) - M(0, 0);
    planes[1][1] = M(3, 1) - M(0, 1);
    planes[1][2] = M(3, 2) - M(0, 2);
    planes[1][3] = M(3, 3) - M(0, 3);
    // bottom = row3 + row1
    planes[2][0] = M(3, 0) + M(1, 0);
    planes[2][1] = M(3, 1) + M(1, 1);
    planes[2][2] = M(3, 2) + M(1, 2);
    planes[2][3] = M(3, 3) + M(1, 3);
    // top = row3 - row1
    planes[3][0] = M(3, 0) - M(1, 0);
    planes[3][1] = M(3, 1) - M(1, 1);
    planes[3][2] = M(3, 2) - M(1, 2);
    planes[3][3] = M(3, 3) - M(1, 3);
    // near = row3 + row2
    planes[4][0] = M(3, 0) + M(2, 0);
    planes[4][1] = M(3, 1) + M(2, 1);
    planes[4][2] = M(3, 2) + M(2, 2);
    planes[4][3] = M(3, 3) + M(2, 3);
    // far = row3 - row2
    planes[5][0] = M(3, 0) - M(2, 0);
    planes[5][1] = M(3, 1) - M(2, 1);
    planes[5][2] = M(3, 2) - M(2, 2);
    planes[5][3] = M(3, 3) - M(2, 3);
#undef M
}

// returns 1 only when the AABB is provably fully outside the frustum. AABB ints are world*VERTEX_SCALE, scaled to
// world float to match plane space. p-vertex test: for each plane pick the AABB corner farthest along the normal; if even that corner is behind the plane (with a small epsilon margin), the box is outside. a straddling box is kept
static int scene_gl_aabb_outside_frustum(const float planes[6][4], int min_x,
                                         int min_y, int min_z, int max_x,
                                         int max_y, int max_z) {
    float fminx = VERTEX_TO_FLOAT(min_x);
    float fminy = VERTEX_TO_FLOAT(min_y);
    float fminz = VERTEX_TO_FLOAT(min_z);
    float fmaxx = VERTEX_TO_FLOAT(max_x);
    float fmaxy = VERTEX_TO_FLOAT(max_y);
    float fmaxz = VERTEX_TO_FLOAT(max_z);

    // margin in world-float units: keeps boxes whose p-vertex sits within ~1 world unit of a plane
    const float epsilon = -1.0f;

    for (int i = 0; i < 6; i++) {
        float a = planes[i][0];
        float b = planes[i][1];
        float c = planes[i][2];
        float d = planes[i][3];

        // p-vertex: the corner most in the +normal direction.
        float px = (a >= 0.0f) ? fmaxx : fminx;
        float py = (b >= 0.0f) ? fmaxy : fminy;
        float pz = (c >= 0.0f) ? fmaxz : fminz;

        if (a * px + b * py + c * pz + d < epsilon) {
            // the most-inside corner is still behind this plane, so the whole box is outside: cull
            return 1;
        }
    }

    return 0;
}
#endif

// set the 3D viewport. on Vita the world is stretched to the full 960x544 panel, so scale the requested
// game-resolution viewport up to the panel. while capturing the login background into the off-screen FBO (gl_capture_active) render at native game resolution. non-Vita GL builds use the viewport unchanged
static void scene_gl_set_viewport(Scene *scene, int x, int y, int w, int h) {
#if defined(__vita__) && defined(RENDER_GL)
    if (!scene->surface->gl_capture_active) {
        float sx = 960.0f / scene->surface->mud->game_width;
        float sy = 544.0f / scene->surface->mud->game_height;
        glViewport((int)(x * sx), (int)(y * sy), (int)(w * sx), (int)(h * sy));
        return;
    }
#endif
    glViewport(x, y, w, h);
}

#if defined(__vita__) && defined(RENDER_GL)
// CPU heightfield ray-march for tap-to-walk terrain picking. marches from the camera along the click ray in
// quarter-tile steps until the sample reaches the terrain surface, then bisects to sub-tile precision. world_get_elevation interpolates the same per-tile triangles the terrain meshes are built from. replaces the FBO face-tag pick pass
static void scene_gl_pick_terrain_cpu(Scene *scene) {
    World *world = scene->surface->mud->world;

    float origin_x = VERTEX_TO_FLOAT(scene->camera_x);
    float origin_y = VERTEX_TO_FLOAT(scene->camera_y);
    float origin_z = VERTEX_TO_FLOAT(scene->camera_z);

    float dir_x = scene->gl_mouse_ray[0];
    float dir_y = scene->gl_mouse_ray[1];
    float dir_z = scene->gl_mouse_ray[2];

    float step = VERTEX_TO_FLOAT(TILE_SIZE) / 4.0f;

    float t = 0;
    float t_hit = -1;

    // FLOAT_TO_VERTEX does not parenthesize its argument (f * VERTEX_SCALE), so compound expressions must go through
    // a temporary: passing origin + dir*t directly scales only the last term
    for (int i = 0; i < 2048; i++) {
        t += step;

        float px = origin_x + dir_x * t;
        float py = origin_y + dir_y * t;
        float pz = origin_z + dir_z * t;

        int ex = FLOAT_TO_VERTEX(px);
        int ez = FLOAT_TO_VERTEX(pz);

        float ground_y =
            -VERTEX_TO_FLOAT((float)world_get_elevation(world, ex, ez));

        if (py >= ground_y) {
            t_hit = t;
            break;
        }
    }

    if (t_hit >= 0) {
        float lo = t_hit - step;
        float hi = t_hit;

        for (int i = 0; i < 16; i++) {
            float mid = (lo + hi) * 0.5f;

            float px = origin_x + dir_x * mid;
            float py = origin_y + dir_y * mid;
            float pz = origin_z + dir_z * mid;

            int ex = FLOAT_TO_VERTEX(px);
            int ez = FLOAT_TO_VERTEX(pz);

            float ground_y =
                -VERTEX_TO_FLOAT((float)world_get_elevation(world, ex, ez));

            if (py >= ground_y) {
                hi = mid;
            } else {
                lo = mid;
            }
        }

        t = hi;
    }

    float final_x = origin_x + dir_x * t;
    float final_z = origin_z + dir_z * t;

    scene->gl_terrain_pick_x = FLOAT_TO_VERTEX(final_x) / MAGIC_LOC;
    scene->gl_terrain_pick_y = FLOAT_TO_VERTEX(final_z) / MAGIC_LOC;

    scene->gl_terrain_pick_step = GL_PICK_STEP_FINISHED;
}
#endif

void scene_gl_render(Scene *scene) {

    int scene_height = scene->gl_height - 1;

#if defined(__vita__) && defined(RENDER_GL)
    // build the 6 view-frustum planes once per frame from the current combined matrix. used below to skip models
    // whose world AABB is fully off-frustum
    float gl_frustum_planes[6][4];
    scene_gl_extract_frustum_planes(scene->gl_projection_view,
                                    gl_frustum_planes);
#endif

    int old_width = scene->surface->width;
    int old_height = scene->surface->height;

    scene->surface->width = scene->width;

    scene->surface->height =
        scene_height + 12 - mudclient_is_ui_scaled(scene->surface->mud);

    surface_reset_bounds(scene->surface);

    game_model_project_view(scene->view, scene->camera_x, scene->camera_y,
                            scene->camera_z, scene->camera_yaw,
                            scene->camera_pitch, scene->camera_roll,
                            scene->view_distance, scene->clip_near);

    scene->visible_polygons_count = 0;

    scene_initialise_polygons_2d(scene);

    for (int i = 0; i < scene->visible_polygons_count; i++) {
        GamePolygon *polygon = scene->visible_polygons[i];
        scene_render_polygon_2d_face(scene, polygon->face);
    }

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

#if defined(__vita__) && defined(RENDER_GL)
    // GL_BLEND is enabled globally (mudclient.c) and never turned off. disable it for the opaque pass; restored at
    // the end of this function for the translucent crystal and the 2D UI flush
    glDisable(GL_BLEND);
#endif

    scene_gl_set_viewport(scene, 0, 13, scene->width, scene_height);

    shader_use(&scene->game_model_shader);

    shader_set_int_loc(scene->game_model_shader.loc_fog_distance,
                       scene->fog_z_distance);

    shader_set_float_loc(scene->game_model_shader.loc_scroll_texture,
                         scene->gl_scroll_texture_position / GL_TEXTURE_SIZE);

#if defined(__vita__) && defined(RENDER_GL)
    // the no-discard shader needs the same per-frame fog + scroll uniforms: set them on its program, then re-activate
    // the clip shader
    shader_use(&scene->game_model_shader_noclip);
    shader_set_int_loc(scene->game_model_shader_noclip.loc_fog_distance,
                       scene->fog_z_distance);
    shader_set_float_loc(scene->game_model_shader_noclip.loc_scroll_texture,
                         scene->gl_scroll_texture_position / GL_TEXTURE_SIZE);
    shader_use(&scene->game_model_shader);
#endif

    if (scene->gl_scroll_texture_position <= 0) {
        scene->gl_scroll_texture_position = SCROLL_TEXTURE_SIZE;
    }

    scene->gl_scroll_texture_position--;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene->gl_model_texture);

    vec3 ray_start = {VERTEX_TO_FLOAT(scene->camera_x),
                      VERTEX_TO_FLOAT(scene->camera_y),
                      VERTEX_TO_FLOAT(scene->camera_z)};

    vec3 ray_end = {0};

    glm_vec3_add(ray_start, scene->gl_mouse_ray, ray_end);

#if defined(__vita__) && defined(RENDER_GL)
    // tap-to-walk terrain picking is resolved entirely on the CPU (see scene_gl_pick_terrain_cpu)
    if (scene->gl_terrain_pick_step == GL_PICK_STEP_SAMPLE) {
        scene_gl_pick_terrain_cpu(scene);
    }
#elif defined(EMSCRIPTEN)
    // RGBA face-tag pick: candidate terrain is rendered with a flat shader that writes each tile's face_tag into the
    // colour buffer, then the picked pixel's colour is read back and the tag decoded
    if (scene->gl_terrain_pick_step == GL_PICK_STEP_SAMPLE) {
        GameModel *terrain_picked[4] = {0};
        int terrain_picked_length = 0;

        for (int i = 0; i < scene->model_count; i++) {
            GameModel *game_model = scene->models[i];

            if (!game_model->autocommit || game_model->unpickable) {
                continue;
            }

            float time = game_model_gl_intersects(game_model,
                                                  scene->gl_mouse_ray, ray_end);

            if (time >= 0 && terrain_picked_length < 4) {
                terrain_picked[terrain_picked_length++] = game_model;
            }
        }

        glDisable(GL_CULL_FACE);

        shader_use(&scene->game_model_pick_shader);

        game_model_gl_buffer_pick_models(&scene->gl_pick_buffer, terrain_picked,
                                         terrain_picked_length);

        for (int i = 0; i < terrain_picked_length; i++) {
            GameModel *game_model = terrain_picked[i];

            mat4 projection_view_model = {0};

            glm_mat4_mul(scene->gl_view, game_model->transform,
                         projection_view_model);

            glm_mat4_mul(scene->gl_projection, projection_view_model,
                         projection_view_model);

            shader_set_mat4(&scene->game_model_pick_shader,
                            "projection_view_model", projection_view_model);

            glDrawElements(
                GL_TRIANGLES, game_model->gl_ebo_length, GL_UNSIGNED_SHORT,
                (void *)(game_model->gl_pick_ebo_offset * sizeof(GLushort)));
        }

        int mouse_x = scene->mouse_x + (scene->surface->width / 2);
        int mouse_y = scene->surface->height - scene->mouse_y;

        uint8_t pick_colour[4] = {0};

        glReadPixels(mouse_x, mouse_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                     pick_colour);

        glClear(GL_DEPTH_BUFFER_BIT);

        scene->gl_terrain_pick_step = GL_PICK_STEP_FINISHED;
        scene->gl_pick_face_tag = (pick_colour[1] << 8) + pick_colour[0];

        shader_use(&scene->game_model_shader);

        glEnable(GL_CULL_FACE);
    }
#else
    /* draw the terrain first for potential mouse picking, since we can click
     * through the unpickable models */
    for (int i = 0; i < scene->model_count; i++) {
        GameModel *game_model = scene->models[i];

        if (game_model->autocommit && !game_model->unpickable) {
            scene_gl_draw_game_model(scene, game_model);
            game_model->gl_invisible = 1;
        }
    }

    if (scene->gl_terrain_pick_step == GL_PICK_STEP_SAMPLE) {
        int mouse_x = scene->mouse_x + (scene->surface->width / 2);
        int mouse_y = scene->surface->height - scene->mouse_y;

        float mouse_z = 0;

        // depth-readback pick for native desktop GL builds. Vita uses the RGBA face-tag pick above instead (vitaGL
        // glReadPixels has no GL_DEPTH_COMPONENT case)
        glReadPixels(mouse_x, mouse_y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
                     &mouse_z);

        vec3 position = {(float)mouse_x, (float)mouse_y, mouse_z};
        vec4 bounds = {0, 0, scene->surface->width, scene->surface->height};

        glm_unproject(position, scene->gl_projection_view, bounds,
                      scene->gl_mouse_world);

        scene->gl_terrain_pick_step = GL_PICK_STEP_FINISHED;

        scene->gl_terrain_pick_x =
            FLOAT_TO_VERTEX(scene->gl_mouse_world[0]) / MAGIC_LOC;

        scene->gl_terrain_pick_y =
            FLOAT_TO_VERTEX(scene->gl_mouse_world[2]) / MAGIC_LOC;
    }
#endif

    // pass 1: keep the mouse-pick ray test iterating scene->models[] in insertion order, but instead of drawing each
    // opaque model, collect it into gl_opaque_sorted with its squared camera distance so pass 2 can draw front-to-back

    int opaque_count = 0;

    for (int i = 0; i < scene->model_count; i++) {
        GameModel *game_model = scene->models[i];

        if (scene->mouse_picking_active && !game_model->unpickable) {
            float time = game_model_gl_intersects(game_model,
                                                  scene->gl_mouse_ray, ray_end);

            if (time >= 0) {
                if (game_model->autocommit) {
                    scene->gl_terrain_walkable = 1;
                } else {
                    GlModelTime model_time = {game_model, time};

                    if (scene->gl_mouse_picked_size <
                        (scene->gl_mouse_picked_count / 2)) {
                        size_t new_size = scene->gl_mouse_picked_count * 2;
                        void *new_ptr = NULL;

                        new_ptr = realloc(scene->gl_mouse_picked_time,
                                          new_size * sizeof(GlModelTime *));
                        if (new_ptr == NULL) {
                            return;
                        }
                        scene->gl_mouse_picked_time = new_ptr;
                        scene->gl_mouse_picked_size = new_size;
                    }

                    scene->gl_mouse_picked_time[scene->gl_mouse_picked_count] =
                        model_time;

                    scene->gl_mouse_picked_count++;
                }
            }
        }

        if (!game_model->gl_invisible && !game_model->transparent) {
            if (game_model->gl_ebo_offset == -1) {
                // never buffered: nothing to draw or bridge
                game_model->gl_invisible = 0;
                continue;
            }

            // hidden = provably contributes no pixels this frame. still collected: a state-compatible hidden model
            // can bridge two mergeable runs into one glDrawElements
            int hidden = !game_model->visible;

#if defined(__vita__) && defined(RENDER_GL)
            // conservative 6-plane frustum cull; models with no faces (min_x > max_x) are left alone
            if (!hidden && game_model->max_x >= game_model->min_x &&
                scene_gl_aabb_outside_frustum(
                    gl_frustum_planes, game_model->min_x, game_model->min_y,
                    game_model->min_z, game_model->max_x, game_model->max_y,
                    game_model->max_z)) {
                hidden = 1;
            }
#endif
#if defined(RENDER_3DS_GL) || GL_PER_FACE_EARLYZ
            // AABB centre vs camera (world/vertex units). coords reach ~1e6 so the squared distance accumulates in
            // int64_t (long is 32-bit on Vita)
            int64_t cx = ((int64_t)game_model->min_x + game_model->max_x) / 2;
            int64_t cy = ((int64_t)game_model->min_y + game_model->max_y) / 2;
            int64_t cz = ((int64_t)game_model->min_z + game_model->max_z) / 2;
            int64_t dx = cx - scene->camera_x, dy = cy - scene->camera_y,
                    dz = cz - scene->camera_z;
            scene->gl_opaque_sorted[opaque_count].dist2 =
                dx * dx + dy * dy + dz * dz;
#endif
            // hidden models stay in the array: they anchor run bridging in the walk below
            scene->gl_opaque_sorted[opaque_count].hidden = hidden;
            scene->gl_opaque_sorted[opaque_count].model = game_model;
            opaque_count++;
        }

        game_model->gl_invisible = 0;
    }

#if defined(RENDER_GL) && !GL_PER_FACE_EARLYZ
    // pass 2: sort the collected opaque models into shared-buffer order and draw them as merged runs: consecutive
    // models whose index ranges are back-to-back and whose GL state is bit-identical (see scene_gl_models_merge_compatible) collapse into one glDrawElements. the depth buffer keeps the result pixel-identical to any draw order
    // other passes (2D flush, transparent, pick) changed GL state since last
    // frame: start the submission cache cold
    gl_run_cache.valid = 0;
    gl_run_cache.values_valid = 0;

    qsort(scene->gl_opaque_sorted, opaque_count, sizeof(GlOpaqueSortEntry),
          scene_gl_opaque_buffer_order_compare);

#if defined(__vita__)
    // two walks over the two-segment family layout: segment A (noclip-safe faces, no-discard early-Z shader) then
    // segment B (clip faces, clip shader). both segments assign slices over the same model sequence, so one (buffer, A-offset, B-offset) sort orders both walks. merged runs fuse the slices of adjacent visible models; a hidden model breaks the run. runs are drawn front to back
    typedef struct {
        GameModel *head;
        int offset;
        int length;
        int64_t dist2;
    } GlSegmentRun;

    GlSegmentRun segment_runs[opaque_count > 0 ? opaque_count : 1];

    for (int segment = 0; segment < 2; segment++) {
        Shader *segment_shader = segment == 0
                                     ? &scene->game_model_shader_noclip
                                     : &scene->game_model_shader;

        int run_count = 0;
        int di = 0;

        while (di < opaque_count) {
            GlOpaqueSortEntry *head_entry = &scene->gl_opaque_sorted[di];
            GameModel *head = head_entry->model;

            int head_offset = segment == 0 ? head->gl_ebo_offset
                                           : head->gl_clip_ebo_offset;
            int head_length = segment == 0 ? head->gl_noclip_ebo_length
                                           : head->gl_clip_ebo_length;

            if (head_entry->hidden || head_length <= 0) {
                di++;
                continue;
            }

            int run_offset = head_offset;
            int run_end = head_offset + head_length;

            // bridged runs: a hidden (frustum-culled) model between two visible ones no longer breaks the run, its
            // slice is drawn through. visible_end trims trailing bridged geometry off the final draw; the per-run bridge budget bounds the extra vertex work
            int visible_end = run_end;
            int bridged = 0;

            int dj = di + 1;

            while (dj < opaque_count) {
                GlOpaqueSortEntry *next_entry = &scene->gl_opaque_sorted[dj];
                GameModel *next = next_entry->model;

                int next_offset = segment == 0 ? next->gl_ebo_offset
                                               : next->gl_clip_ebo_offset;
                int next_length = segment == 0 ? next->gl_noclip_ebo_length
                                               : next->gl_clip_ebo_length;

                if (next_offset != run_end) {
                    break;
                }

                // a zero-length slice (empty chunk, or a model with no faces in this segment) draws nothing and
                // cannot break adjacency: pass through unconditionally
                if (next_length == 0) {
                    dj++;
                    continue;
                }

                if (next_entry->hidden) {
                    // a hidden model's slice emits zero fragments under any uniforms or cull mode: only the transform
                    // must match, not the full merge key
                    if (memcmp(head->transform, next->transform,
                               sizeof(mat4)) != 0 ||
                        bridged + next_length > 16384) {
                        break;
                    }

                    bridged += next_length;
                    run_end += next_length;
                } else {
                    if (!scene_gl_models_merge_compatible(head, next)) {
                        break;
                    }

                    run_end += next_length;
                    visible_end = run_end;
                }

                dj++;
            }

            run_end = visible_end;

            // squared camera->AABB-centre distance of the run's head chunk;
            // world coords reach ~1e6 so the squares need int64
            int64_t cx = ((int64_t)head->min_x + head->max_x) / 2;
            int64_t cy = ((int64_t)head->min_y + head->max_y) / 2;
            int64_t cz = ((int64_t)head->min_z + head->max_z) / 2;
            int64_t dx = cx - scene->camera_x;
            int64_t dy = cy - scene->camera_y;
            int64_t dz = cz - scene->camera_z;

            segment_runs[run_count].head = head;
            segment_runs[run_count].offset = run_offset;
            segment_runs[run_count].length = run_end - run_offset;
            segment_runs[run_count].dist2 = dx * dx + dy * dy + dz * dz;
            run_count++;

            di = dj;
        }

        // nearest-first insertion sort
        for (int i = 1; i < run_count; i++) {
            int j = i;

            while (j > 0 &&
                   segment_runs[j - 1].dist2 > segment_runs[j].dist2) {
                GlSegmentRun swap = segment_runs[j - 1];
                segment_runs[j - 1] = segment_runs[j];
                segment_runs[j] = swap;
                j--;
            }
        }

        for (int i = 0; i < run_count; i++) {
            scene_gl_draw_opaque_run(scene, segment_runs[i].head,
                                     segment_shader, segment_runs[i].offset,
                                     segment_runs[i].length);
        }
    }
#else
    // desktop: single-range layout, one shader
    int di = 0;

    while (di < opaque_count) {
        GlOpaqueSortEntry *head_entry = &scene->gl_opaque_sorted[di];
        GameModel *head = head_entry->model;

        // hidden models never start a run; they only bridge one (below)
        if (head_entry->hidden) {
            di++;
            continue;
        }

        int run_offset = head->gl_ebo_offset;
        int run_end = head->gl_ebo_offset + head->gl_ebo_length;

        // end of the last visible model: trailing hidden (bridge-only) models are trimmed off the final draw
        int visible_end = run_end;

        int dj = di + 1;

        while (dj < opaque_count) {
            GlOpaqueSortEntry *next_entry = &scene->gl_opaque_sorted[dj];
            GameModel *next = next_entry->model;

            if (next->gl_ebo_offset != run_end ||
                !scene_gl_models_merge_compatible(head, next)) {
                break;
            }

            run_end += next->gl_ebo_length;

            if (!next_entry->hidden) {
                visible_end = run_end;
            }

            dj++;
        }

        scene_gl_draw_opaque_run(scene, head, &scene->game_model_shader,
                                 run_offset, visible_end - run_offset);
        di = dj;
    }
#endif // __vita__
#else
    // pass 2: draw the collected opaque models nearest-first
    qsort(scene->gl_opaque_sorted, opaque_count, sizeof(GlOpaqueSortEntry),
          scene_gl_opaque_depth_compare);

    for (int i = 0; i < opaque_count; i++) {
        scene_gl_draw_game_model(scene, scene->gl_opaque_sorted[i].model);
    }
#endif

    qsort(scene->gl_mouse_picked_time, scene->gl_mouse_picked_count,
          sizeof(GlModelTime), scene_gl_model_time_compare);

    for (int i = 0; i < scene->gl_mouse_picked_count; i++) {
        scene_mouse_pick(scene, scene->gl_mouse_picked_time[i].game_model, -1);
    }

    scene->surface->width = old_width;
    scene->surface->height = old_height;

    surface_reset_bounds(scene->surface);

#if defined(__vita__) && defined(RENDER_GL)
    glEnable(GL_BLEND); // restore for the translucent crystal + the 2D UI flush
#endif

    scene_gl_set_viewport(scene, 0, 0, scene->surface->mud->game_width,
                          scene->surface->mud->game_height);
}

/* draw translucent models (giant crystal) */
void scene_gl_render_transparent_models(Scene *scene) {
    int scene_height = scene->gl_height - 1;

#if defined(__vita__) && defined(RENDER_GL)
    // gl_projection_view is still the current frame's matrix (camera unchanged since scene_gl_render): extract the
    // planes again for this pass
    float gl_frustum_planes[6][4];
    scene_gl_extract_frustum_planes(scene->gl_projection_view,
                                    gl_frustum_planes);
#endif

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    scene_gl_set_viewport(scene, 0, 13, scene->width, scene_height);

    shader_use(&scene->game_model_shader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene->gl_model_texture);

    for (int i = 0; i < scene->model_count; i++) {
        GameModel *game_model = scene->models[i];

        if (game_model->transparent) {
#if defined(__vita__) && defined(RENDER_GL)
            if (game_model->gl_ebo_offset != -1 && game_model->visible &&
                game_model->max_x >= game_model->min_x &&
                scene_gl_aabb_outside_frustum(
                    gl_frustum_planes, game_model->min_x, game_model->min_y,
                    game_model->min_z, game_model->max_x, game_model->max_y,
                    game_model->max_z)) {
                continue;
            }
#endif
            scene_gl_draw_game_model(scene, game_model);
        }
    }

    scene_gl_set_viewport(scene, 0, 0, scene->surface->mud->game_width,
                          scene->surface->mud->game_height);
}

#endif
