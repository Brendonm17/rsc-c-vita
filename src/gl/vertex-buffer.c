#include "vertex-buffer.h"

static gl_vertex_buffer *gl_bound_vertex_buffer = NULL;

// TODO vertex_length = vertex_size
void vertex_buffer_gl_new(gl_vertex_buffer *vertex_buffer, int vertex_length,
                          int vbo_length, int ebo_length) {
    vertex_buffer->vertex_length = vertex_length;

    if (vertex_buffer->attribute_index != 0) {
        vertex_buffer->attribute_index = 0;

        vertex_buffer_gl_destroy(vertex_buffer);
    }

    /*printf("new vertex buffer %d * %d: %d\n", vbo_length, vertex_length,
           vbo_length * vertex_length);*/

#ifdef RENDER_GL
    glGenVertexArrays(1, &vertex_buffer->vao);
    glGenBuffers(1, &vertex_buffer->vbo);
    glGenBuffers(1, &vertex_buffer->ebo);
#elif defined(RENDER_3DS_GL)
    vertex_buffer->vbo = linearAlloc(vbo_length * vertex_length);
    vertex_buffer->ebo = linearAlloc(ebo_length * sizeof(uint16_t));

    if (!vertex_buffer->vbo) {
        mud_error("vertex buffer is empty\n");
        exit(1);
    }

    AttrInfo_Init(&vertex_buffer->attr_info);
    BufInfo_Init(&vertex_buffer->buf_info);
#endif

#ifdef RENDER_GL
    vertex_buffer_gl_bind(vertex_buffer);

    glBufferData(GL_ARRAY_BUFFER, vbo_length * vertex_length, NULL,
                 GL_DYNAMIC_DRAW);

    // 16-bit index buffer: every GL vertex buffer is split so its vertex count stays < MAX_VERTEX_INDEX (65535), and
    // the 2D UI buffer holds GL_MAX_QUADS*4 vertices, so every EBO index fits a GLushort. EBO storage, staging offsets and glDrawElements type must all use GLushort. only the element (EBO) buffer is 16-bit, the VBO is unchanged
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ebo_length * sizeof(GLushort), NULL,
                 GL_DYNAMIC_DRAW);

    vertex_buffer->vbo_size = vbo_length * vertex_length;
    vertex_buffer->ebo_size = ebo_length * sizeof(GLushort);

    // no free here: a first-time struct may hold garbage
    vertex_buffer->vbo_mirror = NULL;
    vertex_buffer->ebo_mirror = NULL;
    vertex_buffer->vbo_dirty_min = 0;
    vertex_buffer->vbo_dirty_max = 0;
    vertex_buffer->ebo_dirty_min = 0;
    vertex_buffer->ebo_dirty_max = 0;
#endif
}

#ifdef RENDER_GL
static uint8_t *vertex_buffer_gl_mirror(gl_vertex_buffer *vertex_buffer,
                                        uint8_t **mirror, int size) {
    (void)vertex_buffer;

    if (*mirror == NULL && size > 0) {
        // model buffering fills every byte of the flushed range
        *mirror = malloc(size);
    }

    return *mirror;
}

void vertex_buffer_gl_write_vbo(gl_vertex_buffer *vertex_buffer, int offset,
                                int size, const void *data) {
    uint8_t *mirror = vertex_buffer_gl_mirror(
        vertex_buffer, &vertex_buffer->vbo_mirror, vertex_buffer->vbo_size);

    if (mirror == NULL || offset < 0 ||
        offset + size > vertex_buffer->vbo_size) {
        return;
    }

    memcpy(mirror + offset, data, size);

    if (vertex_buffer->vbo_dirty_max == vertex_buffer->vbo_dirty_min) {
        vertex_buffer->vbo_dirty_min = offset;
        vertex_buffer->vbo_dirty_max = offset + size;
    } else {
        if (offset < vertex_buffer->vbo_dirty_min) {
            vertex_buffer->vbo_dirty_min = offset;
        }

        if (offset + size > vertex_buffer->vbo_dirty_max) {
            vertex_buffer->vbo_dirty_max = offset + size;
        }
    }
}

void vertex_buffer_gl_write_ebo(gl_vertex_buffer *vertex_buffer, int offset,
                                int size, const void *data) {
    uint8_t *mirror = vertex_buffer_gl_mirror(
        vertex_buffer, &vertex_buffer->ebo_mirror, vertex_buffer->ebo_size);

    if (mirror == NULL || offset < 0 ||
        offset + size > vertex_buffer->ebo_size) {
        return;
    }

    memcpy(mirror + offset, data, size);

    if (vertex_buffer->ebo_dirty_max == vertex_buffer->ebo_dirty_min) {
        vertex_buffer->ebo_dirty_min = offset;
        vertex_buffer->ebo_dirty_max = offset + size;
    } else {
        if (offset < vertex_buffer->ebo_dirty_min) {
            vertex_buffer->ebo_dirty_min = offset;
        }

        if (offset + size > vertex_buffer->ebo_dirty_max) {
            vertex_buffer->ebo_dirty_max = offset + size;
        }
    }
}

void *vertex_buffer_gl_stage_vbo(gl_vertex_buffer *vertex_buffer, int offset,
                                 int size) {
    uint8_t *mirror = vertex_buffer_gl_mirror(
        vertex_buffer, &vertex_buffer->vbo_mirror, vertex_buffer->vbo_size);

    if (mirror == NULL || offset < 0 || size <= 0 ||
        offset + size > vertex_buffer->vbo_size) {
        return NULL;
    }

    if (vertex_buffer->vbo_dirty_max == vertex_buffer->vbo_dirty_min) {
        vertex_buffer->vbo_dirty_min = offset;
        vertex_buffer->vbo_dirty_max = offset + size;
    } else {
        if (offset < vertex_buffer->vbo_dirty_min) {
            vertex_buffer->vbo_dirty_min = offset;
        }

        if (offset + size > vertex_buffer->vbo_dirty_max) {
            vertex_buffer->vbo_dirty_max = offset + size;
        }
    }

    return mirror + offset;
}

void *vertex_buffer_gl_stage_ebo(gl_vertex_buffer *vertex_buffer, int offset,
                                 int size) {
    uint8_t *mirror = vertex_buffer_gl_mirror(
        vertex_buffer, &vertex_buffer->ebo_mirror, vertex_buffer->ebo_size);

    if (mirror == NULL || offset < 0 || size <= 0 ||
        offset + size > vertex_buffer->ebo_size) {
        return NULL;
    }

    if (vertex_buffer->ebo_dirty_max == vertex_buffer->ebo_dirty_min) {
        vertex_buffer->ebo_dirty_min = offset;
        vertex_buffer->ebo_dirty_max = offset + size;
    } else {
        if (offset < vertex_buffer->ebo_dirty_min) {
            vertex_buffer->ebo_dirty_min = offset;
        }

        if (offset + size > vertex_buffer->ebo_dirty_max) {
            vertex_buffer->ebo_dirty_max = offset + size;
        }
    }

    return mirror + offset;
}

void vertex_buffer_gl_flush(gl_vertex_buffer *vertex_buffer, int keep_mirror) {
    if (vertex_buffer->gl_deferred) {
        // no GL objects yet: keep the mirrors and dirty ranges for realize
        return;
    }

    vertex_buffer_gl_bind(vertex_buffer);

    if (vertex_buffer->vbo_mirror != NULL &&
        vertex_buffer->vbo_dirty_max > vertex_buffer->vbo_dirty_min) {
        glBufferSubData(GL_ARRAY_BUFFER, vertex_buffer->vbo_dirty_min,
                        vertex_buffer->vbo_dirty_max -
                            vertex_buffer->vbo_dirty_min,
                        vertex_buffer->vbo_mirror +
                            vertex_buffer->vbo_dirty_min);
    }

    if (vertex_buffer->ebo_mirror != NULL &&
        vertex_buffer->ebo_dirty_max > vertex_buffer->ebo_dirty_min) {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, vertex_buffer->ebo_dirty_min,
                        vertex_buffer->ebo_dirty_max -
                            vertex_buffer->ebo_dirty_min,
                        vertex_buffer->ebo_mirror +
                            vertex_buffer->ebo_dirty_min);
    }

    vertex_buffer->vbo_dirty_min = 0;
    vertex_buffer->vbo_dirty_max = 0;
    vertex_buffer->ebo_dirty_min = 0;
    vertex_buffer->ebo_dirty_max = 0;

    if (!keep_mirror) {
        free(vertex_buffer->vbo_mirror);
        free(vertex_buffer->ebo_mirror);
        vertex_buffer->vbo_mirror = NULL;
        vertex_buffer->ebo_mirror = NULL;
    }
}
#endif

void vertex_buffer_gl_bind(gl_vertex_buffer *vertex_buffer) {
#ifdef RENDER_GL
    // checked before the shared gl_bound_vertex_buffer read so a worker thread building a deferred buffer never
    // touches that static
    if (vertex_buffer->gl_deferred) {
        return;
    }
#endif

    if (gl_bound_vertex_buffer == vertex_buffer) {
        return;
    }

#ifdef RENDER_GL
    glBindVertexArray(vertex_buffer->vao);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertex_buffer->ebo);
#elif defined(RENDER_3DS_GL)
    C3D_SetAttrInfo(&vertex_buffer->attr_info);
    C3D_SetBufInfo(&vertex_buffer->buf_info);
#endif

    gl_bound_vertex_buffer = vertex_buffer;
}

#ifdef RENDER_GL
void vertex_buffer_gl_add_attribute_ex(gl_vertex_buffer *vertex_buffer,
                                       int gl_size, GLenum gl_type,
                                       GLboolean normalized, size_t gl_offset) {
#ifdef OPENGL15
    glVertexAttribPointerARB(vertex_buffer->attribute_index, gl_size, gl_type,
                             normalized, vertex_buffer->vertex_length,
                             (void *)gl_offset);

    glEnableVertexAttribArrayARB(vertex_buffer->attribute_index);
#else
    glVertexAttribPointer(vertex_buffer->attribute_index, gl_size, gl_type,
                          normalized, vertex_buffer->vertex_length,
                          (void *)gl_offset);

    glEnableVertexAttribArray(vertex_buffer->attribute_index);
#endif

    vertex_buffer->attribute_index++;
}
#elif defined(RENDER_3DS_GL)
void vertex_buffer_gl_add_attribute_ex(gl_vertex_buffer *vertex_buffer,
                                       int gl_size, GPU_FORMATS c3d_format,
                                       int c3d_count, size_t gl_offset) {
    (void)gl_size; // GL component count is not used by citro3d
    (void)gl_offset; // citro3d derives offsets from cumulative loader sizes

    AttrInfo_AddLoader(&vertex_buffer->attr_info,
                       vertex_buffer->attribute_index, c3d_format, c3d_count);

    vertex_buffer->attribute_index++;
}
#endif

void vertex_buffer_gl_add_attribute(gl_vertex_buffer *vertex_buffer,
                                    int *attribute_offset,
                                    int attribute_length) {
    // Float attribute: byte offset = float-count * sizeof(GLfloat).
#ifdef RENDER_GL
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, attribute_length, GL_FLOAT,
                                      GL_FALSE,
                                      (*attribute_offset) * sizeof(GLfloat));
#elif defined(RENDER_3DS_GL)
    vertex_buffer_gl_add_attribute_ex(vertex_buffer, attribute_length,
                                      GPU_FLOAT, attribute_length,
                                      (*attribute_offset) * sizeof(float));
#endif

    *attribute_offset += attribute_length;
}

#ifdef RENDER_GL
void vertex_buffer_gl_new_deferred(gl_vertex_buffer *vertex_buffer,
                                   int vertex_length, int vbo_length,
                                   int ebo_length) {
    vertex_buffer->vertex_length = vertex_length;
    vertex_buffer->attribute_index = 0;
    vertex_buffer->vao = 0;
    vertex_buffer->vbo = 0;
    vertex_buffer->ebo = 0;
    vertex_buffer->vbo_size = vbo_length * vertex_length;
    vertex_buffer->ebo_size = ebo_length * sizeof(GLushort);
    vertex_buffer->vbo_mirror = NULL;
    vertex_buffer->ebo_mirror = NULL;
    vertex_buffer->vbo_dirty_min = 0;
    vertex_buffer->vbo_dirty_max = 0;
    vertex_buffer->ebo_dirty_min = 0;
    vertex_buffer->ebo_dirty_max = 0;
    vertex_buffer->gl_deferred = 1;
}

// render thread: create the GL objects for a deferred buffer and upload what the build wrote
void vertex_buffer_gl_realize(gl_vertex_buffer *vertex_buffer,
                              int keep_mirror) {
    if (!vertex_buffer->gl_deferred) {
        return;
    }

    vertex_buffer->gl_deferred = 0;

    glGenVertexArrays(1, &vertex_buffer->vao);
    glGenBuffers(1, &vertex_buffer->vbo);
    glGenBuffers(1, &vertex_buffer->ebo);

    // force real binds: the cache may hold a same-address stale entry
    gl_bound_vertex_buffer = NULL;
    vertex_buffer_gl_bind(vertex_buffer);

    glBufferData(GL_ARRAY_BUFFER, vertex_buffer->vbo_size, NULL,
                 GL_DYNAMIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vertex_buffer->ebo_size, NULL,
                 GL_DYNAMIC_DRAW);

    vertex_buffer_gl_flush(vertex_buffer, keep_mirror);
}

void vertex_buffer_gl_realize_begin(gl_vertex_buffer *vertex_buffer) {
    if (!vertex_buffer->gl_deferred) {
        return;
    }

    vertex_buffer->gl_deferred = 0;

    glGenVertexArrays(1, &vertex_buffer->vao);
    glGenBuffers(1, &vertex_buffer->vbo);
    glGenBuffers(1, &vertex_buffer->ebo);

    gl_bound_vertex_buffer = NULL;
    vertex_buffer_gl_bind(vertex_buffer);

    glBufferData(GL_ARRAY_BUFFER, vertex_buffer->vbo_size, NULL,
                 GL_DYNAMIC_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vertex_buffer->ebo_size, NULL,
                 GL_DYNAMIC_DRAW);

    vertex_buffer->realize_cursor = 0;
}

int vertex_buffer_gl_realize_step(gl_vertex_buffer *vertex_buffer,
                                  int max_bytes, int keep_mirror) {
    int total = vertex_buffer->vbo_size + vertex_buffer->ebo_size;

    if (vertex_buffer->realize_cursor >= total) {
        return 1;
    }

    vertex_buffer_gl_bind(vertex_buffer);

    int budget = max_bytes;

    // vbo portion first: cursor in [0, vbo_size)
    if (budget > 0 && vertex_buffer->realize_cursor < vertex_buffer->vbo_size &&
        vertex_buffer->vbo_mirror != NULL) {
        int off = vertex_buffer->realize_cursor;
        int n = vertex_buffer->vbo_size - off;
        if (n > budget) {
            n = budget;
        }
        glBufferSubData(GL_ARRAY_BUFFER, off, n,
                        vertex_buffer->vbo_mirror + off);
        vertex_buffer->realize_cursor += n;
        budget -= n;
    }

    // ebo portion: cursor in [vbo_size, vbo_size + ebo_size)
    if (budget > 0 &&
        vertex_buffer->realize_cursor >= vertex_buffer->vbo_size &&
        vertex_buffer->ebo_mirror != NULL) {
        int off = vertex_buffer->realize_cursor - vertex_buffer->vbo_size;
        int n = vertex_buffer->ebo_size - off;
        if (n > budget) {
            n = budget;
        }
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, off, n,
                        vertex_buffer->ebo_mirror + off);
        vertex_buffer->realize_cursor += n;
        budget -= n;
    }

    if (vertex_buffer->realize_cursor >= total) {
        if (!keep_mirror) {
            free(vertex_buffer->vbo_mirror);
            free(vertex_buffer->ebo_mirror);
            vertex_buffer->vbo_mirror = NULL;
            vertex_buffer->ebo_mirror = NULL;
        }
        return 1;
    }

    return 0;
}
#endif

void vertex_buffer_gl_destroy(gl_vertex_buffer *vertex_buffer) {
    if (vertex_buffer == NULL) {
        return;
    }

    if (gl_bound_vertex_buffer == vertex_buffer) {
        gl_bound_vertex_buffer = NULL;
    }

#ifdef RENDER_GL
    glDeleteVertexArrays(1, &vertex_buffer->vao);
    glDeleteBuffers(1, &vertex_buffer->vbo);
    glDeleteBuffers(1, &vertex_buffer->ebo);

    free(vertex_buffer->vbo_mirror);
    free(vertex_buffer->ebo_mirror);
    vertex_buffer->vbo_mirror = NULL;
    vertex_buffer->ebo_mirror = NULL;
#elif defined(RENDER_3DS_GL)
    linearFree(vertex_buffer->vbo);
    vertex_buffer->vbo = NULL;

    linearFree(vertex_buffer->ebo);
    vertex_buffer->ebo = NULL;
#endif
}
