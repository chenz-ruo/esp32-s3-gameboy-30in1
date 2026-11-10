/*
 * font.h - 4bpp alpha bitmap font description (generated tables live in
 *          font_data.c, produced by tools/gen_font.py from real system fonts).
 */
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef struct {
    uint16_t cp;      /* unicode code point */
    uint8_t  w, h;    /* bitmap size in pixels */
    uint8_t  adv;     /* pen advance */
    int8_t   bx, by;  /* bitmap offset from pen origin, by is above baseline */
    uint32_t off;     /* byte offset into the pool, rows packed 2 px/byte */
} Glyph;

typedef struct {
    const Glyph   *g;
    uint16_t       n;
    const uint8_t *pool;
    uint8_t        ascent;
    uint8_t        line_h;
} Font;

extern const Font font_14;   /* latin only, small labels        */
extern const Font font_18;   /* latin + CJK, body text          */
extern const Font font_28;   /* latin + CJK, headings           */
extern const Font font_52;   /* digits, hero numbers            */

/* Returns NULL when the glyph is missing. */
const Glyph *font_find(const Font *f, uint16_t cp);

/* Length of a UTF-8 sequence starting at s, and its code point. */
int utf8_decode(const char *s, uint32_t *cp);

#endif
