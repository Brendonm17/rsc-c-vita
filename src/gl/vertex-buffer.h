#ifndef _H_VERTEX_BUFFER
#define _H_VERTEX_BUFFER

#include <stdlib.h>

#include "../utility.h"

#ifdef RENDER_GL
#if defined(__vita__)
#include <vitaGL.h>
#elif defined(GLAD)
#include <glad/glad.h>
#else
#include <GL/glew.h>
#include <GL/glu.h>
#endif
#elif defined(RENDER_3DS_GL)
#include <citro3d.h>
#include <tex3ds.h>
#endif

typedef struct gl_vertex_buffer {
    int vertex_length;
    int attribute_index;

#ifdef RENDER_GL
    GLuint vao;
    GLuint vbo;
    GLuint ebo;

    // byte sizes fixed at creation
    int vbo_size;
    int ebo_size;

    // CPU staging mirrors + dirty ranges for batched uploads; allocated lazily, freed on flush unless kept
    uint8_t *vbo_mirror;
    uint8_t *ebo_mirror;
    int vbo_dirty_min;
    int vbo_dirty_max;
    int ebo_dirty_min;
    int ebo_dirty_max;

    // no GL objects yet; writes go to mirrors, bind/flush skip GL until realize
    int gl_deferred;

    // bytes uploaded so far across [vbo then ebo], for incremental realize
    int realize_cursor;
#elif defined(RENDER_3DS_GL)
    C3D_AttrInfo attr_info;
    C3D_BufInfo buf_info;
    void *vbo;
    void *ebo;
#endif
} gl_vertex_buffer;

void vertex_buffer_gl_new(gl_vertex_buffer *vertex_buffer, int vertex_length,
                          int vbo_length, int ebo_length);
void vertex_buffer_gl_bind(gl_vertex_buffer *vertex_buffer);

// float attribute helper: offset counted in floats, advanced by attribute_length
// for entirely float-typed layouts (flat surface, pick buffers)
void vertex_buffer_gl_add_attribute(gl_vertex_buffer *vertex_buffer,
                                    int *attribute_offset,
                                    int attribute_length);

// general attribute helper: explicit byte offset (offsetof), for vertices mixing float and byte fields
// normalized GL_TRUE maps byte 0..255 to float 0..1; 3DS ignores gl_offset, uses c3d_format/c3d_count
#ifdef RENDER_GL
void vertex_buffer_gl_add_attribute_ex(gl_vertex_buffer *vertex_buffer,
                                       int gl_size, GLenum gl_type,
                                       GLboolean normalized, size_t gl_offset);
#elif defined(RENDER_3DS_GL)
void vertex_buffer_gl_add_attribute_ex(gl_vertex_buffer *vertex_buffer,
                                       int gl_size, GPU_FORMATS c3d_format,
                                       int c3d_count, size_t gl_offset);
#endif

void vertex_buffer_gl_destroy(gl_vertex_buffer *vertex_buffer);

#ifdef RENDER_GL
// staged writes into the CPU mirrors
void vertex_buffer_gl_write_vbo(gl_vertex_buffer *vertex_buffer, int offset,
                                int size, const void *data);
void vertex_buffer_gl_write_ebo(gl_vertex_buffer *vertex_buffer, int offset,
                                int size, const void *data);

// reserve a slice, write through the returned pointer; NULL on OOM or out of range
void *vertex_buffer_gl_stage_vbo(gl_vertex_buffer *vertex_buffer, int offset,
                                 int size);
void *vertex_buffer_gl_stage_ebo(gl_vertex_buffer *vertex_buffer, int offset,
                                 int size);

// upload dirty mirror ranges (one glBufferSubData per target), free mirrors unless keep_mirror
void vertex_buffer_gl_flush(gl_vertex_buffer *vertex_buffer, int keep_mirror);

// off-thread pair: _new_deferred records sizes and buffers writes, no GL; _realize creates GL and uploads
void vertex_buffer_gl_new_deferred(gl_vertex_buffer *vertex_buffer,
                                   int vertex_length, int vbo_length,
                                   int ebo_length);
void vertex_buffer_gl_realize(gl_vertex_buffer *vertex_buffer,
                              int keep_mirror);

// incremental realize: _begin creates GL objects; _step uploads up to max_bytes,
// returns 1 when done, freeing mirrors unless keep_mirror
void vertex_buffer_gl_realize_begin(gl_vertex_buffer *vertex_buffer);
int vertex_buffer_gl_realize_step(gl_vertex_buffer *vertex_buffer,
                                  int max_bytes, int keep_mirror);
#endif
#endif
