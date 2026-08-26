#include "../mudclient.h"
#include "shader.h"

#ifdef RENDER_GL
char *buffer_file(char *path) {
    FILE *file = fopen(path, "r");

    if (!file) {
        mud_error("unable to open file: %s\n", path);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    unsigned int size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // one sized read; fixed 1024 byte chunks asked for more than the buffer
    // holds and abort under fortified libc
    char *file_buffer = calloc(size + 1, sizeof(char));

    if (fread(file_buffer, 1, size, file) == 0 && size > 0) {
        mud_error("unable to read file: %s\n", path);
        exit(1);
    }

    fclose(file);

    return file_buffer;
}

#if defined(__vita__)
// binary reader for GXP blobs; buffer_file reads text mode
static char *buffer_file_binary(const char *path, long *out_size) {
    FILE *file = fopen(path, "rb");

    if (!file) {
        mud_error("unable to open gxp: %s\n", path);
        exit(1);
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buffer = malloc(size);

    if (fread(buffer, 1, size, file) != (size_t)size) {
        mud_error("short read on gxp: %s\n", path);
        exit(1);
    }

    fclose(file);

    *out_size = size;
    return buffer;
}
#endif

// resolve uniform locations once at link so draws skip the by-name lookup
static void shader_cache_locations(Shader *shader) {
    shader->loc_model = glGetUniformLocation(shader->id, "model");
    shader->loc_projection_view_model =
        glGetUniformLocation(shader->id, "projection_view_model");
    shader->loc_light_ambience =
        glGetUniformLocation(shader->id, "light_ambience");
    shader->loc_unlit = glGetUniformLocation(shader->id, "unlit");
    shader->loc_light_direction =
        glGetUniformLocation(shader->id, "light_direction");
    shader->loc_light_diffuse =
        glGetUniformLocation(shader->id, "light_diffuse");
    shader->loc_opacity = glGetUniformLocation(shader->id, "opacity");
    shader->loc_cull_front = glGetUniformLocation(shader->id, "cull_front");
    shader->loc_fog_distance = glGetUniformLocation(shader->id, "fog_distance");
    shader->loc_scroll_texture =
        glGetUniformLocation(shader->id, "scroll_texture");
}

void shader_new(Shader *shader, char *vertex_path, char *fragment_path) {
    char info_log[512] = {0};
    (void)info_log;

#if defined(__vita__)
    // loads precompiled GXP binaries; no runtime GLSL to Cg compilation

    long vertex_size = 0, fragment_size = 0;
    char *vertex_gxp = buffer_file_binary(vertex_path, &vertex_size);
    char *fragment_gxp = buffer_file_binary(fragment_path, &fragment_size);

    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    vglShaderGxpBinary(1, &vertex, vertex_gxp, (GLsizei)vertex_size);

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    vglShaderGxpBinary(1, &fragment, fragment_gxp, (GLsizei)fragment_size);

    int id = glCreateProgram();
    glAttachShader(id, vertex);
    glAttachShader(id, fragment);

    // maps .cg attribute names to the glVertexAttribPointer locations; names missing from a shader are ignored
    glBindAttribLocation(id, 0, "position");
    glBindAttribLocation(id, 1, "colour");
    glBindAttribLocation(id, 2, "texture_position");
    glBindAttribLocation(id, 3, "base_texture_position");
    glBindAttribLocation(id, 1, "normal");
    glBindAttribLocation(id, 2, "lighting");
    glBindAttribLocation(id, 3, "front_colour");
    glBindAttribLocation(id, 4, "front_texture_position");
    glBindAttribLocation(id, 5, "back_colour");
    glBindAttribLocation(id, 6, "back_texture_position");
    glBindAttribLocation(id, 1, "face_tag");

    glLinkProgram(id);

    shader->id = id;

    shader_cache_locations(shader);

    // vglShaderGxpBinary registers the shader with the patcher, so no glDeleteShader
    free(vertex_gxp);
    free(fragment_gxp);

#else

    const char *vertex_shader_code = buffer_file(vertex_path);
    const char *fragment_shader_code = buffer_file(fragment_path);

#ifdef OPENGL15
    GLhandleARB vertex = glCreateShaderObjectARB(GL_VERTEX_SHADER);
    glShaderSourceARB(vertex, 1, &vertex_shader_code, NULL);
    glCompileShaderARB(vertex);

    int success = 0;

    glGetObjectParameterfvARB(vertex, GL_COMPILE_STATUS, &success);

    if (!success) {
        glGetInfoLogARB(vertex, 512, NULL, info_log);
        mud_error("vertex shader error: %s\n", info_log);
        exit(1);
    }

    GLhandleARB fragment = glCreateShaderObjectARB(GL_FRAGMENT_SHADER);
    glShaderSourceARB(fragment, 1, &fragment_shader_code, NULL);
    glCompileShaderARB(fragment);

    glGetObjectParameterfvARB(fragment, GL_COMPILE_STATUS, &success);

    if (!success) {
        glGetInfoLogARB(fragment, 512, NULL, info_log);
        mud_error("fragment shader error: %s\n", info_log);
        exit(1);
    }

    int id = glCreateProgramObjectARB();
    glAttachObjectARB(id, vertex);
    glAttachObjectARB(id, fragment);

    /* flats */
    glBindAttribLocationARB(id, 0, "position");
    glBindAttribLocationARB(id, 1, "colour");
    glBindAttribLocationARB(id, 2, "texture_position");
    glBindAttribLocationARB(id, 3, "base_texture_position");

    /* game_models */
    glBindAttribLocationARB(id, 1, "normal");
    glBindAttribLocationARB(id, 2, "lighting");
    glBindAttribLocationARB(id, 3, "front_colour");
    glBindAttribLocationARB(id, 4, "front_texture_position");
    glBindAttribLocationARB(id, 5, "back_colour");
    glBindAttribLocationARB(id, 6, "back_texture_position");

    glLinkProgramARB(id);

    glGetObjectParameterfvARB(id, GL_LINK_STATUS, &success);

    if (!success) {
        glGetInfoLogARB(id, 512, NULL, info_log);
        mud_error("shader link error: %s\n", info_log);
        exit(1);
    }

    shader->id = id;

    shader_cache_locations(shader);

    glDeleteObjectARB(vertex);
    glDeleteObjectARB(fragment);
#else // OpenGL2+
    GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vertex_shader_code, NULL);
    glCompileShader(vertex);

    int success = 0;

    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);

    if (!success) {
        glGetShaderInfoLog(vertex, 512, NULL, info_log);
        mud_error("vertex shader error: %s\n", info_log);
        exit(1);
    }

    GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fragment_shader_code, NULL);
    glCompileShader(fragment);

    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);

    if (!success) {
        glGetShaderInfoLog(fragment, 512, NULL, info_log);
        mud_error("fragment shader error: %s\n", info_log);
        exit(1);
    }

    int id = glCreateProgram();
    glAttachShader(id, vertex);
    glAttachShader(id, fragment);

#ifdef OPENGL20
    /* flats */
    glBindAttribLocation(id, 0, "position");
    glBindAttribLocation(id, 1, "colour");
    glBindAttribLocation(id, 2, "texture_position");
    glBindAttribLocation(id, 3, "base_texture_position");

    /* game_models */
    glBindAttribLocation(id, 1, "normal");
    glBindAttribLocation(id, 2, "lighting");
    glBindAttribLocation(id, 3, "front_colour");
    glBindAttribLocation(id, 4, "front_texture_position");
    glBindAttribLocation(id, 5, "back_colour");
    glBindAttribLocation(id, 6, "back_texture_position");
#endif

    glLinkProgram(id);

    glGetProgramiv(id, GL_LINK_STATUS, &success);

    if (!success) {
        glGetProgramInfoLog(id, 512, NULL, info_log);
        mud_error("shader link error: %s\n", info_log);
        exit(1);
    }

    shader->id = id;

    shader_cache_locations(shader);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    free((char *)vertex_shader_code);
    free((char *)fragment_shader_code);
#endif // OpenGL2+ (OPENGL15 #else)
#endif // end of GXP vs GLSL path
}

void shader_use(Shader *shader) { glUseProgram(shader->id); }

void shader_set_int(Shader *shader, char *name, int value) {
    glUniform1i(glGetUniformLocation(shader->id, name), value);
}

void shader_set_float(Shader *shader, char *name, float value) {
    glUniform1f(glGetUniformLocation(shader->id, name), value);
}

void shader_set_float_array(Shader *shader, char *name, float *values,
                            int length) {
    glUniform1fv(glGetUniformLocation(shader->id, name), length,
                 (float *)values);
}

void shader_set_mat4(Shader *shader, char *name, mat4 value) {
    glUniformMatrix4fv(glGetUniformLocation(shader->id, name), 1, GL_FALSE,
                       (float *)value);
}

void shader_set_vec3(Shader *shader, char *name, vec3 value) {
    glUniform3fv(glGetUniformLocation(shader->id, name), 1, (float *)value);
}

void shader_set_vec3_array(Shader *shader, char *name, vec3 *values,
                           int length) {
    glUniform3fv(glGetUniformLocation(shader->id, name), length,
                 (float *)values);
}

void shader_set_int_loc(GLint loc, int value) { glUniform1i(loc, value); }

void shader_set_float_loc(GLint loc, float value) { glUniform1f(loc, value); }

void shader_set_mat4_loc(GLint loc, mat4 value) {
    glUniformMatrix4fv(loc, 1, GL_FALSE, (float *)value);
}

void shader_set_vec3_loc(GLint loc, vec3 value) {
    glUniform3fv(loc, 1, (float *)value);
}
#endif
