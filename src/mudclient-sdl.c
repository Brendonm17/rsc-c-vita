#include "mudclient.h"

#if defined(SDL12) || defined(SDL2)

#ifdef __SWITCH__
#define MAX_KBD_STR_SIZE 200
static SwkbdConfig switch_keyboard;
static char switch_keyboard_buffer[MAX_KBD_STR_SIZE] = {0};
static uint8_t switch_mouse_button = 1;
#endif

#ifdef __vita__
#include <math.h>

// frame gap (ms) past which a delay counts as suspend/resume
#define VITA_SUSPEND_MS 5000

// stick deflection (out of 32767) that counts as pushed
#define VITA_STICK_DEAD_ZONE 12000

// left-stick walk target: tiles ahead, and reissue threshold in tiles
#define VITA_WALK_TILES 16
#define VITA_WALK_REISSUE 3

// minimum interval between walk packets (~one game tick)
#define VITA_WALK_MIN_MS 600

// latest left-stick reading; applied once per frame
static int vita_left_stick_x = 0;
static int vita_left_stick_y = 0;

// virtual mouse cursor position (sub-pixel) driven by the left stick
#define VITA_CURSOR_MAX_SPEED 14.0f
static float vita_cursor_x = -1.0f;
static float vita_cursor_y = -1.0f;

// analog dead zone (0-32767) as a percent of full deflection
static int vita_deadzone(mudclient *mud) {
    return mud->options->vita_stick_deadzone * 327;
}

// d-pad held direction (-1/0/+1 per axis); nudges the cursor each frame
#define VITA_DPAD_CURSOR_SPEED 5.0f
static int vita_dpad_x = 0;
static int vita_dpad_y = 0;

// map a normalised front-panel touch (0..1) to game surface coords
static void vita_map_touch(mudclient *mud, float finger_x, float finger_y,
                           int *out_x, int *out_y) {
#if defined(RENDER_GL)
    // hardware renderer fills the full panel; no letterbox to undo
    int x = (int)(finger_x * mud->game_width);
    int y = (int)(finger_y * mud->game_height);
#else
    // software renderer letterboxes the surface; undo that to map taps
    float view_w = 960.0f;
    float view_h = 544.0f;
    float scale_x = view_w / mud->game_width;
    float scale_y = view_h / mud->game_height;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    float pad_x = (view_w - mud->game_width * scale) / 2.0f;
    float pad_y = (view_h - mud->game_height * scale) / 2.0f;

    int x = (int)((finger_x * view_w - pad_x) / scale);
    int y = (int)((finger_y * view_h - pad_y) / scale);
#endif

    if (x < 0) {
        x = 0;
    } else if (x >= mud->game_width) {
        x = mud->game_width - 1;
    }

    if (y < 0) {
        y = 0;
    } else if (y >= mud->game_height) {
        y = mud->game_height - 1;
    }

    *out_x = x;
    *out_y = y;
}

// left stick moves a virtual mouse cursor via the touch mouse path
static void vita_move_cursor(mudclient *mud) {
    static int last_set_x = -1;
    static int last_set_y = -1;

    // hold the cursor still while a click waits for this frame's hit tests
    // (pressing the button jostles the stick); bounded so an unconsumed
    // click cannot park the stick
    static int click_hold_frames = 0;

    if (mud->mouse_button_click != 0) {
        if (click_hold_frames < 2) {
            click_hold_frames++;
            return;
        }
    } else {
        click_hold_frames = 0;
    }

    if (vita_cursor_x < 0.0f) {
        // first use: start the cursor centred
        vita_cursor_x = mud->game_width / 2.0f;
        vita_cursor_y = mud->game_height / 2.0f;
        last_set_x = (int)vita_cursor_x;
        last_set_y = (int)vita_cursor_y;
    } else if (mud->mouse_x != last_set_x || mud->mouse_y != last_set_y) {
        // adopt a touch's position so the stick refines from there
        vita_cursor_x = mud->mouse_x;
        vita_cursor_y = mud->mouse_y;
    }

    float nx = vita_left_stick_x / 32768.0f;
    float ny = vita_left_stick_y / 32768.0f;

    int dead = vita_deadzone(mud);

    if (vita_left_stick_x > -dead && vita_left_stick_x < dead) {
        nx = 0.0f;
    }

    if (vita_left_stick_y > -dead && vita_left_stick_y < dead) {
        ny = 0.0f;
    }

    // quadratic response: precise near centre, fast at the edge
    float speed = (float)mud->options->vita_cursor_sensitivity;
    vita_cursor_x += nx * fabsf(nx) * speed;
    vita_cursor_y += ny * fabsf(ny) * speed;

    // d-pad nudges the cursor at a steady speed while held
    vita_cursor_x += vita_dpad_x * VITA_DPAD_CURSOR_SPEED;
    vita_cursor_y += vita_dpad_y * VITA_DPAD_CURSOR_SPEED;

    if (vita_cursor_x < 0.0f) {
        vita_cursor_x = 0.0f;
    } else if (vita_cursor_x > mud->game_width - 1) {
        vita_cursor_x = mud->game_width - 1;
    }

    if (vita_cursor_y < 0.0f) {
        vita_cursor_y = 0.0f;
    } else if (vita_cursor_y > mud->game_height - 1) {
        vita_cursor_y = mud->game_height - 1;
    }

    mudclient_mouse_moved(mud, (int)vita_cursor_x, (int)vita_cursor_y);

    last_set_x = (int)vita_cursor_x;
    last_set_y = (int)vita_cursor_y;
}
#endif

static int mudclient_horizontal_drag = 0;
static int mudclient_vertical_drag = 0;

static double mudclient_pinch_distance = 0;

static int mudclient_has_right_clicked = 0;

static int mudclient_touch_start = 0; // ms
static int mudclient_touch_start_x = 0;
static int mudclient_touch_start_y = 0;

static int64_t mudclient_finger_1_id = 0;
static int64_t mudclient_finger_2_id = 0;

void mudclient_poll_events(mudclient *mud) {
#ifdef __vita__
    // resume from suspend: a large frame gap means the connection died; drop it like a normal disconnect
    {
        static int vita_prev_ticks = 0;
        static int vita_prev_logged_in = 0;
        int vita_now = get_ticks();

        int resumed = vita_prev_logged_in && mud->logged_in == 1 &&
                      vita_prev_ticks != 0 &&
                      vita_now - vita_prev_ticks > VITA_SUSPEND_MS;

#ifdef WITH_SINGLEPLAYER
        // single-player has no socket to drop; skip resume handling
        if (mud->singleplayer) {
            resumed = 0;
        }
#endif

        if (resumed) {
            // close the dead socket first so the reconnect doesn't leak its fd
            if (mud->packet_stream != NULL) {
                packet_stream_close(mud->packet_stream);
            }

            mudclient_lost_connection(mud);
        }

        vita_prev_logged_in = mud->logged_in == 1;
        vita_prev_ticks = get_ticks();
    }

    // on-screen keyboard is non-modal; game keeps running underneath
    if (vita_ime_is_active()) {
        vita_ime_poll(mud);
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        return;
    }
#endif

    // guides have no right-click use; a held scrollbar thumb would sail
    // past the menu delay and pop the context menu mid-drag
    if (!mudclient_has_right_clicked && !mudclient_horizontal_drag &&
        !mudclient_vertical_drag && mudclient_finger_1_down &&
        !mudclient_finger_2_down && !mudclient_guides_visible(mud) &&
        get_ticks() - mudclient_touch_start >= mud->options->touch_menu_delay) {
        mudclient_mouse_pressed(mud, mud->mouse_x, mud->mouse_y, 3);
        mudclient_mouse_released(mud, mud->mouse_x, mud->mouse_y, 3);
        mudclient_has_right_clicked = 1;
    }

    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_QUIT:
            exit(0);
            break;
        case SDL_KEYDOWN: {
            char char_code;
            int code;
            get_sdl_keycodes(&event.key.keysym, &char_code, &code);
#ifdef SDL_TEXTINPUT
            if (isdigit((unsigned char)char_code)) {
		return;
            }
#endif
            mudclient_key_pressed(mud, code, char_code);
            break;
        }
        case SDL_KEYUP: {
            char char_code;
            int code;
            get_sdl_keycodes(&event.key.keysym, &char_code, &code);
            mudclient_key_released(mud, code);

#ifdef ANDROID
            if (code == K_ENTER) {
                SDL_StopTextInput();
            }
#endif
            break;
        }
        case SDL_MOUSEMOTION:
            if (!mudclient_is_touch(mud)) {
                mudclient_mouse_moved(mud, event.motion.x, event.motion.y);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (!mudclient_is_touch(mud)) {
                mudclient_mouse_pressed(mud, event.button.x, event.button.y,
                                        event.button.button);
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (!mudclient_is_touch(mud)) {
                mudclient_mouse_released(mud, event.button.x, event.button.y,
                                         event.button.button);
            }
            break;
#ifndef SDL12
        case SDL_MOUSEWHEEL:
            if (mud->options->mouse_wheel) {
                if (event.wheel.y != 0) {
                    mud->mouse_scroll_delta = (event.wheel.y > 0 ? -1 : 1);
                }

                if (event.wheel.x != 0) {
                    int direction = event.wheel.x > 0 ? 1 : -1;

                    mud->camera_rotation =
                        (mud->camera_rotation + (direction * 3)) & 0xff;
                }
            }
            break;
        case SDL_FINGERMOTION: {
#ifdef ANDROID
            if (SDL_IsTextInputActive()) {
                SDL_StopTextInput();
                return;
            }
#endif

#ifdef __vita__
            if (event.tfinger.touchId != 1) {
                break; // front panel only; ignore the rear touch panel
            }
#endif

            int touch_x = event.tfinger.x * mud->game_width;
            int touch_y = event.tfinger.y * mud->game_height;

#ifdef __vita__
            vita_map_touch(mud, event.tfinger.x, event.tfinger.y, &touch_x,
                           &touch_y);
#endif

#ifdef __SWITCH__
            mudclient_mouse_moved(mud, touch_x, touch_y);
#else
            if (!mudclient_is_touch(mud)) {
                break;
            }

            int64_t finger_id = event.tfinger.fingerId;

            if (finger_id == mudclient_finger_1_id) {
                mudclient_finger_1_x = touch_x;
                mudclient_finger_1_y = touch_y;
            } else if (finger_id == mudclient_finger_2_id) {
                mudclient_finger_2_x = touch_x;
                mudclient_finger_2_y = touch_y;
            }

            if (mud->options->touch_pinch != 0 && mudclient_finger_1_down &&
                mudclient_finger_2_down && !mudclient_guides_visible(mud)) {
                double pinch_distance =
                    distance(mudclient_finger_1_x, mudclient_finger_1_y,
                             mudclient_finger_2_x, mudclient_finger_2_y);

                if (mudclient_pinch_distance > 0) {
                    float scale = mud->options->touch_pinch / 100.0f;

                    mud->mouse_scroll_delta =
                        (mudclient_pinch_distance - pinch_distance) * scale;
                }

                mudclient_pinch_distance = pinch_distance;
                mudclient_has_right_clicked = 1;
            } else if (mudclient_finger_1_down && !mudclient_finger_2_down) {
                int delta_x = touch_x - mudclient_touch_start_x;
                int delta_y = touch_y - mudclient_touch_start_y;

                // a guide window owns its own scrolling; camera rotation
                // and drag-zoom would fight the scrollbar thumb
                if (!mudclient_horizontal_drag && abs(delta_x) > 30 &&
                    !mudclient_guides_visible(mud)) {
                    mudclient_horizontal_drag = 1;

                    mudclient_mouse_pressed(mud, mudclient_touch_start_x,
                                            mudclient_touch_start_y, 2);
                }

                if (mud->options->touch_vertical_drag != 0 &&
                    mud->show_ui_tab == 0 && !mudclient_guides_visible(mud) &&
                    (mudclient_vertical_drag || abs(delta_y) > 30)) {
                    mudclient_vertical_drag = 1;

                    mud->mouse_scroll_delta =
                        (event.tfinger.dy *
                         (mud->options->touch_vertical_drag / 100.0f)) *
                        mud->game_height;
                }

                mudclient_mouse_moved(mud, touch_x, touch_y);
            }
#endif
            break;
        }
        case SDL_FINGERDOWN: {
#ifdef ANDROID
            if (SDL_IsTextInputActive()) {
                SDL_StopTextInput();
                return;
            }
#endif

#ifdef __vita__
            if (event.tfinger.touchId != 1) {
                break; // front panel only; ignore the rear touch panel
            }
#endif

            int touch_x = event.tfinger.x * mud->game_width;
            int touch_y = event.tfinger.y * mud->game_height;

#ifdef __vita__
            vita_map_touch(mud, event.tfinger.x, event.tfinger.y, &touch_x,
                           &touch_y);
#endif

#ifdef __SWITCH__
            mudclient_mouse_pressed(mud, touch_x, touch_y, switch_mouse_button);
#else
            if (!mudclient_is_touch(mud)) {
                break;
            }

            int64_t finger_id = event.tfinger.fingerId;

            if (!mudclient_finger_1_down) {
                mudclient_has_right_clicked = 0;

                mudclient_finger_1_id = finger_id;
                mudclient_finger_1_down = 1;

                mudclient_finger_1_x = touch_x;
                mudclient_finger_1_y = touch_y;

                mudclient_touch_start = get_ticks();

                mudclient_touch_start_x = touch_x;
                mudclient_touch_start_y = touch_y;

                mudclient_mouse_moved(mud, touch_x, touch_y);
            } else if (!mudclient_finger_2_down) {
                mudclient_finger_2_id = finger_id;
                mudclient_finger_2_down = 1;

                mudclient_finger_2_x = touch_x;
                mudclient_finger_2_y = touch_y;
            } else {
                break;
            }
#endif
            break;
        }
        case SDL_FINGERUP: {
#ifdef ANDROID
            if (SDL_IsTextInputActive()) {
                SDL_StopTextInput();
                return;
            }
#endif

#ifdef __vita__
            if (event.tfinger.touchId != 1) {
                break; // front panel only; ignore the rear touch panel
            }
#endif

            int touch_x = event.tfinger.x * mud->game_width;
            int touch_y = event.tfinger.y * mud->game_height;

#ifdef __vita__
            vita_map_touch(mud, event.tfinger.x, event.tfinger.y, &touch_x,
                           &touch_y);
#endif

#ifdef __SWITCH__
            mudclient_mouse_released(mud, touch_x, touch_y,
                                     switch_mouse_button);
#else
            if (!mudclient_is_touch(mud)) {
                break;
            }

            int64_t finger_id = event.tfinger.fingerId;

            if (mudclient_finger_1_down && finger_id == mudclient_finger_1_id) {
                mudclient_finger_1_down = 0;

                if (!mudclient_has_right_clicked && !mudclient_vertical_drag &&
                    !mudclient_horizontal_drag &&
                    mudclient_pinch_distance == 0) {
                    // click where the finger LANDED; lifting rolls the
                    // reported point up a few pixels
                    mudclient_mouse_pressed(mud, mudclient_touch_start_x,
                                            mudclient_touch_start_y, 0);
                    mudclient_mouse_released(mud, mudclient_touch_start_x,
                                             mudclient_touch_start_y, 0);
                } else {
                    mudclient_vertical_drag = 0;

                    if (mudclient_horizontal_drag) {
                        mudclient_mouse_released(mud, mud->mouse_x,
                                                 mud->mouse_y, 2);

                        mudclient_horizontal_drag = 0;
                    }
                }
            } else if (mudclient_finger_2_down &&
                       finger_id == mudclient_finger_2_id) {
                mudclient_finger_2_down = 0;
                mudclient_pinch_distance = 0;
            }
#endif
            break;
        }
#endif
#ifdef __SWITCH__
        case SDL_JOYBUTTONDOWN:
            switch (event.jbutton.button) {
            case 0: // A Button
                mudclient_key_pressed(mud, K_ENTER, K_ENTER);
                break;
            case 1: // B Button
                mudclient_key_pressed(mud, K_BACKSPACE, K_BACKSPACE);
                break;
            case 2: // X Button
                mudclient_key_pressed(mud, K_TAB, -1);
                break;
            case 3: // Y Button
                mudclient_key_pressed(mud, K_HOME, -1);
                break;
            case 6: // L Button
                mudclient_key_pressed(mud, K_ESCAPE, -1);
                break;
            case 7: // R Button
                if (mud->options->display_fps == 0)
                    mud->options->display_fps = 1;
                else
                    mud->options->display_fps = 0;
                break;
            case 8: // ZL
                switch_mouse_button = 3;
                break;
            case 9: // ZR
                // Reserved
                break;
            case 11: // Minus Button
                mudclient_key_pressed(mud, K_F1, -1);
                break;
            case 10: // Plus Button
                swkbdCreate(&switch_keyboard, 0);
                swkbdConfigSetType(&switch_keyboard, SwkbdType_QWERTY);
                swkbdConfigSetBlurBackground(&switch_keyboard, 0);

                swkbdConfigSetTextDrawType(&switch_keyboard,
                                           SwkbdTextDrawType_Box);

                swkbdConfigSetReturnButtonFlag(&switch_keyboard, 0);
                swkbdConfigSetStringLenMax(&switch_keyboard, MAX_KBD_STR_SIZE);
                swkbdConfigSetOkButtonText(&switch_keyboard, "Submit");

                swkbdShow(&switch_keyboard, switch_keyboard_buffer,
                          sizeof(switch_keyboard_buffer));

                for (int i = 0; i < sizeof(switch_keyboard_buffer); i++) {
                    mudclient_key_pressed(mud, -1, switch_keyboard_buffer[i]);
                }

                swkbdClose(&switch_keyboard);
                break;
            case 12: // DPAD LEFT
            case 16: // Left Stick Left
                mudclient_key_pressed(mud, K_LEFT, -1);
                break;
            case 13: // DPAD UP
            case 17: // Left Stick Up
                mudclient_key_pressed(mud, K_UP, -1);
                break;
            case 14: // DPAD RIGHT
            case 18: // Left Stick Right
                mudclient_key_pressed(mud, K_RIGHT, -1);
                break;
            case 15: // DPAD DOWN
            case 19: // Left Stick Down
                mudclient_key_pressed(mud, K_DOWN, -1);
                break;
            case 20: // Right Stick Left
                break;
            case 21: // Right Stick Up
                mudclient_key_pressed(mud, K_PAGE_UP, -1);
                break;
            case 22: // Right Stick Right
                break;
            case 23: // Right Stick Down
                mudclient_key_pressed(mud, K_PAGE_DOWN, -1);
                break;
            }
            break;
        case SDL_JOYBUTTONUP:
            switch (event.jbutton.button) {
            case 0: // A Button
                mudclient_key_released(mud, K_ENTER);
                break;
            case 1: // B Button
                mudclient_key_released(mud, K_BACKSPACE);
                break;
            case 2: // X Button
                mudclient_key_released(mud, K_TAB);
                break;
            case 3: // Y Button
                mudclient_key_released(mud, K_HOME);
                break;
            case 6: // L Button
                mudclient_key_released(mud, K_ESCAPE);
                break;
            case 7: // R Button
                break;
            case 8: // ZL
                switch_mouse_button = 1;
                break;
            case 9: // ZR
                // Reserved
                break;
            case 11: // Minus Button
                mudclient_key_released(mud, K_F1);
                break;
            case 12: // DPAD LEFT
            case 16: // Left Stick Left
                mudclient_key_released(mud, K_LEFT);
                break;
            case 13: // DPAD UP
            case 17: // Left Stick Up
                mudclient_key_released(mud, K_UP);
                break;
            case 14: // DPAD RIGHT
            case 18: // Left Stick Right
                mudclient_key_released(mud, K_RIGHT);
                break;
            case 15: // DPAD DOWN
            case 19: // Left Stick Down
                mudclient_key_released(mud, K_DOWN);
                break;
            case 20: // Right Stick Left
                break;
            case 21: // Right Stick Up
                mudclient_key_released(mud, K_PAGE_UP);
                break;
            case 22: // Right Stick Right
                break;
            case 23: // Right Stick Down
                mudclient_key_released(mud, K_PAGE_DOWN);
                break;
            }
            break;
#endif
#ifdef __vita__
        // sdl2-vita joystick button indices
        case SDL_JOYBUTTONDOWN:
            switch (event.jbutton.button) {
            case 2: // Cross -> left-click at the cursor
                mudclient_mouse_pressed(mud, mud->mouse_x, mud->mouse_y, 1);
                break;
            case 1: // Circle
                mudclient_key_pressed(mud, K_BACKSPACE, K_BACKSPACE);
                break;
            case 3: // Square
                mudclient_key_pressed(mud, K_TAB, -1);
                break;
            case 0: // Triangle
                mudclient_key_pressed(mud, K_HOME, -1);
                break;
            case 4: // L trigger -> escape
                mudclient_key_pressed(mud, K_ESCAPE, -1);
                break;
            case 5: // R trigger sends a right-click
                mudclient_mouse_pressed(mud, mud->mouse_x, mud->mouse_y, 3);
                mudclient_mouse_released(mud, mud->mouse_x, mud->mouse_y, 3);
                break;
            // select and start are unmapped; the on-screen keyboard opens automatically when a text field is focused
            case 6: // D-pad Down -> nudge cursor down
                vita_dpad_y = 1;
                break;
            case 7: // D-pad Left -> nudge cursor left
                vita_dpad_x = -1;
                break;
            case 8: // D-pad Up -> nudge cursor up
                vita_dpad_y = -1;
                break;
            case 9: // D-pad Right -> nudge cursor right
                vita_dpad_x = 1;
                break;
            }
            break;
        case SDL_JOYBUTTONUP:
            switch (event.jbutton.button) {
            case 2: // Cross -> release left-click
                mudclient_mouse_released(mud, mud->mouse_x, mud->mouse_y, 1);
                break;
            case 1: // Circle
                mudclient_key_released(mud, K_BACKSPACE);
                break;
            case 3: // Square
                mudclient_key_released(mud, K_TAB);
                break;
            case 0: // Triangle
                mudclient_key_released(mud, K_HOME);
                break;
            case 4: // L trigger
                mudclient_key_released(mud, K_ESCAPE);
                break;
            case 6: // D-pad Down
            case 8: // D-pad Up
                vita_dpad_y = 0;
                break;
            case 7: // D-pad Left
            case 9: // D-pad Right
                vita_dpad_x = 0;
                break;
            }
            break;
        case SDL_JOYAXISMOTION: {
            int axis = event.jaxis.axis;

            // left stick (axes 0/1) moves the cursor; right stick (axes
            // 2/3) rotates/zooms the camera. axes 4/5 are ignored
            if (axis == 0) {
                vita_left_stick_x = event.jaxis.value;
                break;
            }

            if (axis == 1) {
                vita_left_stick_y = event.jaxis.value;
                break;
            }

            static int axis_direction[6] = {0};

            int negative_key = -1;
            int positive_key = -1;

            if (axis == 2) {
                negative_key = K_LEFT; // rotate camera
                positive_key = K_RIGHT;
            } else if (axis == 3) {
                negative_key = K_UP; // zoom in / out
                positive_key = K_DOWN;
            } else {
                break;
            }

            int direction = 0;
            int cam_dead = vita_deadzone(mud);

            if (event.jaxis.value < -cam_dead) {
                direction = -1;
            } else if (event.jaxis.value > cam_dead) {
                direction = 1;
            }

            if (direction != axis_direction[axis]) {
                // release the key for the previous direction, press the new one
                if (axis_direction[axis] < 0) {
                    mudclient_key_released(mud, negative_key);
                } else if (axis_direction[axis] > 0) {
                    mudclient_key_released(mud, positive_key);
                }

                if (direction < 0) {
                    mudclient_key_pressed(mud, negative_key, -1);
                } else if (direction > 0) {
                    mudclient_key_pressed(mud, positive_key, -1);
                }

                axis_direction[axis] = direction;
            }

            break;
        }
#endif
#ifdef SDL12
        case SDL_VIDEORESIZE:
            mudclient_sdl1_on_resize(mud, event.resize.w, event.resize.h);
            break;
#else
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                mudclient_on_resize(mud);
            }
            break;
        case SDL_TEXTINPUT:
            if (strlen(event.text.text) == 1) {
                char ch = event.text.text[0];

                if (isprint((unsigned char)ch)) {
                    mudclient_key_pressed(mud, ch, ch);
                }
            }
            break;
#endif
        }
    }

#ifdef __vita__
    // apply the held left stick once per frame to move the cursor
    vita_move_cursor(mud);
#endif
}
#endif
