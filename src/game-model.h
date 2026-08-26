#ifndef _H_GAME_MODEL
#define _H_GAME_MODEL

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
#include <cglm/cglm.h>

#include "gl/vertex-buffer.h"

typedef struct gl_model_vertex {
    // position keeps full float range (world coords); everything else packed to 16-bit, the GPU widens SHORT -> float
    // (and normalized SHORT -> [-1,1]) at fetch. layout puts every field on its natural boundary, shrinking the vertex 60 -> 40 bytes
    float x, y, z; // offset  0, 12B
    // normal xyz + magnitude, stored raw as SHORT (sources are int16, no normalize)
    int16_t normal[4]; // offset 12,  8B  was float normal_x,y,z,mag
    // { face_intensity, vertex_intensity+ambience }: SHORT, not normalized; USE_GOURAUD sentinel 32767 = INT16_MAX
    int16_t lighting[2]; // offset 20,  4B  was float face/vertex intensity
    // RSC colours are 5-bit (multiples of 8), each fits a normalized byte. GL_UNSIGNED_BYTE + normalized -> shader
    // float3 colours. [4] keeps 4-byte alignment, index 3 is opaque padding
    unsigned char front_colour[4]; // offset 24,  4B
    // texcoords quantized to S16-normalized (round(clamp(uv,-1,1)*32767)); the GPU expands back to [-1,1] float
    int16_t front_tex[2]; // offset 28,  4B  was float front_texture_u,v
    unsigned char back_colour[4]; // offset 32,  4B
    int16_t back_tex[2]; // offset 36,  4B  was float back_texture_u,v
} gl_model_vertex; // total   40B

extern float gl_tri_face_us[];
extern float gl_tri_face_vs[];

extern float gl_quad_face_us[];
extern float gl_quad_face_vs[];

typedef struct gl_face_fill {
    float r, g, b;
    int texture_index;
} gl_face_fill;
#endif

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

#if defined(EMSCRIPTEN) || defined(__vita__)
typedef struct gl_pick_vertex {
    float x, y, z;
    float r, g;
} gl_pick_vertex;
#endif
#elif defined(RENDER_3DS_GL)
#include <citro3d.h>
#endif

/* states */
#define GAME_MODEL_TRANSFORM_BEGIN 1
#define GAME_MODEL_TRANSFORM_RESET 2

/* types */
#define GAME_MODEL_TRANSFORM_TRANSLATE 1
#define GAME_MODEL_TRANSFORM_ROTATE 2

/* originally 12345678 - allows saving memory */
#define GAME_MODEL_USE_GOURAUD INT16_MAX

typedef struct GameModel GameModel;

#include "scene.h"
#include "utility.h"

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
#include "gl/textures/model_textures.h"
#endif

struct GameModel {
    uint16_t vertex_count;
    int16_t *project_vertex_x;
    int16_t *project_vertex_y;
    int16_t *project_vertex_z;
    int32_t *vertex_view_x;
    int32_t *vertex_view_y;
    int16_t *vertex_intensity;
    int8_t *vertex_ambience;

    uint16_t face_count;
    uint8_t *face_vertex_count;
    uint16_t **face_vertices;
    int16_t *face_fill_front;
    int16_t *face_fill_back;
    int16_t *face_intensity;
    int16_t *face_normal_x;
    int16_t *face_normal_y;
    int16_t *face_normal_z;
    int16_t *normal_scale;
    int32_t *normal_magnitude;

    int depth;
    int8_t visible;

    int min_x;
    int max_x;
    int min_y;
    int max_y;
    int min_z;
    int max_z;

    /* used for walls */
    int8_t unpickable;

    /* used to identify the model in mouse picking. stores entity index */
    int key;

    /* used to determine which face is selected for mouse picking. used with
     * world->local_x and world->local_y */
    int *face_tag;

    int8_t transparent;
    int8_t *is_local_player;
    int8_t isolated;
    int8_t projected;
    uint16_t max_vertices;
    int16_t *vertex_x;
    int16_t *vertex_y;
    int16_t *vertex_z;
    int16_t *vertex_transformed_x;
    int16_t *vertex_transformed_y;
    int16_t *vertex_transformed_z;

    int8_t unlit;
    int light_ambience;
    int light_diffuse;
    int light_direction_x;
    int light_direction_y;
    int light_direction_z;
    int light_direction_magnitude;

    /* treat vertex_ arrays as vertex_transformed_. used for geneated terrain,
     * wall and roof models */
    int8_t autocommit;

    uint16_t max_faces;

    int base_x;
    int base_y;
    int base_z;
    int orientation_yaw;
    int orientation_pitch;
    int orientation_roll;
    int transform_type;
    int transform_state;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    int gl_vbo_offset;
    int gl_ebo_offset;
    int gl_ebo_length;

    // every face has a transparent front (all_back_only, e.g. terrain) or transparent back (all_front_only, e.g.
    // decorations); such a model is one-sided and needs only one cull pass
    int gl_all_front_only;
    int gl_all_back_only;

    // no face has a textured fill (all colour fills / transparent), so it uses the no-discard early-Z shader
    int gl_all_flat;

    // no drawn face samples an alpha-transparent texture on either side (flat colours + opaque textures only), so
    // route to the no-discard shader
    int gl_noclip_safe;

    // the model's EBO range is partitioned so the opaque/no-clip faces' indices come first; this is the count of
    // those leading indices. the opaque range draws with the no-discard shader, the rest with the clip shader. transparent models force this to 0
    int gl_noclip_ebo_length;

    // two-segment family layout: each shared buffer's EBO holds all models' noclip-safe indices first (segment A),
    // then all models' clip indices (segment B). gl_ebo_offset/gl_noclip_ebo_length address this model's slice of segment A, these two address its slice of segment B. gl_ebo_length is the model's total index count
    int gl_clip_ebo_offset;
    int gl_clip_ebo_length;

    int gl_invisible;

    mat4 transform;

    gl_vertex_buffer *gl_buffer;
#endif
#if defined(RENDER_GL) && (defined(EMSCRIPTEN) || defined(__vita__))
    int gl_pick_vbo_offset;
    int gl_pick_ebo_offset;
#endif

    // open-addressed (x,y,z) -> vertex index table, game_model_vertex_at is O(1) not a linear scan. built lazily on
    // first vertex_at, kept in sync by create_vertex, dropped whenever vertex coordinates can change. empty slots are -1
    int32_t *vertex_hash;
    int32_t vertex_hash_mask;

    // optional slab for face index arrays (world split pieces). when faces_pooled is set, face_vertices[] entries
    // point into the slab and are freed with it, never individually
    uint16_t *face_pool;
    int32_t face_pool_used;
    int8_t faces_pooled;
};

void game_model_new(GameModel *game_model);
void game_model_new_alloc(GameModel *game_model, int vertex_count,
                          int face_count);
void game_model_new_merge(GameModel *game_model, GameModel **pieces, int count);
void game_model_new_merge_flags(GameModel *game_model, GameModel **pieces,
                                int count, int autocommit, int isolated,
                                int unlit, int unpickable);
void game_model_new_alloc_flags(GameModel *game_model, int vertex_count,
                                int face_count, int autocommit, int isolated,
                                int unlit, int unpickable, int projected);
void game_model_new_ob3(GameModel *game_model, int8_t *data, size_t len);
void game_model_reset(GameModel *game_model);
void game_model_allocate(GameModel *game_model, int vertex_count,
                         int face_count);
void game_model_projection_prepare(GameModel *game_model);
void game_model_clear(GameModel *game_model);
void game_model_reduce(GameModel *game_model, int delta_faces,
                       int delta_vertices);
void game_model_merge(GameModel *game_model, GameModel **pieces, int count);
int game_model_vertex_at(GameModel *game_model, int x, int y, int z);
int game_model_create_vertex(GameModel *game_model, int x, int y, int z);
int game_model_create_face(GameModel *game_model, int number,
                           uint16_t *vertices, int fill_front, int fill_back);
void game_model_split(GameModel *game_model, GameModel **pieces, int piece_dx,
                      int piece_dz, int rows, int count, int piece_max_vertices,
                      int pickable);
void game_model_copy_lighting(GameModel *game_model, GameModel *model,
                              uint16_t *src_vertices, int vertex_count,
                              int in_face);
void game_model_set_light_dir(GameModel *game_model, int x, int y, int z);
void game_model_set_light_intensity(GameModel *game_model, int ambience,
                                    int diffuse, int x, int y, int z);
void game_model_set_light(GameModel *game_model, int gouraud, int ambience,
                          int diffuse, int x, int y, int z);
void game_model_set_vertex_ambience(GameModel *game_model, int vertex_index,
                                    int ambience);
void game_model_orient(GameModel *game_model, int yaw, int pitch, int roll);
void game_model_rotate(GameModel *game_model, int yaw, int pitch, int roll);
void game_model_place(GameModel *game_model, int x, int y, int z);
void game_model_translate(GameModel *game_model, int x, int y, int z);
void game_model_determine_transform_type(GameModel *game_model);
void game_model_apply_translate(GameModel *game_model, int dx, int dy, int dz);
void game_model_apply_rotation(GameModel *game_model, int yaw, int roll,
                               int pitch);

void game_model_compute_bounds(GameModel *game_model);
void game_model_get_face_normals(GameModel *game_model, int16_t *vertex_x,
                                 int16_t *vertex_y, int16_t *vertex_z,
                                 int16_t *face_normal_x, int16_t *face_normal_y,
                                 int16_t *face_normal_z, int reset_scale);
void game_model_get_vertex_normals(GameModel *game_model,
                                   int16_t *face_normal_x,
                                   int16_t *face_normal_y,
                                   int16_t *face_normal_z, int16_t *normal_x,
                                   int16_t *normal_y, int16_t *normal_z,
                                   int32_t *normal_magnitude);
void game_model_light(GameModel *game_model);
void game_model_relight(GameModel *game_model);
void game_model_reset_transform(GameModel *game_model);
void game_model_apply(GameModel *game_model);
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void game_model_gl_bake_transform(GameModel *game_model, mat4 out);
#endif
void game_model_project_view(GameModel *game_model, int camera_x, int camera_y,
                             int camera_z, int camera_pitch, int camera_roll,
                             int camera_yaw, int view_distance, int clip_near);
void game_model_project(GameModel *game_model, int camera_x, int camera_y,
                        int camera_z, int camera_pitch, int camera_roll,
                        int camera_yaw, int view_distance, int clip_near);
void game_model_commit(GameModel *game_model);
GameModel *game_model_copy(GameModel *game_model);
GameModel *game_model_copy_flags(GameModel *game_model, int autocommit,
                                 int isolated, int unlit, int pickable);
void game_model_copy_position(GameModel *game_model, GameModel *source);
void game_model_destroy(GameModel *game_model);
void game_model_dump(GameModel *game_model, char *file_name);
void game_model_mask_faces(GameModel *game_model, int16_t *face_fill,
                           int mask_colour);

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void game_model_gl_decode_face_fill(int face_fill, gl_face_fill *vbo_face_fill);
void game_model_gl_unwrap_uvs(GameModel *game_model, uint16_t *face_vertices,
                              int face_vertex_count, float *us, float *vs);
void gl_offset_texture_uvs_atlas(gl_atlas_position texture_position,
                                 float *texture_x, float *texture_y);
int game_model_gl_classify(GameModel *game_model, int *face_noclip);
void game_model_gl_buffer_arrays(GameModel *game_model, int *vertex_offset,
                                 int *ebo_offset);
void game_model_get_vertex_ebo_lengths(GameModel **game_models, int length,
                                       int *vertex_count, int *ebo_length);
float game_model_gl_intersects(GameModel *game_model, vec3 ray_start,
                               vec3 ray_end);
void game_model_gl_create_buffer(gl_vertex_buffer *vertex_buffer,
                                 int vbo_length, int ebo_length,
                                 int deferred);
/*void game_model_gl_buffer_models(gl_vertex_buffer *vertex_buffer,
                                 GameModel **game_models, int length);*/
#ifdef RENDER_GL
// render-thread half of a deferred (worker-built) buffer: creates the GL
// objects, uploads everything the worker staged, and adds the attributes
void game_model_gl_realize_buffer(gl_vertex_buffer *vertex_buffer,
                                  int keep_mirror);
// incremental variant: begin (objects + attributes) then step the upload in
// chunks over frames; step returns 1 when the buffer is fully uploaded
void game_model_gl_realize_buffer_begin(gl_vertex_buffer *vertex_buffer);
int game_model_gl_realize_buffer_step(gl_vertex_buffer *vertex_buffer,
                                      int max_bytes, int keep_mirror);
#endif

// keep_mirror keeps the buffers' CPU staging mirrors alive after the flush
int game_model_gl_buffer_models(gl_vertex_buffer ***vertex_buffers,
                                int *vertex_buffers_length,
                                GameModel **game_models,
                                int game_models_length, int keep_mirror,
                                int deferred);
#endif
#if defined(RENDER_GL) && (defined(EMSCRIPTEN) || defined(__vita__))
void game_model_gl_create_pick_buffer(gl_vertex_buffer *pick_buffer,
                                      int vbo_length, int ebo_length);
void game_model_gl_buffer_pick_arrays(GameModel *game_model, int *vertex_offset,
                                      int *ebo_offset);
void game_model_gl_buffer_pick_models(gl_vertex_buffer *pick_buffer,
                                      GameModel **game_models, int length);
#endif
#endif
