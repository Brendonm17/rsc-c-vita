#include "mudclient.h"
#include "protocol177.h"
#include "online-defs.h"
#include "custom-defs.h"

#ifdef WITH_SINGLEPLAYER
#include "singleplayer.h"
#include "sp-net.h" // LAN co-op transport: spnet_get_mode/spnet_pump (loop)
#endif

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <sys/stat.h> // mkdir() for the ux0:data/RuneScape data dir
#endif

#ifdef EMSCRIPTEN
/* clang doesn't know what triple equals is, understandably */
/* clang-format off */
EM_JS(int, can_resize, (), {
    return window._mudclientCanResize &&
               document.activeElement !== window._mudclientKeyboard &&
               document.activeElement !== window._mudclientPassword;
});

EM_JS(int, get_window_width, (), { return window.innerWidth; });
EM_JS(int, get_window_height, (), { return window.innerHeight; });

EM_JS(void, browser_trigger_keyboard,
      (char *text, int is_password, int x, int y, int width, int height,
       int font, int is_centred, int is_scaled), {
          const keyboard = is_password ? window._mudclientPassword :
                                         window._mudclientKeyboard;

          keyboard.value = UTF8ToString(text);

          if (is_centred) {
              keyboard.style.height = `${height}px`;
              keyboard.style.textAlign = 'center';
          } else {
              keyboard.style.height = null;
              keyboard.style.textAlign = 'left';
          }

          keyboard.style.left = `${x}px`;
          keyboard.style.top = `${y}px`;

          keyboard.style.width = `${width}px`;

          if (is_scaled) {
              keyboard.style.transform = 'scale(2)';
          } else {
              keyboard.style.transform = 'none';
          }

          const fonts = {
              1: 'mudclient-font-bold-12',
              4: 'mudclient-font-bold-14',
              5: 'mudclient-font-bold-16'
          };

          keyboard.classList.remove(...Object.values(fonts));

          const fontClass = fonts[font];

          if (fontClass) {
              keyboard.classList.add(fontClass);
          }

          keyboard.style.display = 'block';

          keyboard.focus();
      });

EM_JS(int, browser_is_touch, (), { return window._mudclientIsTouch; });
/* clang-format on */

int last_canvas_check = 0;

mudclient *global_mud = NULL;
#endif

int mudclient_finger_1_x = 0;
int mudclient_finger_1_y = 0;
int mudclient_finger_1_down = 0;

int mudclient_finger_2_x = 0;
int mudclient_finger_2_y = 0;
int mudclient_finger_2_down = 0;

int mudclient_full_width = 0;
int mudclient_full_height = 0;

const char *font_files[] = {"h11p.jf", "h12b.jf", "h12p.jf", "h13b.jf",
                            "h14b.jf", "h16b.jf", "h20b.jf", "h24b.jf"};

/* only the first of models with animations are stored in the cache */
const char *animated_models[] = {
    "torcha2",      "torcha3",    "torcha4",    "skulltorcha2", "skulltorcha3",
    "skulltorcha4", "firea2",     "firea3",     "fireplacea2",  "fireplacea3",
    "firespell2",   "firespell3", "lightning2", "lightning3",   "clawspell2",
    "clawspell3",   "clawspell4", "clawspell5", "spellcharge2", "spellcharge3"};

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
/*
 * animations that experienced a loss of fine detail in January 2002 with the
 * "Compression" update
 *
 * camel - eyes lose distinctiveness.
 * bat - most noticable. mouth is nearly gone entirely.
 * bear - loses some shading that gives it more of a "fur" texture.
 * human heads - eyes lose detail.
 * human tops - belt buckles lose detail or become flesh (ew).
 */
static const char *anims_older_is_better[] = {
    "camel",  "bat",           "battleaxe", "bear",  "fbody1",
    "fhead1", "fplatemailtop", "head1",     "head2", "head3",
    "head4",  "platemailtop",  "staff",     "body1", NULL};
#endif

char login_screen_status[255] = {0};

void mudclient_new(mudclient *mud) {
    memset(mud, 0, sizeof(mudclient));

#if defined(RENDER_GL) && (defined(__vita__) || defined(__linux__))
    mud->region_building_index = -1; // 0 is a valid cache slot
#endif

    mud->target_fps = 20;
    mud->loading_step = 1;
    mud->loading_progess_text = "Loading";
#if defined(__vita__) && defined(RENDER_GL)
    // render the 2D UI at native 960x544 (1:1, no magnification) so text and sprites are pixel-sharp with no
    // atlas-edge bleed. surface->width/height derive from these; layout is resolution-aware; the 3D viewport fills the full 960x544 panel
    mud->game_width = 960;
    mud->game_height = 544;
#else
    mud->game_width = MUD_WIDTH;
    mud->game_height = MUD_HEIGHT;
#endif
    mud->camera_angle = 1;
    mud->camera_rotation = 128;
    mud->camera_rotation_x_increment = 2;
    mud->camera_rotation_y_increment = 2;
    mud->last_plane_index = -1;

    // default to the 18-skill layout until a stat-list packet gives the real count: 18 authentic/Preservation, 19
    // single-player/OpenRSC-custom (adds Runecraft)
    mud->player_skill_count = PLAYER_SKILL_COUNT;

    mud->menu_items_size = 32;
    mud->menu_items = calloc(mud->menu_items_size, sizeof(struct MenuEntry));
    mud->menu_indices = calloc(mud->menu_items_size, sizeof(int));

    mud->options = malloc(sizeof(Options));

    options_new(mud->options);
    options_load(mud->options);

#ifdef __vita__
    // the Vita renders at a fixed game size scaled to the panel; the desktop 2x UI-scale mode is kept off
    mud->options->ui_scale = 0;
#endif

    mud->camera_zoom = mud->options->zoom_camera ? ZOOM_OUTDOORS : ZOOM_INDOORS;

    for (int i = 0; i < MESSAGE_HISTORY_LENGTH; i++) {
        memset(mud->message_history[i], '\0', 255);
    }

    mud->selected_spell = -1;
    mud->selected_item_name = "";
    mud->selected_item_inventory_index = -1;
    mud->quest_complete = calloc(quests_length, sizeof(int8_t));

#ifdef _3DS
    mud->_3ds_sound_position = -1;
#endif

    mud->appearance_body_type = 1;
    mud->appearance_hair_colour = 2;
    mud->appearance_top_colour = 8;
    mud->appearance_bottom_colour = 14;
    mud->appearance_head_gender = 1;

    // OpenRSC creation defaults: Regular (non-ironman), world xp rate,
    // Adventurer class.
    mud->appearance_ironman_mode = 0;
    mud->appearance_one_xp = 0;
    mud->appearance_class = 0;

    mud->sleep_word_delay = 1;

    /* set by the server to 192 on p2p servers */
    mud->bank_items_max = 48;

    mud->bank_selected_item_slot = -1;
    mud->bank_selected_item = -2;

    mud->drop_offer_index = -1;

    // sprite_media stays at the authentic 2000; the Vita GL renderer draws entity sprites (ids 0..sprite_media) from
    // a prebaked atlas via the fixed 2000-entry gl_entities_texture_positions[] table (surface-gl.c)
    mud->sprite_media = 2000;
    mud->sprite_util = mud->sprite_media + 100;
    mud->sprite_item = mud->sprite_util + 50;
    mud->sprite_logo = mud->sprite_item + 1000;
    mud->sprite_projectile = mud->sprite_logo + 10;
    // TODO this is also used for sleep word
    mud->sprite_texture = mud->sprite_projectile + 50;
    mud->sprite_texture_world = mud->sprite_texture + 10;
}

void mudclient_resize(mudclient *mud) {
#if !defined(WII) && !defined(_3DS)
    SDL_FreeSurface(mud->screen);
    SDL_FreeSurface(mud->pixel_surface);

    int surface_width = mud->game_width;
    int surface_height = mud->game_height;

#ifdef SDL12
    mud->screen = SDL_GetVideoSurface();
#else
#ifdef __vita__
    // present via vita_renderer/vita_texture, not a window surface
    mud->screen = NULL;
#else
    mud->screen = SDL_GetWindowSurface(mud->window);
#endif

#ifdef RENDER_SW
    if (mudclient_is_ui_scaled(mud)) {
        surface_width /= 2;
        surface_height /= 2;
    }
#endif
#endif

    mud->pixel_surface =
        SDL_CreateRGBSurface(0, surface_width, surface_height, 32, 0xff0000,
                             0x00ff00, 0x0000ff, 0);

#ifdef __vita__
    if (mud->vita_texture != NULL) {
        SDL_DestroyTexture(mud->vita_texture);
        mud->vita_texture = NULL;
    }

    if (mud->vita_renderer != NULL) {
        // no SDL_RenderSetLogicalSize: it breaks RenderCopy on the Vita gxm renderer (no-op -> black screen);
        // surface_draw() letterboxes the copy manually instead

        // ARGB8888 matches the XRGB software surface channel order; the surface has no alpha, so draw the texture
        // opaque (BLENDMODE_NONE)
        mud->vita_texture = SDL_CreateTexture(
            mud->vita_renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING, surface_width, surface_height);

        if (mud->vita_texture != NULL) {
            SDL_SetTextureBlendMode(mud->vita_texture, SDL_BLENDMODE_NONE);
        }
    }
#endif

    if (mud->surface != NULL) {
#ifdef RENDER_SW
        mud->surface->pixels = mud->pixel_surface->pixels;
#endif

#ifdef RENDER_GL
        free(mud->surface->pixels);

        mud->surface->pixels =
            calloc(mud->game_width * mud->game_height, sizeof(int32_t));
#endif

        panel_destroy(mud->panel_login_welcome);
        free(mud->panel_login_welcome);

        panel_destroy(mud->panel_login_new_user);
        free(mud->panel_login_new_user);

        panel_destroy(mud->panel_login_worldlist);
        free(mud->panel_login_worldlist);

        panel_destroy(mud->panel_login_existing_user);
        free(mud->panel_login_existing_user);

        worldlist_new(mud);

        mudclient_create_login_panels(mud);

        panel_destroy(mud->panel_appearance);
        free(mud->panel_appearance);

        mudclient_create_appearance_panel(mud);

        mud->scene->raster = mud->surface->pixels;

        int is_compact = mud->surface->width < MUD_VANILLA_WIDTH ||
                         mud->surface->height < MUD_VANILLA_HEIGHT;

        int is_touch = mudclient_is_touch(mud);

        int full_offset_x = mud->surface->width - MUD_WIDTH;
        int full_offset_y = mud->surface->height - MUD_HEIGHT;
        int half_offset_x = (mud->surface->width / 2) - (MUD_WIDTH / 2);
        int half_offset_y = (mud->surface->height / 2) - (MUD_HEIGHT / 2);

        int dynamic_offset_x =
            (mud->surface->width / 2) -
            (is_compact ? MUD_MIN_WIDTH : MUD_VANILLA_WIDTH) / 2;

        int dynamic_offset_y =
            (mud->surface->height / 2) -
            (is_compact ? MUD_MIN_HEIGHT : MUD_VANILLA_HEIGHT) / 2;

        if (mud->panel_login_welcome != NULL) {
            mud->panel_login_welcome->offset_x = dynamic_offset_x;
            mud->panel_login_welcome->offset_y = dynamic_offset_y;
        }

        if (mud->panel_login_new_user != NULL) {
            mud->panel_login_new_user->offset_x = dynamic_offset_x;
            mud->panel_login_new_user->offset_y = dynamic_offset_y;
        }

        if (mud->panel_login_existing_user != NULL) {
            mud->panel_login_existing_user->offset_x = dynamic_offset_x;
            mud->panel_login_existing_user->offset_y = dynamic_offset_y;
        }

        if (mud->panel_login_worldlist != NULL) {
            mud->panel_login_worldlist->offset_x = dynamic_offset_x;
            mud->panel_login_worldlist->offset_y = dynamic_offset_y;
        }

        if (mud->panel_appearance != NULL) {
            mud->panel_appearance->offset_x = dynamic_offset_x;
            mud->panel_appearance->offset_y = dynamic_offset_y;
        }

        if (mud->panel_message_tabs != NULL && !is_touch) {
            mud->panel_message_tabs->offset_y = full_offset_y;
        }

#if defined(__vita__) && defined(RENDER_GL)
        // the in-game HUD panels (quests/magic/social) and options panels are authored with coordinates that bake in
        // the real surface size, so they keep offset 0; the full/half_offset below is for the desktop 512x346 canvas layout. the login/appearance dynamic_offset is still applied
        (void)full_offset_x;
        (void)half_offset_x;
        (void)half_offset_y;
#else
        if (mud->panel_quests != NULL) {
            mud->panel_quests->offset_x = full_offset_x;

            if (is_touch) {
                mud->panel_quests->offset_y = full_offset_y;
            }
        }

        if (mud->panel_magic != NULL) {
            mud->panel_magic->offset_x = full_offset_x;

            if (is_touch) {
                mud->panel_magic->offset_y = full_offset_y;
            }
        }

        if (mud->panel_social_list != NULL) {
            mud->panel_social_list->offset_x = full_offset_x;

            if (is_touch) {
                mud->panel_social_list->offset_y = full_offset_y;
            }
        }

        if (mud->panel_game_options != NULL) {
            mud->panel_game_options->offset_x = half_offset_x;
            mud->panel_game_options->offset_y = half_offset_y;
        }

        if (mud->panel_control_options != NULL) {
            mud->panel_control_options->offset_x = half_offset_x;
            mud->panel_control_options->offset_y = half_offset_y;
        }

        if (mud->panel_ui_options != NULL) {
            mud->panel_ui_options->offset_x = half_offset_x;
            mud->panel_ui_options->offset_y = half_offset_y;
        }

        if (mud->panel_bank_options != NULL) {
            mud->panel_bank_options->offset_x = half_offset_x;
            mud->panel_bank_options->offset_y = half_offset_y;
        }
#endif
    }

#ifdef RENDER_GL
#ifdef __vita__
    // the game renders at game_width x game_height but the panel is 960x544; the 2D UI is composed in NDC, so a
    // full-panel viewport stretches it to fullscreen
    glViewport(0, 0, 960, 544);
#else
    glViewport(0, 0, mud->game_width, mud->game_height);
#endif
#endif
#endif
}

#ifdef WITH_SINGLEPLAYER
// drives the loading display while the embedded server loads the world on its own thread (main loop blocked in
// singleplayer_start): advances the login banner and calls mudclient_show_login_screen_status, which renders and presents a complete frame. nothing else may draw or swap here
static void sp_loading_frame(void *ctx) {
    mudclient *mud = (mudclient *)ctx;

    // keep the loading-screen pace in step with the 30/60 FPS setting
    singleplayer_set_loading_sleep_us(mud->options->fps_60 ? 16000 : 33000);

    mud->login_timer++; // advance the revolving banner

    // drive the co-op transport while the main loop is blocked in singleplayer_start(). main-thread only; no-op when
    // co-op mode is off
    if (spnet_get_mode() != SPNET_MODE_OFF) {
        spnet_pump();
    }

    // the status text is centred, so its width must stay constant frame to frame; keep both lines steady
    char text[48];
    int pct = singleplayer_boot_progress(text, sizeof(text));

    if (pct > 0) {
        // real per-phase progress (mudclient-singleplayer.c sp_boot_pct/sp_boot_text)
        char line2[64];
        snprintf(line2, sizeof(line2), "%s - %d%%", text, pct);
        mudclient_show_login_screen_status(mud, "Single-player world", line2);
    } else {
        // no phase reported yet
        mudclient_show_login_screen_status(mud, "Single-player world",
                                           "loading the world ...");
    }
}
#endif

static void mudclient_start_application_common(struct mudclient *mud) {
#ifdef WITH_SINGLEPLAYER
    singleplayer_set_progress_cb(sp_loading_frame, mud);
#endif

#ifdef RENDER_GL

#ifdef GLAD
#if defined(SDL_OPENGL) || defined(SDL_WINDOW_OPENGL)
    if (gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress) == 0) {
        mud_error("Error loading GL library through GLAD/SDL\n");
        exit(1);
    }
#else
    if (gladLoadGL() == 0) {
        mud_error("Error loading GL library through GLAD\n");
        exit(1);
    }
#endif
    printf("INFO: Loaded OpenGL version %d.%d\n", GLVersion.major,
           GLVersion.minor);
#elif !defined(ANDROID) && !defined(__vita__)
    glewExperimental = GL_TRUE;

    GLenum glew_error = glewInit();

    if (glew_error != GLEW_OK) {
        mud_error("GLEW error: %s\n", glewGetErrorString(glew_error));
        exit(1);
    }
#endif

#ifdef __vita__
    // the game renders at game_width x game_height but the panel is 960x544; the 2D UI is composed in NDC, so a
    // full-panel viewport stretches it to fullscreen
    glViewport(0, 0, 960, 544);
#else
    glViewport(0, 0, mud->game_width, mud->game_height);
#endif
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

    /* when two vertices have the same depth, the last one gets drawn rather
     * than the first one. used for entity quads */
    glDepthFunc(GL_LEQUAL);

    /* transparent textures */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // the scissor test is disabled by default: 2D quads are clipped on the CPU when buffered, and surface_gl_draw
    // only enables it around the rare contexts needing a real scissor rect (rotated/skewed quads)

#ifndef __vita__
    // vitaGL has no GL_MULTISAMPLE toggle; MSAA is set via vglInitExtended's
    // msaa parameter (NONE) instead.
    glDisable(GL_MULTISAMPLE);
#endif
#endif /* RENDER_GL */

    mudclient_resize(mud);

    mud->surface = malloc(sizeof(Surface));

#if defined(RENDER_GL) && defined(__vita__)
#endif

    surface_new(mud->surface, mud->game_width, mud->game_height, SPRITE_LIMIT,
                mud);

#if defined(RENDER_GL) && defined(__vita__)
#endif

    surface_set_bounds(mud->surface, 0, 0, mud->game_width, mud->game_height);

    mud_log("Started application\n");

#if defined(RENDER_GL) && defined(__vita__)
#endif

#ifdef _3DS
    mudclient_3ds_draw_top_background(mud);
#endif

#ifdef ANDROID
    SDL_SetWindowFullscreen(mud->window, SDL_WINDOW_FULLSCREEN);
#endif

    mudclient_run(mud);
}

void mudclient_handle_key_press(mudclient *mud, int key_code) {
    if (mud->show_additional_options) {
        Panel *panel = mudclient_get_active_option_panel(mud);
        panel_key_press(panel, key_code);
        return;
    }

    if (!mud->logged_in) {
        if (mud->login_screen == LOGIN_STAGE_WELCOME &&
            mud->panel_login_welcome) {
            panel_key_press(mud->panel_login_welcome, key_code);
        }

        if (mud->login_screen == LOGIN_STAGE_NEW && mud->panel_login_new_user) {
            panel_key_press(mud->panel_login_new_user, key_code);
        }

        if (mud->login_screen == LOGIN_STAGE_EXISTING &&
            mud->panel_login_existing_user) {
            panel_key_press(mud->panel_login_existing_user, key_code);
        }

#ifdef WITH_SINGLEPLAYER
        if (mud->login_screen == LOGIN_STAGE_WORLD) {
            // the world screen's only text field is the single-player world editor's Name box; route typed characters
            // (including the Vita IME's replayed keystrokes) to the editor
            worldlist_handle_key(mud, key_code);
        }
#endif

        /*if (mud->login_screen == 3 && mud->panel_recover_user) {
            panel_key_press(mud->panel_recover_user, key_code);
        }*/
    } else {
        if (mud->show_appearance_change && mud->panel_appearance) {
            panel_key_press(mud->panel_appearance, key_code);
            return;
        }

        if (mud->show_change_password_step == PASSWORD_STEP_NONE &&
            mud->show_dialog_social_input == 0 &&
            mud->show_dialog_offer_x == 0 &&
            !(mud->bank_search_focus && mud->show_dialog_bank) &&
            /*mud->show_dialog_report_abuse_step == 0 &&*/
            !mud->is_sleeping && mud->panel_message_tabs) {
            int is_option_number = mudclient_option_numbers_level(mud) > 0 &&
                                   mud->show_option_menu && key_code >= '1' &&
                                   key_code <= '5';

            if (!is_option_number) {
                panel_key_press(mud->panel_message_tabs, key_code);
            }
        }

        if (mud->show_change_password_step == PASSWORD_STEP_MISMATCH ||
            mud->show_change_password_step == PASSWORD_STEP_FINISHED) {
            mud->show_change_password_step = PASSWORD_STEP_NONE;
        }
    }
}

void mudclient_key_pressed(mudclient *mud, int code, int char_code) {
    if (char_code == -1) {
        if (code == K_LEFT) {
            mud->key_left = 1;
        } else if (code == K_RIGHT) {
            mud->key_right = 1;
        } else if (code == K_UP) {
            mud->key_up = 1;
        } else if (code == K_DOWN) {
            mud->key_down = 1;
        } else if (code == K_PAGE_UP) {
            mud->key_page_up = 1;
        } else if (code == K_PAGE_DOWN) {
            mud->key_page_down = 1;
        } else if (code == K_HOME) {
            mud->key_home = 1;
        } else if (code == K_F1) {
            mud->options->interlace = !mud->options->interlace;

            /*for (int i = 0; i < mud->panel_game_options->control_count; i++) {
                if ((int *)mud->ui_options[i] == &mud->options->interlace) {
                    panel_toggle_checkbox(mud->panel_ui_options, i,
                                          mud->options->interlace);
                    break;
                }
            }*/
        } else if (mud->options->keyboard_shortcuts &&
                   (code == K_F2 || code == K_F3 || code == K_F4 ||
                    code == K_F5 || code == K_F6 || code == K_F7)) {
            // OpenRSC want_keyboard_shortcuts: F2..F7 switch the six UI tabs (Inventory, Map, Stats, Spellbook,
            // Friends, Options); pressing the open tab's key closes it; ignored while a modal dialog owns the screen
            int no_dialog = !mud->show_dialog_bank && !mud->show_dialog_shop &&
                            !mud->show_dialog_trade && !mud->show_dialog_duel &&
                            !mud->show_dialog_offer_x &&
                            !mud->show_option_menu &&
                            !mud->show_dialog_social_input;

            if (no_dialog) {
                int tab = INVENTORY_TAB + (code - K_F2);
                mud->show_ui_tab = mud->show_ui_tab == tab ? 0 : tab;
            }
        } else if (mud->options->escape_clear && code == K_ESCAPE) {
            memset(mud->input_text_current, '\0', INPUT_TEXT_LENGTH + 1);
            memset(mud->input_pm_current, '\0', INPUT_PM_LENGTH + 1);
            memset(mud->input_digits_current, '\0', INPUT_DIGITS_LENGTH + 1);
        }
    } else {
        if (code == K_TAB) {
            mud->key_tab = 1;
        } else if (mud->show_option_menu &&
                   mudclient_option_numbers_level(mud) > 0) {
            if (code == K_1) {
                mud->key_1 = 1;
            } else if (code == K_2) {
                mud->key_2 = 1;
            } else if (code == K_3) {
                mud->key_3 = 1;
            } else if (code == K_4) {
                mud->key_4 = 1;
            } else if (code == K_5) {
                mud->key_5 = 1;
            }
        }

        mudclient_handle_key_press(mud, char_code);
    }

    int found_text = 0;

    for (int i = 0; i < CHAR_SET_LENGTH; i++) {
        if (CHAR_SET[i] == char_code) {
            found_text = 1;
            break;
        }
    }

    int should_append_pm = (mud->show_dialog_bank ? mud->bank_search_focus : 1);

    if (found_text) {
        if (!mud->show_dialog_offer_x) {
            size_t current_length = strlen(mud->input_text_current);

            if (current_length < INPUT_TEXT_LENGTH) {
                mud->input_text_current[current_length] = char_code;
                mud->input_text_current[current_length + 1] = '\0';
            }

            size_t pm_length = strlen(mud->input_pm_current);

            if (pm_length < INPUT_PM_LENGTH && should_append_pm) {
                mud->input_pm_current[pm_length] = char_code;
                mud->input_pm_current[pm_length + 1] = '\0';
            }
        } else if ((IS_DIGIT_SEPARATOR(char_code) ||
                    IS_DIGIT_SUFFIX(char_code) ||
                    isdigit((unsigned char)char_code))) {
            size_t digits_length = strlen(mud->input_digits_current);

            if (digits_length < INPUT_DIGITS_LENGTH) {
                int add_digit_char = 1;

                if (digits_length > 0) {
                    int last_digit_char =
                        mud->input_digits_current[digits_length - 1];

                    /* only one suffix */
                    if (IS_DIGIT_SUFFIX(last_digit_char)) {
                        add_digit_char = 0;
                    } else {
                        /* don't allow consecutive decimals or separators */
                        add_digit_char = !(IS_DIGIT_SEPARATOR(char_code) &&
                                           IS_DIGIT_SEPARATOR(last_digit_char));
                    }
                } else {
                    /* don't allow separators or suffixes as first characters */
                    add_digit_char = !IS_DIGIT_SUFFIX(char_code) &&
                                     !IS_DIGIT_SEPARATOR(char_code);
                }

                if (add_digit_char) {
                    mud->input_digits_current[digits_length] = char_code;
                    mud->input_digits_current[digits_length + 1] = '\0';
                }
            }
        }
    }

    if (code == K_ENTER) {
        strcpy(mud->input_text_final, mud->input_text_current);

        if (should_append_pm) {
            strcpy(mud->input_pm_final, mud->input_pm_current);
        }

        if (mud->options->offer_x) {
            char filtered_digits[INPUT_DIGITS_LENGTH + 1] = {0};
            int filtered_length = 0;
            char digits_suffix = '\0';
            int has_decimal = 0;
            size_t digits_length = strlen(mud->input_digits_current);

            for (size_t i = 0; i < digits_length; i++) {
                char digit_char = mud->input_digits_current[i];

                if (isdigit((unsigned char)digit_char)) {
                    filtered_digits[filtered_length++] = digit_char;
                } else if (tolower((unsigned char)digit_char) == 'k' ||
                           tolower((unsigned char)digit_char) == 'm') {
                    digits_suffix = digit_char;
                } else if (!has_decimal && digit_char == '.') {
                    filtered_digits[filtered_length++] = digit_char;
                    has_decimal = 1;
                }
            }

            int scale = 1;

            if (digits_suffix == 'k') {
                scale = 1000;
            } else if (digits_suffix == 'm') {
                scale = 1000000;
            }

            mud->input_digits_final =
                (int)(atof(filtered_digits) * (float)scale);

            memset(mud->input_digits_current, '\0', INPUT_DIGITS_LENGTH + 1);
        }
    } else if (code == K_BACKSPACE) {
        size_t current_length = strlen(mud->input_text_current);

        if (current_length > 0) {
            mud->input_text_current[current_length - 1] = '\0';
        }

        size_t pm_length = strlen(mud->input_pm_current);

        if (pm_length > 0 && should_append_pm) {
            mud->input_pm_current[pm_length - 1] = '\0';
        }

        if (mud->options->offer_x) {
            size_t digits_length = strlen(mud->input_digits_current);

            if (digits_length > 0) {
                mud->input_digits_current[digits_length - 1] = '\0';
            }
        }
    }
}

void mudclient_key_released(mudclient *mud, int code) {
    if (code == K_LEFT) {
        mud->key_left = 0;
    } else if (code == K_RIGHT) {
        mud->key_right = 0;
    } else if (code == K_UP) {
        mud->key_up = 0;
    } else if (code == K_DOWN) {
        mud->key_down = 0;
    } else if (code == K_PAGE_UP) {
        mud->key_page_up = 0;
    } else if (code == K_PAGE_DOWN) {
        mud->key_page_down = 0;
    } else if (code == K_HOME) {
        mud->key_home = 0;
    } else if (code == K_TAB) {
        mud->key_tab = 0;
    } else if (code == K_1) {
        mud->key_1 = 0;
    } else if (code == K_2) {
        mud->key_2 = 0;
    } else if (code == K_3) {
        mud->key_3 = 0;
    } else if (code == K_4) {
        mud->key_4 = 0;
    } else if (code == K_5) {
        mud->key_5 = 0;
    }
}

void mudclient_mouse_moved(mudclient *mud, int x, int y) {
    mud->mouse_x = x;
    mud->mouse_y = y;

#ifdef RENDER_GL
    mud->gl_mouse_x = x;
    mud->gl_mouse_y = y;
#endif

    if (mudclient_is_ui_scaled(mud)) {
        mud->mouse_x /= 2;
        mud->mouse_y /= 2;
    }

    mud->mouse_action_timeout = 0;
}

void mudclient_mouse_released(mudclient *mud, int x, int y, int button) {
    mud->mouse_x = x;
    mud->mouse_y = y;

#ifdef RENDER_GL
    mud->gl_mouse_x = x;
    mud->gl_mouse_y = y;
#endif

    if (mudclient_is_ui_scaled(mud)) {
        mud->mouse_x /= 2;
        mud->mouse_y /= 2;
    }

    mud->mouse_button_down = 0;

    if (button == 2) {
        mud->middle_button_down = 0;

        int tick_delta = get_ticks() - mud->last_mouse_sample_ticks;

        if (tick_delta <= 0) {
            return;
        }

        int x_delta = mud->mouse_x - mud->last_mouse_sample_x;

        mud->camera_momentum = 2 * ((float)x_delta / (float)tick_delta);
    }
}

void mudclient_handle_mouse_history(mudclient *mud, int x, int y) {
    mud->mouse_click_x_history[mud->mouse_click_count] = x;
    mud->mouse_click_y_history[mud->mouse_click_count] = y;

    mud->mouse_click_count =
        (mud->mouse_click_count + 1) & (MOUSE_HISTORY_LENGTH - 1);

    for (int i = 10; i < 4000; i++) {
        int i1 = (mud->mouse_click_count - i) & (MOUSE_HISTORY_LENGTH - 1);

        if (mud->mouse_click_x_history[i1] == x &&
            mud->mouse_click_y_history[i1] == y) {
            int flag = 0;

            for (int j = 1; j < i; j++) {
                int k1 =
                    (mud->mouse_click_count - j) & (MOUSE_HISTORY_LENGTH - 1);

                int l1 = (i1 - j) & (MOUSE_HISTORY_LENGTH - 1);

                if (mud->mouse_click_x_history[l1] != x ||
                    mud->mouse_click_y_history[l1] != y) {
                    flag = 1;
                }

                if (mud->mouse_click_x_history[k1] !=
                        mud->mouse_click_x_history[l1] ||
                    mud->mouse_click_y_history[k1] !=
                        mud->mouse_click_y_history[l1]) {
                    break;
                }

                if (j == i - 1 && flag && mud->combat_timeout == 0 &&
                    mud->logout_timeout == 0) {
                    mudclient_send_logout(mud);
                    return;
                }
            }
        }
    }
}

void mudclient_mouse_pressed(mudclient *mud, int x, int y, int button) {
    mud->mouse_x = x;
    mud->mouse_y = y;

#ifdef RENDER_GL
    mud->gl_mouse_x = x;
    mud->gl_mouse_y = y;
#endif

    if (mudclient_is_ui_scaled(mud)) {
        mud->mouse_x /= 2;
        mud->mouse_y /= 2;
    }

    /*
     * in SDL12 mouse wheel scrolling is treated as digital button press,
     * while in SDL2 it is handled as a different type of event entirely.
     */
    if (button == 4 || button == 5) {
        if (mud->options->mouse_wheel) {
            if (button == 4) {
                mud->mouse_scroll_delta--;
            } else {
                mud->mouse_scroll_delta++;
            }
            return;
        } else {
            /* treat it as a right click when scrolling is disabled */
            button = 3;
        }
    }

    if ((mud->options->middle_click_camera != 0 ||
         (mudclient_is_touch(mud) &&
          mud->options->touch_horizontal_drag != 0)) &&
        button == 2) {
        mud->middle_button_down = 1;
        mud->origin_rotation = mud->camera_rotation;
        mud->origin_mouse_x = mud->mouse_x;

        mud->last_mouse_sample_ticks = get_ticks();
        mud->last_mouse_sample_x = mud->mouse_x;
        mud->camera_momentum = 0;
        return;
    }

    mud->mouse_button_down = button == 3 ? 2 : 1;
    mud->last_mouse_button_down = mud->mouse_button_down;
    mud->mouse_action_timeout = 0;

    mudclient_handle_mouse_history(mud, x, y);
}

void mudclient_set_target_fps(mudclient *mud, int fps) {
    mud->target_fps = 1000 / fps;
}

void mudclient_reset_timings(mudclient *mud) {
    for (int i = 0; i < 10; i++) {
        mud->timings[i] = 0;
    }
}

void mudclient_start(mudclient *mud) {
    if (mud->stop_timeout >= 0) {
        mud->stop_timeout = 0;
    }
}

void mudclient_stop(mudclient *mud) {
    if (mud->stop_timeout >= 0) {
        mud->stop_timeout = 4000 / mud->target_fps;
    }
}

void mudclient_draw_loading_progress(mudclient *mud, int percent, char *text) {
    surface_black_screen(mud->surface);

    /* hide the previously drawn textures */
    surface_draw_box(mud->surface, 0, 0, 128, 128, BLACK);

    if (!mud->options->lowmem) {
        /* jagex logo */
        int logo_sprite_id = SPRITE_LIMIT - 1;

        if (mud->surface->sprite_width[logo_sprite_id]) {
            int offset_x = 2;

            int logo_x = (mud->game_width / 2) -
                         (mud->surface->sprite_width[logo_sprite_id] / 2) -
                         offset_x;

            int logo_y = (mud->game_height / 2) -
                         (mud->surface->sprite_height[logo_sprite_id] / 2) - 46;

            surface_draw_sprite(mud->surface, logo_x, logo_y, logo_sprite_id);
        }
    }

    /* loading bar */
    int bar_x = (mud->game_width / 2.0f) - (LOADING_WIDTH / 2.0f);
    int bar_y = (mud->game_height / 2) + 2;
    int width = (int)((percent / (float)100) * LOADING_WIDTH);

    surface_draw_border(mud->surface, bar_x - 2, bar_y - 2, LOADING_WIDTH + 4,
                        LOADING_HEIGHT + 4, GREY_84);

    surface_draw_box(mud->surface, bar_x, bar_y, LOADING_WIDTH, LOADING_HEIGHT,
                     BLACK);

    surface_draw_box(mud->surface, bar_x, bar_y, width, LOADING_HEIGHT,
                     GREY_84);

    int copyright_x = (mud->surface->width / 2) - 1;
    int copyright_y = (mud->surface->height / 2) + 16;

    if (game_fonts[2] != NULL) {
        surface_draw_string_centre(mud->surface, text, copyright_x, copyright_y,
                                   FONT_REGULAR_12, GREY_C6);
    }

    /* footer */
    if (game_fonts[3] != NULL) {
        copyright_y += 20;

        surface_draw_string_centre(
            mud->surface, "Created by JAGeX - visit www.jagex.com", copyright_x,
            copyright_y, FONT_BOLD_13, GREY_C6);

        copyright_x += 7;
        copyright_y += 16;

        char *copyright_date = "2001-2002 Andrew Gower and Jagex Ltd";

        int copyright_icon_x =
            copyright_x - (surface_text_width(copyright_date, 3) / 2) - 8;

        surface_draw_circle(mud->surface, copyright_icon_x + 2, copyright_y - 5,
                            5, GREY_C6, 255, 0);

        surface_draw_circle(mud->surface, copyright_icon_x + 2, copyright_y - 5,
                            4, BLACK, 255, 0);

        surface_draw_string(mud->surface, "c", copyright_icon_x,
                            copyright_y - 2, FONT_REGULAR_11, GREY_C6);

        surface_draw_string_centre(mud->surface, copyright_date, copyright_x,
                                   copyright_y, FONT_BOLD_13, GREY_C6);
    }

#ifdef RENDER_GL
    if (mud->gl_last_swap == 0 || get_ticks() - mud->gl_last_swap >= 16) {
        mudclient_poll_events(mud);
        surface_draw(mud->surface);
#if defined(__vita__)
        // GL_TRUE = composite any active common dialog (the IME keyboard) over
        // the GL frame.
        vglSwapBuffers(GL_TRUE);
#elif defined(SDL12)
        SDL_GL_SwapBuffers();
#else
        SDL_GL_SwapWindow(mud->gl_window);
#endif
        mud->gl_last_swap = get_ticks();
    } else {
        surface_gl_reset_context(mud->surface);
    }
#elif defined(RENDER_3DS_GL)
    mudclient_3ds_gl_frame_start(mud, 1);
    surface_draw(mud->surface);
    mudclient_3ds_gl_frame_end();
#else
    surface_draw(mud->surface);
#endif
}

int8_t *mudclient_read_data_file(mudclient *mud, char *file, char *description,
                                 int percent) {
    char loading_text[35] = {0}; /* max description is 19 */

    sprintf(loading_text, "Loading %s - 0%%", description);
    mudclient_draw_loading_progress(mud, percent, loading_text);

    int8_t header[6];
#ifdef WII
    const int8_t *file_data = NULL;

    if (strstr(file, "jagex.jag") != NULL) {
        file_data = (int8_t *)jagex_jag;
    } else if (strstr(file, "config") != NULL) {
        file_data = (int8_t *)config85_jag;
    } else if (strstr(file, "media") != NULL) {
        file_data = (int8_t *)media58_jag;
    } else if (strstr(file, "entity") != NULL && strstr(file, ".mem") == NULL) {
        file_data = (int8_t *)entity24_jag;
    } else if (strstr(file, "entity") != NULL && strstr(file, ".mem") != NULL) {
        file_data = (int8_t *)entity24_mem;
    } else if (strstr(file, "textures") != NULL) {
        file_data = (int8_t *)textures17_jag;
    } else if (strstr(file, "maps") != NULL && strstr(file, ".mem") == NULL) {
        file_data = (int8_t *)maps63_jag;
    } else if (strstr(file, "maps") != NULL && strstr(file, ".mem") != NULL) {
        file_data = (int8_t *)maps63_mem;
    } else if (strstr(file, "land") != NULL && strstr(file, ".mem") == NULL) {
        file_data = (int8_t *)land63_jag;
    } else if (strstr(file, "land") != NULL && strstr(file, ".mem") != NULL) {
        file_data = (int8_t *)land63_mem;
    } else if (strstr(file, "models") != NULL) {
        file_data = (int8_t *)models36_jag;
    } else if (strstr(file, "sounds") != NULL) {
        file_data = (int8_t *)sounds1_mem;
    }

    if (file_data == NULL) {
        mud_error("Unable to read file: %s\n", file);
        exit(1);
    }

    memcpy(header, file_data, sizeof(header));
#else

#ifdef ANDROID
    char *prefixed_file = file;
    SDL_RWops *archive_stream = SDL_RWFromFile(prefixed_file, "rb");
#elif defined(_3DS) || defined(__SWITCH__)
    char prefixed_file[PATH_MAX];
    snprintf(prefixed_file, sizeof(prefixed_file), "romfs:/%s", file);
#elif defined(__vita__)
    // the cache/ directory is bundled into the .vpk, mounted read-only at app0:
    char prefixed_file[PATH_MAX];
    snprintf(prefixed_file, sizeof(prefixed_file), "app0:/cache/%s", file);
#else
    char prefixed_file[PATH_MAX];
    snprintf(prefixed_file, sizeof(prefixed_file), "./cache/%s", file);
#endif

#ifndef ANDROID
    printf("INFO: Loading %s\n", prefixed_file);
    FILE *archive_stream = fopen(prefixed_file, "rb");
#endif

    /* attempt to read cache from the current working directory first */
    if (archive_stream == NULL) {
        /* cwd failed, now try the xdg home directory... */
        const char *xdg_home = getenv("XDG_DATA_HOME");

        if (xdg_home == NULL) {
            const char *home = getenv("HOME");
            if (home == NULL) {
                home = "";
            }
            snprintf(prefixed_file, sizeof(prefixed_file),
                     "%s/.local/share/rsc-c/%s", home, file);
        } else {
            snprintf(prefixed_file, sizeof(prefixed_file), "%s/rsc-c/%s",
                     xdg_home, file);
        }

        printf("INFO: Loading %s\n", prefixed_file);
        archive_stream = fopen(prefixed_file, "rb");

        /* XDG failed, now try the global prefix... */
        if (archive_stream == NULL) {
            snprintf(prefixed_file, sizeof(prefixed_file), "%s/%s", MUD_DATADIR,
                     file);

            printf("INFO: Loading %s\n", prefixed_file);
            archive_stream = fopen(prefixed_file, "rb");
        }
    }

    if (archive_stream == NULL) {
        mud_error("Unable to read file: %s\n", prefixed_file);
        exit(1);
    }

#ifdef ANDROID
    SDL_RWread(archive_stream, header, sizeof(header), 1);
#else
    fread(header, sizeof(header), 1, archive_stream);
#endif
#endif

    int archive_size = ((header[0] & 0xff) << 16) + ((header[1] & 0xff) << 8) +
                       (header[2] & 0xff);

    int archive_size_compressed = ((header[3] & 0xff) << 16) +
                                  ((header[4] & 0xff) << 8) +
                                  (header[5] & 0xff);

    sprintf(loading_text, "Loading %s - 5%%", description);
    mudclient_draw_loading_progress(mud, percent, loading_text);

#ifdef WII
    int8_t *archive_data = file_data + 6;
#else
    int read = 0;
    int8_t *archive_data = malloc(archive_size_compressed);

    while (read < archive_size_compressed) {
        int length = archive_size_compressed - read;

#ifdef ANDROID
        SDL_RWread(archive_stream, archive_data + read, length, 1);
#else
        fread(archive_data + read, length, 1, archive_stream);
#endif

        read += length;

        sprintf(loading_text, "Loading %s - %d%%", description,
                5 + (read * 95) / archive_size_compressed);

        mudclient_draw_loading_progress(mud, percent, loading_text);
    }

#ifdef ANDROID
    SDL_RWclose(archive_stream);
#else
    fclose(archive_stream);
#endif
#endif

    sprintf(loading_text, "Unpacking %s", description);
    mudclient_draw_loading_progress(mud, percent, loading_text);

    if (archive_size_compressed != archive_size) {
        int8_t *decompressed = malloc(archive_size);
        bzip_decompress(decompressed, archive_data, archive_size_compressed, 0);

#ifndef WII
        free(archive_data);
#endif

        return decompressed;
    }

    return archive_data;
}

void mudclient_load_jagex(mudclient *mud) {
    int8_t *jagex_jag =
        mudclient_read_data_file(mud, "jagex.jag", "Jagex library", 0);

    if (jagex_jag != NULL) {
#ifdef RENDER_SW
        if (!mud->options->lowmem) {
            size_t len = 0;
            int8_t *logo_tga = load_data("logo.tga", 0, jagex_jag, &len);

            surface_parse_sprite_tga(mud->surface, SPRITE_LIMIT - 1, logo_tga,
                                     len, 0, 0);

            free(logo_tga);
        }
#endif
        for (size_t i = 0; i < FONT_FILES_LENGTH; i++) {
            int8_t *font = load_data(font_files[i], 0, jagex_jag, NULL);
            if (font == NULL) {
                break;
            }
            create_font(font, i);
        }

#ifndef WII
        free(jagex_jag);
#endif
    }
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    int logo_sprite_id = SPRITE_LIMIT - 1;

    mud->surface->sprite_width[logo_sprite_id] = 281;
    mud->surface->sprite_height[logo_sprite_id] = 85;
#endif
}

void mudclient_load_game_config(mudclient *mud) {
    char jag[16];

    snprintf(jag, sizeof(jag), "config%d.jag", mud->options->version_config);

    int8_t *config_jag = mudclient_read_data_file(
        mud, jag, "Configuration", 10);

    if (config_jag == NULL) {
        mud->error_loading_data = 1;
        return;
    }

    game_data_load_data(config_jag, mud->options->members,
                        mud->options->version_config);
    free(config_jag);

    // append OpenRSC custom item defs (ids 1290+)
    game_data_append_custom();

    /*int8_t *filter_jag = mudclient_read_data_file(
        mud, "filter" VERSION_STR(VERSION_FILTER) ".jag", "Chat system", 15);

    if (filter_jag == NULL) {
        mud->error_loading_data = 1;
        return;
    }

    free(filter_jag);*/

    if (mud->options->members && mud->options->rename_herblaw_items) {
        modify_unidentified_herbs();
        modify_unfinished_potions();
    }
    if (mud->options->rename_herblaw_items) {
        modify_potion_dosage();
    }
}

static void mudclient_load_media_dat(mudclient *mud, void *media_jag) {
    int8_t *index_dat = load_data("index.dat", 0, media_jag, NULL);

    if (mud->options->version_media < 59) {
        surface_parse_sprite(mud->surface, mud->sprite_media,
                             load_data("inv1.dat", 0, media_jag, NULL),
                             index_dat, 1);
    }

    surface_parse_sprite(mud->surface, mud->sprite_media + 1,
                         load_data("inv2.dat", 0, media_jag, NULL), index_dat,
                         6);

    surface_parse_sprite(mud->surface, mud->sprite_media + 9,
                         load_data("bubble.dat", 0, media_jag, NULL), index_dat,
                         1);

    if (!mud->options->lowmem) {
        surface_parse_sprite(mud->surface, mud->sprite_media + 10,
                             load_data("runescape.dat", 0, media_jag, NULL),
                             index_dat, 1);
    }

    surface_parse_sprite(mud->surface, mud->sprite_media + 11,
                         load_data("splat.dat", 0, media_jag, NULL), index_dat,
                         3);

    surface_parse_sprite(mud->surface, mud->sprite_media + 14,
                         load_data("icon.dat", 0, media_jag, NULL), index_dat,
                         8);

    if (!mud->options->lowmem) {
        surface_parse_sprite(mud->surface, mud->sprite_media + 22,
                             load_data("hbar.dat", 0, media_jag, NULL),
                             index_dat, 1);
    }

    surface_parse_sprite(mud->surface, mud->sprite_media + 23,
                         load_data("hbar2.dat", 0, media_jag, NULL), index_dat,
                         1);

    surface_parse_sprite(mud->surface, mud->sprite_media + 24,
                         load_data("compass.dat", 0, media_jag, NULL),
                         index_dat, 1);

    surface_parse_sprite(mud->surface, mud->sprite_media + 25,
                         load_data("buttons.dat", 0, media_jag, NULL),
                         index_dat, 2);

    if (mud->options->version_media >= 59) {
        surface_parse_sprite(mud->surface, mud->sprite_media + 27,
                             load_data("labels.dat", 0, media_jag, NULL),
                             index_dat, 6);

        surface_parse_sprite(mud->surface, mud->sprite_media + 33,
                             load_data("inv3.dat", 0, media_jag, NULL),
                             index_dat, 6);

        surface_parse_sprite(mud->surface, mud->sprite_media + 39,
                             load_data("message.dat", 0, media_jag, NULL),
                             index_dat, 1);

        surface_parse_sprite(mud->surface, mud->sprite_media + 40,
                             load_data("keyboard.dat", 0, media_jag, NULL),
                             index_dat, 1);
    }

    surface_parse_sprite(mud->surface, mud->sprite_util,
                         load_data("scrollbar.dat", 0, media_jag, NULL),
                         index_dat, 2);

    surface_parse_sprite(mud->surface, mud->sprite_util + 2,
                         load_data("corners.dat", 0, media_jag, NULL),
                         index_dat, 4);

    surface_parse_sprite(mud->surface, mud->sprite_util + 6,
                         load_data("arrows.dat", 0, media_jag, NULL), index_dat,
                         2);

    surface_parse_sprite(mud->surface, mud->sprite_projectile,
                         load_data("projectile.dat", 0, media_jag, NULL),
                         index_dat, game_data.projectile_sprite);

    int sprite_count = game_data.item_sprite_count;

    for (int i = 1; sprite_count > 0; i++) {
        char file_name[20] = {0};
        sprintf(file_name, "objects%d.dat", i);

        int current_sprite_count = sprite_count;
        sprite_count -= 30;

        if (current_sprite_count > 30) {
            current_sprite_count = 30;
        }

        surface_parse_sprite(mud->surface, mud->sprite_item + (i - 1) * 30,
                             load_data(file_name, 0, media_jag, NULL),
                             index_dat, current_sprite_count);
    }

    free(index_dat);
}

static void mudclient_load_media_tga(mudclient *mud, void *media_jag) {
    void *data;
    size_t len;

    data = load_data("inv1.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media,
                         data, len, 1, 1);

    data = load_data("inv2.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media + 1,
                         data, len, 1, 6);

    data = load_data("bubble.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media + 9,
                         data, len, 1, 1);

    if (!mud->options->lowmem) {
        data = load_data("runescape.tga", 0, media_jag, &len);
        assert(data != NULL);
        surface_parse_sprite_tga(mud->surface, mud->sprite_media + 10,
                             data, len, 1, 1);
    }

    data = load_data("splat.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media + 11,
                             data, len, 3, 1);

    data = load_data("icon.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media + 14,
                             data, len, 4, 2);

    if (!mud->options->lowmem) {
        data = load_data("hbar.tga", 0, media_jag, &len);
        assert(data != NULL);
        surface_parse_sprite_tga(mud->surface, mud->sprite_media + 22,
                                 data, len, 1, 1);
    }

    data = load_data("hbar2.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media + 23,
                             data, len, 1, 1);

    data = load_data("compass.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media + 24,
                             data, len, 1, 1);

    data = load_data("buttons.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_media + 25,
                             data, len, 1, 2);

    data = load_data("scrollbar.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_util,
                             data, len, 2, 1);

    data = load_data("corners.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_util + 2,
                             data, len, 4, 1);

    data = load_data("arrows.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_util + 6,
                         data, len, 2, 1);

    data = load_data("projectile.tga", 0, media_jag, &len);
    assert(data != NULL);
    surface_parse_sprite_tga(mud->surface, mud->sprite_projectile,
                             data, len, 3, 1);

    int sprite_count = game_data.item_sprite_count;

    for (int i = 1; sprite_count > 0; i++) {
        char file_name[32];

        snprintf(file_name, sizeof(file_name), "objects%d.tga", i);

        data = load_data(file_name, 0, media_jag, &len);
        if (data == NULL) {
            break;
        }

        int current_sprite_count = sprite_count;
        sprite_count -= 30;

        if (current_sprite_count > 30) {
            current_sprite_count = 30;
        }


        surface_parse_sprite_tga(mud->surface, mud->sprite_item + (i - 1) * 30,
                             data, len, 10, i < 7 ? 3 : 1);
    }
}

void mudclient_load_media(mudclient *mud) {
#if defined(RENDER_GL) || defined(RENDER_SW) || defined(RENDER_3DS_GL)
    char jag[16];

    snprintf(jag, sizeof(jag), "media%d.jag", mud->options->version_media);

    int8_t *media_jag = mudclient_read_data_file(
        mud, jag, "2d graphics", 20);

    if (media_jag == NULL) {
        mud->error_loading_data = 1;
        return;
    }

    if (!MEDIA_IS_TGA(mud->options->version_media)) {
        mudclient_load_media_dat(mud, media_jag);
    } else {
        mudclient_load_media_tga(mud, media_jag);
    }

    // dimensions for the custom item icon slots (drawn from the separate GL custom atlas)
    surface_setup_custom_item_sprites(mud->surface, mud->sprite_item);

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    // OpenRSC rank-crown slots share the custom atlas, right after the items
    surface_setup_custom_crown_sprites(mud->surface, mud->sprite_item);

    // per-frame geometry for the custom worn-equipment / no-body-NPC entity sprite id range (drawn from the separate
    // GL custom-entity atlas), GL-only
    surface_setup_custom_entity_sprites(mud->surface);
#endif

#ifdef RENDER_SW
    /* this is probably for an optimization, but it is necessary for the action
     * bubble scaling */
    if (mud->options->version_media >= 59) {
        for (int i = 0; i < 6; i++) {
            surface_load_sprite(mud->surface, mud->sprite_media + 33 + i);
        }

        surface_load_sprite(mud->surface, mud->sprite_media + 39);
    } else {
        surface_load_sprite(mud->surface, mud->sprite_media);
    }

    surface_load_sprite(mud->surface, mud->sprite_media + 9);

    for (int i = 11; i <= 26; i++) {
        surface_load_sprite(mud->surface, mud->sprite_media + i);
    }

    for (int i = 0; i < game_data.projectile_sprite; i++) {
        surface_load_sprite(mud->surface, mud->sprite_projectile + i);
    }

    for (int i = 0; i < game_data.item_sprite_count; i++) {
        surface_load_sprite(mud->surface, mud->sprite_item + i);
    }
#endif

#ifndef WII
    free(media_jag);
#endif
#endif
}

void mudclient_load_entities(mudclient *mud) {
#if defined(RENDER_GL) || defined(RENDER_SW) || defined(RENDER_3DS_GL)
    char jag[16];
    snprintf(jag, sizeof(jag), "entity%d.jag", mud->options->version_entity);

    int8_t *entity_jag = mudclient_read_data_file(
        mud, jag, "people and monsters", 30);

    int8_t *entity_jag_legacy = NULL;

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
    if (mud->options->tga_sprites) {
        entity_jag_legacy = mudclient_read_data_file(mud, "entity8.jag",
                                                     "people and monsters", 37);
    }
    if (ENTITY_IS_TGA(mud->options->version_entity)) {
        entity_jag_legacy = entity_jag;
    }
#endif

    if (entity_jag == NULL) {
        mud->error_loading_data = 1;
        return;
    }

    int8_t *index_dat = load_data("index.dat", 0, entity_jag, NULL);
    int8_t *entity_jag_mem = NULL;
    int8_t *index_dat_mem = NULL;

    if (mud->options->members && !ENTITY_IS_TGA(mud->options->version_entity)) {
        snprintf(jag, sizeof(jag), "entity%d.mem",
            mud->options->version_entity);

        entity_jag_mem = mudclient_read_data_file(
            mud, jag, "member graphics", 45);

        if (entity_jag_mem == NULL) {
            mud->error_loading_data = 1;
            return;
        }

        index_dat_mem = load_data("index.dat", 0, entity_jag_mem, NULL);
    }

    int frame_count = 0;
    int animation_index = 0;

    int i = 0;

    for (;;) {
    label0:;
        if (i >= game_data.animation_count) {
            break;
        }
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        // skip OpenRSC custom entity animations: pre-assigned file_id in the high range (>=
        // GL_CUSTOM_ENTITY_FILE_BASE), packed in custom_entities.png atlas not entity{N}.jag
        if (game_data.animations[i].file_id >= GL_CUSTOM_ENTITY_FILE_BASE) {
            i++;
            goto label0;
        }
#endif
        char *animation_name = game_data.animations[i].name;
        for (int j = 0; j < i; j++) {
            if (strcmp(game_data.animations[j].name, animation_name) != 0) {
                continue;
            }

            game_data.animations[i].file_id = game_data.animations[j].file_id;
            i++;
            goto label0;
        }

        bool older_is_better = false;
        const char *extension = "dat";
        int8_t *archive_file = entity_jag;

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
        if (ENTITY_IS_TGA(mud->options->version_entity)) {
            older_is_better = true;
            extension = "tga";
        } else if (mud->options->tga_sprites) {
            const char **older_names = anims_older_is_better;
            while (*older_names != NULL) {
                if (strcmp(animation_name, *older_names) == 0) {
                    older_is_better = true;
                    extension = "tga";
                    archive_file = entity_jag_legacy;
                    break;
                }

                older_names++;
            }
        }
#endif

        char file_name[255] = {0};
        sprintf(file_name, "%s.%s", animation_name, extension);

        size_t len = 0;

        int8_t *animation_dat = load_data(file_name, 0, archive_file, &len);
        int8_t *animation_index_dat = index_dat;

        if (animation_dat == NULL && mud->options->members) {
            animation_dat = load_data(file_name, 0, entity_jag_mem, &len);
            animation_index_dat = index_dat_mem;
        }

        if (animation_dat != NULL) {
            if (older_is_better) {
                surface_parse_sprite_tga(mud->surface, animation_index,
                                         animation_dat, len, 15, 1);
            } else {
                surface_parse_sprite(mud->surface, animation_index,
                                     animation_dat, animation_index_dat, 15);
            }

            frame_count += 15;

            if (game_data.animations[i].has_a) {
                if (older_is_better && strcmp(animation_name, "camel") == 0) {
                    /* camel attack anim was a much later addition */
                    older_is_better = false;
                    extension = "dat";
                    archive_file = entity_jag;
                }

                sprintf(file_name, "%sa.%s", animation_name, extension);

                int8_t *a_dat = load_data(file_name, 0, archive_file, &len);
                int8_t *a_index_dat = index_dat;

                if (a_dat == NULL && mud->options->members) {
                    a_dat = load_data(file_name, 0, entity_jag_mem, &len);
                    a_index_dat = index_dat_mem;
                }

                if (a_dat == NULL) {
                    goto fallthrough;
                }

                if (older_is_better) {
                    surface_parse_sprite_tga(mud->surface, animation_index + 15,
                                             a_dat, len, 3, 1);
                } else {
                    surface_parse_sprite(mud->surface, animation_index + 15,
                                         a_dat, a_index_dat, 3);
                }

                frame_count += 3;
            }

            if (game_data.animations[i].has_f) {
                sprintf(file_name, "%sf.%s", animation_name, extension);

                int8_t *f_dat = load_data(file_name, 0, archive_file, &len);
                int8_t *f_index_dat = index_dat;

                if (f_dat == NULL && mud->options->members) {
                    f_dat = load_data(file_name, 0, entity_jag_mem, &len);
                    f_index_dat = index_dat_mem;
                }

                if (older_is_better) {
                    surface_parse_sprite_tga(mud->surface, animation_index + 18,
                                             f_dat, len, 9, 1);
                } else {
                    surface_parse_sprite(mud->surface, animation_index + 18,
                                         f_dat, f_index_dat, 9);
                }

                frame_count += 9;
            }

fallthrough:
            /* TODO why? */
            if (game_data.animations[i].gender != 0) {
                for (int j = animation_index; j < animation_index + 27; j++) {
                    surface_load_sprite(mud->surface, j);
                }
            }
        }

        game_data.animations[i].file_id = animation_index;
        animation_index += 27;

        i++;
    }

    mud_log("Loaded: %d frames of animation\n", frame_count);

    // guard: entity animation top (animation_index) stays below sprite_media
    if (animation_index > mud->sprite_media) {
        mud_error("FATAL: animation region top %d overran sprite_media %d\n",
                  animation_index, mud->sprite_media);
        mud->error_loading_data = 1;
    }

#ifndef WII
    free(entity_jag);
    if (entity_jag_legacy != entity_jag) {
        free(entity_jag_legacy);
    }
    free(entity_jag_mem);
#endif

    free(index_dat);
    free(index_dat_mem);
#endif
}

void mudclient_load_textures(mudclient *mud) {
#ifdef RENDER_SW
    char jag[16];

    snprintf(jag, sizeof(jag), "textures%d.jag",
        mud->options->version_textures);

    int8_t *textures_jag = mudclient_read_data_file(mud, jag, "Textures", 50);

    if (textures_jag == NULL) {
        mud->error_loading_data = 1;
        return;
    }

    int8_t *index_dat = load_data("index.dat", 0, textures_jag, NULL);

    scene_allocate_textures(mud->scene, game_data.texture_count, 7, 11);

    char file_name[255] = {0};

    Surface *surface = mud->surface;

    for (int i = 0; i < game_data.texture_count; i++) {
#ifdef USE_TOONSCAPE
        if (toonscape_avoid_load(i)) {
            continue;
        }
#endif
        sprintf(file_name, "%s.dat", game_data.textures[i].name);

        int8_t *texture_dat = load_data(file_name, 0, textures_jag, NULL);
        if (texture_dat == NULL) {
            continue;
        }

        surface_parse_sprite(surface, mud->sprite_texture, texture_dat,
                             index_dat, 1);

        surface_draw_box(surface, 0, 0, 128, 128, MAGENTA);
        surface_draw_sprite(surface, 0, 0, mud->sprite_texture);

#ifndef USE_LOCOLOUR
        free(surface->sprite_palette[mud->sprite_texture]);
        surface->sprite_palette[mud->sprite_texture] = NULL;
#endif

        free(surface->sprite_colours[mud->sprite_texture]);
        surface->sprite_colours[mud->sprite_texture] = NULL;

        int texture_size = surface->sprite_width_full[mud->sprite_texture];
        char *name_sub = game_data.textures[i].subtype_name;

        if (name_sub) {
            int sub_length = strlen(name_sub);

            if (sub_length > 0 && sub_length <= 250) {
                sprintf(file_name, "%s.dat", name_sub);

                int8_t *texture_sub_dat =
                    load_data(file_name, 0, textures_jag, NULL);

                surface_parse_sprite(surface, mud->sprite_texture,
                                     texture_sub_dat, index_dat, 1);

                surface_draw_sprite(surface, 0, 0, mud->sprite_texture);

#ifndef USE_LOCOLOUR
                free(surface->sprite_palette[mud->sprite_texture]);
                surface->sprite_palette[mud->sprite_texture] = NULL;
#endif

                free(surface->sprite_colours[mud->sprite_texture]);
                surface->sprite_colours[mud->sprite_texture] = NULL;
            }
        }

        surface_screen_raster_to_sprite(surface, mud->sprite_texture_world + i,
                                        0, 0, texture_size, texture_size);

        for (int j = 0; j < texture_size * texture_size; j++) {
            if (surface->surface_pixels[mud->sprite_texture_world + i][j] ==
                GREEN) {
                surface->surface_pixels[mud->sprite_texture_world + i][j] =
                    MAGENTA;
            }
        }

        surface_screen_raster_to_palette_sprite(surface,
                                                mud->sprite_texture_world + i);

        scene_define_texture(
            mud->scene, i,
            surface->sprite_colours[mud->sprite_texture_world + i],
            surface->sprite_palette[mud->sprite_texture_world + i],
            (texture_size / 64) - 1);

        free(surface->surface_pixels[mud->sprite_texture_world + i]);
        surface->surface_pixels[mud->sprite_texture_world + i] = NULL;
    }

    free(index_dat);

#ifndef WII
    free(textures_jag);
#endif
#else
    (void)mud;
#endif
}

void mudclient_load_models(mudclient *mud) {
    if (!mud->options->lowmem) {
        for (int i = 0; i < ANIMATED_MODELS_LENGTH; i++) {
            game_data_get_model_index(mud_strdup(animated_models[i]));
        }
    }

    char models_filename[16];

    snprintf(models_filename, sizeof(models_filename),
        "models%d.jag", mud->options->version_models);

    int8_t *models_jag =
        mudclient_read_data_file(mud, models_filename, "3d models", 60);

    if (models_jag == NULL) {
        mud->error_loading_data = 1;
        return;
    }

    // custom-model fallback: names missing in models36 resolve from custom-models.jag (STORED JAG, original name
    // hashes); optional, absent = no-op
    int8_t *custom_models_jag = NULL;
    {
        char custom_path[PATH_MAX];
#if defined(__vita__)
        snprintf(custom_path, sizeof(custom_path),
                 "app0:/cache/custom-models.jag");
#else
        snprintf(custom_path, sizeof(custom_path), "./cache/custom-models.jag");
#endif
        FILE *cf = fopen(custom_path, "rb");
        if (cf != NULL) {
            fseek(cf, 0, SEEK_END);
            long fsize = ftell(cf);
            fseek(cf, 0, SEEK_SET);
            if (fsize > 6) {
                int8_t header[6];
                if (fread(header, sizeof(header), 1, cf) == 1) {
                    // STORED whole-archive: body after the 6-byte header is the raw name-table and entry data, no
                    // bzip2 inflate
                    int unpacked = ((header[0] & 0xff) << 16) +
                                   ((header[1] & 0xff) << 8) + (header[2] & 0xff);
                    int packed = ((header[3] & 0xff) << 16) +
                                 ((header[4] & 0xff) << 8) + (header[5] & 0xff);
                    if (unpacked == packed && packed == fsize - 6) {
                        custom_models_jag = malloc(packed);
                        if (custom_models_jag != NULL &&
                            fread(custom_models_jag, packed, 1, cf) != 1) {
                            free(custom_models_jag);
                            custom_models_jag = NULL;
                        }
                    }
                }
            }
            fclose(cf);
        }
    }

    for (int i = 0; i < game_data.model_count; i++) {
        char *model_name = game_data.model_name[i];

        char file_name[strlen(model_name) + 5];
        sprintf(file_name, "%s.ob3", model_name);

        uint32_t offset = get_data_file_offset(file_name, models_jag);
        uint32_t len = get_data_file_length(file_name, models_jag);
        int8_t *base = models_jag;

        // authentic models resolve from models36 first; names that miss fall back to the custom jag
        if (offset == 0 && custom_models_jag != NULL) {
            uint32_t coffset = get_data_file_offset(file_name, custom_models_jag);
            if (coffset != 0) {
                offset = coffset;
                len = get_data_file_length(file_name, custom_models_jag);
                base = custom_models_jag;
            }
        }

        GameModel *game_model = malloc(sizeof(GameModel));

        if (offset != 0) {
            game_model_new_ob3(game_model, base + offset, len);
        } else {
            mud_error("missing model \"%s.ob3\" from %s\n", model_name,
                      models_filename);

            game_model_new_alloc(game_model, 1, 1);
        }

        mud->game_models[i] = game_model;

        if (strcmp(model_name, "giantcrystal") == 0) {
            mud->game_models[i]->transparent = 1;
        }
    }

    if (mud->options->ground_item_models) {
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        mud->item_models = calloc(game_data.item_count, sizeof(GameModel *));

        for (int i = 0; i < game_data.item_count; i++) {
            int sprite_id = game_data.items[i].sprite;

            char file_name[21] = {0};
            sprintf(file_name, "item-%d.ob3", sprite_id);

            uint32_t offset = get_data_file_offset(file_name, models_jag);
            uint32_t len = get_data_file_length(file_name, models_jag);

            if (offset == 0) {
                continue;
            }

            GameModel *game_model = malloc(sizeof(GameModel));
            game_model_new_ob3(game_model, models_jag + offset, len);

            int mask_colour = game_data.items[i].mask;

            if (mask_colour != 0) {
                game_model_mask_faces(game_model, game_model->face_fill_back,
                                      mask_colour);

                game_model_mask_faces(game_model, game_model->face_fill_front,
                                      mask_colour);
            }

            mud->item_models[i] = game_model;
        }
#else
        int max_sprite_id = 0;

        for (int i = 0; i < game_data.item_count; i++) {
            int sprite_id = game_data.items[i].sprite;

            if (sprite_id > max_sprite_id) {
                max_sprite_id = sprite_id;
            }
        }

        mud->item_models = calloc(max_sprite_id, sizeof(GameModel *));

        for (int i = 0; i < max_sprite_id; i++) {
            char file_name[21] = {0};
            sprintf(file_name, "item-%d.ob3", i);

            uint32_t offset = get_data_file_offset(file_name, models_jag);
            uint32_t len = get_data_file_length(file_name, models_jag);

            if (offset == 0) {
                continue;
            }

            GameModel *game_model = malloc(sizeof(GameModel));
            game_model_new_ob3(game_model, models_jag + offset, len);

            mud->item_models[i] = game_model;
        }
#endif
    }

    free(models_jag);
    free(custom_models_jag);

#ifdef RENDER_GL
    int models_length = game_data.model_count - 1;
    int item_models_length = game_data.item_count;

    if (mud->options->ground_item_models) {
        models_length += item_models_length;
    }

    GameModel *models_buffer[models_length];

    for (int i = 0; i < game_data.model_count - 1; i++) {
        models_buffer[i] = mud->game_models[i];
    }

    if (mud->options->ground_item_models) {
        for (int i = 0; i < item_models_length; i++) {
            models_buffer[game_data.model_count - 1 + i] = mud->item_models[i];
        }
    }

    game_model_gl_buffer_models(&mud->scene->gl_game_model_buffers,
                                &mud->scene->gl_game_model_buffer_length,
                                models_buffer, models_length, 0, 0);
#endif
}

void mudclient_load_maps(mudclient *mud) {
    char jag[16];

    snprintf(jag, sizeof(jag), "maps%d.jag", mud->options->version_maps);
    mud->world->map_pack = mudclient_read_data_file(
        mud, jag, "map", 70);

    if (mud->options->members) {
        snprintf(jag, sizeof(jag), "maps%d.mem", mud->options->version_maps);
        mud->world->member_map_pack = mudclient_read_data_file(
            mud, jag, "members map", 75);
    }

    if (HAS_SEPARATE_LAND(mud->options->version_maps)) {
        snprintf(jag, sizeof(jag), "land%d.jag", mud->options->version_maps);
        mud->world->landscape_pack = mudclient_read_data_file(
            mud, jag, "landscape", 80);

        if (mud->options->members) {
            snprintf(jag, sizeof(jag), "land%d.mem",
                mud->options->version_maps);
            mud->world->member_landscape_pack = mudclient_read_data_file(
                mud, jag, "members landscape", 85);
        }
    }
}

void mudclient_load_sounds(mudclient *mud) {
    char jag[16];

    snprintf(jag, sizeof(jag), "sounds%d.mem", mud->options->version_sounds);

    mud->sound_data = mudclient_read_data_file(mud, jag, "Sound effects", 90);
}

void mudclient_reset_game(mudclient *mud) {
#ifndef REVISION_177
    mud->system_update = 0;
#endif

    mud->combat_style = 0;
    mud->logout_timeout = 0;
    mud->login_screen = 0;
    mud->logged_in = 1;

    memset(mud->input_pm_current, '\0', INPUT_PM_LENGTH + 1);
    memset(mud->input_pm_final, '\0', INPUT_PM_LENGTH + 1);

    surface_black_screen(mud->surface);

#ifdef RENDER_3DS_GL
    mudclient_3ds_gl_frame_start(mud, 1);
    surface_draw(mud->surface);
    mudclient_3ds_gl_frame_end();
#else
    surface_draw(mud->surface);
#endif

    for (int i = 0; i < mud->object_count; i++) {
        scene_remove_model(mud->scene, mud->objects[i].model);

        world_remove_object(mud->world, mud->objects[i].x, mud->objects[i].y,
                            mud->objects[i].id);

#ifdef RENDER_SW
        game_model_destroy(mud->objects[i].model);
#endif
        free(mud->objects[i].model);
        mud->objects[i].model = NULL;
    }

    for (int i = 0; i < mud->wall_object_count; i++) {
        scene_remove_model(mud->scene, mud->wall_objects[i].model);

        world_remove_wall_object(
            mud->world, mud->wall_objects[i].x, mud->wall_objects[i].y,
            mud->wall_objects[i].direction, mud->wall_objects[i].id);

        game_model_destroy(mud->wall_objects[i].model);
        free(mud->wall_objects[i].model);
        mud->wall_objects[i].model = NULL;
    }

    mud->object_count = 0;
    mud->wall_object_count = 0;
    mud->ground_item_count = 0;
    mud->player_count = 0;

    GameCharacter *freed_characters[NPCS_SERVER_MAX] = {0};
    int freed_count = 0;

    for (int i = 0; i < PLAYERS_SERVER_MAX; i++) {
        GameCharacter *player = mud->player_server[i];

        if (player != NULL) {
            freed_characters[freed_count++] = player;
            free(player);
            mud->player_server[i] = NULL;
        }
    }

    for (int i = 0; i < PLAYERS_MAX; i++) {
    label0:;
        GameCharacter *player = mud->players[i];

        if (player) {
            for (int j = 0; j < NPCS_SERVER_MAX; j++) {
                if (freed_characters[j] == player) {
                    mud->players[i] = NULL;
                    i++;
                    goto label0;
                }
            }
        }

        free(player);
        mud->players[i] = NULL;
    }

    mud->combat_target = NULL;
    mud->local_player = malloc(sizeof(GameCharacter));
    game_character_new(mud->local_player);

    memset(freed_characters, 0, sizeof(GameCharacter *) * NPCS_SERVER_MAX);
    freed_count = 0;

    mud->npc_count = 0;

    for (int i = 0; i < NPCS_SERVER_MAX; i++) {
        GameCharacter *npc = mud->npcs_server[i];

        if (npc != NULL) {
            freed_characters[freed_count++] = npc;
            free(npc);
            mud->npcs_server[i] = NULL;
        }
    }

    for (int i = 0; i < NPCS_MAX; i++) {
    label1:;
        GameCharacter *npc = mud->npcs[i];

        if (npc != NULL) {
            for (int j = 0; j < freed_count; j++) {
                if (freed_characters[j] == npc) {
                    mud->npcs[i] = NULL;
                    i++;
                    goto label1;
                }
            }
        }

        free(npc);
        mud->npcs[i] = NULL;
    }

    for (int i = 0; i < PRAYER_COUNT; i++) {
        mud->prayer_on[i] = 0;
    }

    mud->mouse_button_click = 0;
    mud->last_mouse_button_down = 0;
    mud->mouse_button_down = 0;
    mud->show_dialog_shop = 0;
    mud->show_dialog_bank = 0;
    mud->show_dialog_bank_pin = 0;
    // OpenRSC resetGame() hides every custom interface; hide these two dialogs too
    mud->show_dialog_bank_preset = 0;
    mud->show_dialog_online_list = 0;
    // -1 = no online-list entry menu open (0 would mean "the first entry")
    mud->online_list_menu_entry = -1;
    // -1 = nothing picked up for a bank reorder (0 is a real slot)
    mud->bank_organize_slot = -1;
    mud->bank_pin_length = 0;
    mud->bank_pin_input[0] = '\0';
    mud->orsc_progress_visible = 0;
    mud->orsc_clan_in = 0;
    mud->orsc_clan_size = 0;
    mud->orsc_clan_is_leader = 0;
    mud->orsc_party_in = 0;
    mud->orsc_party_size = 0;
    mud->orsc_party_is_leader = 0;
    mud->show_dialog_ironman = 0;
    mud->orsc_auction_visible = 0;
    mud->orsc_auction_sell_mode = 0;
    mud->orsc_auction_offer_stage = 0;
    mud->orsc_kill_feed_count = 0;
    mud->orsc_unlocked_skin_known = 0;
    memset(mud->orsc_unlocked_skin, 0, sizeof(mud->orsc_unlocked_skin));
    mud->orsc_trawler_visible = 0;
    mud->orsc_black_hole = 0;
    mud->orsc_show_points_to_gp = 0;
    // OpenRSC resetGame() zeroes these: elixir timer and Note withdraw-mode
    mud->orsc_elixir_timer = 0;
    mud->bank_swap_note_mode = 0;
    // default the per-player HUD show flags on until SEND_GAME_SETTINGS says otherwise
    mud->orsc_show_side_menu = 1;
    mud->orsc_show_kill_feed = 1;
    // kill-counter lines default off like OpenRSC C_ toggles
    mud->orsc_show_npc_kc = MUD_SP_WIRE(mud) ? 1 : 0;
    mud->orsc_show_recent_npc_kc = MUD_SP_WIRE(mud) ? 1 : 0;
    mud->orsc_bank_cert_mode = 0;
    mud->bank_offer_uncert = 0;

    // xp-counter: OpenRSC C_EXPERIENCE_COUNTER default 1 (Recent); session tracking starts empty
    mud->orsc_xp_counter_synced = 1;
    memset(mud->player_experience_gained, 0,
           sizeof(mud->player_experience_gained));
    memset(mud->xp_gain_start_ms, 0, sizeof(mud->xp_gain_start_ms));
    mud->xp_gained_total = 0;
    mud->xp_gained_total_start_ms = 0;
    mud->xp_last_gain_skill = -1;
    memset(mud->orsc_party_member_flash, 0,
           sizeof(mud->orsc_party_member_flash));
    // clear these custom caches on reset, else they survive into the next world
    mud->orsc_clan_browse_count = 0;
    memset(mud->orsc_preset_known, 0, sizeof(mud->orsc_preset_known));
    mud->is_sleeping = 0;
    mud->friend_list_count = 0;
}

void mudclient_login(mudclient *mud, char *username, char *password,
                     int reconnecting) {
    if (mud->world_full_timeout > 0) {
        mudclient_show_login_screen_status(mud, "Please wait...",
                                           "Connecting to server");

        delay_ticks(2000);

        mudclient_show_login_screen_status(
            mud, "Sorry! the server is currently full.",
            "Please try again later");

        return;
    }

#ifdef WITH_SINGLEPLAYER
    // single-player and co-op guests are username-only: fill a fixed internal password before the "both required"
    // check so a blank password box passes; username still required
    if (MUD_SP_WIRE(mud)) {
        password = (char *)"rscsp";
    }
#endif

    if (strlen(username) == 0 || strlen(password) == 0) {
        mudclient_show_login_screen_status(mud,
                                           "You must enter both a username",
                                           "and a password - Please try again");
        return;
    }

    if (mud->username != username) {
        strcpy(mud->username, username);
    }

    if (mud->password != password) {
        strcpy(mud->password, password);
    }

    char formatted_username[USERNAME_LENGTH + 1] = {0};
    format_auth_string(username, USERNAME_LENGTH, formatted_username);

    char formatted_password[PASSWORD_LENGTH + 1] = {0};
    format_auth_string(password, PASSWORD_LENGTH, formatted_password);

    if (reconnecting) {
#ifdef RENDER_3DS_GL
        mudclient_3ds_gl_frame_start(mud, 0);
#endif

        mudclient_draw_lost_connection(mud);
        surface_draw(mud->surface);

#ifdef RENDER_GL
#ifdef SDL12
        SDL_GL_SwapBuffers();
#else
        SDL_GL_SwapWindow(mud->gl_window);
#endif
#elif defined(RENDER_3DS_GL)
        mudclient_3ds_gl_frame_end();
#endif
    } else {
        mudclient_show_login_screen_status(mud, "Please wait...",
                                           "Connecting to server");
    }

    free(mud->packet_stream);
    mud->packet_stream = malloc(sizeof(PacketStream));
    packet_stream_new(mud->packet_stream, mud);

    if (mud->packet_stream->closed) {
        mud_error("[login] socket failed to open (connect refused/timeout)\n");
    }

    if (mud->packet_stream->closed) {
        goto login_fail;
    }

#ifdef REVISION_177
    int session_id = packet_stream_get_int(mud->packet_stream);
    mud->session_id = session_id;
#else
    int64_t session_id;

    if (mud->protocol177 || mud->protocol_custom) {
        // OpenRSC (177 and 10010) pushes a 4-byte session id on connect, no CLIENT_SESSION request; consume it to
        // stay aligned
        session_id =
            (int64_t)(uint32_t)packet_stream_get_int(mud->packet_stream);
        mud->session_id = session_id;
    } else {
#ifdef WITH_SINGLEPLAYER
        if (MUD_SP_WIRE(mud) && !reconnecting) {
            // single-player and co-op auto-create the character before login (register, session, login); register of
            // an existing name is a no-op
            packet_stream_new_packet(mud->packet_stream, CLIENT_REGISTER);
            packet_stream_put_short(mud->packet_stream, LOGIN_VERSION(mud));
            packet_stream_put_bytes(mud->packet_stream,
                                    (void *)formatted_username, 0,
                                    USERNAME_LENGTH);
            packet_stream_put_bytes(mud->packet_stream,
                                    (void *)formatted_password, 0,
                                    PASSWORD_LENGTH);
            if (packet_stream_flush_packet(mud->packet_stream) >= 0) {
                // register reply: ignored
                packet_stream_get_byte(mud->packet_stream);
            }
        }
#endif

        packet_stream_new_packet(mud->packet_stream, CLIENT_SESSION);

        int64_t encoded_username = encode_username(formatted_username);

        packet_stream_put_byte(mud->packet_stream,
                               (int)((encoded_username >> 16) & 31));

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            mud_error("[login] session-request flush failed -> login_fail\n");
            goto login_fail;
        }

        session_id = packet_stream_get_long(mud->packet_stream);
        mud->session_id = session_id;

        if (session_id == 0) {
            mud_error("[login] session refused (server offline?)\n");
        }
    }
#endif

    if (mud->session_id == 0) {
        mudclient_show_login_screen_status(mud, "Login server offline.",
                                           "Please try again in a few mins");
        return;
    }

#ifdef REVISION_177
    mud_log("Session id: %d\n", session_id);

    packet_stream_new_packet(mud->packet_stream,
                             reconnecting ? CLIENT_RECONNECT : CLIENT_LOGIN);

    packet_stream_put_short(mud->packet_stream, LOGIN_VERSION(mud));

    /* limit30 */
    packet_stream_put_short(mud->packet_stream, 0);

    packet_stream_put_long(mud->packet_stream,
                           encode_username(formatted_username));

    packet_stream_put_password(mud->packet_stream, session_id,
                               formatted_password);

    /* uid/randomDat */
    packet_stream_put_int(mud->packet_stream, 0);

    if (packet_stream_flush_packet(mud->packet_stream) < 0) {
        goto login_fail;
    }

    packet_stream_get_byte(mud->packet_stream);

    int response = packet_stream_get_byte(mud->packet_stream);
#else
#ifdef _3DS
    mud_log("Verb: Session id: %lld\n", session_id); /* ? */
#else
    mud_log("Verb: Session id: %ld\n", session_id);
#endif

    int response;

    if (mud->protocol_custom) {
        // OpenRSC custom (10010) login: opcode 0 (CLIENT_LOGIN), plaintext body with \n-terminated strings and
        // big-endian ints, then a ClientLimitations trailer; loginEncryptionVersion 0 = plaintext password, no ISAAC; 2-byte custom framing from packet_stream
        packet_stream_new_packet(mud->packet_stream, CLIENT_LOGIN); // wire op 0
        packet_stream_put_byte(mud->packet_stream, reconnecting ? 1 : 0);
        packet_stream_put_int(mud->packet_stream, 10010); // clientVersion
        packet_stream_put_string(mud->packet_stream, formatted_username);
        packet_stream_put_byte(mud->packet_stream, '\n');
        packet_stream_put_byte(mud->packet_stream, 0); // loginEncryptionVersion=0
        packet_stream_put_string(mud->packet_stream, formatted_password);
        packet_stream_put_byte(mud->packet_stream, '\n');
        packet_stream_put_long(mud->packet_stream, 0); // uid (random, ignored)
        // ClientLimitations trailer: each field declares what this client can render; the server substitutes or hides
        // anything beyond. values mirror OpenRSC tellLimitations() = count-1 (max index) except where noted; field order per LoginPacketHandler
        packet_stream_put_short(mud->packet_stream,
                                game_data.animation_count - 1); // maxAnimationId
        // items/npcs: declare the OpenRSC id range the custom overlay grows these tables to, not the pre-overlay
        // count
        packet_stream_put_int(mud->packet_stream, online_item_max_id); // maxItemId
        packet_stream_put_int(mud->packet_stream, online_npc_max_id); // maxNpcId
        packet_stream_put_int(mud->packet_stream,
                              game_data.object_count - 1); // maxSceneryId
        packet_stream_put_short(mud->packet_stream,
                                game_data.prayer_count - 1); // maxPrayerId
        packet_stream_put_short(mud->packet_stream,
                                game_data.spell_count - 1); // maxSpellId
        // stat arrays are PLAYER_SKILL_MAX wide; the parser clamps the server's real count into them
        packet_stream_put_byte(mud->packet_stream,
                               PLAYER_SKILL_MAX - 1); // maxSkillId (ubyte)
        packet_stream_put_short(mud->packet_stream,
                                game_data.roof_count - 1); // maxRoofId
        packet_stream_put_short(mud->packet_stream,
                                game_data.texture_count - 1); // maxTextureId
        packet_stream_put_short(mud->packet_stream,
                                game_data.tile_count - 1); // maxTileId
        packet_stream_put_int(mud->packet_stream,
                              game_data.wall_object_count - 1); // maxBoundaryId
        packet_stream_put_byte(mud->packet_stream, 2); // maxTeleBubbleId (ubyte)
        packet_stream_put_short(mud->packet_stream,
                                game_data.projectile_sprite - 1); // maxProjectileSprite
        // these three declare what the client can render; the server sends appearance indices to match (max index).
        // keep equal to the real array sizes - 1
        packet_stream_put_int(mud->packet_stream,
                              PLAYER_SKIN_COLOUR_COUNT - 1); // maxSkinColor
        packet_stream_put_int(mud->packet_stream,
                              PLAYER_HAIR_COLOUR_COUNT - 1); // maxHairColor
        packet_stream_put_int(mud->packet_stream,
                              PLAYER_TOP_BOTTOM_COLOUR_COUNT - 1); // maxClothingColor
        packet_stream_put_short(mud->packet_stream, 200); // maxQuestId
        packet_stream_put_int(mud->packet_stream, 100); // numberOfSounds (COUNT)
        // despite the field name, OpenRSC fills this with crownCount - 1 (rank crowns)
        packet_stream_put_byte(mud->packet_stream,
                               ORSC_CROWN_COUNT - 1); // supportsModSprites (ubyte)
        packet_stream_put_byte(mud->packet_stream, 5); // maxDialogueOptions (COUNT, ubyte)
        // count (no -1): bank array capacity, matches OpenRSC ItemId.maxCustom (1592). this and maxDialogueOptions
        // are decorative, the server never reads them back; the client clamps on its own
        packet_stream_put_int(mud->packet_stream, BANK_ITEMS_MAX); // maxBankItems
        packet_stream_put_string(mud->packet_stream, "63"); // mapHash
        packet_stream_put_byte(mud->packet_stream, '\n'); // mapHash terminator

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            mud_error("[login] custom login flush failed -> login_fail\n");
            goto login_fail;
        }

        // custom login response is a single RAW byte; success iff bit 0x40 set
        response = packet_stream_get_byte(mud->packet_stream);
    } else if (mud->protocol177) {
        // revision-177 login: version + RSA-encrypted password, no ISAAC. 177 reconnect opcode is 19; 204 has none
        // and passes through unchanged
        packet_stream_new_packet(
            mud->packet_stream,
            reconnecting ? (ClientOpcode)PROTOCOL177_CLIENT_RECONNECT
                         : CLIENT_LOGIN);

        packet_stream_put_short(mud->packet_stream, PROTOCOL177_VERSION);

        // limit30
        packet_stream_put_short(mud->packet_stream, 0);

        packet_stream_put_long(mud->packet_stream,
                               encode_username(formatted_username));

        packet_stream_put_password(mud->packet_stream, (int)session_id,
                                   formatted_password);

        // uid/randomDat
        packet_stream_put_int(mud->packet_stream, 0);

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            mud_error("[login] 177 login flush failed -> login_fail\n");
            goto login_fail;
        }

        // the response is two bytes: a leading byte, then the code
        packet_stream_get_byte(mud->packet_stream);
        response = packet_stream_get_byte(mud->packet_stream);
    } else {
        uint32_t keys[4] = {0};
        keys[0] = (int)(((float)rand() / (float)RAND_MAX) * (float)99999999);
        keys[1] = (int)(((float)rand() / (float)RAND_MAX) * (float)99999999);
        keys[2] = (int32_t)(session_id >> 32);
        keys[3] = (int32_t)(session_id);

        packet_stream_new_packet(mud->packet_stream, CLIENT_LOGIN);
        packet_stream_put_byte(mud->packet_stream, reconnecting);
        packet_stream_put_short(mud->packet_stream, LOGIN_VERSION(mud));
        packet_stream_put_byte(mud->packet_stream, 0); // limit30

        packet_stream_put_login_block(mud->packet_stream, formatted_username,
                                      formatted_password, keys, 0);

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            goto login_fail;
        }

        response = packet_stream_get_byte(mud->packet_stream);
    }
#endif

    // custom (10010) success sets bit 0x40 (normal 64, mod tiers 82-89); authentic/SP use 0/1/25. require response >=
    // 0 first: a -1 failed read has all bits set and would pass the 0x40 test
    int login_ok = mud->protocol_custom
                       ? (response >= 0 && (response & 0x40) != 0)
                       : (response == 0 || response == 1 || response == 25);

    if (!login_ok) {
        mud_error("[login] rejected: response=%d\n", response);
    }

    if (login_ok) {
        mud->moderator_level =
            mud->protocol_custom ? (response != 64) : (response == 25);

        // gate the injected custom landscape by session type: SP/co-op keep it, authentic 177 never gets it,
        // custom-online starts off until SEND_SERVER_CONFIGS sets want_custom_landscape
        if (mud->world != NULL) {
            mud->world->apply_custom_landscape =
                (mud->protocol177 || mud->protocol_custom) ? 0 : 1;
        }

        if (mud->protocol_custom) {
            // overlay the OpenRSC real-id def table so streamed item/npc ids resolve to the right name/examine
            game_data_load_online_defs();
        } else {
            // entering SP/authentic/177: undo any overlay left by an earlier custom login, no-op if none
            game_data_unload_online_defs();
        }

        mud->auto_login_attempts = 0;

        strcpy(mud->options->username,
               mud->options->remember_username ? username : "");

#ifdef WITH_SINGLEPLAYER
        // SP/co-op logs in with username alone; don't persist the empty password, it would wipe the remembered online
        // one
        if (!MUD_SP_WIRE(mud))
#endif
        {
            strcpy(mud->options->password,
                   mud->options->remember_password ? password : "");
        }

        if (mud->options->remember_username ||
            mud->options->remember_password) {
            options_save(mud->options);
        }

        mudclient_reset_game(mud);
        return;
    }

    /*if (response == 1) {
        mud->auto_login_attempts = 0;
        return;
    }*/

    if (reconnecting) {
        mudclient_reset_login_screen(mud);
        return;
    }

    // TODO enums
    switch (response) {
    case -1:
        mudclient_show_login_screen_status(mud, "Error unable to login.",
                                           "Server timed out");
        return;
    case 3:
        mudclient_show_login_screen_status(
            mud, "Invalid username or password.",
            "Try again, or create a new account");
        return;
    case 4:
        mudclient_show_login_screen_status(
            mud, "That username is already logged in.",
            "Wait 60 seconds then retry");
        return;
    case 5:
        mudclient_show_login_screen_status(mud, "The client has been updated.",
                                           "Please reload this page");
        return;
    case 6:
        mudclient_show_login_screen_status(
            mud, "You may only use 1 character at once.",
            "Your ip-address is already in use");
        return;
    case 7:
        mudclient_show_login_screen_status(mud, "Login attempts exceeded!",
                                           "Please try again in 5 minutes");
        return;
    case 8:
        mudclient_show_login_screen_status(mud, "Error unable to login.",
                                           "Server rejected session");
        return;
    case 9:
        // OpenRSC under-13 refusal, not a session error
        mudclient_show_login_screen_status(
            mud, "Error unable to login.",
            "Under 13 accounts cannot access RuneScape Classic");
        return;
    case 10:
        mudclient_show_login_screen_status(mud,
                                           "That username is already in use.",
                                           "Wait 60 seconds then retry");
        return;
    case 11:
        mudclient_show_login_screen_status(
            mud, "Account temporarily disabled.",
            "Check your message inbox for details");
        return;
    case 12:
        mudclient_show_login_screen_status(
            mud, "Account permanently disabled.",
            "Check your message inbox for details");
        return;
    case 14:
        mudclient_show_login_screen_status(
            mud, "Sorry! This world is currently full.",
            "Please try a different world");

        mud->world_full_timeout = 1500;
        return;
    case 15:
        mudclient_show_login_screen_status(mud, "You need a members account",
                                           "to login to this world");
        return;
    case 16:
        mudclient_show_login_screen_status(
            mud, "Error - no reply from loginserver.", "Please try again");
        return;
    case 17:
        mudclient_show_login_screen_status(mud,
                                           "Error - failed to decode profile.",
                                           "Contact customer support");
        return;
    case 18:
        mudclient_show_login_screen_status(
            mud, "Account suspected stolen.",
            "Press \"recover a locked account\" on front page.");
        return;
    case 20:
        mudclient_show_login_screen_status(mud, "Error - loginserver mismatch",
                                           "Please try a different world");
        return;
    case 21:
        mudclient_show_login_screen_status(
            mud, "That is not a veteran RS-Classic account.",
            "Please try a non-veterans world.");
        return;
    case 22:
        mudclient_show_login_screen_status(
            mud, "Password suspected stolen.",
            "Press \"change your password\" on front page.");
        return;
    case 23:
        mudclient_show_login_screen_status(
            mud, "You need to set your display name.",
            "Please set it on the Account Management page");
        return;
    case 25:
        mudclient_show_login_screen_status(
            mud, "None of your characters can log in.",
            "Contact customer support");
        return;
    case 24:
        // OpenRSC showLoginScreenStatus for response 24
        mudclient_show_login_screen_status(
            mud, "This world does not accept new players.",
            "Please see the launch page for help");
        return;
    default:
        mudclient_show_login_screen_status(mud, "Error unable to login.",
                                           "Unrecognised response code");
        return;
    }

login_fail:
    if (mud->auto_login_attempts > 0) {
        int delay = 0;

        while (delay < 5000) {
            mudclient_poll_events(mud);
            delay += 16;
            delay_ticks(16);
        }

        mud->auto_login_attempts--;
        mudclient_login(mud, username, password, reconnecting);
        return;
    }

    if (reconnecting) {
        mudclient_reset_login_screen(mud);
        mud->login_screen = LOGIN_STAGE_EXISTING;
    }

    mudclient_show_login_screen_status(
        mud, "Sorry! Unable to connect.",
        "Check internet settings or try another world");
}

void mudclient_registration_login(mudclient *mud) {
    char *username =
        panel_get_text(mud->panel_login_new_user, mud->control_register_user);

    char *password = panel_get_text(mud->panel_login_new_user,
                                    mud->control_register_password);

    mud->login_screen = 2;

    panel_update_text(mud->panel_login_existing_user, mud->control_login_status,
                      "Please enter your username and password");

    panel_update_text(mud->panel_login_existing_user,
                      mud->control_login_username, username);

    panel_update_text(mud->panel_login_existing_user,
                      mud->control_login_password, password);

    mudclient_draw_login_screens(mud);
    mudclient_reset_timings(mud);
    mudclient_login(mud, username, password, 0);
}

void mudclient_register(mudclient *mud, char *username, char *password) {
    if (mud->world_full_timeout > 0) {
        mudclient_show_login_screen_status(mud, "Please wait...",
                                           "Connecting to server");

        delay_ticks(2000);

        mudclient_show_login_screen_status(
            mud, "Sorry! The server is currently full.",
            "Please try again later");

        return;
    }

    char formatted_username[USERNAME_LENGTH + 1] = {0};
    format_auth_string(username, USERNAME_LENGTH, formatted_username);

    char formatted_password[PASSWORD_LENGTH + 1] = {0};
    format_auth_string(password, PASSWORD_LENGTH, formatted_password);

    mudclient_show_login_screen_status(mud, "Please wait...",
                                       "Connecting to server");

    free(mud->packet_stream);
    mud->packet_stream = malloc(sizeof(PacketStream));
    packet_stream_new(mud->packet_stream, mud);

    if (mud->packet_stream->closed) {
        goto register_fail;
    }

#ifdef REVISION_177
    int session_id = packet_stream_get_int(mud->packet_stream);
    mud->session_id = session_id;
#else
    int64_t session_id;

    if (mud->protocol177) {
        // revision-177 (OpenRSC): 4-byte session id arrives on connect
        session_id =
            (int64_t)(uint32_t)packet_stream_get_int(mud->packet_stream);
        mud->session_id = session_id;
    } else if (mud->protocol_custom) {
        // custom register has no session handshake: OpenRSC sendRegister sends opcode 2 immediately. set a nonzero
        // dummy so the offline check passes, read nothing
        session_id = 1;
        mud->session_id = 1;
    } else {
        packet_stream_new_packet(mud->packet_stream, CLIENT_SESSION);

        int64_t encoded_username = encode_username(formatted_username);

        packet_stream_put_byte(mud->packet_stream,
                               (int)((encoded_username >> 16) & 31));

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            goto register_fail;
        }

        session_id = packet_stream_get_long(mud->packet_stream);
        mud->session_id = session_id;
    }
#endif

    if (mud->session_id == 0) {
        mudclient_show_login_screen_status(mud, "Login server offline.",
                                           "Please try again in a few mins");
        return;
    }

#ifdef REVISION_177
    mud_log("Session id: %d\n", session_id);

    packet_stream_new_packet(mud->packet_stream, CLIENT_REGISTER);
    packet_stream_put_short(mud->packet_stream, LOGIN_VERSION(mud));

    packet_stream_put_long(mud->packet_stream,
                           encode_username(formatted_username));

    /* refer id */
    packet_stream_put_short(mud->packet_stream, 0);

    packet_stream_put_password(mud->packet_stream, session_id,
                               formatted_password);

    /* uid/randomDat */
    packet_stream_put_int(mud->packet_stream, 0);

    if (packet_stream_flush_packet(mud->packet_stream) < 0) {
        goto register_fail;
    }

    packet_stream_get_byte(mud->packet_stream);
#else
    mud_log("Verb: Session id: %ld\n", session_id);

#ifdef WITH_SINGLEPLAYER
    if (MUD_SP_WIRE(mud)) {
        // embedded server register decoder reads a plaintext block: version + 20-byte username + 20-byte password, no
        // RSA/keys/session; co-op guest joins too
        packet_stream_new_packet(mud->packet_stream, CLIENT_REGISTER);
        packet_stream_put_short(mud->packet_stream, LOGIN_VERSION(mud));
        packet_stream_put_bytes(mud->packet_stream, (void *)formatted_username,
                                0, USERNAME_LENGTH);
        packet_stream_put_bytes(mud->packet_stream, (void *)formatted_password,
                                0, PASSWORD_LENGTH);

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            goto register_fail;
        }
    } else
#endif
    if (mud->protocol177) {
        // revision-177 register: version + username + referrer + RSA password
        packet_stream_new_packet(mud->packet_stream, CLIENT_REGISTER);
        packet_stream_put_short(mud->packet_stream, PROTOCOL177_VERSION);

        packet_stream_put_long(mud->packet_stream,
                               encode_username(formatted_username));

        // refer id
        packet_stream_put_short(mud->packet_stream, 0);

        packet_stream_put_password(mud->packet_stream, (int)session_id,
                                   formatted_password);

        // uid/randomDat
        packet_stream_put_int(mud->packet_stream, 0);

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            goto register_fail;
        }

        // leading byte before the response code
        packet_stream_get_byte(mud->packet_stream);
    } else if (mud->protocol_custom) {
        // custom register wire from OpenRSC sendRegister: newPacket(2), putString(user), putString(pass), optional
        // putString(email), then read one response byte; putString appends 0x0A. send an empty email line, no RSA or session
        packet_stream_new_packet(mud->packet_stream, CLIENT_REGISTER);
        packet_stream_put_string(mud->packet_stream, formatted_username);
        packet_stream_put_byte(mud->packet_stream, 10);
        packet_stream_put_string(mud->packet_stream, formatted_password);
        packet_stream_put_byte(mud->packet_stream, 10);
        packet_stream_put_byte(mud->packet_stream, 10); // empty email line

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            goto register_fail;
        }
    } else {
        uint32_t keys[4] = {0};
        keys[0] = (int)(((float)rand() / (float)RAND_MAX) * (float)99999999);
        keys[1] = (int)(((float)rand() / (float)RAND_MAX) * (float)99999999);
        keys[2] = (int32_t)(session_id >> 32);
        keys[3] = (int32_t)(session_id);

        packet_stream_new_packet(mud->packet_stream, CLIENT_REGISTER);
        packet_stream_put_byte(mud->packet_stream, 0);
        packet_stream_put_short(mud->packet_stream, LOGIN_VERSION(mud));
        packet_stream_put_byte(mud->packet_stream, 0); // limit30

        packet_stream_put_login_block(mud->packet_stream, formatted_username,
                                      formatted_password, keys, 0);

        if (packet_stream_flush_packet(mud->packet_stream) < 0) {
            goto register_fail;
        }
    }
#endif

    int response = packet_stream_get_byte(mud->packet_stream);
    mud_log("Newplayer response: %d\n", response);

#ifndef REVISION_177
    if (mud->protocol_custom) {
        // custom register response codes differ from authentic: 0 = success, 2 = username taken, 3/6 = empty-email on
        // a want_email world, 4 = register on the website
        switch (response) {
        case 0:
            mudclient_registration_login(mud);
            return;
        case 2:
            mudclient_show_login_screen_status(mud, "Username already taken",
                                               "choose a different one");
            return;
        case 3:
            mudclient_show_login_screen_status(mud,
                                               "E-mail address already in use",
                                               "use another E-mail");
            return;
        case 4:
            mudclient_show_login_screen_status(mud, "Registration disabled",
                                               "try registering from website.");
            return;
        case 5:
            mudclient_show_login_screen_status(mud,
                                               "You have registered recently",
                                               "to prevent flooding, wait an hour.");
            return;
        case 6:
            mudclient_show_login_screen_status(mud, "Invalid e-mail address",
                                               "please use a valid email address");
            return;
        case 7:
            mudclient_show_login_screen_status(mud, "Username must be 2-12",
                                               "characters long!");
            return;
        case 8:
            mudclient_show_login_screen_status(mud, "Invalid username",
                                               "please use an appropriate username");
            return;
        default:
            mudclient_show_login_screen_status(mud, "Error unable to login.",
                                               "Unrecognised response code");
            return;
        }
    }
#endif

    switch (response) {
    case 2:
        mudclient_registration_login(mud);
        return;
    case 13:
    case 3:
        mudclient_show_login_screen_status(mud, "Username already taken.",
                                           "Please choose another username");
        return;
    case 4:
        mudclient_show_login_screen_status(mud,
                                           "That username is already in use.",
                                           "Wait 60 seconds then retry");
        return;
    case 5:
        mudclient_show_login_screen_status(mud, "The client has been updated.",
                                           "Please reload this page");
        return;
    case 6:
        mudclient_show_login_screen_status(
            mud, "You may only use 1 character at once.",
            "Your ip-address is already in use");
        return;
    case 7:
        mudclient_show_login_screen_status(mud, "Login attempts exceeded!",
                                           "Please try again in 5 minutes");
        return;
    case 11:
        mudclient_show_login_screen_status(
            mud, "Account has been temporarily disabled",
            "for cheating or abuse");
        return;
    case 12:
        mudclient_show_login_screen_status(
            mud, "Account has been permanently disabled",
            "for cheating or abuse");
        /* ^ this would be "Check your message inbox for details." */
        return;
    case 14:
        mudclient_show_login_screen_status(
            mud, "Sorry! The server is currently full.",
            "Please try again later");

        mud->world_full_timeout = 1500;
        return;
    case 15:
        mudclient_show_login_screen_status(mud, "You need a members account",
                                           "to login to this server");
        return;
    case 16:
        mudclient_show_login_screen_status(mud,
                                           "Please login to a members server",
                                           "to access member-only features");
        return;
    default:
        mudclient_show_login_screen_status(mud,
                                           "Error unable to create username.",
                                           "Unrecognised response code");
        return;
    }

register_fail:
    mudclient_show_login_screen_status(
        mud, "Sorry! Unable to connect.",
        "Check internet settings or try another world");
}

void mudclient_change_password(mudclient *mud, char *old_password,
                               char *new_password) {
    char formatted_old_password[PASSWORD_LENGTH + 1] = {0};
    format_auth_string(old_password, 20, formatted_old_password);

    char formatted_new_password[PASSWORD_LENGTH + 1] = {0};
    format_auth_string(new_password, 20, formatted_new_password);

    char passwords[(PASSWORD_LENGTH * 2) + 1] = {0};
    sprintf(passwords, "%s%s", formatted_old_password, formatted_new_password);

    packet_stream_new_packet(mud->packet_stream, CLIENT_CHANGE_PASSWORD);

#ifdef REVISION_177
    packet_stream_put_password(mud->packet_stream, mud->session_id, passwords);
#else
    if (mud->protocol177) {
        packet_stream_put_password(mud->packet_stream, mud->session_id,
                                   passwords);
    } else if (mud->protocol_custom) {
        // custom CHANGE_PASS is two plaintext strings (old, new), each padded to 20 with a 0x0A terminator
        packet_stream_put_string(mud->packet_stream, formatted_old_password);
        packet_stream_put_byte(mud->packet_stream, 10);
        packet_stream_put_string(mud->packet_stream, formatted_new_password);
        packet_stream_put_byte(mud->packet_stream, 10);
    }
#endif

    packet_stream_flush_packet(mud->packet_stream);
}

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
void mudclient_update_fov(mudclient *mud) {
    if (mud->options->field_of_view) {
        mud->scene->gl_fov = glm_rad(mud->options->field_of_view / 10.0f);

        int view_distance =
            round((-254.452344 * pow(mud->scene->gl_fov, 3)) +
                  (1142.234460 * pow(mud->scene->gl_fov, 2)) -
                  (1901.194134 * mud->scene->gl_fov) + 1318.230265);

        mud->scene->view_distance =
            round((float)mud->scene->gl_height *
                  ((float)view_distance / (float)(346 - 12)));
    } else {
        float scaled_scene_height =
            (float)(mud->scene->gl_height - 1) / 1000.0f;

        /* no idea, i just used cubic regression */
        mud->scene->gl_fov = (-0.1608132078 * powf(scaled_scene_height, 3)) -
                             (0.3012063997 * powf(scaled_scene_height, 2)) +
                             (2.0149949882 * scaled_scene_height) -
                             0.0030409762;

        mud->scene->view_distance = 512;
    }
}
#endif

void mudclient_start_game(mudclient *mud) {
    mudclient_load_game_config(mud);

    if (mud->error_loading_data) {
        return;
    }

    mudclient_set_target_fps(mud, 50);

    panel_base_sprite_start = mud->sprite_util;

    int x = MUD_WIDTH - 199;
    int y = UI_BUTTON_SIZE + 1;

    int is_touch = mudclient_is_touch(mud);

    mud->panel_quests = malloc(sizeof(Panel));
    panel_new(mud->panel_quests, mud->surface, 5);

    if (is_touch) {
        x = UI_TABS_TOUCH_X - STATS_WIDTH - 1;

        y = (UI_TABS_TOUCH_Y + UI_TABS_TOUCH_HEIGHT) - STATS_COMPACT_HEIGHT -
            STATS_TAB_HEIGHT - 5;
    }

    mud->control_list_quest = panel_add_text_list_interactive(
        mud->panel_quests, x, y + STATS_TAB_HEIGHT, STATS_WIDTH,
        STATS_HEIGHT - STATS_TAB_HEIGHT, FONT_BOLD_12, 500, 1);

    mud->panel_magic = malloc(sizeof(Panel));
    panel_new(mud->panel_magic, mud->surface, 5);

    if (is_touch) {
        x = UI_TABS_TOUCH_X - MAGIC_WIDTH - 1;
        y = UI_TABS_TOUCH_Y + 10;
    }

    mud->control_list_magic = panel_add_text_list_interactive(
        mud->panel_magic, x, y + MAGIC_TAB_HEIGHT - (is_touch ? 11 : 0),
        MAGIC_WIDTH, 90 + (is_touch ? 16 : 0), FONT_BOLD_12, 500, 1);

    mud->panel_social_list = malloc(sizeof(Panel));
    panel_new(mud->panel_social_list, mud->surface, 5);

    mud->control_list_social = panel_add_text_list_interactive(
        mud->panel_social_list, x,
        y + SOCIAL_TAB_HEIGHT + 16 - (is_touch ? 11 : 0), 196,
        126 + (is_touch ? 16 : 0), FONT_BOLD_12, 500, 1);

    mudclient_load_media(mud);

    if (mud->error_loading_data) {
        return;
    }

    mudclient_load_entities(mud);

    if (mud->error_loading_data) {
        return;
    }

    mud->scene = malloc(sizeof(Scene));
    if (mud->options->lowmem) {
        scene_new(mud->scene, mud->surface, 7500, 7500, 1000);
    } else {
        scene_new(mud->scene, mud->surface, 15000, 15000, 1000);
    }

#ifdef RENDER_3DS_GL
    scene_set_bounds(mud->scene, mud->game_width, mud->game_height);
#else
    scene_set_bounds(mud->scene, mud->game_width, mud->game_height - 12);
#endif

    mud->scene->clip_far_3d = 2400;
    mud->scene->clip_far_2d = 2400;
    mud->scene->fog_z_distance = 2300;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    mudclient_update_fov(mud);
#endif

    mud->world = malloc(sizeof(World));
    world_new(mud->world, mud->scene, mud->surface, mud->options->version_maps);

    /* used for storing minimap sprite */
    mud->world->base_media_sprite = mud->sprite_media;

    mud->world->thick_walls = mud->options->thick_walls;

    mudclient_load_textures(mud);

    if (mud->error_loading_data) {
        return;
    }

    mudclient_load_models(mud);

    if (mud->error_loading_data) {
        return;
    }

    mudclient_load_maps(mud);

    if (mud->error_loading_data) {
        return;
    }

    if (mud->options->members && !mud->options->lowmem) {
        mudclient_load_sounds(mud);
    }

    if (mud->error_loading_data) {
        return;
    }

    mudclient_draw_loading_progress(mud, 100, "Starting game...");
    mudclient_create_message_tabs_panel(mud);
    mudclient_create_login_panels(mud);
    mudclient_create_appearance_panel(mud);
    mudclient_create_options_panel(mud);
    mudclient_reset_login_screen(mud);

    worldlist_new(mud);

    if (!mud->options->lowmem) {
        mudclient_render_login_scene_sprites(mud);
    }

    free(surface_texture_pixels);
    surface_texture_pixels = NULL;
}

GameModel *mudclient_create_wall_object(mudclient *mud, int x, int y,
                                        int direction, int id, int count) {
    int x1 = x;
    int y1 = y;
    int x2 = x;
    int y2 = y;

    int front_texture = game_data.wall_objects[id].texture_front;
    int back_texture = game_data.wall_objects[id].texture_back;
    int height = game_data.wall_objects[id].height;

    GameModel *game_model = malloc(sizeof(GameModel));
    game_model_new_alloc(game_model, 4, 1);

    if (direction == 0) {
        x2 = x + 1;
    } else if (direction == 1) {
        y2 = y + 1;
    } else if (direction == 2) {
        x1 = x + 1;
        y2 = y + 1;
    } else if (direction == 3) {
        x2 = x + 1;
        y2 = y + 1;
    }

    x1 *= MAGIC_LOC;
    y1 *= MAGIC_LOC;
    x2 *= MAGIC_LOC;
    y2 *= MAGIC_LOC;

    uint16_t *vertices = malloc(4 * sizeof(uint16_t));

    vertices[0] = game_model_vertex_at(
        game_model, x1, -world_get_elevation(mud->world, x1, y1), y1);

    vertices[1] = game_model_vertex_at(
        game_model, x1, -world_get_elevation(mud->world, x1, y1) - height, y1);

    vertices[2] = game_model_vertex_at(
        game_model, x2, -world_get_elevation(mud->world, x2, y2) - height, y2);

    vertices[3] = game_model_vertex_at(
        game_model, x2, -world_get_elevation(mud->world, x2, y2), y2);

    game_model_create_face(game_model, 4, vertices, front_texture,
                           back_texture);

    game_model_set_light(game_model, 0, 60, 24, -50, -10, -50);

    if (x >= 0 && y >= 0 && x < 96 && y < 96) {
        scene_add_model(mud->scene, game_model);
    }

    game_model->key = count + 10000;

    return game_model;
}

// shift every scenery object to the new window origin and re-register it
static void mudclient_region_rebase_objects(mudclient *mud, int offset_x,
                                            int offset_y) {
    for (int i = 0; i < mud->object_count; i++) {
        mud->objects[i].x -= offset_x;
        mud->objects[i].y -= offset_y;

        int object_x = mud->objects[i].x;
        int object_y = mud->objects[i].y;
        int object_id = mud->objects[i].id;

        GameModel *game_model = mud->objects[i].model;

        int object_direction = mud->objects[i].direction;
        int object_width = 0;
        int object_height = 0;

        if (object_direction == DIR_NORTH || object_direction == DIR_SOUTH) {
            object_width = game_data.objects[object_id].width;
            object_height = game_data.objects[object_id].height;
        } else {
            object_height = game_data.objects[object_id].width;
            object_width = game_data.objects[object_id].height;
        }

        int base_x = ((object_x + object_x + object_width) * MAGIC_LOC) / 2;
        int base_y = ((object_y + object_y + object_height) * MAGIC_LOC) / 2;

        if (object_x >= 0 && object_y >= 0 && object_x < 96 && object_y < 96) {
            scene_add_model(mud->scene, game_model);

            game_model_place(game_model, base_x,
                             -world_get_elevation(mud->world, base_x, base_y),
                             base_y);

            world_register_object(mud->world, object_x, object_y, object_id);

            if (object_id == WINDMILL_SAILS_ID) {
                game_model_translate(game_model, 0, -480, 0);
            }
        }
    }
}

// wall objects, ground items and characters follow the same shift
static void mudclient_region_rebase_rest(mudclient *mud, int offset_x,
                                         int offset_y) {
    for (int i = 0; i < mud->wall_object_count; i++) {
        mud->wall_objects[i].x -= offset_x;
        mud->wall_objects[i].y -= offset_y;

        int wall_object_x = mud->wall_objects[i].x;
        int wall_object_y = mud->wall_objects[i].y;
        int wall_object_id = mud->wall_objects[i].id;
        int wall_object_dir = mud->wall_objects[i].direction;

        world_register_wall_object(mud->world, wall_object_x, wall_object_y,
                                   wall_object_dir, wall_object_id);

        game_model_destroy(mud->wall_objects[i].model);
        free(mud->wall_objects[i].model);

        GameModel *wall_object_model =
            mudclient_create_wall_object(mud, wall_object_x, wall_object_y,
                                         wall_object_dir, wall_object_id, i);

        mud->wall_objects[i].model = wall_object_model;
    }

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    mudclient_gl_update_wall_models(mud);
#endif

    for (int i = 0; i < mud->ground_item_count; i++) {
        mud->ground_items[i].x -= offset_x;
        mud->ground_items[i].y -= offset_y;
    }

    mudclient_update_ground_item_models(mud);

    for (int i = 0; i < mud->player_count; i++) {
        GameCharacter *player = mud->players[i];

        player->current_x -= offset_x * MAGIC_LOC;
        player->current_y -= offset_y * MAGIC_LOC;

        for (int j = 0; j <= player->waypoint_current; j++) {
            player->waypoints_x[j] -= offset_x * MAGIC_LOC;
            player->waypoints_y[j] -= offset_y * MAGIC_LOC;
        }
    }

    for (int i = 0; i < mud->npc_count; i++) {
        GameCharacter *npc = mud->npcs[i];

        npc->current_x -= offset_x * MAGIC_LOC;
        npc->current_y -= offset_y * MAGIC_LOC;

        for (int j = 0; j <= npc->waypoint_current; j++) {
            npc->waypoints_x[j] -= offset_x * MAGIC_LOC;
            npc->waypoints_y[j] -= offset_y * MAGIC_LOC;
        }
    }
}

static void mudclient_region_set_window(mudclient *mud, int section_x,
                                        int section_y, int plane) {
    mud->last_plane_index = plane;
    mud->region_x = section_x * REGION_SIZE - REGION_SIZE;
    mud->region_y = section_y * REGION_SIZE - REGION_SIZE;
    mud->local_lower_x = section_x * REGION_SIZE - 32;
    mud->local_lower_y = section_y * REGION_SIZE - 32;
    mud->local_upper_x = section_x * REGION_SIZE + 32;
    mud->local_upper_y = section_y * REGION_SIZE + 32;
}

#if defined(RENDER_GL) && (defined(__vita__) || defined(__linux__))
#define MUD_REGION_ASYNC 1
#endif

#ifdef MUD_REGION_ASYNC
// background region loader

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
static SceUID region_thread_id = -1;
#else
#include <pthread.h>
static pthread_t region_thread;
#endif

// region prefetch cache: a worker builds a neighbouring region's geometry into a detached world with deferred
// (GL-less) buffers, held in a small LRU cache. the window switch, rebase and scene swap stay synchronous; a crossing into a cached region installs cheaply

static mudclient *region_prefetch_mud;

static void mudclient_region_prefetch_build(void) {
    mudclient *mud = region_prefetch_mud;
    World *world = mud->region_next_world;

    world->defer_scene_adds = 1;

    world_load_section(world, mud->region_load_lx, mud->region_load_ly,
                       mud->region_load_plane);

    world_gl_buffer_world_models_to(world, &mud->region_next_buffers,
                                    &mud->region_next_buffer_length, 1);

    mud->region_load_done = 1;
}

#ifdef __vita__
static int region_thread_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    mudclient_region_prefetch_build();
    return 0;
}
#else
static void *region_thread_entry(void *arg) {
    (void)arg;
    mudclient_region_prefetch_build();
    return NULL;
}
#endif

static void mudclient_region_prefetch_join(void) {
#ifdef __vita__
    if (region_thread_id >= 0) {
        sceKernelWaitThreadEnd(region_thread_id, NULL, NULL);
        sceKernelDeleteThread(region_thread_id);
        region_thread_id = -1;
    }
#else
    pthread_join(region_thread, NULL);
#endif
}

// free a detached built world, its buffers and minimap scratch. model teardown goes to the per-frame slice-free slot;
// small buffers freed inline
static void mudclient_region_free_world(mudclient *mud, World *world,
                                        gl_vertex_buffer **buffers,
                                        int buffer_length, int realized) {
    if (world != NULL) {
#ifdef RENDER_GL
        free(world->minimap_buffer);
        world->minimap_buffer = NULL;
#endif
        if (mud->region_old_world == NULL) {
            // free its models a slice per frame
            mud->region_old_world = world;
            mud->region_old_free_index = 0;
        } else {
            world_free_models(world);
            free(world);
        }
    }

    for (int i = 0; i < buffer_length; i++) {
        if (realized) {
            vertex_buffer_gl_destroy(buffers[i]);
        }
        free(buffers[i]);
    }

    free(buffers);
}

static void mudclient_region_free_entry(mudclient *mud,
                                        struct RegionCacheEntry *e) {
    if (e->state == REGION_SLOT_EMPTY) {
        return;
    }

    mudclient_region_free_world(mud, e->world, e->buffers, e->buffer_length,
                                e->realized);

    e->state = REGION_SLOT_EMPTY;
    e->world = NULL;
    e->buffers = NULL;
    e->buffer_length = 0;
    e->realized = 0;
}

static int mudclient_region_cache_find(mudclient *mud, int sx, int sy,
                                       int plane) {
    for (int i = 0; i < REGION_CACHE_SLOTS; i++) {
        struct RegionCacheEntry *e = &mud->region_cache[i];

        if (e->state != REGION_SLOT_EMPTY && e->sx == sx && e->sy == sy &&
            e->plane == plane) {
            return i;
        }
    }

    return -1;
}

// Chebyshev distance in tiles from absolute plane tile (px,py) to the centre of section (sx,sy)
static int mudclient_region_section_distance(int px, int py, int sx, int sy) {
    int dx = px - sx * REGION_SIZE;
    int dy = py - sy * REGION_SIZE;

    if (dx < 0) {
        dx = -dx;
    }
    if (dy < 0) {
        dy = -dy;
    }

    return dx > dy ? dx : dy;
}

// pick a slot for a new build/retain: an empty one, else evict the ready entry farthest from the player (ties: least
// recently touched), never a building slot. candidate_dist is the requester's section distance; eviction refused (-1) unless the candidate is strictly nearer, or -1 to always win. (px,py) is the player's absolute plane tile
static int mudclient_region_cache_slot(mudclient *mud, int candidate_dist,
                                       int px, int py) {
    int worst = -1;
    int worst_dist = -1;

    for (int i = 0; i < REGION_CACHE_SLOTS; i++) {
        struct RegionCacheEntry *e = &mud->region_cache[i];

        if (e->state == REGION_SLOT_EMPTY) {
            return i;
        }

        if (e->state != REGION_SLOT_READY) {
            continue;
        }

        // another plane is never re-entered by walking: evict first
        int d = e->plane != mud->plane_index
                    ? 10000
                    : mudclient_region_section_distance(px, py, e->sx, e->sy);

        if (worst < 0 || d > worst_dist ||
            (d == worst_dist &&
             e->last_touch < mud->region_cache[worst].last_touch)) {
            worst = i;
            worst_dist = d;
        }
    }

    if (worst < 0) {
        return -1;
    }

    if (candidate_dist >= 0 && candidate_dist >= worst_dist) {
        return -1; // everything cached is at least as close: keep it
    }

    mudclient_region_free_entry(mud, &mud->region_cache[worst]);

    return worst;
}

// if the in-flight build has finished, move it into its cache slot (READY)
static void mudclient_region_promote_build(mudclient *mud) {
    if (mud->region_load_state != 1 || !mud->region_load_done) {
        return;
    }

    mudclient_region_prefetch_join();

    int idx = mud->region_building_index;

    if (idx >= 0) {
        struct RegionCacheEntry *e = &mud->region_cache[idx];
        e->state = REGION_SLOT_READY;
        e->world = mud->region_next_world;
        e->buffers = mud->region_next_buffers;
        e->buffer_length = mud->region_next_buffer_length;
        e->realized = 0; // upload spread over the next frames
        e->realize_index = 0; // start uploading buffer 0 next frame
        e->last_touch = ++mud->region_touch_clock;

        fprintf(stderr, "[rgn] ready (%d,%d)\n", e->sx, e->sy);
    }

    mud->region_next_world = NULL;
    mud->region_next_buffers = NULL;
    mud->region_next_buffer_length = 0;
    mud->region_building_index = -1;
    mud->region_load_state = 0;
}

// GL upload budget per frame in bytes, spread over approach frames
#define REGION_REALIZE_BUDGET (512 * 1024)

// upload one chunk of a not-yet-realized cache entry's buffers
static void mudclient_region_realize_step(mudclient *mud) {
    struct RegionCacheEntry *e = NULL;

    for (int i = 0; i < REGION_CACHE_SLOTS; i++) {
        if (mud->region_cache[i].state == REGION_SLOT_READY &&
            !mud->region_cache[i].realized) {
            e = &mud->region_cache[i];
            break;
        }
    }

    if (e == NULL || e->realize_index >= e->buffer_length) {
        if (e != NULL) {
            e->realized = 1;
        }
        return;
    }

#ifdef __vita__
    vglUseVram(GL_FALSE);
#endif

    gl_vertex_buffer *buf = e->buffers[e->realize_index];

    if (buf->gl_deferred) {
        game_model_gl_realize_buffer_begin(buf);
    }

    if (game_model_gl_realize_buffer_step(buf, REGION_REALIZE_BUDGET, 1)) {
        e->realize_index++;
    }

#ifdef __vita__
    vglUseVram(GL_TRUE);
#endif

    if (e->realize_index >= e->buffer_length) {
        e->realized = 1;
    }
}

// fully realize an entry now if a crossing arrived before the incremental upload finished
static void mudclient_region_realize_finish(mudclient *mud,
                                            struct RegionCacheEntry *e) {
    (void)mud;

    if (e->realized) {
        return;
    }

#ifdef __vita__
    vglUseVram(GL_FALSE);
#endif

    while (e->realize_index < e->buffer_length) {
        gl_vertex_buffer *buf = e->buffers[e->realize_index];

        if (buf->gl_deferred) {
            game_model_gl_realize_buffer_begin(buf);
        }

        while (!game_model_gl_realize_buffer_step(buf, 64 * 1024 * 1024, 1)) {
        }

        e->realize_index++;
    }

#ifdef __vita__
    vglUseVram(GL_TRUE);
#endif

    e->realized = 1;
}

// start a background build of section (sx,sy,plane) into cache slot idx; lx=sx*REGION_SIZE since center coords depend
// only on the section
static void mudclient_region_build_into(mudclient *mud, int idx, int sx, int sy,
                                        int plane) {
    if (mud->region_bake_state != 0) {
        return; // one background worker at a time; retry next packet
    }

    World *world = malloc(sizeof(World));

    if (world == NULL) {
        return;
    }

    world_new(world, mud->scene, mud->surface, mud->world->version);

    world->base_media_sprite = mud->world->base_media_sprite;
    world->thick_walls = mud->world->thick_walls;
    world->apply_custom_landscape = mud->world->apply_custom_landscape;
    world->map_pack = mud->world->map_pack;
    world->landscape_pack = mud->world->landscape_pack;
    world->member_map_pack = mud->world->member_map_pack;
    world->member_landscape_pack = mud->world->member_landscape_pack;

    // the worker touches no shared render state: models go in no scene, minimap pixels land in a private buffer
    // seeded from the live one
    world->detached = 1;

#ifdef RENDER_GL
    world->minimap_buffer = malloc(1024 * 1024 * 3);

    if (world->minimap_buffer == NULL) {
        // refuse to build off-thread without a private buffer
        free(world);
        return;
    }

    if (mud->surface->gl_dynamic_texture_buffer != NULL) {
        memcpy(world->minimap_buffer,
               mud->surface->gl_dynamic_texture_buffer, 1024 * 1024 * 3);
    }
#endif

    mud->region_next_world = world;
    mud->region_next_buffers = NULL;
    mud->region_next_buffer_length = 0;
    mud->region_load_lx = sx * REGION_SIZE;
    mud->region_load_ly = sy * REGION_SIZE;
    mud->region_load_plane = plane;
    mud->region_load_done = 0;
    region_prefetch_mud = mud;

#ifdef __vita__
    region_thread_id =
        sceKernelCreateThread("rsc_region", region_thread_entry, 0x10000100,
                              512 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);

    if (region_thread_id < 0) {
        free(world->minimap_buffer);
        free(world);
        mud->region_next_world = NULL;
        return;
    }

    sceKernelStartThread(region_thread_id, 0, NULL);
#else
    if (pthread_create(&region_thread, NULL, region_thread_entry, NULL) != 0) {
        free(world->minimap_buffer);
        free(world);
        mud->region_next_world = NULL;
        return;
    }
#endif

    mud->region_building_index = idx;
    mud->region_cache[idx].state = REGION_SLOT_BUILDING;
    mud->region_cache[idx].sx = sx;
    mud->region_cache[idx].sy = sy;
    mud->region_cache[idx].plane = plane;
    mud->region_load_state = 1;

    fprintf(stderr, "[rgn] build (%d,%d)\n", sx, sy);
}

// proximity request: build (sx,sy,plane) unless it's already cached or in flight
static void mudclient_region_request_neighbor(mudclient *mud, int sx, int sy,
                                              int plane) {
    if (mud->region_load_force_sync) {
        return;
    }

    mudclient_region_promote_build(mud);

    int found = mudclient_region_cache_find(mud, sx, sy, plane);

    if (found >= 0) {
        mud->region_cache[found].last_touch = ++mud->region_touch_clock;
        return;
    }

    if (mud->region_load_state == 1) {
        return; // one worker at a time
    }

    // post-rebase frame: absolute plane tile of the player
    int px = mud->local_region_x + mud->region_x + mud->plane_width;
    int py = mud->local_region_y + mud->region_y + mud->plane_height;

    int idx = mudclient_region_cache_slot(
        mud, mudclient_region_section_distance(px, py, sx, sy), px, py);

    if (idx < 0) {
        return;
    }

    mudclient_region_build_into(mud, idx, sx, sy, plane);
}

// on each movement packet, build whichever neighbour region the player is approaching; triggered by proximity to a
// window edge (within K tiles of the reload boundary)
void mudclient_region_proximity_check(mudclient *mud) {
    if (mud->region_load_force_sync || mud->local_player == NULL) {
        return;
    }

    mudclient_region_promote_build(mud);

    if (mud->last_plane_index != mud->plane_index) {
        return; // mid plane change; the sync path owns this
    }

    int plane = mud->plane_index;
    int sx = (mud->region_x + mud->plane_width + REGION_SIZE) / REGION_SIZE;
    int sy = (mud->region_y + mud->plane_height + REGION_SIZE) / REGION_SIZE;

    // window-relative player tile, 0..96, window centre 48, reload boundary at
    // 16 and 80
    int lx = mud->local_region_x;
    int ly = mud->local_region_y;
    const int K = 8;

    int near_e = lx >= 80 - K;
    int near_w = lx <= 16 + K;
    int near_n = ly >= 80 - K;
    int near_s = ly <= 16 + K;

    if (near_e) {
        mudclient_region_request_neighbor(mud, sx + 1, sy, plane);
    }
    if (near_w) {
        mudclient_region_request_neighbor(mud, sx - 1, sy, plane);
    }
    if (near_n) {
        mudclient_region_request_neighbor(mud, sx, sy + 1, plane);
    }
    if (near_s) {
        mudclient_region_request_neighbor(mud, sx, sy - 1, plane);
    }
    if (near_e && near_n) {
        mudclient_region_request_neighbor(mud, sx + 1, sy + 1, plane);
    }
    if (near_e && near_s) {
        mudclient_region_request_neighbor(mud, sx + 1, sy - 1, plane);
    }
    if (near_w && near_n) {
        mudclient_region_request_neighbor(mud, sx - 1, sy + 1, plane);
    }
    if (near_w && near_s) {
        mudclient_region_request_neighbor(mud, sx - 1, sy - 1, plane);
    }
}

// claim a cached (or just-finished) region for (sx,sy,plane); NULL on miss
static World *mudclient_region_cache_take(mudclient *mud, int sx, int sy,
                                          int plane,
                                          gl_vertex_buffer ***buffers,
                                          int *buffer_length, int *realized) {
    mudclient_region_promote_build(mud);

    // the in-flight build might be the requested region but not finished
    if (mud->region_load_state == 1 && mud->region_building_index >= 0) {
        struct RegionCacheEntry *b =
            &mud->region_cache[mud->region_building_index];

        if (b->sx == sx && b->sy == sy && b->plane == plane) {
            mudclient_region_prefetch_join();
            mudclient_region_promote_build(mud);
        }
    }

    int idx = mudclient_region_cache_find(mud, sx, sy, plane);

    if (idx < 0 || mud->region_cache[idx].state != REGION_SLOT_READY) {
        return NULL;
    }

    struct RegionCacheEntry *e = &mud->region_cache[idx];

    // if the player crossed early, finish the remaining upload bytes now
    mudclient_region_realize_finish(mud, e);

    World *world = e->world;
    *buffers = e->buffers;
    *buffer_length = e->buffer_length;
    *realized = 1; // fully realized -> install is a pointer swap

    e->state = REGION_SLOT_EMPTY;
    e->world = NULL;
    e->buffers = NULL;
    e->buffer_length = 0;
    e->realized = 0;

    return world;
}

// replace the live world/scene/buffers with a prebuilt world and retain the outgoing region in the cache
static void mudclient_region_install_prebuilt(mudclient *mud, World *world,
                                              gl_vertex_buffer **buffers,
                                              int buffer_length, int realized,
                                              int old_sx, int old_sy,
                                              int old_plane, int px, int py) {
    World *old = mud->world;
    gl_vertex_buffer **old_buffers = mud->scene->gl_terrain_buffers;
    int old_buffer_length = mud->scene->gl_terrain_buffer_length;

    // minimap handover is two pointer swaps, not copies: the outgoing world retains the live buffer as its snapshot,
    // the incoming world's private buffer becomes the live one
#ifdef RENDER_GL
    if (world->minimap_buffer != NULL &&
        mud->surface->gl_dynamic_texture_buffer != NULL) {
        free(old->minimap_buffer);
        old->minimap_buffer = mud->surface->gl_dynamic_texture_buffer;
        mud->surface->gl_dynamic_texture_buffer = world->minimap_buffer;
        world->minimap_buffer = NULL;
    }
#endif

    scene_dispose(mud->scene);

    mud->world = world;

    world->detached = 0;

    // re-add the incoming region's models to the live scene + upload minimap
    world->defer_scene_adds = 1;
    world_load_section_commit(world);

    mud->scene->gl_terrain_buffers = buffers;
    mud->scene->gl_terrain_buffer_length = buffer_length;

    if (!realized) {
#ifdef __vita__
        vglUseVram(GL_FALSE);
#endif
        for (int i = 0; i < buffer_length; i++) {
            game_model_gl_realize_buffer(buffers[i], 1);
        }
#ifdef __vita__
        vglUseVram(GL_TRUE);
#endif
    }

    // retain the outgoing region (GL buffers stay live, realized=1). evicts the farthest entry if the cache is full
    // (-1 = retain always wins); the evicted world frees a slice per frame
    int slot = mudclient_region_cache_slot(mud, -1, px, py);

    if (slot >= 0) {
        struct RegionCacheEntry *e = &mud->region_cache[slot];
        e->state = REGION_SLOT_READY;
        e->sx = old_sx;
        e->sy = old_sy;
        e->plane = old_plane;
        e->world = old;
        e->buffers = old_buffers;
        e->buffer_length = old_buffer_length;
        e->realized = 1; // GL objects are already live
        e->realize_index = old_buffer_length;
        e->last_touch = ++mud->region_touch_clock;

        // keep the retained world's model list and mark it detached so world_reset won't touch the shared scene
        old->detached = 1;
    } else {
        mudclient_region_free_world(mud, old, old_buffers, old_buffer_length,
                                    1);
    }
}
#else
void mudclient_region_proximity_check(mudclient *mud) { (void)mud; }
#endif // MUD_REGION_ASYNC


int mudclient_load_next_region(mudclient *mud, int lx, int ly) {
    if (mud->death_screen_timeout != 0) {
        mud->world->player_alive = 0;
        return 0;
    }

    mud->loading_area = 0;

    lx += mud->plane_width;
    ly += mud->plane_height;

    if (mud->last_plane_index == mud->plane_index && lx > mud->local_lower_x &&
        lx < mud->local_upper_x && ly > mud->local_lower_y &&
        ly < mud->local_upper_y) {
        mud->world->player_alive = 1;
        return 0;
    }

    // a crossing re-places every scenery object; drop any in-flight bake
#ifdef MUD_REGION_ASYNC
    mudclient_gl_bake_cancel(mud);
#endif

    int section_x = (lx + (REGION_SIZE / 2)) / REGION_SIZE;
    int section_y = (ly + (REGION_SIZE / 2)) / REGION_SIZE;

#ifdef MUD_REGION_ASYNC
    // a cached region for this section installs cheaply; the window switch and rebase below match the sync path
    gl_vertex_buffer **prebuilt_buffers = NULL;
    int prebuilt_buffer_length = 0;
    int prebuilt_realized = 0;
    World *prebuilt = NULL;

    // the section being LEFT, needed to retain it in the cache
    int old_sx = (mud->region_x + mud->plane_width + REGION_SIZE) / REGION_SIZE;
    int old_sy = (mud->region_y + mud->plane_height + REGION_SIZE) / REGION_SIZE;
    int old_plane = mud->last_plane_index;

    if (!mud->region_load_force_sync) {
        prebuilt = mudclient_region_cache_take(mud, section_x, section_y,
                                               mud->plane_index,
                                               &prebuilt_buffers,
                                               &prebuilt_buffer_length,
                                               &prebuilt_realized);
    }
#endif

#ifdef MUD_REGION_ASYNC
    if (prebuilt == NULL)
#endif
    {
        surface_draw_string_centre(mud->surface, "Loading... Please wait",
                                   mud->surface->width / 2,
                                   mud->surface->height / 2 + 19, FONT_BOLD_12,
                                   WHITE);

        mudclient_draw_chat_message_tabs(mud);

#ifdef RENDER_3DS_GL
        mudclient_3ds_gl_frame_start(mud, 0);
#endif

        surface_draw(mud->surface);

#ifdef RENDER_GL
#ifdef SDL12
        SDL_GL_SwapBuffers();
#else
        SDL_GL_SwapWindow(mud->gl_window);
#endif
#elif defined(RENDER_3DS_GL)
        mudclient_3ds_gl_frame_end();
#endif
    }

    int ax = mud->region_x;
    int ay = mud->region_y;

    mudclient_region_set_window(mud, section_x, section_y, mud->plane_index);

#ifdef MUD_REGION_ASYNC
    if (prebuilt != NULL) {
        mudclient_region_install_prebuilt(mud, prebuilt, prebuilt_buffers,
                                          prebuilt_buffer_length,
                                          prebuilt_realized, old_sx, old_sy,
                                          old_plane, lx, ly);
    } else
#endif
    {
#ifdef RENDER_GL
        // pure-CPU build first, live-side effects replayed by the commit
        mud->world->defer_scene_adds = 1;
#endif

        world_load_section(mud->world, lx, ly, mud->last_plane_index);

        world_load_section_commit(mud->world);
    }

    // the scene model list was rebuilt: the roof-visibility state machine in
    // draw_game must re-apply its adds/removes
    mud->gl_roof_scene_state = 0;

    mud->region_x -= mud->plane_width;
    mud->region_y -= mud->plane_height;

    int offset_x = mud->region_x - ax;
    int offset_y = mud->region_y - ay;

    // rebase the camera-follow anchor by the same offset as the player
    mud->camera_auto_rotate_player_x -= offset_x * MAGIC_LOC;
    mud->camera_auto_rotate_player_y -= offset_y * MAGIC_LOC;

    mudclient_region_rebase_objects(mud, offset_x, offset_y);

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
#ifdef MUD_REGION_ASYNC
    if (prebuilt == NULL)
#endif
    {
#ifdef __vita__
        // build the world buffers in cached RAM like the object buffers
        vglUseVram(GL_FALSE);
#endif

        world_gl_buffer_world_models(mud->world);

#ifdef __vita__
        vglUseVram(GL_TRUE);
#endif
    }
#endif

    mudclient_region_rebase_rest(mud, offset_x, offset_y);

    mud->world->player_alive = 1;

    return 1;
}

GameCharacter *mudclient_add_character(mudclient *mud,
                                       GameCharacter **character_server,
                                       GameCharacter **known_characters,
                                       int known_character_count,
                                       int server_index, int x, int y,
                                       int animation, int npc_id) {
    if (character_server[server_index] == NULL) {
        if (npc_id == -1 && server_index == mud->local_player_server_index) {
            // unlikely but just in case
            for (int i = 0; i < PLAYERS_SERVER_MAX; i++) {
                if (mud->player_server[i] == mud->local_player) {
                    mud->player_server[i] = NULL;
                    break;
                }
            }

            for (int i = 0; i < PLAYERS_MAX; i++) {
                if (mud->players[i] == mud->local_player) {
                    mud->players[i] = NULL;
                    break;
                }
            }

            free(mud->local_player);
            mud->local_player = NULL;
        }

        GameCharacter *character = malloc(sizeof(GameCharacter));

        if (character == NULL) {
            return NULL;
        }

        game_character_new(character);

        character_server[server_index] = character;
        character_server[server_index]->server_index = server_index;
    }

    GameCharacter *character = character_server[server_index];
    int exists = 0;

    for (int i = 0; i < known_character_count; i++) {
        if (known_characters[i]->server_index != server_index) {
            continue;
        }

        exists = 1;
        break;
    }

    if (npc_id > -1) {
        character->npc_id = npc_id;
    }

    if (exists) {
        character->next_animation = animation;
        int waypoint_index = character->waypoint_current;

        if (x != character->waypoints_x[waypoint_index] ||
            y != character->waypoints_y[waypoint_index]) {
            waypoint_index = (waypoint_index + 1) % 10;
            character->waypoint_current = waypoint_index;
            character->waypoints_x[waypoint_index] = x;
            character->waypoints_y[waypoint_index] = y;
        }
    } else {
        character->server_index = server_index;
        character->moving_step = 0;
        character->waypoint_current = 0;
        character->current_x = x;
        character->current_y = y;
        character->waypoints_x[0] = x;
        character->waypoints_y[0] = y;
        character->current_animation = animation;
        character->next_animation = animation;
        character->step_count = 0;
    }

    return character;
}

GameCharacter *mudclient_add_player(mudclient *mud, int server_index, int x,
                                    int y, int animation) {
    if (server_index >= PLAYERS_SERVER_MAX ||
        mud->player_count >= PLAYERS_MAX) {
        return NULL;
    }

    GameCharacter *player = mudclient_add_character(
        mud, mud->player_server, mud->known_players, mud->known_player_count,
        server_index, x, y, animation, -1);

    if (player == NULL) {
        return NULL;
    }

    mud->players[mud->player_count++] = player;

    return player;
}

GameCharacter *mudclient_add_npc(mudclient *mud, int server_index, int x, int y,
                                 int animation, int npc_id) {
    if (server_index >= NPCS_SERVER_MAX || mud->npc_count >= NPCS_MAX) {
        return NULL;
    }

#ifdef RENDER_SW
    if (mud->options->diversify_npcs) {
        npc_id = diversify_npc(npc_id, server_index, x, y);
    }
#endif

    GameCharacter *npc = mudclient_add_character(
        mud, mud->npcs_server, mud->known_npcs, mud->known_npc_count,
        server_index, x, y, animation, npc_id);

    if (npc == NULL) {
        return NULL;
    }

    mud->npcs[mud->npc_count++] = npc;

    return npc;
}

void mudclient_update_bank_items(mudclient *mud) {
    mud->bank_item_count = mud->new_bank_item_count;

    for (int i = 0; i < mud->new_bank_item_count; i++) {
        mud->bank_items[i] = mud->new_bank_items[i];
        mud->bank_items_count[i] = mud->new_bank_items_count[i];
    }

    for (int i = 0; i < mud->inventory_items_count; i++) {
        if (mud->bank_item_count >= mud->bank_items_max) {
            break;
        }

        int inventory_id = mud->inventory_item_id[i];
        int has_item_in_bank = 0;

        for (int j = 0; j < mud->bank_item_count; j++) {
            if (mud->bank_items[j] == inventory_id) {
                has_item_in_bank = 1;
                break;
            }
        }

        if (!has_item_in_bank) {
            mud->bank_items[mud->bank_item_count] = inventory_id;
            mud->bank_items_count[mud->bank_item_count] = 0;
            mud->bank_item_count++;
        }
    }
}

void mudclient_close_connection(mudclient *mud) {
    if (mud->packet_stream != NULL) {
        packet_stream_new_packet(mud->packet_stream, CLIENT_CLOSE_CONNECTION);
        packet_stream_flush_packet(mud->packet_stream);
    }

    memset(mud->username, '\0', USERNAME_LENGTH + 1);
    memset(mud->password, '\0', PASSWORD_LENGTH + 1);

    mudclient_reset_login_screen(mud);

#ifdef WITH_SINGLEPLAYER
    // leaving the world for good: end the embedded server (host only, no-op for guests/online)
    singleplayer_leave_world();
#endif
}

void mudclient_lost_connection(mudclient *mud) {
    mud_error("[net] lost connection (logout_timeout=%d, in-game=%d)\n",
              mud->logout_timeout, mud->logged_in);
#ifndef REVISION_177
    mud->system_update = 0;
    // OpenRSC zeroes elixirTimer in lostConnection too, not only resetGame
    mud->orsc_elixir_timer = 0;
#endif

    if (mud->logout_timeout != 0) {
        mudclient_reset_login_screen(mud);
#ifdef WITH_SINGLEPLAYER
        // back to the login screen for good: end the embedded server (host only); the reconnect branch below does not
        singleplayer_leave_world();
#endif
    } else {
#ifdef WITH_SINGLEPLAYER
        // co-op guest's host is gone: drop the dead transport and return to the login screen
        if (mud->spnet_guest) {
            if (mud->packet_stream != NULL) {
                packet_stream_close(mud->packet_stream);
            }

            mudclient_reset_login_screen(mud);
            mudclient_show_login_screen_status(mud, "Lost connection to host",
                                               "Rejoin from the world list");
            return;
        }
#endif
        mud->auto_login_attempts = 10;
        mudclient_login(mud, mud->username, mud->password, 1);
    }
}

int mudclient_is_valid_camera_angle(mudclient *mud, int angle) {
    int x = mud->local_player->current_x / 128;
    int y = mud->local_player->current_y / 128;

    for (int i = 2; i >= 1; i--) {
        if (angle == 1 &&
            ((mud->world->object_adjacency[x][y - i] & 128) == 128 ||
             (mud->world->object_adjacency[x - i][y] & 128) == 128 ||
             (mud->world->object_adjacency[x - i][y - i] & 128) == 128)) {
            return 0;
        }

        if (angle == 3 &&
            ((mud->world->object_adjacency[x][y + i] & 128) == 128 ||
             (mud->world->object_adjacency[x - i][y] & 128) == 128 ||
             (mud->world->object_adjacency[x - i][y + i] & 128) == 128)) {
            return 0;
        }

        if (angle == 5 &&
            ((mud->world->object_adjacency[x][y + i] & 128) == 128 ||
             (mud->world->object_adjacency[x + i][y] & 128) == 128 ||
             (mud->world->object_adjacency[x + i][y + i] & 128) == 128)) {
            return 0;
        }

        if (angle == 7 &&
            ((mud->world->object_adjacency[x][y - i] & 128) == 128 ||
             (mud->world->object_adjacency[x + i][y] & 128) == 128 ||
             (mud->world->object_adjacency[x + i][y - i] & 128) == 128)) {
            return 0;
        }

        if (angle == 0 &&
            (mud->world->object_adjacency[x][y - i] & 128) == 128) {
            return 0;
        }

        if (angle == 2 &&
            (mud->world->object_adjacency[x - i][y] & 128) == 128) {
            return 0;
        }

        if (angle == 4 &&
            (mud->world->object_adjacency[x][y + i] & 128) == 128) {
            return 0;
        }

        if (angle == 6 &&
            (mud->world->object_adjacency[x + i][y] & 128) == 128) {
            return 0;
        }
    }

    return 1;
}

void mudclient_auto_rotate_camera(mudclient *mud) {
    if ((mud->camera_angle & 1) == 1 &&
        mudclient_is_valid_camera_angle(mud, mud->camera_angle)) {
        return;
    }

    if ((mud->camera_angle & 1) == 0 &&
        mudclient_is_valid_camera_angle(mud, mud->camera_angle)) {
        if (mudclient_is_valid_camera_angle(mud, (mud->camera_angle + 1) & 7)) {
            mud->camera_angle = (mud->camera_angle + 1) & 7;
            return;
        }

        if (mudclient_is_valid_camera_angle(mud, (mud->camera_angle + 7) & 7)) {
            mud->camera_angle = (mud->camera_angle + 7) & 7;
        }

        return;
    }

    int angles[] = {1, -1, 2, -2, 3, -3, 4};

    for (int i = 0; i < 7; i++) {
        int angle = (mud->camera_angle + angles[i] + 8) & 7;

        if (!mudclient_is_valid_camera_angle(mud, angle)) {
            continue;
        }

        mud->camera_angle = angle;
        break;
    }

    if ((mud->camera_angle & 1) == 0 &&
        mudclient_is_valid_camera_angle(mud, mud->camera_angle)) {
        if (mudclient_is_valid_camera_angle(mud, (mud->camera_angle + 1) & 7)) {
            mud->camera_angle = (mud->camera_angle + 1) & 7;
            return;
        }

        if (mudclient_is_valid_camera_angle(mud, (mud->camera_angle + 7) & 7)) {
            mud->camera_angle = (mud->camera_angle + 7) & 7;
        }
    }
}

void mudclient_handle_camera_zoom(mudclient *mud) {
    if (mud->key_up) {
        mud->camera_zoom -= 16;
    } else if (mud->key_down) {
        mud->camera_zoom += 16;
    } else if (mud->key_page_up) {
        mud->camera_zoom = ZOOM_MIN;
    } else if (mud->key_page_down) {
        mud->camera_zoom = ZOOM_MAX;
    } else if (mud->key_home) {
        mud->camera_zoom = ZOOM_OUTDOORS;
    }

    int is_touch = mudclient_is_touch(mud);

    int exclude_max_x = MUD_VANILLA_WIDTH;

    int exclude_min_y = is_touch ? 0 : mud->surface->height - 80;
    int exclude_max_y = is_touch ? 100 : mud->surface->height;

    if (mud->mouse_scroll_delta != 0 &&
        (mud->show_ui_tab == 0 || mud->show_ui_tab == MAP_TAB) &&
        !(mud->message_tab_selected != MESSAGE_TAB_ALL &&
          mud->mouse_y > exclude_min_y && mud->mouse_y <= exclude_max_y &&
          mud->mouse_x <= exclude_max_x) &&
        !mud->show_dialog_bank) {
        mud->camera_zoom += mud->mouse_scroll_delta * 24;
    }

    if (mud->camera_zoom > ZOOM_MAX) {
        mud->camera_zoom = ZOOM_MAX;
    } else if (mud->camera_zoom < ZOOM_MIN) {
        mud->camera_zoom = ZOOM_MIN;
    }
}

void mudclient_handle_game_input(mudclient *mud) {
#ifndef REVISION_177
    if (mud->system_update > 1) {
        mud->system_update--;
    }
#endif

    if (mud->show_dialog_confirm) {
        mudclient_handle_confirm_input(mud);
    } else if (mud->show_additional_options) {
        mudclient_handle_additional_options_input(mud);
    }

    if (mud->options->tab_respond && mud->key_tab &&
        mud->private_message_target != 0) {
        int is_online = 0;

        for (int i = 0; i < mud->friend_list_count; i++) {
            if (mud->friend_list[i] == mud->private_message_target &&
                mud->friend_list_online[i] > 0) {
                is_online = 1;
                break;
            }
        }

        if (is_online) {
            mud->show_dialog_social_input = SOCIAL_MESSAGE_FRIEND;

            memset(mud->input_pm_current, '\0', INPUT_PM_LENGTH + 1);
            memset(mud->input_pm_final, '\0', INPUT_PM_LENGTH + 1);
        }

        mud->key_tab = 0;
    }

    if (mud->options->middle_click_camera != 0 && mud->middle_button_down) {
        int ticks = get_ticks();

        if (ticks - mud->last_mouse_sample_ticks >= 250) {
            mud->last_mouse_sample_ticks = ticks;
            mud->last_mouse_sample_x = mud->mouse_x;
        }
    }

    mudclient_packet_tick(mud);

    if (mud->logout_timeout > 0) {
        mud->logout_timeout--;
    }

    if (mud->options->idle_logout && mud->mouse_action_timeout > 4500 &&
        mud->combat_timeout == 0 && mud->logout_timeout == 0) {
        mud->mouse_action_timeout -= 500;
        mudclient_send_logout(mud);
        return;
    }

    if (mud->local_player->current_animation == 8 ||
        mud->local_player->current_animation == 9) {
        mud->combat_timeout = 500;
    }

    if (mud->combat_timeout > 0) {
        mud->combat_timeout--;
    }

#ifndef REVISION_177
    // OpenRSC custom EXP elixir: raw wire units tick down once per engine cycle
    if (mud->orsc_elixir_timer > 1) {
        mud->orsc_elixir_timer--;

        if (mud->orsc_elixir_timer <= 1) {
            mud->orsc_elixir_timer = 0;
        }
    }

    // kill-feed TTL: entries expire after 8s (OpenRSC KillAnnouncerQueue.clean()); compact expired rows out
    for (int i = 0; i < mud->orsc_kill_feed_count;) {
        if (--mud->orsc_kill_feed[i].ticks_left <= 0) {
            for (int j = i; j < mud->orsc_kill_feed_count - 1; j++) {
                mud->orsc_kill_feed[j] = mud->orsc_kill_feed[j + 1];
            }
            mud->orsc_kill_feed_count--;
        } else {
            i++;
        }
    }
#endif

    if (mud->show_appearance_change) {
        mudclient_handle_appearance_panel_input(mud);
        return;
    }

    for (int i = 0; i < mud->player_count; i++) {
        game_character_move(mud->players[i]);
    }

    if (mud->death_screen_timeout > 0) {
        mud->death_screen_timeout--;

        if (mud->death_screen_timeout == 0) {
            mudclient_show_message(mud,
                                   "You have been granted another life. Be "
                                   "more careful this time!",
                                   MESSAGE_TYPE_GAME);

            mudclient_show_message(
                mud, "You retain your skills. Your objects land where you died",
                MESSAGE_TYPE_GAME);
        }
    }

    for (int i = 0; i < mud->npc_count; i++) {
        game_character_move(mud->npcs[i]);
    }

    if (mud->show_ui_tab != MAP_TAB) {
        if (an_int_346 > 0) {
            mud->sleep_word_delay_timer++;
        }

        if (an_int_347 > 0) {
            mud->sleep_word_delay_timer = 0;
        }

        an_int_346 = 0;
        an_int_347 = 0;
    }

    for (int i = 0; i < mud->player_count; i++) {
        GameCharacter *player = mud->players[i];

        if (player->projectile_range > 0) {
            player->projectile_range--;
        }
    }

    // teleport detection: when the camera anchor is far behind the player, snap instead of panning. scale the
    // threshold by the follow divisor (16*divisor) so it tracks zoom: 512 at default, 2112 at max, well below a real teleport (>=1 region = 6144)
    int cam_snap_threshold = 16 * (16 + ((mud->camera_zoom - 500) / 15));

    if (mud->camera_auto_rotate_player_x - mud->local_player->current_x <
            -cam_snap_threshold ||
        mud->camera_auto_rotate_player_x - mud->local_player->current_x >
            cam_snap_threshold ||
        mud->camera_auto_rotate_player_y - mud->local_player->current_y <
            -cam_snap_threshold ||
        mud->camera_auto_rotate_player_y - mud->local_player->current_y >
            cam_snap_threshold) {
        mud->camera_auto_rotate_player_x = mud->local_player->current_x;
        mud->camera_auto_rotate_player_y = mud->local_player->current_y;
    }

    if (mud->camera_auto_rotate_player_x != mud->local_player->current_x) {
        mud->camera_auto_rotate_player_x +=
            (mud->local_player->current_x - mud->camera_auto_rotate_player_x) /
            (16 + ((mud->camera_zoom - 500) / 15));
    }

    if (mud->camera_auto_rotate_player_y != mud->local_player->current_y) {
        mud->camera_auto_rotate_player_y +=
            (mud->local_player->current_y - mud->camera_auto_rotate_player_y) /
            (16 + ((mud->camera_zoom - 500) / 15));
    }

    if (mud->settings_camera_auto) {
        int k1 = mud->camera_angle * 32;
        int j3 = k1 - mud->camera_rotation;
        int direction = 1;

        if (j3 != 0) {
            mud->camera_auto_counter++;

            if (j3 > 128) {
                direction = -1;
                j3 = 256 - j3;
            } else if (j3 > 0)
                direction = 1;
            else if (j3 < -128) {
                direction = 1;
                j3 = 256 + j3;
            } else if (j3 < 0) {
                direction = -1;
                j3 = -j3;
            }

            mud->camera_rotation +=
                ((mud->camera_auto_counter * j3 + 255) / 256) * direction;

            mud->camera_rotation &= 0xff;
        } else {
            mud->camera_auto_counter = 0;
        }
    } else if (mud->camera_momentum != 0) {
        int sign = mud->camera_momentum > 0 ? 1 : -1;

        mud->camera_rotation += abs(mud->camera_momentum) * sign;
        mud->camera_momentum -= 1 * sign;
    }

    if (mud->sleep_word_delay_timer > 20) {
        mud->sleep_word_delay = 0;
        mud->sleep_word_delay_timer = 0;
    }

    if (mud->is_sleeping) {
        mudclient_handle_sleep_input(mud);
        return;
    }

    mudclient_handle_message_tabs_input(mud);

    if (mud->death_screen_timeout != 0) {
        mud->last_mouse_button_down = 0;
    }

    if (mud->show_dialog_trade || mud->show_dialog_duel ||
        (mud->show_dialog_shop && mud->options->hold_to_buy)) {

        if (mud->mouse_button_down != 0) {
            mud->mouse_button_down_time++;
        } else {
            mud->mouse_button_down_time = 0;
        }

        if (mud->mouse_button_down_time > 600) {
            mud->mouse_item_count_increment += 5000;
        } else if (mud->mouse_button_down_time > 450) {
            mud->mouse_item_count_increment += 500;
        } else if (mud->mouse_button_down_time > 300) {
            mud->mouse_item_count_increment += 50;
        } else if (mud->mouse_button_down_time > 150) {
            mud->mouse_item_count_increment += 5;
        } else if (mud->mouse_button_down_time > 50) {
            mud->mouse_item_count_increment++;
        } else if (mud->mouse_button_down_time > 20 &&
                   (mud->mouse_button_down_time & 5) == 0) {
            mud->mouse_item_count_increment++;
        }
    } else {
        mud->mouse_button_down_time = 0;
        mud->mouse_item_count_increment = 0;
    }

    if (mud->last_mouse_button_down == 1) {
        mud->mouse_button_click = 1;
    } else if (mud->last_mouse_button_down == 2) {
        mud->mouse_button_click = 2;
    }

#ifdef RENDER_GL
    scene_set_mouse_location(mud->scene, mud->gl_mouse_x, mud->gl_mouse_y);
#else
    scene_set_mouse_location(mud->scene, mud->mouse_x, mud->mouse_y);
#endif

    mud->last_mouse_button_down = 0;

    if (mud->settings_camera_auto) {
        if (mud->camera_auto_counter == 0) {
            if (mud->key_left) {
                mud->camera_angle = (mud->camera_angle + 1) & 7;
                mud->key_left = 0;

                if (!mud->fog_of_war) {
                    if ((mud->camera_angle & 1) == 0) {
                        mud->camera_angle = (mud->camera_angle + 1) & 7;
                    }

                    for (int i = 0; i < 8; i++) {
                        if (mudclient_is_valid_camera_angle(
                                mud, mud->camera_angle)) {
                            break;
                        }

                        mud->camera_angle = (mud->camera_angle + 1) & 7;
                    }
                }
            } else if (mud->key_right) {
                mud->camera_angle = (mud->camera_angle + 7) & 7;
                mud->key_right = 0;

                if (!mud->fog_of_war) {
                    if ((mud->camera_angle & 1) == 0) {
                        mud->camera_angle = (mud->camera_angle + 7) & 7;
                    }

                    for (int i = 0; i < 8; i++) {
                        if (mudclient_is_valid_camera_angle(
                                mud, mud->camera_angle)) {
                            break;
                        }

                        mud->camera_angle = (mud->camera_angle + 7) & 7;
                    }
                }
            }
        }
    } else if (mud->key_left) {
        mud->camera_rotation = (mud->camera_rotation + 2) & 0xff;
    } else if (mud->key_right) {
        mud->camera_rotation = (mud->camera_rotation - 2) & 0xff;
    }

    if (!mud->settings_camera_auto && mud->middle_button_down) {
        // touch drags use a separate rotate sensitivity
        int rotate_option = mudclient_is_touch(mud)
                                ? mud->options->touch_horizontal_drag
                                : mud->options->middle_click_camera;

        if (rotate_option != 0) {
            float scale = rotate_option / 100.0f;

            mud->camera_rotation =
                (mud->origin_rotation +
                 (int)((mud->mouse_x - mud->origin_mouse_x) * scale)) &
                0xff;
        }
    }

    if (mud->options->zoom_camera) {
        mudclient_handle_camera_zoom(mud);
    } else {
        if (mud->fog_of_war && mud->camera_zoom > ZOOM_INDOORS) {
            mud->camera_zoom -= 4;
        } else if (!mud->fog_of_war && mud->camera_zoom < ZOOM_OUTDOORS) {
            mud->camera_zoom += 4;
        }
    }

    if (mud->mouse_click_x_step > 0) {
        mud->mouse_click_x_step--;
    } else if (mud->mouse_click_x_step < 0) {
        mud->mouse_click_x_step++;
    }

#ifdef RENDER_SW
    scene_scroll_texture(mud->scene, FOUNTAIN_ID);
#endif

    mud->object_animation_count++;

    if (mud->object_animation_count > 5) {
        mud->object_animation_count = 0;
        mud->object_animation_cycle = (mud->object_animation_cycle + 1) % 3;
        mud->torch_animation_cycle = (mud->torch_animation_cycle + 1) % 4;
        mud->claw_animation_cycle = (mud->claw_animation_cycle + 1) % 5;
    }

    for (int i = 0; i < mud->object_count; i++) {
        int x = mud->objects[i].x;
        int y = mud->objects[i].y;

        if (x >= 0 && y >= 0 && x < 96 && y < 96 &&
            mud->objects[i].id == WINDMILL_SAILS_ID) {
            game_model_rotate(mud->objects[i].model, 1, 0, 0);
        }
    }

    for (int i = 0; i < mud->magic_bubble_count; i++) {
        mud->magic_bubbles[i].time++;

        if (mud->magic_bubbles[i].time > 50) {
            mud->magic_bubble_count--;

            for (int j = i; j < mud->magic_bubble_count; j++) {
                mud->magic_bubbles[j] = mud->magic_bubbles[j + 1];
            }
        }
    }
}

void mudclient_handle_inputs(mudclient *mud) {
    if (mud->error_loading_data) {
        return;
    }

    mud->login_timer++;

    if (mud->logged_in == 0) {
        mud->mouse_action_timeout = 0;
        mudclient_handle_login_screen_input(mud);
    } else if (mud->logged_in == 1) {
        mud->mouse_action_timeout++;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        if (mud->gl_is_walking &&
            mud->scene->gl_terrain_pick_step == GL_PICK_STEP_FINISHED) {
            mud->gl_is_walking = 0;
            mud->scene->gl_terrain_pick_step = GL_PICK_STEP_NONE;

#if defined(EMSCRIPTEN)
            int x = mud->world->local_x[mud->scene->gl_pick_face_tag];
            int y = mud->world->local_y[mud->scene->gl_pick_face_tag];
#else
            int x = mud->scene->gl_terrain_pick_x;
            int y = mud->scene->gl_terrain_pick_y;
#endif

            mudclient_walk_to_action_source(mud, mud->local_region_x,
                                            mud->local_region_y, x, y, 0);

            if (mud->mouse_click_x_step == -24) {
                mud->mouse_click_x_step = 24;
            }
        }
#endif

        mudclient_handle_game_input(mud);
    }

    mud->last_mouse_button_down = 0;
    mud->camera_rotation_time++;

    if (mud->camera_rotation_time > 500) {
        mud->camera_rotation_time = 0;

        if (mud->options->anti_macro) {
            int roll = (int)(((float)rand() / (float)RAND_MAX) * 4.0f);

            if ((roll & 1) == 1) {
                mud->camera_rotation_x += mud->camera_rotation_x_increment;
            }

            if ((roll & 2) == 2) {
                mud->camera_rotation_y += mud->camera_rotation_y_increment;
            }
        }
    }

    if (mud->camera_rotation_x < -50) {
        mud->camera_rotation_x_increment = 2;
    } else if (mud->camera_rotation_x > 50) {
        mud->camera_rotation_x_increment = -2;
    }

    if (mud->camera_rotation_y < -50) {
        mud->camera_rotation_y_increment = 2;
    } else if (mud->camera_rotation_y > 50) {
        mud->camera_rotation_y_increment = -2;
    }

    mudclient_decrement_message_flash(mud);
}

void mudclient_update_object_animation(mudclient *mud, int object_index,
                                       char *model_name) {
    int object_x = mud->objects[object_index].x;
    int object_y = mud->objects[object_index].y;

    int within_distance = 0;

    if (mud->options->distant_animation) {
        within_distance = 1;
    } else {
        int distance_x = object_x - (mud->local_player->current_x / 128);
        int distance_y = object_y - (mud->local_player->current_y / 128);

        within_distance = distance_x > -OBJECT_ANIMATION_DISTANCE &&
                          distance_x < OBJECT_ANIMATION_DISTANCE &&
                          distance_y > -OBJECT_ANIMATION_DISTANCE &&
                          distance_y < OBJECT_ANIMATION_DISTANCE;
    }

    if (object_x >= 0 && object_y >= 0 && object_x < 96 && object_y < 96 &&
        within_distance) {
        scene_remove_model(mud->scene, mud->objects[object_index].model);

        int model_index = game_data_get_model_index(model_name);
        GameModel *game_model = game_model_copy(mud->game_models[model_index]);

        scene_add_model(mud->scene, game_model);

        game_model_set_light(game_model, 1, 48, 48, -50, -10, -50);
        game_model_copy_position(game_model, mud->objects[object_index].model);

        game_model->key = object_index;

#ifdef RENDER_SW
        game_model_destroy(mud->objects[object_index].model);
#endif
        free(mud->objects[object_index].model);

        mud->objects[object_index].model = game_model;
    }
}

void mudclient_draw_character_message(mudclient *mud, GameCharacter *character,
                                      int x, int y, int width) {
    if (character->message_timeout <= 0) {
        return;
    }
    if (mud->received_messages_count >= RECEIVED_MESSAGE_MAX) {
        return;
    }

    int text_width = surface_text_width(character->message, 1);

    mud->received_message_mid_point[mud->received_messages_count] =
        text_width / 2;

    if (mud->received_message_mid_point[mud->received_messages_count] > 150) {
        mud->received_message_mid_point[mud->received_messages_count] = 150;
    }

    mud->received_message_height[mud->received_messages_count] =
        (text_width / 300) * surface_text_height(1);

    mud->received_message_x[mud->received_messages_count] = x + (width / 2);
    mud->received_message_y[mud->received_messages_count] = y;
    mud->received_messages[mud->received_messages_count++] = character->message;
}

#ifndef REVISION_177
// OpenRSC floating name/clan-tag overlay, players only; hidden when a side tab is open. the name uses drawShadowText
// (three passes), the clan tag drawColoredString (one pass, no shadow); both honour @xxx@ codes, so a rank prefix recolours the shadow passes and staff render as a thick single-colour name
void mudclient_draw_character_nametag(mudclient *mud, GameCharacter *player,
                                      int x, int y, int width) {
    if (!mud->protocol_custom || !mud->orsc.floating_nametags ||
        !mud->orsc_name_clan_tag_overlay || mud->show_ui_tab != 0) {
        return;
    }

    if (player->name[0] != '\0') {
        // sized off the field itself; the prefix is at most "@dcy@"
        char staff_name[sizeof(player->name) + 8];

        snprintf(staff_name, sizeof(staff_name), "%s%s",
                 orsc_staff_prefix(mud, player->group_id), player->name);

        int text_x =
            ((width - surface_text_width(staff_name, FONT_REGULAR_11)) / 2) + x +
            1;

        // their drawShadowText(text, x, y, colour, 0, center = false)
        surface_draw_string(mud->surface, staff_name, text_x - 1, y - 14,
                            FONT_REGULAR_11, 0x0f0f0f);

        surface_draw_string(mud->surface, staff_name, text_x, y - 15,
                            FONT_REGULAR_11, 0x0f0f0f);

        surface_draw_string(mud->surface, staff_name, text_x, y - 14,
                            FONT_REGULAR_11, STRING_YEL);
    }

    // clan tag stored in a fixed buffer, empty when the appearance's presence byte is 0, so empty == absent
    if (player->clan_tag[0] != '\0') {
        char tag[sizeof(player->clan_tag) + 5];
        snprintf(tag, sizeof(tag), "< %s >", player->clan_tag);

        int tag_x =
            ((width - surface_text_width(tag, FONT_REGULAR_11)) / 2) + x + 1;

        surface_draw_string(mud->surface, tag, tag_x, y - 5, FONT_REGULAR_11,
                            STRING_CLA);
    }
}
#endif

void mudclient_draw_character_damage(mudclient *mud, GameCharacter *character,
                                     int x, int y, int ty, int width,
                                     int height, int is_npc, float depth) {
    if (character->current_animation != 8 &&
        character->current_animation != 9 && character->combat_timer == 0) {
        return;
    }

    if (character->combat_timer > 0) {
        int offset_x = x;

        if (character->current_animation == 8) {
            offset_x -= (20 * ty) / 100;
        } else if (character->current_animation == 9) {
            offset_x += (20 * ty) / 100;
        }

        int missing = (character->current_hits * 30) / character->max_hits;

        if (mud->health_bar_count < HEALTH_BAR_MAX) {
            mud->health_bars[mud->health_bar_count].x = offset_x + (width / 2);
            mud->health_bars[mud->health_bar_count].y = y;
            mud->health_bars[mud->health_bar_count++].missing = missing;
        }
    }

    if (character->combat_timer > 150) {
        int offset_x = x;

        if (character->current_animation == 8) {
            offset_x -= (10 * ty) / 100;
        } else if (character->current_animation == 9) {
            offset_x += (10 * ty) / 100;
        }

        surface_draw_sprite_depth(mud->surface, (offset_x + (width / 2)) - 12,
                                  (y + (height / 2)) - 12,
                                  mud->sprite_media + 11 + (is_npc ? 1 : 0),
                                  depth, depth);

        char damage_string[12] = {0};
        sprintf(damage_string, "%d", character->damage_taken);

        surface_draw_string_centre_depth(
            mud->surface, damage_string, (offset_x + (width / 2)) - 1,
            y + (height / 2) + 5, FONT_BOLD_13, WHITE, depth);
    }
}

// TODO make sure it's a human
int mudclient_should_chop_head(mudclient *mud, GameCharacter *character,
                               ANIMATION_INDEX animation_index) {
#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    int roof_id = world_get_wall_roof(mud->world, character->current_x / 128,
                                      character->current_y / 128);

    return (mud->options->show_roofs && roof_id > 0 &&
            /* check if he's smol */
            (character->npc_id > -1
                 ? game_data.npcs[character->npc_id].height >= 200
                 : 1) &&
            (animation_index == ANIMATION_INDEX_HEAD ||
             animation_index == ANIMATION_INDEX_HEAD_OVERLAY) &&
            !world_is_under_roof(mud->world, mud->local_player->current_x,
                                 mud->local_player->current_y) &&
            world_is_under_roof(mud->world, character->current_x,
                                character->current_y));
#else
    (void)mud;
    (void)character;
    (void)animation_index;

    return 0;
#endif
}

void mudclient_draw_player(mudclient *mud, int x, int y, int width, int height,
                           int id, int skew_x, int ty, float depth_top,
                           float depth_bottom) {
    GameCharacter *player = mud->players[id];

    if (player->bottom_colour == 255) {
        return;
    }

    int animation_order =
        (player->current_animation + (mud->camera_rotation + 16) / 32) & 7;

    int flip = 0;
    int i2 = animation_order;

    if (i2 == 5) {
        i2 = 3;
        flip = 1;
    } else if (i2 == 6) {
        i2 = 2;
        flip = 1;
    } else if (i2 == 7) {
        i2 = 1;
        flip = 1;
    }

    int j2 = i2 * 3 + character_walk_model[(player->step_count / 6) % 4];

    if (player->current_animation == 8) {
        i2 = 5;
        animation_order = 2;
        flip = 0;
        x -= (5 * ty) / 100;
        j2 = i2 * 3 + character_combat_model_array1[(mud->login_timer / 5) % 8];
    } else if (player->current_animation == 9) {
        i2 = 5;
        animation_order = 2;
        flip = 1;
        x += (5 * ty) / 100;
        j2 = i2 * 3 + character_combat_model_array2[(mud->login_timer / 6) % 8];
    }

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    depth_top = (depth_bottom + depth_top) / 2.0f;
    depth_bottom = depth_top;
#endif

    for (int i = 0; i < ANIMATION_COUNT; i++) {
        ANIMATION_INDEX animation_index =
            character_animation_array[animation_order][i];

        int animation_id = player->animations[animation_index] - 1;

        if (animation_id < 0) {
            continue;
        }

        if (mudclient_should_chop_head(mud, player, animation_index)) {
            continue;
        }

        int offset_x = 0;
        int offset_y = 0;
        int j5 = j2;

        if (flip && i2 >= 1 && i2 <= 3) {
            if (game_data.animations[animation_id].has_f == 1) {
                j5 += 15;
            } else if (animation_index == ANIMATION_INDEX_RIGHT_HAND &&
                       i2 == 1) {
                offset_x = -22;
                offset_y = -3;

                j5 = i2 * 3 +
                     character_walk_model[(2 + (player->step_count / 6)) % 4];
            } else if (animation_index == ANIMATION_INDEX_RIGHT_HAND &&
                       i2 == 2) {
                offset_x = 0;
                offset_y = -8;

                j5 = i2 * 3 +
                     character_walk_model[(2 + (player->step_count / 6)) % 4];
            } else if (animation_index == ANIMATION_INDEX_RIGHT_HAND &&
                       i2 == 3) {
                offset_x = 26;
                offset_y = -5;

                j5 = i2 * 3 +
                     character_walk_model[(2 + (player->step_count / 6)) % 4];
            } else if (animation_index == ANIMATION_INDEX_LEFT_HAND &&
                       i2 == 1) {
                offset_x = 22;
                offset_y = 3;

                j5 = i2 * 3 +
                     character_walk_model[(2 + (player->step_count / 6)) % 4];
            } else if (animation_index == ANIMATION_INDEX_LEFT_HAND &&
                       i2 == 2) {
                offset_x = 0;
                offset_y = 8;

                j5 = i2 * 3 +
                     character_walk_model[(2 + (player->step_count / 6)) % 4];
            } else if (animation_index == ANIMATION_INDEX_LEFT_HAND &&
                       i2 == 3) {
                offset_x = -26;
                offset_y = 5;

                j5 = i2 * 3 +
                     character_walk_model[(2 + (player->step_count / 6)) % 4];
            }
        }

        if (i2 != 5 || game_data.animations[animation_id].has_a == 1) {
            int sprite_id = j5 + game_data.animations[animation_id].file_id;

#ifdef RENDER_SW
            if (mud->surface->surface_pixels[sprite_id] == NULL &&
                mud->surface->sprite_colours[sprite_id] == NULL) {
                /* sprite file was not loaded, probably on f2p version */
                continue;
            }
#endif

            offset_x =
                (offset_x * width) / mud->surface->sprite_width_full[sprite_id];

            offset_y = (offset_y * height) /
                       mud->surface->sprite_height_full[sprite_id];

            int clip_width =
                (width * mud->surface->sprite_width_full[sprite_id]) /
                mud->surface->sprite_width_full
                    [game_data.animations[animation_id].file_id];

            offset_x -= (clip_width - width) / 2;

            int animation_colour = game_data.animations[animation_id].colour;

            if (animation_colour == 1) {
                animation_colour =
                    player_hair_colours[player->hair_colour >= 0 &&
                                                player->hair_colour <
                                                    PLAYER_HAIR_COLOUR_COUNT
                                            ? player->hair_colour
                                            : 0];
            } else if (animation_colour == 2) {
                animation_colour = player_top_bottom_colours
                    [player->top_colour >= 0 &&
                             player->top_colour < PLAYER_TOP_BOTTOM_COLOUR_COUNT
                         ? player->top_colour
                         : 0];
            } else if (animation_colour == 3) {
                animation_colour = player_top_bottom_colours
                    [player->bottom_colour >= 0 &&
                             player->bottom_colour <
                                 PLAYER_TOP_BOTTOM_COLOUR_COUNT
                         ? player->bottom_colour
                         : 0];
            }

            // appearance indices come straight off the wire; clamp so a server can't read past the palettes
            int skin_colour =
                player_skin_colours[player->skin_colour >= 0 &&
                                            player->skin_colour <
                                                PLAYER_SKIN_COLOUR_COUNT
                                        ? player->skin_colour
                                        : 0];

            surface_draw_sprite_transform_mask_depth(
                mud->surface, x + offset_x, y + offset_y, clip_width, height,
                sprite_id, animation_colour, skin_colour, skew_x, flip,
                depth_top, depth_bottom);
        }
    }

    mudclient_draw_character_message(mud, player, x, y, width);

#ifndef REVISION_177
    mudclient_draw_character_nametag(mud, player, x, y, width);
#endif

    if (player->bubble_timeout > 0 &&
        mud->action_bubble_count < ACTION_BUBBLE_MAX) {
        mud->action_bubbles[mud->action_bubble_count].x = x + (width / 2);
        mud->action_bubbles[mud->action_bubble_count].y = y;
        mud->action_bubbles[mud->action_bubble_count].scale = ty;

        mud->action_bubbles[mud->action_bubble_count++].item =
            player->bubble_item;
    }

    float damage_depth = 0.0f;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    damage_depth = depth_top;
#endif

    mudclient_draw_character_damage(mud, player, x, y, ty, width, height, 0,
                                    damage_depth);

    if (player->skull_visible && player->bubble_timeout == 0) {
        int k3 = skew_x + x + (width / 2);

        if (player->current_animation == 8) {
            k3 -= (20 * ty) / 100;
        } else if (player->current_animation == 9) {
            k3 += (20 * ty) / 100;
        }

        int width = (16 * ty) / 100;
        int height = (16 * ty) / 100;

        surface_draw_sprite_scale(mud->surface, k3 - (width / 2),
                                  y - (height / 2) - ((10 * ty) / 100), width,
                                  height, mud->sprite_media + 13, damage_depth);
    }
}

void mudclient_draw_npc(mudclient *mud, int x, int y, int width, int height,
                        int id, int skew_x, int ty, float depth_top,
                        float depth_bottom) {
    GameCharacter *npc = mud->npcs[id];

    int animation_order =
        (npc->current_animation + (mud->camera_rotation + 16) / 32) & 7;

    int flip = 0;
    int i2 = animation_order;

    if (i2 == 5) {
        i2 = 3;
        flip = 1;
    } else if (i2 == 6) {
        i2 = 2;
        flip = 1;
    } else if (i2 == 7) {
        i2 = 1;
        flip = 1;
    }

    int j2 =
        i2 * 3 + character_walk_model[(npc->step_count /
                                       game_data.npcs[npc->npc_id].walk_speed) %
                                      4];

    if (npc->current_animation == 8) {
        i2 = 5;
        animation_order = 2;
        flip = 0;
        x -= (game_data.npcs[npc->npc_id].combat_width * ty) / 100;
        j2 = i2 * 3 +
             character_combat_model_array1[((mud->login_timer /
                                                 (game_data.npcs[npc->npc_id]
                                                      .combat_speed) -
                                             1)) %
                                           8];
    } else if (npc->current_animation == 9) {
        i2 = 5;
        animation_order = 2;
        flip = 1;
        x += (game_data.npcs[npc->npc_id].combat_width * ty) / 100;

        j2 =
            i2 * 3 +
            character_combat_model_array2
                [(mud->login_timer / game_data.npcs[npc->npc_id].combat_speed) %
                 8];
    }

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    depth_top = (depth_bottom + depth_top) / 2.0f;
    depth_bottom = depth_top;
#endif

    for (int i = 0; i < ANIMATION_COUNT; i++) {
        int animation_index = character_animation_array[animation_order][i];
        int animation_id = game_data.npcs[npc->npc_id].sprites[animation_index];

        if (animation_id < 0) {
            continue;
        }

        if (mudclient_should_chop_head(mud, npc, animation_index)) {
            continue;
        }

        int offset_x = 0;
        int offset_y = 0;
        int k4 = j2;

        if (flip && i2 >= 1 && i2 <= 3 &&
            game_data.animations[animation_id].has_f == 1) {
            k4 += 15;
        }

        if (i2 != 5 || game_data.animations[animation_id].has_a == 1) {
            int sprite_id = k4 + game_data.animations[animation_id].file_id;

#ifdef RENDER_SW
            if (mud->surface->surface_pixels[sprite_id] == NULL &&
                mud->surface->sprite_colours[sprite_id] == NULL) {
                /* sprite file was not loaded, probably on f2p version */
                continue;
            }
#endif

            offset_x =
                (offset_x * width) / mud->surface->sprite_width_full[sprite_id];

            offset_y = (offset_y * height) /
                       mud->surface->sprite_height_full[sprite_id];

            int clip_width =
                (width * mud->surface->sprite_width_full[sprite_id]) /
                mud->surface->sprite_width_full
                    [game_data.animations[animation_id].file_id];

            offset_x -= (clip_width - width) / 2;

            int animation_colour = game_data.animations[animation_id].colour;

            int skin_colour = 0;

            if (animation_colour == 1) {
                animation_colour = game_data.npcs[npc->npc_id].hair_colour;
                skin_colour = game_data.npcs[npc->npc_id].skin_colour;
            } else if (animation_colour == 2) {
                animation_colour = game_data.npcs[npc->npc_id].top_colour;
                skin_colour = game_data.npcs[npc->npc_id].skin_colour;
            } else if (animation_colour == 3) {
                animation_colour = game_data.npcs[npc->npc_id].bottom_colour;
                skin_colour = game_data.npcs[npc->npc_id].skin_colour;
            }

            surface_draw_sprite_transform_mask_depth(
                mud->surface, x + offset_x, y + offset_y, clip_width, height,
                sprite_id, animation_colour, skin_colour, skew_x, flip,
                depth_top, depth_bottom);
        }
    }

    mudclient_draw_character_message(mud, npc, x, y, width);

    float damage_depth = 0.0f;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    damage_depth = depth_top;
#endif

    mudclient_draw_character_damage(mud, npc, x, y, ty, width, height, 1,
                                    damage_depth);
}

void mudclient_draw_blue_bar(mudclient *mud) {
#ifdef __vita__
    // the Vita touch layout draws no bottom bar (menu, appearance or in-game)
    (void)mud;
    return;
#else
    int bars = 1;

    if (mud->surface->width > HBAR_WIDTH) {
        bars += mud->surface->width / HBAR_WIDTH;
    }

    for (int i = 0; i < bars; i++) {
        surface_draw_sprite(mud->surface, i * HBAR_WIDTH,
                            mud->surface->height - 16 +
                                (mud->surface->height < 268 ? 4 : 0),
                            mud->sprite_media + 22);
    }
#endif
}

int mudclient_is_in_combat(mudclient *mud) {
    return mud->local_player->current_animation == 8 ||
           mud->local_player->current_animation == 9;
}

GameCharacter *mudclient_get_opponent(mudclient *mud) {
    if (!mudclient_is_in_combat(mud)) {
        if (mud->combat_target != NULL) {
            if (mud->combat_target->max_hits <= 0) {
                return NULL;
            }

            /*
             * if there is a target, check that they are still in view
             */
            if (mud->combat_target->npc_id != -1) {
                for (int i = 0; i < mud->known_npc_count; i++) {
                    if (mud->known_npcs[i] == mud->combat_target) {
                        return mud->combat_target;
                    }
                }
            } else {
                for (int i = 0; i < mud->known_player_count; i++) {
                    if (mud->known_players[i] == mud->combat_target) {
                        return mud->combat_target;
                    }
                }
            }
        }

        return NULL;
    }

    int desired_animation = mud->local_player->current_animation == 8 ? 9 : 8;

    for (int i = 0; i < mud->known_npc_count; i++) {
        GameCharacter *npc = mud->known_npcs[i];

        if (npc->current_x == mud->local_player->current_x &&
            npc->current_y == mud->local_player->current_y &&
            npc->current_animation == desired_animation) {
            return npc;
        }
    }

    for (int i = 0; i < mud->known_player_count; i++) {
        GameCharacter *player = mud->known_players[i];

        if (player->current_x == mud->local_player->current_x &&
            player->current_y == mud->local_player->current_y &&
            player->current_animation == desired_animation) {
            return player;
        }
    }

    return NULL;
}

void mudclient_draw_ui(mudclient *mud) {
    mudclient_draw_ui_tabs(mud);

    // OpenRSC want_drop_x: the drop-X amount dialog is modal; when active it owns the offer-x dialog, skip the rest
    // of the UI
    if (mudclient_handle_drop_x(mud)) {
        return;
    }

#ifndef REVISION_177
    // OpenRSC custom auction: buy-amount / sell-amount / sell-price prompts
    // own the offer-x dialog the same way
    if (mudclient_handle_auction_offer(mud)) {
        return;
    }
#endif

    int no_menus = !mud->show_option_menu && !mud->show_right_click_menu;

    if (no_menus) {
        mud->menu_items_count = 0;
    }

    if (mud->options->experience_drops) {
        mudclient_draw_experience_drops(mud);
    }

#ifndef REVISION_177
    // OpenRSC xp counter (their drawExperienceCounter); self-gated
    mudclient_draw_experience_counter(mud);
#endif

    if (mud->options->status_bars && !mudclient_is_touch(mud)) {
        mudclient_draw_status_bars(mud);
    }

#ifndef REVISION_177
    // OpenRSC custom batch progress bar: a passive overlay during batching,
    // shown/updated/hidden entirely by the server (opcode 134)
    if (mud->protocol_custom && mud->orsc_progress_visible) {
        mudclient_draw_progress_bar(mud);
    }

    // OpenRSC custom kill feed: top-right ticker (their C_KILL_FEED)
    if (mud->protocol_custom && mud->orsc.want_kill_feed &&
        mud->orsc_show_kill_feed && mud->orsc_kill_feed_count > 0) {
        mudclient_draw_kill_feed(mud);
    }

    // OpenRSC custom party HUD (PartyGUI, auto-shown while in a party)
    mudclient_draw_party_hud(mud);

    // the skill/quest guide windows, over everything; self-gated
    mudclient_draw_guides(mud);

    // OpenRSC custom EXP-elixir overlay: wilderness anchors map to height-68/-32/-19 (a constant +12); the elixir
    // line is gameWidth-53 centred, height-19 normally, height-74 when the wilderness gauge is up, smaller font in the wild; colour 0x9139e7, M:SS timer
    if (mud->protocol_custom && mud->orsc_elixir_timer > 0) {
        int elixir_seconds = mud->orsc_elixir_timer / 50;

        char formatted_elixir[32] = {0};
        sprintf(formatted_elixir, "EXP Elixir: %d:%02d", elixir_seconds / 60,
                elixir_seconds % 60);

        if (mud->is_in_wilderness) {
            surface_draw_string_centre(mud->surface, formatted_elixir,
                                       mud->surface->width - 53,
                                       mud->surface->height - 74,
                                       FONT_REGULAR_11, 0x9139e7);
        } else {
            surface_draw_string_centre(mud->surface, formatted_elixir,
                                       mud->surface->width - 53,
                                       mud->surface->height - 19, FONT_BOLD_12,
                                       0x9139e7);
        }
    }

    // OpenRSC custom side-menu HUD (S_SIDE_MENU_TOGGLE, 14px rows: Hits, Prayer, Kills, Last NPC Kills; Tile line
    // skipped). on the Vita touch UI it starts at y=300 since the combat-style selector occupies 13,140..250, computed from UI_TABS constants
    if (((mud->protocol_custom && mud->orsc.side_menu) || MUD_SP_WIRE(mud)) &&
        mud->orsc_show_side_menu) {
        int hud_y = mudclient_is_touch(mud) ? 300 : 130;

        surface_draw_stringf(mud->surface, 7, hud_y, FONT_BOLD_12, WHITE,
                             "Hits: %d@gre@/@whi@%d",
                             mud->player_skill_current[SKILL_HITS],
                             mud->player_skill_base[SKILL_HITS]);
        hud_y += 14;

        surface_draw_stringf(mud->surface, 7, hud_y, FONT_BOLD_12, WHITE,
                             "Prayer: %d@gre@/@whi@%d",
                             mud->player_skill_current[SKILL_PRAYER],
                             mud->player_skill_base[SKILL_PRAYER]);
        hud_y += 14;

        // each kill-counter line is its own account opt-in (OpenRSC C_TOTAL_NPC_KC / C_RECENT_NPC_KC, both default
        // off, synced in the settings tail)
        if (mud->orsc_npc_kills_seen && mud->orsc_show_npc_kc) {
            surface_draw_stringf(mud->surface, 7, hud_y, FONT_BOLD_12, WHITE,
                                 "Kills: %d", mud->orsc_npc_kills_total);
            hud_y += 14;
        }

        if (mud->orsc_npc_kills_seen && mud->orsc_show_recent_npc_kc &&
            mud->orsc_npc_kills_recent_count > 0) {
            surface_draw_stringf(mud->surface, 7, hud_y, FONT_BOLD_12, WHITE,
                                 "Last NPC Kills: %d",
                                 mud->orsc_npc_kills_recent_count);
        }
    }

    // OpenRSC custom fishing trawler status (FishingTrawlerInterface panel; water gauge renders as text)
    if (mud->protocol_custom && mud->orsc_trawler_visible) {
        int trawler_x = 8;
        int trawler_y = 90;

        surface_draw_box_alpha(mud->surface, trawler_x - 4, trawler_y - 12,
                               128, 62, GREY_98, 160);

        surface_draw_stringf(mud->surface, trawler_x, trawler_y, FONT_BOLD_12,
                             WHITE, "Water: %d", mud->orsc_trawler_water);
        surface_draw_string(mud->surface,
                            mud->orsc_trawler_net_ripped ? "Net: @red@Ripped!"
                                                         : "Net: @gre@OK",
                            trawler_x, trawler_y + 14, FONT_BOLD_12, WHITE);
        surface_draw_stringf(mud->surface, trawler_x, trawler_y + 28,
                             FONT_BOLD_12, WHITE, "Catch: %d fish",
                             mud->orsc_trawler_fish);
        surface_draw_stringf(mud->surface, trawler_x, trawler_y + 42,
                             FONT_BOLD_12, WHITE, "Time left: %d min",
                             mud->orsc_trawler_minutes);
    }
#endif

    if (mud->show_additional_options) {
        mudclient_draw_additional_options(mud);

        if (mud->show_dialog_confirm) {
            mudclient_draw_confirm(mud);
        }
    } else if (mud->show_dialog_confirm) {
        mudclient_draw_confirm(mud);
    } else if (mud->logout_timeout != 0) {
        mudclient_draw_logout(mud);
    } else if (mud->show_dialog_welcome) {
        mudclient_draw_welcome(mud);
    } else if (mud->show_dialog_server_message) {
        mudclient_draw_server_message(mud);
    } else if (mud->show_wilderness_warning == 1) {
        mudclient_draw_wilderness_warning(mud);
    } else if (mud->show_dialog_bank_pin) {
        // OpenRSC custom bank PIN pad: outranks the bank screen
        mudclient_draw_bank_pin(mud);
    } else if (mud->show_dialog_online_list) {
        // OpenRSC custom online-player popup, opened by the ::online reply (136)
        mudclient_draw_online_list(mud);
        mudclient_handle_online_list_input(mud);
    } else if (mud->show_dialog_bank_preset) {
        // OpenRSC custom preset viewer, outranks the bank screen
        mudclient_draw_bank_preset(mud);
        mudclient_handle_bank_preset_input(mud);
    } else if (mud->show_dialog_ironman) {
        // OpenRSC custom ironman selection (opened by the tutor NPCs)
        mudclient_draw_ironman_interface(mud);
    } else if (mud->orsc_auction_visible) {
        // OpenRSC custom auction house (opened by the Auctioneer NPCs)
        mudclient_draw_auction(mud);
    } else if (mud->show_dialog_bank && mud->combat_timeout == 0) {
        mudclient_draw_bank(mud);

        if (mud->options->bank_menus) {
            if (mud->show_right_click_menu) {
                mudclient_draw_right_click_menu(mud);
            } else {
                mudclient_create_top_mouse_menu(mud);
            }
        }
    } else if (mud->show_dialog_shop && mud->combat_timeout == 0) {
        mudclient_draw_shop(mud);
    } else if (mud->show_dialog_trade_confirm) {
        mudclient_draw_trade_confirm(mud);
    } else if (mud->show_dialog_trade) {
        mudclient_draw_trade(mud);

        if (mud->options->transaction_menus) {
            if (mud->show_right_click_menu) {
                mudclient_draw_right_click_menu(mud);
            } else {
                mudclient_create_top_mouse_menu(mud);
            }
        }
    } else if (mud->show_dialog_duel_confirm) {
        mudclient_draw_duel_confirm(mud);
    } else if (mud->show_dialog_duel) {
        mudclient_draw_duel(mud);

        if (mud->options->transaction_menus) {
            if (mud->show_right_click_menu) {
                mudclient_draw_right_click_menu(mud);
            } else {
                mudclient_create_top_mouse_menu(mud);
            }
        }
    } else if (mud->show_change_password_step != 0) {
        mudclient_draw_change_password(mud);
    } else if (mud->show_dialog_social_input != 0) {
        mudclient_draw_social_input(mud);
    } else {
        if (mud->show_option_menu) {
            mudclient_draw_option_menu(mud);
        }

        mudclient_set_active_ui_tab(mud, no_menus);

        if (mudclient_is_in_combat(mud) || mud->options->combat_style_always) {
            mudclient_draw_combat_style(mud);
        }

        if (mud->show_ui_tab == 0 && no_menus) {
            mudclient_create_right_click_menu(mud);
        }

        mudclient_draw_active_ui_tab(mud, no_menus);

        if (!mud->show_option_menu) {
            if (mud->show_right_click_menu) {
                mudclient_draw_right_click_menu(mud);
            } else {
                mudclient_create_top_mouse_menu(mud);

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
                if (!mud->gl_is_walking) {
                    mud->scene->gl_terrain_pick_step = GL_PICK_STEP_NONE;
                }
#endif
            }
        }

        mudclient_draw_hover_tooltip(mud);
    }

    mud->mouse_button_click = 0;
}

int mudclient_compare_text(const void *v1, const void *v2) {
    struct OverworldText *ot1 = (struct OverworldText *)v1;
    struct OverworldText *ot2 = (struct OverworldText *)v2;
    return strcmp(ot1->text, ot2->text);
}

void mudclient_draw_overhead(mudclient *mud) {
    for (int i = 0; i < mud->received_messages_count; i++) {
        int text_height = surface_text_height(1);
        int x = mud->received_message_x[i];
        int y = mud->received_message_y[i];
        int message_mid = mud->received_message_mid_point[i];
        int message_height = mud->received_message_height[i];
        int flag = 1;

        while (flag) {
            flag = 0;

            for (int j = 0; j < i; j++) {
                if (y + message_height >
                        mud->received_message_y[j] - text_height &&
                    y - text_height < mud->received_message_y[j] +
                                          mud->received_message_height[j] &&
                    x - message_mid < mud->received_message_x[j] +
                                          mud->received_message_mid_point[j] &&
                    x + message_mid > mud->received_message_x[j] -
                                          mud->received_message_mid_point[j] &&
                    mud->received_message_y[j] - text_height - message_height <
                        y) {
                    y = mud->received_message_y[j] - text_height -
                        message_height;

                    flag = 1;
                }
            }
        }

        mud->received_message_y[i] = y;

#ifdef RENDER_GL
        if (mudclient_is_ui_scaled(mud)) {
            x /= 2;
            y /= 2;
        }
#endif

        surface_draw_paragraph(mud->surface, mud->received_messages[i], x, y, 1,
                               YELLOW, 300);
    }

    for (int i = 0; i < mud->action_bubble_count; i++) {
        int x = mud->action_bubbles[i].x;
        int y = mud->action_bubbles[i].y;
        int scale = mud->action_bubbles[i].scale;

#ifdef RENDER_GL
        if (mudclient_is_ui_scaled(mud)) {
            x /= 2;
            y /= 2;
            scale /= 2;
        }
#endif

        int id = mud->action_bubbles[i].item;
        int scale_x = (39 * scale) / 100;
        int scale_y = (27 * scale) / 100;

        surface_draw_sprite_scale_alpha(mud->surface, x - (scale_x / 2),
                                        y - scale_y, scale_x, scale_y,
                                        mud->sprite_media + 9, 85);

        int scale_x_clip = (36 * scale) / 100;
        int scale_y_clip = (24 * scale) / 100;

        int final_x = x - (scale_x_clip / 2);
        int final_y = (y - scale_y + (scale_y / 2)) - (scale_y_clip / 2);

        surface_draw_sprite_transform_mask(
            mud->surface, final_x, final_y, scale_x_clip, scale_y_clip,
            game_data.items[id].sprite + mud->sprite_item,
            game_data.items[id].mask, 0, 0, 0);
    }

    // prevent strobing from random sort order
    qsort(mud->overworld_text, mud->overworld_text_count,
          sizeof(struct OverworldText), mudclient_compare_text);

    // check and fix overlapping text
    for (int i = 0; i < mud->overworld_text_count; i++) {
        int x = mud->overworld_text[i].x;
        int y = mud->overworld_text[i].y;
        int width =
            surface_text_width(mud->overworld_text[i].text, FONT_REGULAR_11);
        int height = surface_text_height(FONT_REGULAR_11);
        for (int j = 0; j < mud->overworld_text_count; j++) {
            int x2 = mud->overworld_text[j].x;
            int y2 = mud->overworld_text[j].y;
            if ((x + width + 2) < x2 || (x - width - 2) > x2) {
                continue;
            }
            if ((y + height + 2) < y2 || (y - height - 2) > y2) {
                continue;
            }
            mud->overworld_text[i].y += (height + 1);
        }
    }

    for (int i = 0; i < mud->overworld_text_count; i++) {
        int32_t colour = (int32_t)mud->overworld_text[i].colour;
        int x = mud->overworld_text[i].x;
        int y = mud->overworld_text[i].y;

        surface_draw_string_centre(mud->surface, mud->overworld_text[i].text, x,
                                   y, FONT_REGULAR_11, colour);
    }

    for (int i = 0; i < mud->health_bar_count; i++) {
        int x = mud->health_bars[i].x;
        int y = mud->health_bars[i].y;
        int missing = mud->health_bars[i].missing;

#ifdef RENDER_GL
        if (mudclient_is_ui_scaled(mud)) {
            x /= 2;
            y /= 2;
        }
#endif

        surface_draw_box_alpha(mud->surface, x - 15, y - 3, missing, 5, GREEN,
                               192);

        surface_draw_box_alpha(mud->surface, (x - 15) + missing, y - 3,
                               30 - missing, 5, RED, 192);
    }
}

void mudclient_animate_objects(mudclient *mud) {
    char name[23] = {0};

#ifdef MUD_REGION_ASYNC
    // object animation frees + recreates object models the bake worker may be
    // reading; finish/drop it first
    mudclient_gl_bake_cancel(mud);
#endif

    if (mud->object_animation_cycle != mud->last_object_animation_cycle) {
        mud->last_object_animation_cycle = mud->object_animation_cycle;

        for (int i = 0; i < mud->object_count; i++) {
            if (mud->objects[i].id == FIRE_ID) {
                sprintf(name, "firea%d", (mud->object_animation_cycle + 1));
                mudclient_update_object_animation(mud, i, name);
            } else if (mud->objects[i].id == FIREPLACE_ID) {
                sprintf(name, "fireplacea%d",
                        (mud->object_animation_cycle + 1));
                mudclient_update_object_animation(mud, i, name);
            } else if (mud->objects[i].id == LIGHTNING_ID) {
                sprintf(name, "lightning%d", (mud->object_animation_cycle + 1));
                mudclient_update_object_animation(mud, i, name);
            } else if (mud->objects[i].id == FIRE_SPELL_ID) {
                sprintf(name, "firespell%d", (mud->object_animation_cycle + 1));
                mudclient_update_object_animation(mud, i, name);
            } else if (mud->objects[i].id == SPELL_CHARGE_ID) {
                sprintf(name, "spellcharge%d",
                        (mud->object_animation_cycle + 1));

                mudclient_update_object_animation(mud, i, name);
            }
        }
    }

    if (mud->torch_animation_cycle != mud->last_torch_animation_cycle) {
        mud->last_torch_animation_cycle = mud->torch_animation_cycle;

        for (int i = 0; i < mud->object_count; i++) {
            if (mud->objects[i].id == TORCH_ID) {
                sprintf(name, "torcha%d", mud->torch_animation_cycle + 1);
                mudclient_update_object_animation(mud, i, name);
            } else if (mud->objects[i].id == SKULL_TORCH_ID) {
                sprintf(name, "skulltorcha%d", mud->torch_animation_cycle + 1);
                mudclient_update_object_animation(mud, i, name);
            }
        }
    }

    if (mud->claw_animation_cycle != mud->last_claw_animation_cycle) {
        mud->last_claw_animation_cycle = mud->claw_animation_cycle;

        for (int i = 0; i < mud->object_count; i++) {
            if (mud->objects[i].id == CLAW_SPELL_ID) {
                sprintf(name, "clawspell%d", mud->claw_animation_cycle + 1);
                mudclient_update_object_animation(mud, i, name);
            }
        }
    }
}

#ifdef RENDER_GL
// backing store for the baked world-space vertex arrays of the current region's scenery; one block per bake, freed
// when the next region bake replaces it
static int16_t *gl_object_bake_arena = NULL;

// bake the region's static scenery into world space so the opaque pass can merge it: apply each instance's transform
// to its vertices once and re-buffer contiguously, turning ~90 per-model draws into a few merged runs. normals are rebuilt from the baked vertices. windmill sails and fire/torch animations keep the library route; baking is idempotent
void mudclient_gl_bake_region_objects(mudclient *mud) {
    int total_vertices = 0;

    for (int i = 0; i < mud->object_count; i++) {
        GameModel *model = mud->objects[i].model;

        if (model == NULL || mud->objects[i].id == WINDMILL_SAILS_ID) {
            continue;
        }

        total_vertices += model->vertex_count;
    }

    if (total_vertices == 0) {
        return;
    }

    int16_t *arena =
        malloc((size_t)total_vertices * 3 * sizeof(int16_t));

    if (arena == NULL) {
        return; // out of memory: models keep drawing per-model as before
    }

    GameModel *baked_models[mud->object_count > 0 ? mud->object_count : 1];
    int baked_count = 0;

    int16_t *slice = arena;

    for (int i = 0; i < mud->object_count; i++) {
        GameModel *model = mud->objects[i].model;

        if (model == NULL || mud->objects[i].id == WINDMILL_SAILS_ID) {
            continue;
        }

        // build the pending transform now if this model was placed after the
        // last frame (no-op when it is already applied or already baked)
        game_model_apply(model);

        int vertex_count = model->vertex_count;

        int16_t *baked_x = slice;
        int16_t *baked_y = slice + vertex_count;
        int16_t *baked_z = slice + vertex_count * 2;

        slice += (size_t)vertex_count * 3;

        // world AABB for the frustum cull and mouse picking, tracked inline while baking
        int min_x = 999999, min_y = 999999, min_z = 999999;
        int max_x = -999999, max_y = -999999, max_z = -999999;

        for (int v = 0; v < vertex_count; v++) {
            vec3 vertex = {VERTEX_TO_FLOAT(model->vertex_x[v]),
                           VERTEX_TO_FLOAT(model->vertex_y[v]),
                           VERTEX_TO_FLOAT(model->vertex_z[v])};

            vec3 world = {0};
            glm_mat4_mulv3(model->transform, vertex, 1, world);

            int bx = FLOAT_TO_VERTEX(world[0]);
            int by = FLOAT_TO_VERTEX(world[1]);
            int bz = FLOAT_TO_VERTEX(world[2]);

            baked_x[v] = (int16_t)bx;
            baked_y[v] = (int16_t)by;
            baked_z[v] = (int16_t)bz;

            if (bx < min_x) min_x = bx;
            if (bx > max_x) max_x = bx;
            if (by < min_y) min_y = by;
            if (by > max_y) max_y = by;
            if (bz < min_z) min_z = bz;
            if (bz > max_z) max_z = bz;
        }

        model->vertex_x = baked_x;
        model->vertex_y = baked_y;
        model->vertex_z = baked_z;

        model->min_x = min_x;
        model->min_y = min_y;
        model->min_z = min_z;
        model->max_x = max_x;
        model->max_y = max_y;
        model->max_z = max_z;

        glm_mat4_identity(model->transform);
        model->transform_state = 0;

        baked_models[baked_count++] = model;
    }

    // the old arena was read above (idempotent re-bake); safe to drop now
    free(gl_object_bake_arena);
    gl_object_bake_arena = arena;

#ifdef __vita__
    // build the object buffers in cached RAM instead of CDRAM
    vglUseVram(GL_FALSE);
#endif

    // group by merge-class so same-class scenery packs contiguously and the run merger fuses whole class groups
    for (int i = 0; i < baked_count; i++) {
        game_model_gl_classify(baked_models[i], NULL);
    }

    for (int i = 1; i < baked_count; i++) {
        GameModel *m = baked_models[i];
        int key = (m->gl_all_back_only << 1) | m->gl_all_front_only;
        int j = i - 1;

        while (j >= 0) {
            GameModel *p = baked_models[j];
            int pkey = (p->gl_all_back_only << 1) | p->gl_all_front_only;

            if (pkey <= key) {
                break;
            }

            baked_models[j + 1] = baked_models[j];
            j--;
        }

        baked_models[j + 1] = m;
    }

    game_model_gl_buffer_models(&mud->scene->gl_object_buffers,
                                &mud->scene->gl_object_buffer_length,
                                baked_models, baked_count, 0, 0);

#ifdef __vita__
    vglUseVram(GL_TRUE);
#endif
}

#ifdef MUD_REGION_ASYNC
// off-thread scenery bake: the worker bakes into a private arena with copy models and deferred (GL-less) buffers,
// touching no live model; the render thread realizes the buffers and re-points the live models between frames

static mudclient *region_bake_mud;
#ifdef __vita__
static SceUID region_bake_thread_id = -1;
#else
static pthread_t region_bake_thread;
#endif

static void mudclient_gl_bake_worker(void) {
    mudclient *mud = region_bake_mud;
    int16_t *slice = mud->bake_next_arena;

    for (int i = 0; i < mud->bake_next_count; i++) {
        GameModel *live = mud->bake_live_models[i];

        mat4 xf;
        game_model_gl_bake_transform(live, xf);

        int vertex_count = live->vertex_count;
        int16_t *baked_x = slice;
        int16_t *baked_y = slice + vertex_count;
        int16_t *baked_z = slice + vertex_count * 2;
        slice += (size_t)vertex_count * 3;

        int min_x = 999999, min_y = 999999, min_z = 999999;
        int max_x = -999999, max_y = -999999, max_z = -999999;

        for (int v = 0; v < vertex_count; v++) {
            vec3 vertex = {VERTEX_TO_FLOAT(live->vertex_x[v]),
                           VERTEX_TO_FLOAT(live->vertex_y[v]),
                           VERTEX_TO_FLOAT(live->vertex_z[v])};
            vec3 world = {0};
            glm_mat4_mulv3(xf, vertex, 1, world);

            int bx = FLOAT_TO_VERTEX(world[0]);
            int by = FLOAT_TO_VERTEX(world[1]);
            int bz = FLOAT_TO_VERTEX(world[2]);

            baked_x[v] = (int16_t)bx;
            baked_y[v] = (int16_t)by;
            baked_z[v] = (int16_t)bz;

            if (bx < min_x) min_x = bx;
            if (bx > max_x) max_x = bx;
            if (by < min_y) min_y = by;
            if (by > max_y) max_y = by;
            if (bz < min_z) min_z = bz;
            if (bz > max_z) max_z = bz;
        }

        // a shallow copy aliases the live model's face/normal arrays (read only); only vertex_x/y/z point at the
        // private baked arena
        GameModel *copy = game_model_copy(live);
        copy->vertex_x = baked_x;
        copy->vertex_y = baked_y;
        copy->vertex_z = baked_z;
        copy->min_x = min_x;
        copy->min_y = min_y;
        copy->min_z = min_z;
        copy->max_x = max_x;
        copy->max_y = max_y;
        copy->max_z = max_z;
        glm_mat4_identity(copy->transform);
        copy->transform_state = 0;

        mud->bake_next_copies[i] = copy;
    }

    // group by merge-class before packing so runs fuse across whole class groups (only buffer order changes; draw
    // order is re-derived every frame). classify() is a pure CPU face-scan; the live and copy arrays are permuted identically so bake_finish's index pairing stays correct
    {
        int n = mud->bake_next_count;
        int *keys = malloc(sizeof(int) * (n > 0 ? n : 1));

        if (keys != NULL) {
            for (int i = 0; i < n; i++) {
                GameModel *c = mud->bake_next_copies[i];
                game_model_gl_classify(c, NULL);
                keys[i] = (c->gl_all_back_only << 1) | c->gl_all_front_only;
            }

            for (int i = 1; i < n; i++) {
                int key = keys[i];
                GameModel *copy = mud->bake_next_copies[i];
                GameModel *live = mud->bake_live_models[i];
                int j = i - 1;

                while (j >= 0 && keys[j] > key) {
                    keys[j + 1] = keys[j];
                    mud->bake_next_copies[j + 1] = mud->bake_next_copies[j];
                    mud->bake_live_models[j + 1] = mud->bake_live_models[j];
                    j--;
                }

                keys[j + 1] = key;
                mud->bake_next_copies[j + 1] = copy;
                mud->bake_live_models[j + 1] = live;
            }

            free(keys);
        }
    }

    game_model_gl_buffer_models(&mud->bake_next_buffers,
                                &mud->bake_next_buffer_length,
                                mud->bake_next_copies, mud->bake_next_count, 0,
                                1); // deferred: no GL on the worker

    // new object shadows change terrain lighting; patch the live terrain mirrors here (CPU only, the render thread
    // reads the VBOs)
    world_gl_update_terrain_patch(mud->world);

    mud->region_bake_done = 1;
}

#ifdef __vita__
static int region_bake_thread_entry(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    mudclient_gl_bake_worker();
    return 0;
}
#else
static void *region_bake_thread_entry(void *arg) {
    (void)arg;
    mudclient_gl_bake_worker();
    return NULL;
}
#endif

static void mudclient_gl_bake_join(void) {
#ifdef __vita__
    if (region_bake_thread_id >= 0) {
        sceKernelWaitThreadEnd(region_bake_thread_id, NULL, NULL);
        sceKernelDeleteThread(region_bake_thread_id);
        region_bake_thread_id = -1;
    }
#else
    pthread_join(region_bake_thread, NULL);
#endif
}

// free an in-flight/finished bake's private resources without applying it
static void mudclient_gl_bake_discard(mudclient *mud) {
    for (int i = 0; i < mud->bake_next_count; i++) {
        if (mud->bake_next_copies != NULL && mud->bake_next_copies[i] != NULL) {
            free(mud->bake_next_copies[i]); // shell only; arrays are aliased
        }
    }

    for (int i = 0; i < mud->bake_next_buffer_length; i++) {
        vertex_buffer_gl_destroy(mud->bake_next_buffers[i]);
        free(mud->bake_next_buffers[i]);
    }

    free(mud->bake_next_buffers);
    free(mud->bake_next_copies);
    free(mud->bake_live_models);
    free(mud->bake_next_arena);

    mud->bake_next_buffers = NULL;
    mud->bake_next_buffer_length = 0;
    mud->bake_next_copies = NULL;
    mud->bake_live_models = NULL;
    mud->bake_next_arena = NULL;
    mud->bake_next_count = 0;
    mud->region_bake_state = 0;
}

// join any in-flight bake and drop it
void mudclient_gl_bake_cancel(mudclient *mud) {
    if (mud->region_bake_state != 1) {
        return;
    }

    mudclient_gl_bake_join();
    mudclient_gl_bake_discard(mud);
}

// start a background bake of the current scenery object set
static void mudclient_gl_bake_start(mudclient *mud) {
    if (mud->region_bake_state != 0 || mud->region_load_state != 0) {
        return; // one background worker at a time
    }

    int total_vertices = 0;
    int count = 0;

    for (int i = 0; i < mud->object_count; i++) {
        GameModel *model = mud->objects[i].model;

        if (model == NULL || mud->objects[i].id == WINDMILL_SAILS_ID) {
            continue;
        }

        total_vertices += model->vertex_count;
        count++;
    }

    if (total_vertices == 0 || count == 0) {
        return;
    }

    mud->bake_next_arena =
        malloc((size_t)total_vertices * 3 * sizeof(int16_t));
    mud->bake_next_copies = calloc(count, sizeof(GameModel *));
    mud->bake_live_models = malloc(count * sizeof(GameModel *));

    if (mud->bake_next_arena == NULL || mud->bake_next_copies == NULL ||
        mud->bake_live_models == NULL) {
        free(mud->bake_next_arena);
        free(mud->bake_next_copies);
        free(mud->bake_live_models);
        mud->bake_next_arena = NULL;
        mud->bake_next_copies = NULL;
        mud->bake_live_models = NULL;
        return;
    }

    int idx = 0;
    for (int i = 0; i < mud->object_count; i++) {
        GameModel *model = mud->objects[i].model;

        if (model == NULL || mud->objects[i].id == WINDMILL_SAILS_ID) {
            continue;
        }

        mud->bake_live_models[idx++] = model;
    }

    mud->bake_next_count = count;
    mud->bake_next_buffers = NULL;
    mud->bake_next_buffer_length = 0;
    mud->region_bake_done = 0;
    region_bake_mud = mud;

#ifdef __vita__
    region_bake_thread_id = sceKernelCreateThread(
        "rsc_bake", region_bake_thread_entry, 0x10000100, 512 * 1024, 0,
        SCE_KERNEL_CPU_MASK_USER_1, NULL);

    if (region_bake_thread_id < 0) {
        mudclient_gl_bake_discard(mud);
        return;
    }

    sceKernelStartThread(region_bake_thread_id, 0, NULL);
#else
    if (pthread_create(&region_bake_thread, NULL, region_bake_thread_entry,
                       NULL) != 0) {
        mudclient_gl_bake_discard(mud);
        return;
    }
#endif

    mud->region_bake_state = 1;
}

// render thread: realize the worker's deferred buffers and re-point the live
// models to the freshly baked geometry, between frames
static void mudclient_gl_bake_finish(mudclient *mud) {
    if (mud->region_bake_state != 1 || !mud->region_bake_done) {
        return;
    }

    mudclient_gl_bake_join();

#ifdef __vita__
    vglUseVram(GL_FALSE);
#endif
    for (int i = 0; i < mud->bake_next_buffer_length; i++) {
        game_model_gl_realize_buffer(mud->bake_next_buffers[i], 0);
    }
#ifdef __vita__
    vglUseVram(GL_TRUE);
#endif

    for (int i = 0; i < mud->bake_next_count; i++) {
        GameModel *live = mud->bake_live_models[i];
        GameModel *copy = mud->bake_next_copies[i];

        live->vertex_x = copy->vertex_x; // into the new arena
        live->vertex_y = copy->vertex_y;
        live->vertex_z = copy->vertex_z;
        live->min_x = copy->min_x;
        live->min_y = copy->min_y;
        live->min_z = copy->min_z;
        live->max_x = copy->max_x;
        live->max_y = copy->max_y;
        live->max_z = copy->max_z;
        live->gl_buffer = copy->gl_buffer;
        live->gl_vbo_offset = copy->gl_vbo_offset;
        live->gl_ebo_offset = copy->gl_ebo_offset;
        live->gl_ebo_length = copy->gl_ebo_length;
        live->gl_noclip_ebo_length = copy->gl_noclip_ebo_length;
        live->gl_clip_ebo_offset = copy->gl_clip_ebo_offset;
        live->gl_clip_ebo_length = copy->gl_clip_ebo_length;
        glm_mat4_identity(live->transform);
        live->transform_state = 0;

        free(copy); // shell only; arrays are aliased to the live model
    }

    // swap in the new object buffers + arena
    for (int i = 0; i < mud->scene->gl_object_buffer_length; i++) {
        vertex_buffer_gl_destroy(mud->scene->gl_object_buffers[i]);
        free(mud->scene->gl_object_buffers[i]);
    }

    free(mud->scene->gl_object_buffers);
    mud->scene->gl_object_buffers = mud->bake_next_buffers;
    mud->scene->gl_object_buffer_length = mud->bake_next_buffer_length;

    free(gl_object_bake_arena);
    gl_object_bake_arena = mud->bake_next_arena;

    // upload the terrain-lighting mirror the worker patched (GL, small)
    world_gl_update_terrain_flush(mud->world);

    free(mud->bake_next_copies);
    free(mud->bake_live_models);

    mud->bake_next_buffers = NULL;
    mud->bake_next_buffer_length = 0;
    mud->bake_next_copies = NULL;
    mud->bake_live_models = NULL;
    mud->bake_next_arena = NULL;
    mud->bake_next_count = 0;
    mud->region_bake_state = 0;
}
#else
void mudclient_gl_bake_cancel(mudclient *mud) { (void)mud; }
#endif // MUD_REGION_ASYNC
#endif // RENDER_GL

// TODO prepare entity sprites
void mudclient_draw_entity_sprites(mudclient *mud) {
    scene_reduce_sprites(mud->scene, mud->scene_sprite_count);

    mud->scene_sprite_count = 0;

    for (int i = 0; i < mud->player_count; i++) {
        GameCharacter *player = mud->players[i];

        if (player->bottom_colour == 255) {
            continue;
        }

        int x = player->current_x;
        int y = player->current_y;
        int elevation = -world_get_elevation(mud->world, x, y);

        int sprite_id = scene_add_sprite(mud->scene, 5000 + i, x, elevation, y,
                                         145, 220, i + PLAYER_FACE_TAG);

        mud->scene_sprite_count++;

        if (player == mud->local_player) {
            scene_set_local_player(mud->scene, sprite_id);
        }

        if (player->current_animation == 8) {
            scene_set_sprite_translate_x(mud->scene, sprite_id, -30);
        } else if (player->current_animation == 9) {
            scene_set_sprite_translate_x(mud->scene, sprite_id, 30);
        }
    }

    for (int i = 0; i < mud->player_count; i++) {
        GameCharacter *player = mud->players[i];

        if (player->projectile_range > 0) {
            GameCharacter *character = NULL;

            if (player->attacking_npc_server_index != -1) {
                character =
                    mud->npcs_server[player->attacking_npc_server_index];
            } else if (player->attacking_player_server_index != -1) {
                character =
                    mud->player_server[player->attacking_player_server_index];
            }

            if (character != NULL) {
                int sx = player->current_x;
                int sy = player->current_y;
                int selev = -world_get_elevation(mud->world, sx, sy) - 110;
                int dx = character->current_x;
                int dy = character->current_y;

                /*
                 * Original game incorrectly uses the height of unicorns
                 * for players here, match it.
                 */
                int target_height =
                    player->attacking_npc_server_index != -1
                        ? game_data.npcs[character->npc_id].height
                        : game_data.npcs[0].height;

                int delev = -world_get_elevation(mud->world, dx, dy) -
                            (target_height / 2);

                int rx =
                    (sx * player->projectile_range +
                     dx * (PROJECTILE_RANGE_MAX - player->projectile_range)) /
                    PROJECTILE_RANGE_MAX;

                int rz = (selev * player->projectile_range +
                          delev * (PROJECTILE_RANGE_MAX -
                                   player->projectile_range)) /
                         PROJECTILE_RANGE_MAX;

                int ry =
                    (sy * player->projectile_range +
                     dy * (PROJECTILE_RANGE_MAX - player->projectile_range)) /
                    PROJECTILE_RANGE_MAX;

                scene_add_sprite(mud->scene,
                                 mud->sprite_projectile +
                                     player->incoming_projectile_sprite,
                                 rx, rz, ry, 32, 32, 0);

                mud->scene_sprite_count++;
            }
        }
    }

    for (int i = 0; i < mud->npc_count; i++) {
        GameCharacter *npc = mud->npcs[i];

        int x = npc->current_x;
        int y = npc->current_y;
        int elevation = -world_get_elevation(mud->world, x, y);

        int sprite_id = scene_add_sprite(mud->scene, 20000 + i, x, elevation, y,
                                         game_data.npcs[npc->npc_id].width,
                                         game_data.npcs[npc->npc_id].height,
                                         i + NPC_FACE_TAG);

        mud->scene_sprite_count++;

        if (npc->current_animation == 8) {
            scene_set_sprite_translate_x(mud->scene, sprite_id, -30);
        } else if (npc->current_animation == 9) {
            scene_set_sprite_translate_x(mud->scene, sprite_id, 30);
        }
    }

    for (int i = 0; i < mud->ground_item_count; i++) {
        int x = mud->ground_items[i].x * MAGIC_LOC + 64;
        int y = mud->ground_items[i].y * MAGIC_LOC + 64;
        int id = mud->ground_items[i].id;
        int elevation =
            -world_get_elevation(mud->world, x, y) - mud->ground_items[i].z;

        scene_add_sprite(mud->scene, 40000 + id, x, elevation, y, 96, 64,
                         i + GROUND_ITEM_FACE_TAG);

        mud->scene_sprite_count++;
    }

    for (int i = 0; i < mud->magic_bubble_count; i++) {
        int x = mud->magic_bubbles[i].x * MAGIC_LOC + 64;
        int y = mud->magic_bubbles[i].y * MAGIC_LOC + 64;
        int type = mud->magic_bubbles[i].type;
        int height = type == 0 ? 256 : 64;

        scene_add_sprite(mud->scene, 50000 + i, x,
                         -world_get_elevation(mud->world, x, y), y, 128, height,
                         i + 50000);

        mud->scene_sprite_count++;
    }
}

void mudclient_draw_game(mudclient *mud) {
#ifdef RENDER_3DS_GL
    mudclient_3ds_gl_frame_start(mud, 1);
#endif

#ifdef MUD_REGION_ASYNC
    // fold a finished background build into the cache without waiting for a
    // crossing
    mudclient_region_promote_build(mud);

    // spread a prebuilt region's GL upload across approach frames
    mudclient_region_realize_step(mud);

    // an evicted region frees a slice of its models per frame
    if (mud->region_old_world != NULL) {
        mud->region_old_free_index = world_free_models_step(
            mud->region_old_world, mud->region_old_free_index, 64);

        if (mud->region_old_free_index < 0) {
            free(mud->region_old_world);
            mud->region_old_world = NULL;
        }
    }
#endif

#ifdef RENDER_GL
    // deferred from SERVER_REGION_OBJECTS: the first request bakes at once, follow-up packets merge into one bake
    // after the cooldown
#ifdef MUD_REGION_ASYNC
    // apply a finished off-thread bake (cheap: realize + re-point, between
    // frames)
    mudclient_gl_bake_finish(mud);

    // drain the coalesced wall and ground-item rebuilds deferred by the crossing packet burst: at most one whole-set
    // GPU rebuild per frame, before the scene draws
    if (mud->gl_wall_update_pending) {
        mud->gl_wall_update_pending = 0;
        mudclient_gl_update_wall_models(mud);
    }

    if (mud->gl_ground_item_update_pending) {
        mud->gl_ground_item_update_pending = 0;
        mudclient_update_ground_item_models(mud);
    }

    // start one when the object set changed and no worker is busy
    if (mud->gl_region_bake_pending && mud->region_bake_state == 0 &&
        mud->region_load_state == 0) {
        mud->gl_region_bake_pending = 0;

        // bake_start spawns the worker (also patches terrain lighting off thread); bake_finish flushes it. no
        // main-thread lighting pass
        mudclient_gl_bake_start(mud);
    }
#else
    if (mud->gl_region_bake_pending) {
        int bake_now = get_ticks();

        if (mud->gl_region_bake_last_ms == 0 ||
            bake_now - mud->gl_region_bake_last_ms >= 1000) {
            mud->gl_region_bake_pending = 0;
            mud->gl_region_bake_last_ms = bake_now;

            mudclient_gl_bake_region_objects(mud);
            world_gl_update_terrain_buffers(mud->world);
        }
    }
#endif
#endif

    if (mud->death_screen_timeout != 0) {
        surface_fade_to_black(mud->surface);

        surface_draw_string_centre(
            mud->surface, "Oh dear! You are dead...", mud->surface->width / 2,
            (mud->surface->height - 12) / 2, FONT_BOLD_24, RED);

        mudclient_draw_chat_message_tabs(mud);

        surface_draw(mud->surface);
        return;
    }

    if (mud->show_appearance_change) {
        mudclient_draw_appearance_panel(mud);
        return;
    }

    if (mud->is_sleeping) {
        mudclient_draw_sleep(mud);
        return;
    }

    if (!mud->world->player_alive) {
        // world-entry: the local player isn't spawned yet while region data streams in; present a cleared loading
        // frame instead of returning without drawing
        surface_black_screen(mud->surface);
        surface_draw_string_centre(mud->surface, "Please wait - Loading...",
                                   mud->surface->width / 2,
                                   mud->surface->height / 2, FONT_BOLD_24, WHITE);
        surface_draw(mud->surface);
        return;
    }

    {
        // roof visibility: apply the adds/removes only when the state changes (walk under or out from a roof, change
        // floor, toggle the option); a section reload resets gl_roof_scene_state to force a re-apply
        int roofs_shown =
            mud->options->show_roofs && mud->last_plane_index == 0 &&
            !world_is_under_roof(mud->world, mud->local_player->current_x,
                                 mud->local_player->current_y);

        if (mud->options->show_roofs) {
            mud->fog_of_war = roofs_shown ? 0 : 1;
        }

        int desired_state = 1 + (mud->last_plane_index * 2) + roofs_shown;

        if (mud->gl_roof_scene_state != desired_state) {
            mud->gl_roof_scene_state = desired_state;

            for (int i = 0; i < TERRAIN_COUNT; i++) {
                scene_remove_model(
                    mud->scene,
                    mud->world->roof_models[mud->last_plane_index][i]);

                if (mud->last_plane_index == 0) {
                    scene_remove_model(mud->scene,
                                       mud->world->wall_models[1][i]);
                    scene_remove_model(mud->scene,
                                       mud->world->roof_models[1][i]);
                    scene_remove_model(mud->scene,
                                       mud->world->wall_models[2][i]);
                    scene_remove_model(mud->scene,
                                       mud->world->roof_models[2][i]);
                }

                if (roofs_shown) {
                    scene_add_model(
                        mud->scene,
                        mud->world->roof_models[mud->last_plane_index][i]);

                    scene_add_model(mud->scene, mud->world->wall_models[1][i]);
                    scene_add_model(mud->scene, mud->world->roof_models[1][i]);
                    scene_add_model(mud->scene, mud->world->wall_models[2][i]);
                    scene_add_model(mud->scene, mud->world->roof_models[2][i]);
                }
            }
        }
    }


    if (!mud->options->lowmem) {
        mudclient_animate_objects(mud);
    }

    mudclient_draw_entity_sprites(mud);

    mud->surface->interlace = 0;

#if !defined(RENDER_GL) && !defined(RENDER_3DS_GL)
    // in-game the later surface_black_screen overdraws this clear; sw renderer still needs it for interlace fields
    surface_black_screen(mud->surface);
#endif

    mud->surface->interlace = mud->options->interlace;

    /* flickering lights in dungeons */
    if (mud->last_plane_index == 3 && mud->options->flicker) {
        int ambience = 40 + ((float)rand() / (float)RAND_MAX) * 3;
        int diffuse = 40 + ((float)rand() / (float)RAND_MAX) * 7;

        scene_set_light(mud->scene, ambience, diffuse, -50, -10, -50);
    }

    mud->action_bubble_count = 0;
    mud->received_messages_count = 0;
    mud->health_bar_count = 0;
    mud->overworld_text_count = 0;

    if (mud->settings_camera_auto && !mud->fog_of_war) {
        mudclient_auto_rotate_camera(mud);
    }

    if (mud->options->zoom_camera) {
        int clip_far =
            (int)((2400.0f / ZOOM_OUTDOORS) * (float)mud->camera_zoom);

        mud->scene->clip_far_3d = clip_far;
        mud->scene->clip_far_2d = clip_far;
        mud->scene->fog_z_distance = clip_far - 100;
    } else {
        mud->scene->clip_far_3d = 2400;
        mud->scene->clip_far_2d = 2400;
        mud->scene->fog_z_distance = 2300;
    }

    if (mud->options->interlace) {
        mud->scene->clip_far_3d -= 200;
        mud->scene->clip_far_2d -= 200;
        mud->scene->fog_z_distance -= 200;
    }

    /* TODO this should probably be tied with FOV instead */
#ifdef RENDER_SW
    /*
     * Keep the fog roughly "feeling the same" as the vanilla
     * 512x346 client when resized beyond that.
     */
    if (mud->game_height > MUD_VANILLA_HEIGHT) {
        int clip_far = mud->scene->clip_far_3d /
                       (MUD_VANILLA_HEIGHT / (float)mud->game_height);

        mud->scene->clip_far_3d = clip_far;
        mud->scene->clip_far_2d = clip_far;
        mud->scene->fog_z_distance = clip_far - 100;
    }
#endif

    if (!mud->options->fog_of_war) {
        mud->scene->clip_far_3d = 20000;
        mud->scene->clip_far_2d = 20000;
        mud->scene->fog_z_distance = 20000;
    }

    int camera_x = mud->camera_auto_rotate_player_x + mud->camera_rotation_x;
    int camera_z = mud->camera_auto_rotate_player_y + mud->camera_rotation_y;

    int offset_y = 0;

    int is_touch = mudclient_is_touch(mud);

    /* centres the camera for the smaller FOV */
    /* TODO could be an option */
    if (is_touch) {
        offset_y = 100;
    } else if (MUD_IS_COMPACT) {
        offset_y = 75;
    }

    scene_set_camera(
        mud->scene, camera_x,
        -world_get_elevation(mud->world, camera_x, camera_z) - offset_y,
        camera_z, 912, (mud->camera_rotation * 4), 0, (mud->camera_zoom * 2));

    surface_black_screen(mud->surface);

#if defined(RENDER_GL) && !defined(EMSCRIPTEN)
    /*if (mud->options->anti_alias) {
        glEnable(GL_MULTISAMPLE);
    } else {
        glDisable(GL_MULTISAMPLE);
    }*/
#endif

    scene_render(mud->scene);

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    surface_gl_draw(mud->surface, GL_DEPTH_DISABLED);
#endif

    mudclient_draw_overhead(mud);

    /* draw the animated X sprite when clicking */
    if (mud->mouse_click_x_step > 0) {
        surface_draw_sprite(
            mud->surface, mud->mouse_click_x_x - 8, mud->mouse_click_x_y - 8,
            mud->sprite_media + 14 + ((24 - mud->mouse_click_x_step) / 6));
    } else if (mud->mouse_click_x_step < 0) {
        surface_draw_sprite(
            mud->surface, mud->mouse_click_x_x - 8, mud->mouse_click_x_y - 8,
            mud->sprite_media + 18 + ((24 + mud->mouse_click_x_step) / 6));
    }

    if (mud->options->display_fps) {
        int offset_x = mud->is_in_wilderness ? 70 : 0;

        char fps[17] = {0};
        sprintf(fps, "Fps: %d", mud->fps);

        surface_draw_string(mud->surface, fps,
                            is_touch ? 9 + offset_x
                                     : mud->surface->width - 62 - offset_x,
                            mud->surface->height - 22, FONT_BOLD_12, YELLOW);
    }

#ifndef REVISION_177
    if (mud->system_update != 0) {
        int seconds = mud->system_update / 50;
        int minutes = seconds / 60;

        seconds %= 60;

        char formatted_update[41] = {0};

        sprintf(formatted_update, "System update in: %d:%02d", minutes,
                seconds);

#if defined(__vita__) && defined(RENDER_GL)
        // centre on the real screen; 256 is MUD_VANILLA_WIDTH/2
        int system_update_x = mud->surface->width / 2;
#else
        int system_update_x = 256;
#endif

        surface_draw_string_centre(mud->surface, formatted_update,
                                   system_update_x, mud->game_height - 19,
                                   FONT_BOLD_12, YELLOW);
    }
#endif

    if (!mud->loading_area) {
        int wilderness_depth = mudclient_get_wilderness_depth(mud);

        mud->is_in_wilderness = wilderness_depth > 0;

        if (mud->is_in_wilderness) {
            int x = is_touch ? 29 : mud->surface->width - 59;

            surface_draw_sprite(mud->surface, x, mud->surface->height - 68,
                                mud->sprite_media + 13);

            surface_draw_string_centre(mud->surface, "Wilderness", x + 12,
                                       mud->surface->height - 32, FONT_BOLD_12,
                                       YELLOW);

            int wilderness_level = 1 + (wilderness_depth / 6);

            char formatted_level[19] = {0};
            sprintf(formatted_level, "Level: %d", wilderness_level);

            surface_draw_string_centre(mud->surface, formatted_level, x + 12,
                                       mud->surface->height - 19, FONT_BOLD_12,
                                       YELLOW);

            if (mud->show_wilderness_warning == 0) {
                mud->show_wilderness_warning = 2;
            }
        }

        if (mud->options->wilderness_warning &&
            mud->show_wilderness_warning == 0 && wilderness_depth > -10 &&
            wilderness_depth <= 0) {
            mud->show_wilderness_warning = 1;
        }
    }

    mudclient_draw_chat_message_tabs_panel(mud);

    mudclient_draw_ui(mud);

    mud->surface->draw_string_shadow = 0;
    mudclient_draw_chat_message_tabs(mud);

    if (mud->options->status_bars && mudclient_is_touch(mud)) {
        mud->surface->draw_string_shadow = 1;
        mudclient_draw_status_bars(mud);
    }

#ifdef RENDER_GL
    scene_gl_render_transparent_models(mud->scene);
#elif defined(RENDER_3DS_GL)
    scene_3ds_gl_render_transparent_models(mud->scene);
#endif

#if defined(__vita__) && defined(RENDER_GL)
    // draw the virtual cursor on top of the world frame
    vita_draw_cursor(mud->surface, mud->mouse_x, mud->mouse_y);
#endif

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
    surface_gl_draw(mud->surface, GL_DEPTH_ENABLED);
    surface_gl_reset_context(mud->surface);
#else
    surface_draw(mud->surface);
#endif

#if defined(_3DS) && defined(RENDER_SW)
    gfxFlushBuffers();
    gfxSwapBuffers();
#endif
}

void mudclient_draw(mudclient *mud) {
#ifdef EMSCRIPTEN
    if (get_ticks() - last_canvas_check > 1000) {
        if (can_resize() && (get_window_width() != mud->game_width ||
                             get_window_height() != mud->game_height)) {
            mudclient_on_resize(mud);
        }

        last_canvas_check = get_ticks();
    }
#endif

    if (mud->error_loading_data) {
        /* TODO draw error */
        // mud_log("ERROR LOADING DATA\n");
        return;
    }


#ifdef WII
    draw_background(mud->framebuffer, 0);
#endif

#ifdef RENDER_GL
    // skip this depth clear in-game; login/loading screens still need it
    if (mud->logged_in != 1) {
        glClear(GL_DEPTH_BUFFER_BIT);
    }
#endif

    if (mud->logged_in == 0) {
        mud->surface->draw_string_shadow = 0;
        mudclient_draw_login_screens(mud);
    } else if (mud->logged_in == 1) {
        mud->surface->draw_string_shadow = 1;

        mudclient_draw_game(mud);

#ifdef RENDER_GL
#if defined(__vita__)
        // Vita: present the world frame with vglSwapBuffers; vitaGL owns the display
        vglSwapBuffers(GL_TRUE);
#elif defined(SDL12)
        SDL_GL_SwapBuffers();
#else
        SDL_GL_SwapWindow(mud->gl_window);
#endif
#elif defined(RENDER_3DS_GL)
        mudclient_3ds_gl_frame_end();
#endif
    }
}

#ifdef SDL12
void mudclient_sdl1_on_resize(mudclient *mud, int width, int height) {
    int new_width = width;
    int new_height = height;
#ifdef RENDER_SW
    if ((SDL_SetVideoMode(width, height, 32, SDL_HWSURFACE | SDL_RESIZABLE)) ==
        NULL) {
        return;
    }
#else
    if ((SDL_SetVideoMode(width, height, 32, SDL_OPENGL | SDL_RESIZABLE)) ==
        NULL) {
        return;
    }
#endif
    mud->game_width = new_width;
    mud->game_height = new_height;

    if (mud->surface != NULL) {
        if (mudclient_is_ui_scaled(mud)) {
            mud->surface->width = new_width / 2;
            mud->surface->height = new_height / 2;
        } else {
            mud->surface->width = new_width;
            mud->surface->height = new_height;
        }

        surface_reset_bounds(mud->surface);
    }

    if (mud->scene != NULL) {
#ifdef RENDER_SW
        free(mud->scene->scanlines);
#endif

        // TODO change 12 to bar height - 1
        scene_set_bounds(mud->scene, new_width, new_height - 12);

#ifdef RENDER_GL
        mudclient_update_fov(mud);
#endif
    }

    mudclient_resize(mud);
}
#endif

void mudclient_on_resize(mudclient *mud) {
    int new_width = MUD_WIDTH;
    int new_height = MUD_HEIGHT;

#if !defined(_3DS) && !defined(WII) && !defined(SDL12)
#ifdef RENDER_GL
    SDL_Window *window = mud->gl_window;
#else
    SDL_Window *window = mud->window;
#endif

#ifdef EMSCRIPTEN
    new_width = get_window_width();
    new_height = get_window_height();
    SDL_SetWindowSize(window, new_width, new_height);
#endif

    SDL_GetWindowSize(window, &new_width, &new_height);
#endif

#ifdef ANDROID
    mudclient_full_width = new_width;
    mudclient_full_height = new_height;

    if (new_width > new_height) {
        new_width =
            roundf(360 * (mudclient_full_width / (float)mudclient_full_height));

        new_height = 360;
    } else {
        new_width = 360;

        new_height =
            roundf(360 * (mudclient_full_height / (float)mudclient_full_width));
    }
#endif

    mud->game_width = new_width;
    mud->game_height = new_height;

    if (mud->surface != NULL) {
        if (mudclient_is_ui_scaled(mud)) {
            mud->surface->width = new_width / 2;
            mud->surface->height = new_height / 2;
        } else {
            mud->surface->width = new_width;
            mud->surface->height = new_height;
        }

        surface_reset_bounds(mud->surface);
    }

    if (mud->scene != NULL) {
#ifdef RENDER_SW
        free(mud->scene->scanlines);

        if (mudclient_is_ui_scaled(mud)) {
            new_width /= 2;
            new_height /= 2;
        }
#endif

        // TODO change 12 to bar height - 1
        scene_set_bounds(mud->scene, new_width, new_height - 12);

#ifdef RENDER_GL
        mudclient_update_fov(mud);
#endif
    }

    mudclient_resize(mud);
}


int mudclient_is_skin_colour_unlocked(mudclient *mud, int index) {
    if (index < 0 || index >= PLAYER_SKIN_COLOUR_COUNT) {
        return 0;
    }

    // five original RSC skins always available; everything past them is an OpenRSC unlockable, granted via 250
    if (index < PLAYER_SKIN_COLOUR_BASE_COUNT) {
        return 1;
    }

#ifndef REVISION_177
    return mud->protocol_custom && mud->orsc_unlocked_skin_known &&
           mud->orsc_unlocked_skin[index];
#else
    (void)mud;
    return 0;
#endif
}

int mudclient_is_touch(mudclient *mud) {
    (void)(mud);

#ifdef ANDROID
    return 1; // TODO maybe still make this toggleable
#elif defined(__vita__)
    // enables tap-to-move, hold-for-menu, drag-to-rotate, pinch-zoom, and the on-screen keyboard on text-field focus
    return 1;
#elif defined(EMSCRIPTEN)
    return browser_is_touch();
#else
    return 0;
#endif
}

// TODO open_keyboard
void mudclient_trigger_keyboard(mudclient *mud, char *text, int is_password,
                                int x, int y, int width, int height, int font,
                                int is_centred) {
    (void)mud;
    (void)text;
    (void)is_password;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)font;
    (void)is_centred;
#ifdef ANDROID
    SDL_StartTextInput();
#elif defined(__vita__)
    // open the system IME, seeded with the field's current text
    vita_ime_open(is_password ? "Enter password" : "Enter text", text,
                  is_password, text != NULL ? (int)strlen(text) : 0);
#elif defined(EMSCRIPTEN)
    int is_scaled = mudclient_is_ui_scaled(mud);

    if (is_scaled) {
        x *= 2;
        y *= 2;
    }

    browser_trigger_keyboard(text, is_password, x, y, width, height, font,
                             is_centred, is_scaled);
#endif
}

#ifdef __vita__
// write error-level messages through stderr (redirected to error.log); flush each line to the SD card
static void vita_log_to_file(void *userdata, int category,
                             SDL_LogPriority priority, const char *message) {
    (void)userdata;
    (void)category;
    (void)priority;
    fputs(message, stderr);
    fputc('\n', stderr);
    fflush(stderr);
    extern int sceIoSync(const char *device, int flag);
    sceIoSync("ux0:", 0);
}
#endif

void mudclient_run(mudclient *mud) {
#ifdef WII
    draw_background(mud->framebuffers[0], 1);
    draw_background(mud->framebuffers[1], 1);

    mud->active_framebuffer ^= 1;
    mud->framebuffer = mud->framebuffers[mud->active_framebuffer];
#endif

    if (mud->loading_step == 1) {
        mud->loading_step = 2;
        mudclient_load_jagex(mud);
        mudclient_start_game(mud);
        mud->loading_step = 0;

#ifdef EMSCRIPTEN
        mudclient_on_resize(mud);
#endif

#if defined(__vita__) && defined(RENDER_GL)
        // apply dynamic_offset to centre the login/character-design panels; HUD and options panels stay at offset 0
        {
            int dynamic_offset_x =
                (mud->surface->width / 2) - (MUD_VANILLA_WIDTH / 2);
            int dynamic_offset_y =
                (mud->surface->height / 2) - (MUD_VANILLA_HEIGHT / 2);

            if (mud->panel_login_welcome != NULL) {
                mud->panel_login_welcome->offset_x = dynamic_offset_x;
                mud->panel_login_welcome->offset_y = dynamic_offset_y;
            }

            if (mud->panel_login_new_user != NULL) {
                mud->panel_login_new_user->offset_x = dynamic_offset_x;
                mud->panel_login_new_user->offset_y = dynamic_offset_y;
            }

            if (mud->panel_login_existing_user != NULL) {
                mud->panel_login_existing_user->offset_x = dynamic_offset_x;
                mud->panel_login_existing_user->offset_y = dynamic_offset_y;
            }

            if (mud->panel_login_worldlist != NULL) {
                mud->panel_login_worldlist->offset_x = dynamic_offset_x;
                mud->panel_login_worldlist->offset_y = dynamic_offset_y;
            }

            if (mud->panel_appearance != NULL) {
                mud->panel_appearance->offset_x = dynamic_offset_x;
                mud->panel_appearance->offset_y = dynamic_offset_y;
            }
        }
#endif
    }

    int timing_index = 0;
    int j = 256;
    int delay = 1;
    int i1 = 0;

    for (int i = 0; i < 10; i++) {
        mud->timings[i] = get_ticks();
    }

    while (mud->stop_timeout >= 0) {
#ifdef WITH_SINGLEPLAYER
        // advance the co-op transport state machines once per frame; no-op when co-op is off
        if (spnet_get_mode() != SPNET_MODE_OFF) {
            spnet_pump();
        }
#endif

        if (mud->stop_timeout > 0) {
            mud->stop_timeout--;

            if (mud->stop_timeout == 0) {
                mudclient_close_connection(mud);
                return;
            }
        }

        int k1 = j;
        int last_delay = delay;

        j = 300;
        delay = 1;

        int time = get_ticks();

        if (mud->timings[timing_index] == 0) {
            j = k1;
            delay = last_delay;
        } else if (time > mud->timings[timing_index]) {
            j = (float)(2560 * mud->target_fps) /
                (float)(time - mud->timings[timing_index]);
        }

        if (j < 25) {
            j = 25;
        }

        if (j > 256) {
            j = 256;
            delay = mud->target_fps - (time - mud->timings[timing_index]) / 10;

            // TODO minimum delay
            if (delay < 10) {
                delay = 10;
            }
        }

#if defined(__vita__) && defined(RENDER_GL)
        // force delay to 1; hardware vsync paces the loop to 60 fps
        int window_ms = time - mud->timings[timing_index];
        delay = 1;
#else
        delay_ticks(delay);
#endif

        mud->timings[timing_index] = time;
        timing_index = (timing_index + 1) % 10;

        if (delay > 1) {
            for (int i = 0; i < 10; i++) {
                if (mud->timings[i] != 0) {
                    mud->timings[i] += delay;
                }
            }
        }

        int k2 = 0;

#if defined(RENDER_GL) || defined(RENDER_3DS_GL)
        // new rendered frame: allow one socket-IO packet tick
        mud->gl_net_io_this_frame = 0;
#endif

        while (i1 < 256) {
            mudclient_poll_events(mud);
            mudclient_handle_inputs(mud);

            i1 += j;

            // TODO magic #
            if (++k2 > 1000) {
                i1 = 0;
                break;
            }
        }

        i1 &= 255;

#ifdef _3DS
        if (!mud->keyboard_open) {
            mudclient_draw(mud);
        }

        mudclient_3ds_flush_audio(mud);

        if (!aptMainLoop()) {
            return;
        }
#else
#if defined(__vita__) && defined(RENDER_GL)
        // sync the vsync interval with the 30/60 fps setting: 1 = 60, 2 = 30
        {
            extern uint32_t vsync_interval;
            vsync_interval = mud->options->fps_60 ? 1 : 2;
        }
#endif
        mudclient_draw(mud);
#endif

#if defined(__vita__) && defined(RENDER_GL)
        // real wall fps over the last 10 frames
        if (window_ms > 0 && window_ms < 10000) {
            mud->fps = (10 * 1000) / window_ms;
        }
#else
        mud->fps = (j * 1000) / (mud->target_fps * 256);
#endif

        mud->mouse_scroll_delta = 0;
    }
}

void mudclient_draw_magic_bubble(mudclient *mud, int x, int y, int width,
                                 int height, int id, float depth) {
    int type = mud->magic_bubbles[id].type;
    int time = mud->magic_bubbles[id].time;

    if (type == 0) {
        /* blue bubble used for teleports */
        int colour = BLUE + time * 5 * 256;

        surface_draw_circle(mud->surface, x + (width / 2), y + (height / 2),
                            20 + time * 2, colour, 255 - time * 5, depth);
    } else if (type == 1) {
        /* red bubble used for telegrab */
        int colour = RED + time * 5 * 256;

        surface_draw_circle(mud->surface, x + (width / 2), y + (height / 2),
                            10 + time, colour, 255 - time * 5, depth);
    }
}

void mudclient_draw_ground_item(mudclient *mud, int x, int y, int width,
                                int height, int id, float depth_top,
                                float depth_bottom) {
    int32_t highlight_colour = highlight_item(id);

    if (highlight_colour != 0 && mud->options->ground_item_text &&
        mud->overworld_text_count < OVERWORLD_TEXT_MAX) {
        struct OverworldText text = {0};

        text.text = game_data.items[id].name;
        text.colour = highlight_colour;
        text.x = x + (width / 2);
        text.y = y - (height / 2);

        mud->overworld_text[mud->overworld_text_count++] = text;
    }

    if (!mud->options->ground_item_models) {
        int picture = game_data.items[id].sprite + mud->sprite_item;
        int mask = game_data.items[id].mask;

        surface_draw_sprite_transform_mask_depth(mud->surface, x, y, width,
                                                 height, picture, mask, 0, 0, 0,
                                                 depth_top, depth_bottom);
    }
}

int mudclient_is_item_equipped(mudclient *mud, int id) {
    for (int i = 0; i < mud->inventory_items_count; i++) {
        if (mud->inventory_item_id[i] == id && mud->inventory_equipped[i]) {
            return 1;
        }
    }

    return 0;
}

int mudclient_get_inventory_count(mudclient *mud, int id) {
    int count = 0;

    for (int i = 0; i < mud->inventory_items_count; i++) {
        if (mud->inventory_item_id[i] == id) {
            if (game_data.items[id].stackable == 1) {
                count++;
            } else {
                count += mud->inventory_item_stack_count[i];
            }
        }
    }

    return count;
}

int mudclient_has_inventory_item(mudclient *mud, int id, int minimum) {
    if (id == FIRE_RUNE_ID &&
        (mudclient_is_item_equipped(mud, FIRE_STAFF_ID) ||
         mudclient_is_item_equipped(mud, FIRE_BATTLESTAFF_ID) ||
         mudclient_is_item_equipped(mud, ENCHANTED_FIRE_BATTLESTAFF_ID))) {
        return 1;
    }

    if (id == WATER_RUNE_ID &&
        (mudclient_is_item_equipped(mud, WATER_STAFF_ID) ||
         mudclient_is_item_equipped(mud, WATER_BATTLESTAFF_ID) ||
         mudclient_is_item_equipped(mud, ENCHANTED_WATER_BATTLESTAFF_ID))) {
        return 1;
    }

    if (id == AIR_RUNE_ID &&
        (mudclient_is_item_equipped(mud, AIR_STAFF_ID) ||
         mudclient_is_item_equipped(mud, AIR_BATTLESTAFF_ID) ||
         mudclient_is_item_equipped(mud, ENCHANTED_AIR_BATTLESTAFF_ID))) {
        return 1;
    }

    if (id == EARTH_RUNE_ID &&
        (mudclient_is_item_equipped(mud, EARTH_STAFF_ID) ||
         mudclient_is_item_equipped(mud, EARTH_BATTLESTAFF_ID) ||
         mudclient_is_item_equipped(mud, ENCHANTED_EARTH_BATTLESTAFF_ID))) {
        return 1;
    }

    return mudclient_get_inventory_count(mud, id) >= minimum;
}

void mudclient_send_logout(mudclient *mud) {
    if (mud->logged_in == 0) {
        return;
    }

    if (mud->combat_timeout > 450) {
        mudclient_show_message(mud, "@cya@You can't logout during combat!",
                               MESSAGE_TYPE_GAME);

        return;
    }

    if (mud->combat_timeout > 0) {
        mudclient_show_message(
            mud, "@cya@You can't logout for 10 seconds after combat",
            MESSAGE_TYPE_GAME);

        return;
    }

    packet_stream_new_packet(mud->packet_stream, CLIENT_LOGOUT);
    packet_stream_send_packet(mud->packet_stream);

    mud->logout_timeout = 1000;
}

void mudclient_play_sound(mudclient *mud, char *name) {
    if (!mud->options->members || mud->settings_sound_disabled ||
        mud->options->lowmem) {
        return;
    }

#ifdef _3DS
    if (mud->_3ds_sound_position != -1) {
        return;
    }
#endif

    char file_name[strlen(name) + 5];
    sprintf(file_name, "%s.pcm", name);

    uint32_t offset = get_data_file_offset(file_name, mud->sound_data);

    if (offset == 0) {
        return;
    }

    uint32_t length = get_data_file_length(file_name, mud->sound_data);

    memset(mud->pcm_out, 0, PCM_LENGTH * sizeof(uint16_t));

    ulaw_to_linear(length, (uint8_t *)mud->sound_data + offset, mud->pcm_out);

#ifdef WII
    // ASND_StopVoice(0);

    ASND_SetVoice(0, VOICE_MONO_16BIT_BE, SAMPLE_RATE, 0, mud->pcm_out,
                  length * 2, 127, 127, NULL);
#elif defined(_3DS)
    mud->_3ds_sound_position = 0;
    mud->_3ds_sound_length = length * 2;
#elif defined(SDL_VERSION_ATLEAST)
#if SDL_VERSION_ATLEAST(2, 0, 4)
    SDL_PauseAudio(0);
    SDL_ClearQueuedAudio(1);
    SDL_QueueAudio(1, mud->pcm_out, length * 2);
#endif
#endif
}

int mudclient_walk_to(mudclient *mud, int start_x, int start_y, int x1, int y1,
                      int x2, int y2, int check_objects, int walk_to_action,
                      int first_step) {
    int steps = world_route(mud->world, start_x, start_y, x1, y1, x2, y2,
                            mud->walk_path_x, mud->walk_path_y, check_objects);

    if (first_step) {
        if (steps == -1) {
            if (walk_to_action) {
                steps = 1;
                mud->walk_path_x[0] = x1;
                mud->walk_path_y[0] = y1;
            } else {
                return 0;
            }
        }
    } else {
        if (steps == -1) {
            return 0;
        }
    }

    steps--;
    start_x = mud->walk_path_x[steps];
    start_y = mud->walk_path_y[steps];
    steps--;

    packet_stream_new_packet(mud->packet_stream,
                             walk_to_action ? CLIENT_WALK_ACTION : CLIENT_WALK);

    packet_stream_put_short(mud->packet_stream, start_x + mud->region_x);
    packet_stream_put_short(mud->packet_stream, start_y + mud->region_y);

    if (walk_to_action && steps == -1 && (start_x + mud->region_x) % 5 == 0) {
        steps = 0;
    }

    for (int i = steps; i >= 0 && i > steps - 25; i--) {
        packet_stream_put_byte(mud->packet_stream,
                               mud->walk_path_x[i] - start_x);

        packet_stream_put_byte(mud->packet_stream,
                               mud->walk_path_y[i] - start_y);
    }

    packet_stream_send_packet(mud->packet_stream);

    mud->mouse_click_x_step = -24;
    mud->mouse_click_x_x = mud->mouse_x;
    mud->mouse_click_x_y = mud->mouse_y;

    return 1;
}

void mudclient_walk_to_action_source(mudclient *mud, int start_x, int start_y,
                                     int dest_x, int dest_y, int action) {
    mudclient_walk_to(mud, start_x, start_y, dest_x, dest_y, dest_x, dest_y, 0,
                      action, 1);
}

void mudclient_walk_to_ground_item(mudclient *mud, int start_x, int start_y,
                                   int dest_x, int dest_y, int walk_to_action) {
    if (mudclient_walk_to(mud, start_x, start_y, dest_x, dest_y, dest_x, dest_y,
                          0, walk_to_action, 0)) {
        return;
    }

    mudclient_walk_to(mud, start_x, start_y, dest_x, dest_y, dest_x, dest_y, 1,
                      walk_to_action, 1);
}

void mudclient_walk_to_wall_object(mudclient *mud, int dest_x, int dest_y,
                                   int direction) {
    if (direction == 0) {
        mudclient_walk_to(mud, mud->local_region_x, mud->local_region_y, dest_x,
                          dest_y - 1, dest_x, dest_y, 0, 1, 1);
    } else if (direction == 1) {
        mudclient_walk_to(mud, mud->local_region_x, mud->local_region_y,
                          dest_x - 1, dest_y, dest_x, dest_y, 0, 1, 1);
    } else {
        mudclient_walk_to(mud, mud->local_region_x, mud->local_region_y, dest_x,
                          dest_y, dest_x, dest_y, 1, 1, 1);
    }
}

void mudclient_walk_to_object(mudclient *mud, int x, int y, int direction,
                              int id) {
    int width = 0;
    int height = 0;

    if (direction == DIR_NORTH || direction == DIR_SOUTH) {
        width = game_data.objects[id].width;
        height = game_data.objects[id].height;
    } else {
        height = game_data.objects[id].width;
        width = game_data.objects[id].height;
    }

    if (game_data.objects[id].type == 2 || game_data.objects[id].type == 3) {
        if (direction == DIR_NORTH) {
            x--;
            width++;
        } else if (direction == DIR_WEST) {
            height++;
        } else if (direction == DIR_SOUTH) {
            width++;
        } else if (direction == DIR_EAST) {
            y--;
            height++;
        }

        mudclient_walk_to(mud, mud->local_region_x, mud->local_region_y, x, y,
                          (x + width) - 1, (y + height) - 1, 0, 1, 1);
    } else {
        mudclient_walk_to(mud, mud->local_region_x, mud->local_region_y, x, y,
                          (x + width) - 1, (y + height) - 1, 1, 1, 1);
    }
}

int mudclient_is_ui_scaled(mudclient *mud) {
#if defined(RENDER_GL) || defined(SDL2)
    return mud->options->ui_scale && mud->game_width >= (MUD_WIDTH * 2) &&
           mud->game_height >= (MUD_HEIGHT * 2);
#else
    (void)mud;

    return 0;
#endif
}

// dialogue-option hotkey level: 0 = off, 1 = number keys 1-5, 2 = keys plus a "(N)" prefix
int mudclient_option_numbers_level(mudclient *mud) {
#ifdef WITH_SINGLEPLAYER
    if (MUD_SP_WIRE(mud)) {
        return mud->options->option_numbers ? 2 : 0;
    }
#endif
    if (mud->protocol_custom) {
        return mud->orsc.want_keyboard_shortcuts;
    }
    return 0;
}

void mudclient_format_number_commas(mudclient *mud, int number, char *dest) {
    if (mud->options->number_commas) {
        format_number_commas(number, dest);
    } else {
        sprintf(dest, "%d", number);
    }
}

void mudclient_format_item_amount(mudclient *mud, int item_amount, char *dest) {
    if (mud->options->condense_item_amounts) {
        format_amount_suffix(item_amount, 1, 0, mud->options->number_commas,
                             dest);
    } else {
        mudclient_format_number_commas(mud, item_amount, dest);
    }
}

// noted-item sprites: note 438 (48x32 backing + item icon inset) or certificate 180 alone; both use a zero mask, both
// from the stock item cache
void mudclient_draw_noted_item(mudclient *mud, int x, int y, int slot_width,
                               int slot_height, int item_id, int inset_y) {
    if (!mud->orsc.want_cert_as_notes) {
        surface_draw_sprite_transform_mask(
            mud->surface, x, y, slot_width - 1, slot_height - 2,
            mud->sprite_item + ORSC_CERTIFICATE_SPRITE, 0, 0, 0, 0);
        return;
    }

    surface_draw_sprite_transform_mask(mud->surface, x, y, slot_width - 1,
                                       slot_height - 2,
                                       mud->sprite_item + ORSC_NOTE_SPRITE, 0, 0,
                                       0, 0);

    if (item_id < 0 || item_id >= game_data.item_count) {
        return;
    }

    surface_draw_sprite_transform_mask(
        mud->surface, x + 7, y + inset_y, 33, 23,
        mud->sprite_item + game_data.items[item_id].sprite,
        game_data.items[item_id].mask, 0, 0, 0);
}

int mudclient_item_noted(mudclient *mud, int inventory_slot) {
#ifdef REVISION_177
    (void)mud;
    (void)inventory_slot;
    return 0;
#else
    // inventory_item_noted[]: set by custom (10010) parses when S_WANT_BANK_NOTES, else 0
    if (inventory_slot < 0 || inventory_slot >= INVENTORY_ITEMS_MAX) {
        return 0;
    }

    return mud->inventory_item_noted[inventory_slot];
#endif
}

void mudclient_item_display_name(mudclient *mud, int item_id, int noted,
                                 char *out, int out_size) {
    int cert_as_notes = 0;

#ifndef REVISION_177
    cert_as_notes = mud->orsc.want_cert_as_notes;
#else
    (void)mud;
#endif

    game_data_item_note_name(item_id, noted, cert_as_notes, out, out_size);
}

int mudclient_get_wilderness_depth(mudclient *mud) {
    int wilderness_depth =
        2203 - (mud->local_region_y + mud->plane_height + mud->region_y);

    if (mud->local_region_x + mud->plane_width + mud->region_x >= 2640) {
        wilderness_depth = -50;
    }

    return wilderness_depth;
}

void mudclient_draw_item(mudclient *mud, int x, int y, int slot_width,
                         int slot_height, int item_id) {
    int certificate_item_id = -1;

    if (mud->options->certificate_items) {
        certificate_item_id = get_certificate_item_id(item_id);
    }

    int offset_x = 0;

    if (certificate_item_id != -1) {
        offset_x = -2;
    }

    surface_draw_item(mud->surface, x + offset_x, y, slot_width, slot_height,
                      item_id);

    if (certificate_item_id != -1) {
        int og_width = ITEM_GRID_SLOT_WIDTH - 1;
        int og_height = ITEM_GRID_SLOT_HEIGHT - 2;

        surface_draw_sprite_transform_mask(
            mud->surface, x + 4 + og_width * 0.125f, y + 2 + og_height * 0.125f,
            og_width * 0.75f, og_height * 0.75f,
            mud->surface->mud->sprite_item +
                game_data.items[certificate_item_id].sprite,
            game_data.items[certificate_item_id].mask, 0, 0, 0);
    }
}
#ifdef WIN9X
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine,
                   int nCmdShow) {
    int argc;
    char **argv;

    argv = (char**)CommandLineToArgvW(GetCommandLineW(), &argc);
#else
int main(int argc, char **argv) {
#endif
#ifdef _3DS
    osSetSpeedupEnable(true);
#endif
    srand(0);

#ifdef __vita__
    // release the co-op transport on any exit path; idempotent
    atexit(spnet_shutdown);

    // create ux0:data/RuneScape/ before the logs
    mkdir("ux0:data/RuneScape", 0777);

    // delete log/cache files earlier builds wrote elsewhere; a missing-file remove() is a no-op
    remove("ux0:data/rsc-sp-bundle.bc");
    remove("ux0:data/rsc-vita-debug.log");
    remove("ux0:data/rsc-vita-stderr.log");
    remove("ux0:data/RuneScape/rsc-vita-debug.log");
    remove("ux0:data/RuneScape/rsc-vita-stderr.log");

    // single log: stderr and error-level SDL_Log/mud_error both go to error.log, unbuffered, truncated each boot
    freopen("ux0:data/RuneScape/error.log", "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);

    SDL_LogSetOutputFunction(vita_log_to_file, NULL);
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_ERROR);
#endif

    init_utility_global();
    init_surface_global();
    init_world_global();
    /*init_packet_stream_global();*/
    init_stats_tab_global();

    mudclient *mud = malloc(sizeof(mudclient));
    mudclient_new(mud);

#ifdef EMSCRIPTEN
    global_mud = mud;
#endif

    if (argc > 1 && strlen(argv[1]) > 0) {
        mud->options->members = strcmp(argv[1], "members") == 0;
    }

    if (argc > 2) {
        strcpy(mud->server, argv[2]);
    }

    if (argc > 3) {
        mud->port = atoi(argv[3]);
    }

#ifdef REVISION_177
    /* BEGIN INAUTHENTIC COMMAND LINE ARGUMENTS */
    if (argc > 4) {
        strcpy(mud->rsa_exponent, argv[4]);
    }

    if (argc > 5) {
        strcpy(mud->rsa_modulus, argv[5]);
    }
    /* END INAUTHENTIC COMMAND LINE ARGUMENTS */
#endif

    mudclient_start_application(mud, "Runescape by Andrew Gower");
    mudclient_start_application_common(mud);

#ifdef RENDER_3DS_GL
    shaderProgramFree(&mud->surface->_3ds_gl_flat_shader);
    DVLB_Free(mud->surface->_3ds_gl_flat_shader_dvlb);

    C3D_Fini();
#endif

#ifdef _3DS
    linearFree(audio_buffer);
    ndspExit();

    gfxExit();
#endif

    return 0;
}

#ifdef EMSCRIPTEN
void browser_mouse_moved(int x, int y) {
    mudclient_mouse_moved(global_mud, x, y);
}

void browser_key_pressed(int code, int char_code) {
    mudclient_key_pressed(global_mud, code, char_code);
}
#endif
