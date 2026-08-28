#include "mudclient.h"

#ifdef __vita__

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <psp2/libime.h>
#include <stdint.h>
#include <string.h>

// heap budget: 144mb on the gl build (leaves room for vitagl gpu pools), 192mb software
#ifdef RENDER_GL
unsigned int _newlib_heap_size_user = 144 * 1024 * 1024;
#else
unsigned int _newlib_heap_size_user = 192 * 1024 * 1024;
#endif

// main-thread stack size; 3d render path overflows the default 256kb stack, corrupting return addresses
#ifdef RENDER_GL
__attribute__((used)) unsigned int sceUserMainThreadStackSize = 4 * 1024 * 1024;
#endif

// utf-8/ascii <-> utf-16 conversion helpers
static void vita_utf8_to_utf16(const uint8_t *src, uint16_t *dst) {
    for (int i = 0; src[i];) {
        if ((src[i] & 0xE0) == 0xE0) {
            *(dst++) = ((src[i] & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) |
                       (src[i + 2] & 0x3F);
            i += 3;
        } else if ((src[i] & 0xC0) == 0xC0) {
            *(dst++) = ((src[i] & 0x1F) << 6) | (src[i + 1] & 0x3F);
            i += 2;
        } else {
            *(dst++) = src[i];
            i += 1;
        }
    }

    *dst = '\0';
}

static void vita_utf16_to_utf8(const uint16_t *src, uint8_t *dst) {
    for (int i = 0; src[i]; i++) {
        if ((src[i] & 0xFF80) == 0) {
            *(dst++) = src[i] & 0xFF;
        } else if ((src[i] & 0xF800) == 0) {
            *(dst++) = ((src[i] >> 6) & 0xFF) | 0xC0;
            *(dst++) = (src[i] & 0x3F) | 0x80;
        } else if ((src[i] & 0xFC00) == 0xD800 &&
                   (src[i + 1] & 0xFC00) == 0xDC00) {
            *(dst++) = (((src[i] + 64) >> 8) & 0x3) | 0xF0;
            *(dst++) = (((src[i] >> 2) + 16) & 0x3F) | 0x80;
            *(dst++) = ((src[i] >> 4) & 0x30) | 0x80 | ((src[i + 1] << 2) & 0xF);
            *(dst++) = (src[i + 1] & 0x3F) | 0x80;
            i += 1;
        } else {
            *(dst++) = ((src[i] >> 12) & 0xF) | 0xE0;
            *(dst++) = ((src[i] >> 6) & 0x3F) | 0x80;
            *(dst++) = (src[i] & 0x3F) | 0x80;
        }
    }

    *dst = '\0';
}

// non-modal on-screen keyboard: vita_ime_open() returns immediately, vita_ime_poll() is called once per frame
static int vita_ime_active = 0;
static int vita_ime_initial_length = 0;
/* When set, confirming the IME also forwards a K_ENTER to the focused field
 * (commits input_text_current -> input_text_final), so Enter-submit screens with
 * no on-screen submit button (the sleep word) can be completed. Off by default;
 * set per-open via vita_ime_open's last argument. */
static int vita_ime_submit_on_enter = 0;
static uint16_t vita_ime_input[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

int vita_ime_is_active(void) { return vita_ime_active; }

void vita_ime_open(const char *title, const char *initial, int is_password,
                   int initial_length, int submit_on_enter) {
    if (vita_ime_active) {
        return;
    }

    vita_ime_submit_on_enter = submit_on_enter;

    static uint16_t title_u16[SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1];
    static uint16_t initial_u16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

    memset(title_u16, 0, sizeof(title_u16));
    memset(initial_u16, 0, sizeof(initial_u16));
    memset(vita_ime_input, 0, sizeof(vita_ime_input));

    vita_utf8_to_utf16((const uint8_t *)(title ? title : ""), title_u16);
    vita_utf8_to_utf16((const uint8_t *)(initial ? initial : ""), initial_u16);

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);

    param.supportedLanguages = 0; // 0 = allow all languages
    param.languagesForced = SCE_FALSE;
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
    param.textBoxMode = is_password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
                                    : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
    param.title = title_u16;
    param.maxTextLength = SCE_IME_DIALOG_MAX_TEXT_LENGTH;
    param.initialText = initial_u16;
    param.inputTextBuffer = vita_ime_input;

    if (sceImeDialogInit(&param) < 0) {
        return;
    }

    vita_ime_active = 1;
    vita_ime_initial_length = initial_length;
}

void vita_ime_poll(mudclient *mud) {
    if (!vita_ime_active) {
        return;
    }

    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        return; // still typing, the game keeps running this frame
    }

    SceImeDialogResult result;
    memset(&result, 0, sizeof(result));
    sceImeDialogGetResult(&result);

    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        static uint8_t utf8[SCE_IME_DIALOG_MAX_TEXT_LENGTH * 3 + 1];
        vita_utf16_to_utf8(vita_ime_input, utf8);

        // replaces the focused field's contents through its input path; does not send enter
        for (int i = 0; i < vita_ime_initial_length; i++) {
            mudclient_key_pressed(mud, K_BACKSPACE, K_BACKSPACE);
        }

        for (int i = 0; utf8[i] != '\0'; i++) {
            mudclient_key_pressed(mud, (unsigned char)utf8[i],
                                  (unsigned char)utf8[i]);
        }

        if (vita_ime_submit_on_enter) {
            mudclient_key_pressed(mud, K_ENTER, K_ENTER);
        }
    }

    sceImeDialogTerm();
    vita_ime_active = 0;

    // drops touch/button events that landed on the keyboard while open
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

#endif
