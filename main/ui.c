#include <string.h>
#include "ui.h"
#include "font.h"

void ui_header(const char *title, const char *meta)
{
    gfx_text(&font_28, UI_PAD, 6, C_TEXT, title);
    if (meta) gfx_text_a(&font_14, GFX_W - UI_PAD, 14, C_DIM, ALIGN_R, meta);
    gfx_fill_rect(UI_PAD, 44, GFX_W - 2 * UI_PAD, 1, C_LINE);
}

void ui_hint(const char *left, const char *right)
{
    gfx_fill_rect(0, GFX_H - 24, GFX_W, 24, C_BG);
    gfx_fill_rect(0, GFX_H - 24, GFX_W, 1, C_LINE);
    gfx_text(&font_14, UI_PAD, GFX_H - 20, C_DIM, left);
    if (right) gfx_text_a(&font_14, GFX_W - UI_PAD, GFX_H - 20, C_DIM, ALIGN_R, right);
}

int ui_chip_w(const char *text)
{
    return gfx_text_w(&font_18, text) + 28;
}

void ui_chip(int x, int y, const char *text, uint16_t fg, uint16_t bg)
{
    int w = ui_chip_w(text);
    gfx_fill_round(x, y, w, 30, 15, bg);
    gfx_text_a(&font_18, x + w / 2, y + 5, fg, ALIGN_C, text);
}

/* One centred line under the play area: the games have no bottom bar any
 * more, and without this the lower third of a 320 px screen is dead space. */
void ui_note(const char *text)
{
    gfx_text_a(&font_14, GFX_W / 2, 272, C_DIMMER, ALIGN_C, text);
}

/* Greedy wrap for mixed CJK/latin. CJK breaks anywhere, ASCII on spaces.
 * Returns the number of lines drawn; truncates with an ellipsis when it runs
 * past max_lines. */
int ui_wrap(const char *text, int x, int y, int max_w, int line_h, int max_lines, uint16_t color)
{
    const char *p = text;
    int line = 0;

    while (*p && line < max_lines) {
        const char *start = p;
        const char *last_break = NULL;
        int w = 0;

        while (*p) {
            /* UTF-8 aware: CJK is 3 bytes, measuring single bytes gives
             * nonsense widths and breaks lines mid-character. */
            unsigned char c0 = (unsigned char)*p;
            int clen = 1;
            if (c0 >= 0xF0) clen = 4;
            else if (c0 >= 0xE0) clen = 3;
            else if (c0 >= 0xC0) clen = 2;

            char ch[5] = {0};
            memcpy(ch, p, clen);
            int cw = gfx_text_w(&font_18, ch);
            if (w + cw > max_w) break;
            w += cw;
            p += clen;
            /* remember the last punctuation so a CJK line breaks between
             * phrases instead of in the middle of a word */
            if (clen == 3 && c0 == 0xEF && p - start > 2) last_break = p;
        }
        if (last_break && last_break > start + (p - start) / 3) p = last_break;
        (void)last_break;
        if (p == start) break;                  /* cannot fit a single glyph */

        char buf[64];
        int n = (int)(p - start);
        if (n > (int)sizeof buf - 1) n = (int)sizeof buf - 1;
        memcpy(buf, start, n);
        buf[n] = 0;
        gfx_text(&font_18, x, y + line * line_h, color, buf);
        line++;
    }
    return line;
}

/* GAMEBOY, one colour per letter, after the Game Boy Color logotype.
 * Saturation is pulled back so it belongs on a graphite shell instead of
 * shouting over it. Returns the width drawn so the caller can centre it. */
int ui_wordmark(int x, int y)
{
    static const uint16_t ramp[7] = {
        RGB(0xD2, 0x5A, 0x3A),   /* G  burnt orange */
        RGB(0xD8, 0x85, 0x46),   /* A */
        RGB(0xD9, 0xC2, 0x5E),   /* M  sand yellow  */
        RGB(0xB6, 0xC9, 0x66),   /* E */
        RGB(0x86, 0xBC, 0x74),   /* B  sage         */
        RGB(0x5F, 0xB4, 0x83),   /* O */
        RGB(0x4C, 0xAB, 0x96),   /* Y  teal         */
    };
    static const char *letters = "GAMEBOY";
    int cx = x;

    for (int i = 0; i < 7; i++) {
        char c[2] = { letters[i], 0 };
        gfx_text(&font_28, cx, y, ramp[i], c);
        cx += gfx_text_w(&font_28, c) + 1;
        if (i == 3) cx += 3;      /* word break: GAME | BOY, not GAMEBOY */
    }
    return cx - x;
}
