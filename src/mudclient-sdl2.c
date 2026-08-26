#include "mudclient.h"

#ifdef __vita__
#include <psp2/kernel/sysmem.h>
#endif

#ifdef SDL2

#if defined(__SWITCH__) || defined(__vita__)
static SDL_Joystick *joystick;
#endif

void get_sdl_keycodes(SDL_Keysym *keysym, char *char_code, int *code) {
    *code = -1;
    *char_code = -1;

    switch (keysym->scancode) {
    case SDL_SCANCODE_LEFT:
        *code = K_LEFT;
        break;
    case SDL_SCANCODE_RIGHT:
        *code = K_RIGHT;
        break;
    case SDL_SCANCODE_UP:
        *code = K_UP;
        break;
    case SDL_SCANCODE_DOWN:
        *code = K_DOWN;
        break;
    case SDL_SCANCODE_PAGEUP:
        *code = K_PAGE_UP;
        break;
    case SDL_SCANCODE_PAGEDOWN:
        *code = K_PAGE_DOWN;
        break;
    case SDL_SCANCODE_HOME:
        *code = K_HOME;
        break;
    case SDL_SCANCODE_F1:
        *code = K_F1;
        break;
    case SDL_SCANCODE_ESCAPE:
        *code = K_ESCAPE;
        break;
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_RETURN:
        *code = K_ENTER;
        *char_code = K_ENTER;
        break;
    // TODO: Swallow "bad inputs" by default? ie. numlock, capslock
    case SDL_SCANCODE_NUMLOCKCLEAR:
        *code = -1;
        *char_code = 1;
        break;
    case SDL_SCANCODE_CAPSLOCK:
        *code = -1;
        *char_code = 1;
        break;
    case SDL_SCANCODE_TAB:
        *code = K_TAB;
        *char_code = K_TAB;
        break;
    case SDL_SCANCODE_BACKSPACE:
        *code = K_BACKSPACE;
        *char_code = K_BACKSPACE;
        break;
    default:
        break;
    }
}

void mudclient_start_application(mudclient *mud, char *title) {
#ifdef __SWITCH__
    Result romfs_res = romfsInit();

    if (romfs_res) {
        mud_error("romfsInit: %08lX\n", romfs_res);
        exit(1);
    }
#endif

#ifdef __vita__
    // front touch drives the cursor via finger events; stop SDL synthesising mouse events from the touchscreens, and
    // scale the software surface up to the panel
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#endif

    int init = SDL_INIT_VIDEO;

    if (mud->options->members && !mud->options->lowmem) {
        init |= SDL_INIT_AUDIO;
    }

#if defined(__SWITCH__) || defined(__vita__)
    init |= SDL_INIT_JOYSTICK;
#endif

    if (SDL_Init(init) < 0) {
        mud_error("SDL_Init(): %s\n", SDL_GetError());
        exit(1);
    }

#ifdef SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

#if defined(__SWITCH__) || defined(__vita__)
    SDL_JoystickEventState(SDL_ENABLE);
    joystick = SDL_JoystickOpen(0);
#endif

/* XXX: currently require non-callback-based audio from SDL >= 2.0.4 */
#ifdef SDL_VERSION_ATLEAST
#if SDL_VERSION_ATLEAST(2, 0, 4)
    if (mud->options->members && !mud->options->lowmem) {
        SDL_AudioSpec wanted_audio;

        wanted_audio.freq = SAMPLE_RATE;
        wanted_audio.format = AUDIO_S16;
        wanted_audio.channels = 1;
        wanted_audio.silence = 0;
        wanted_audio.samples = 1024;
        wanted_audio.callback = NULL;

        if (SDL_OpenAudio(&wanted_audio, NULL) < 0) {
            mud_error("SDL_OpenAudio(): %s\n", SDL_GetError());
        }
    }
#endif
#endif

    uint32_t windowflags = SDL_WINDOW_SHOWN;

#if !defined(WII) && !defined(_3DS) && !defined(EMSCRIPTEN)
    windowflags |= SDL_WINDOW_RESIZABLE;
#endif

#ifdef RENDER_GL
#ifndef __vita__
    // vitaGL owns the GXM display directly; it needs no SDL GL window flag or context attributes
    windowflags |= SDL_WINDOW_OPENGL;

#ifdef EMSCRIPTEN
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#elif defined(OPENGL15)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
#elif defined(OPENGL20)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);

    // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    // SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#endif /* EMSCRIPTEN */
#endif // !__vita__
#endif /* RENDER_GL */

#ifdef __vita__
    // Fixed 960x544 panel.
    (void)windowflags;

    mud->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, 960, 544,
                                   SDL_WINDOW_SHOWN);

#ifndef RENDER_GL
    // software renderer: SDL's gxm renderer upscales the software surface to the panel. the hardware (vitaGL) build
    // owns GXM itself and must not create an SDL renderer
    mud->vita_renderer = SDL_CreateRenderer(
        mud->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (mud->vita_renderer == NULL) {
        mud_error("SDL_CreateRenderer(): %s\n", SDL_GetError());
        exit(1);
    }
#endif
#else
    mud->window =
        SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         mud->game_width, mud->game_height, windowflags);

    SDL_SetWindowMinimumSize(mud->window, MUD_MIN_WIDTH, MUD_MIN_HEIGHT);
#endif

    mud->default_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    mud->hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);

#ifdef RENDER_GL
    mud->gl_window = mud->window;
    mud->window = NULL;

    if (IMG_Init(IMG_INIT_PNG) == 0) {
        mud_error("unable to initialize sdl_image: %s\n", IMG_GetError());
    }

#ifdef __vita__
    // must be set before vglInit*. triple buffering (vitaGL's default); costs one extra 960x544x4 (~2MB) colour
    // surface in CDRAM, depth/stencil shared
    vglUseTripleBuffering(GL_TRUE);

#if defined(RENDER_GL)
    // enlarge the shader-patcher buffers (must be set before vglInit*) and pin the circular pool so the first world
    // frame records into a large pool
    vglSetupShaderPatcher(4 * 1024 * 1024, 2 * 1024 * 1024, 2 * 1024 * 1024);
    vglSetCircularPoolSize(32 * 1024 * 1024);

    // GXM parameter buffer: where the tiler stores post-vertex-shader data and tile lists mid-frame. sceGxm's default
    // is 16MB and vitaGL never enlarges it; a dense zoomed-out scene overflows it, so 32MB. must be set before vglInit*
    vglSetParamBufferSize(24 * 1024 * 1024);
    // 24MB not 32: the PB is carved from CDRAM; 24MB keeps +8MB over the 16MB default while restoring ~8MB CDRAM

    // scene splits disabled (see scene-gl.c); the depth-persistence force-store is not enabled
#endif

    // use vglInitExtended with a 56MB RAM threshold so ~56MB stays free for GXM's structural buffers (parameter
    // buffer, ring buffers, USSE, shader-patcher); textures/vertices go to CDRAM via vglUseVram

    // vglInit* returns res_fallback, not a success flag: GL_FALSE = inited at the requested 960x544, GL_TRUE = inited
    // but resolution downgraded. on genuine failure vitaGL aborts internally, so call it, log the fallback flag, and continue
    GLboolean vgl_res_fallback =
        vglInitExtended(0, 960, 544, 40 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);

    if (vgl_res_fallback) {
        mud_error("[gl] vglInitExtended fell back below native 960x544\n");
    }

    // Prefer VRAM (CDRAM) first for textures and vertex data. Post-init.
    vglUseVram(GL_TRUE);

#if defined(__vita__) && defined(RENDER_GL)
    // hardware vsync at 60Hz: vitaGL's display callback waits sceDisplayWaitVblankStartMulti(vsync_interval) per
    // present, so 1 = tear-free 60fps, no busy-wait
    {
        extern uint32_t vsync_interval;
        // initial value from the saved fps mode. 1 = 60 FPS target (hardware vsync), 2 = locked 30 FPS
        vsync_interval = mud->options->fps_60 ? 1 : 2;
    }
#endif

#if defined(RENDER_GL)
    // warm-up: flush every backbuffer with a clean clear+swap before any real frame, establishing a sceGxm scene
    // cycle (BeginScene/EndScene) on each swapchain buffer
    for (int warm = 0; warm < 3; warm++) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        vglSwapBuffers(GL_FALSE);
    }
#endif

    // vitaGL's runtime GLSL->Cg compiler (vitashark -> SceShaccCg). requires libshacccg.suprx installed on the device
    // (ur0:/data/)
    vglSetupRuntimeShaderCompiler(SHARK_OPT_DEFAULT, 0, 0, 0);
#else
    SDL_GLContext *context = SDL_GL_CreateContext(mud->gl_window);

    if (!context) {
        mud_error("SDL_GL_CreateContext(): %s\n", SDL_GetError());
        exit(1);
    }

    if (SDL_GL_MakeCurrent(mud->gl_window, context) != 0) {
        mud_error("SDL_GL_MakeCurrent(): %s\n", SDL_GetError());
        exit(1);
    }
#endif
#endif
}
#endif
