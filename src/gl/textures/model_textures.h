#ifndef _H_MODEL_TEXTURES_TEXTURES
#define _H_MODEL_TEXTURES_TEXTURES

#include "../../surface.h"

extern gl_atlas_position gl_texture_atlas_positions[];
extern gl_atlas_position gl_white_model_atlas_position;
extern gl_atlas_position gl_transparent_model_atlas_position;

// number of entries in gl_texture_atlas_positions[]
int model_textures_count(void);

#if defined(__vita__) && defined(RENDER_GL)
// precompute per-cell alpha transparency for early-Z shader routing
void model_textures_compute_alpha(SDL_Surface *atlas);
int model_texture_has_alpha(int texture_index);
#endif

#endif
