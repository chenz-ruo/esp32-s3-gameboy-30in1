/*
 * gfx.c - software renderer for the 240x135 panel (RGB565, double buffered).
 */
#include "gfx.h"
#include "bsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

/* One 153.6 KB surface in internal RAM: the 8080 bus needs ~15 ms per frame,
 * which is slower than drawing, so a second buffer would buy nothing. */
static uint16_t *s_buf;
static const uint16_t *s_shown;

static uint16_t *surface(void)
{
    if (!s_buf) {
        s_buf = heap_caps_malloc(GFX_W * GFX_H * sizeof(uint16_t),
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        s_shown = s_buf;
    }
    return s_buf;
}

uint16_t *gfx_surface(void) { return surface(); }

/* Flush a horizontal band only. The band must span the full width, which is
 * what the animated menu needs: 80 px instead of 240 px keeps 60 fps alive. */
void gfx_present_band(int y0, int y1)
{
    uint16_t *fb = surface();
    if (y0 < 0) y0 = 0;
    if (y1 > GFX_H) y1 = GFX_H;
    if (y1 <= y0) return;
    bsp_lcd_flush(0, y0, GFX_W, y1 - y0, fb + y0 * GFX_W);
    bsp_lcd_wait();
}

/* Flush a narrow vertical strip: the rows of a sub-rectangle are not
 * contiguous in the framebuffer, so pack them first. Used by the scrollbar so
 * it can move every frame without repainting the whole screen. */
void gfx_present_strip(int x, int y, int w, int h)
{
    static uint16_t strip[8 * GFX_H];
    if (w <= 0 || w > 8 || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > GFX_H) h = GFX_H - y;
    if (h <= 0) return;

    uint16_t *fb = surface();
    for (int j = 0; j < h; j++)
        memcpy(&strip[j * w], fb + (y + j) * GFX_W + x, w * sizeof(uint16_t));
    bsp_lcd_flush(x, y, w, h, strip);
    bsp_lcd_wait();
}

void gfx_present(void)
{
    uint16_t *fb = surface();
    bsp_lcd_flush(0, 0, GFX_W, GFX_H, fb);
    bsp_lcd_wait();
    s_shown = fb;
}

const uint16_t *gfx_onscreen(void) { surface(); return s_shown; }

uint16_t gfx_mix(uint16_t a, uint16_t b, uint8_t t)
{
    /* signed maths: unsigned deltas wrap when mixing dark->bright and give a
     * wrong colour after the shift */
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r  = ar + ((br - ar) * t) / 256;
    int g  = ag + ((bg - ag) * t) / 256;
    int bl = ab + ((bb - ab) * t) / 256;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

void gfx_clear(uint16_t color)
{
    uint16_t *d = surface();
    for (int i = 0; i < GFX_W * GFX_H; i++) d[i] = color;
}

void gfx_px(int x, int y, uint16_t c)
{
    if ((unsigned)x >= GFX_W || (unsigned)y >= GFX_H) return;
    surface()[y * GFX_W + x] = c;
}

void gfx_hline(int x, int y, int w, uint16_t c)
{
    if ((unsigned)y >= GFX_H) return;
    int x0 = x < 0 ? 0 : x;
    int x1 = x + w; if (x1 > GFX_W) x1 = GFX_W;
    if (x1 <= x0) return;
    uint16_t *d = surface() + y * GFX_W;
    for (int i = x0; i < x1; i++) d[i] = c;
}

void gfx_vline(int x, int y, int h, uint16_t c)
{
    if ((unsigned)x >= GFX_W) return;
    int y0 = y < 0 ? 0 : y;
    int y1 = y + h; if (y1 > GFX_H) y1 = GFX_H;
    if (y1 <= y0) return;
    uint16_t *d = surface();
    for (int j = y0; j < y1; j++) d[j * GFX_W + x] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    int y0 = y < 0 ? 0 : y, y1 = y + h; if (y1 > GFX_H) y1 = GFX_H;
    for (int j = y0; j < y1; j++) gfx_hline(x, j, w, c);
}

void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c);
    gfx_vline(x + w - 1, y, h, c);
}

static int round_inset(int r, int j, int h)
{
    int dy;
    if (j < r)                     dy = r - 1 - j;
    else if (j >= h - r)           dy = j - (h - r);
    else return 0;
    int d = r * r - dy * dy;
    if (d <= 0) return r;
    return r - (int)(sqrtf((float)d) + 0.5f);
}

void gfx_fill_round(int x, int y, int w, int h, int r, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    int maxr = (w < h ? w : h) / 2;
    if (r > maxr) r = maxr;
    if (r <= 0) { gfx_fill_rect(x, y, w, h, c); return; }
    for (int j = 0; j < h; j++) {
        int in = round_inset(r, j, h);
        gfx_hline(x + in, y + j, w - 2 * in, c);
    }
}

void gfx_round(int x, int y, int w, int h, int r, uint16_t c)
{
    if (r <= 0) { gfx_rect(x, y, w, h, c); return; }
    if (r > (w < h ? w : h) / 2) r = (w < h ? w : h) / 2;
    gfx_hline(x + r, y, w - 2 * r, c);
    gfx_hline(x + r, y + h - 1, w - 2 * r, c);
    gfx_vline(x, y + r, h - 2 * r, c);
    gfx_vline(x + w - 1, y + r, h - 2 * r, c);
    /* corner arcs */
    int cx0 = x + r, cy0 = y + r, cx1 = x + w - 1 - r, cy1 = y + h - 1 - r;
    for (int i = 0; i <= r; i++) {
        int dy = (int)(sqrtf((float)(r * r - (r - i) * (r - i))) + 0.5f);
        int yy = r - dy;
        gfx_px(cx0 - i, cy0 - yy, c);
        gfx_px(cx1 + i, cy0 - yy, c);
        gfx_px(cx0 - i, cy1 + yy, c);
        gfx_px(cx1 + i, cy1 + yy, c);
    }
}

void gfx_fill_circle(int cx, int cy, int r, uint16_t c)
{
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int d = r * r - dy * dy;
        if (d < 0) continue;
        int dx = (int)(sqrtf((float)d) + 0.5f);
        gfx_hline(cx - dx, cy + dy, 2 * dx + 1, c);
    }
}

void gfx_circle(int cx, int cy, int r, uint16_t c)
{
    for (int a = 0; a < 360; a += 2) {
        float rad = a * 3.14159265f / 180.0f;
        gfx_px(cx + (int)(cosf(rad) * r + 0.5f), cy + (int)(sinf(rad) * r + 0.5f), c);
    }
}

static inline uint8_t clamp_u8(float v)
{
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (uint8_t)v;
}

void gfx_ring_soft(int cx, int cy, int r, int thickness, uint16_t c, uint8_t alpha)
{
    if (alpha == 0 || r <= 0) return;
    float inner = (float)r - thickness * 0.5f;
    float outer = (float)r + thickness * 0.5f;
    int x0 = cx - (int)outer - 1, x1 = cx + (int)outer + 1;
    int y0 = cy - (int)outer - 1, y1 = cy + (int)outer + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_W) x1 = GFX_W - 1;
    if (y1 >= GFX_H) y1 = GFX_H - 1;
    uint16_t *d = surface();
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx);
            float dist = sqrtf(dx * dx + dy * dy);
            float cover = (outer + 0.5f - dist);
            if (cover <= 0.0f) continue;
            if (cover > 1.0f) cover = 1.0f;
            float ci = dist - inner + 0.5f;
            if (ci < 0.0f) continue;
            if (ci > 1.0f) ci = 1.0f;
            uint8_t a = (uint8_t)((uint32_t)alpha * (uint8_t)(cover * 255.0f) * (uint8_t)(ci * 255.0f) / 65025u);
            if (!a) continue;
            d[y * GFX_W + x] = gfx_mix(d[y * GFX_W + x], c, a);
        }
    }
}

void gfx_disc_soft(int cx, int cy, int r, uint16_t c, uint8_t alpha)
{
    if (alpha == 0 || r <= 0) return;
    int x0 = cx - r - 1, x1 = cx + r + 1, y0 = cy - r - 1, y1 = cy + r + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_W) x1 = GFX_W - 1;
    if (y1 >= GFX_H) y1 = GFX_H - 1;
    uint16_t *d = surface();
    for (int y = y0; y <= y1; y++) {
        float dy = (float)(y - cy);
        for (int x = x0; x <= x1; x++) {
            float dx = (float)(x - cx);
            float dist = sqrtf(dx * dx + dy * dy);
            float cover = (float)r + 0.5f - dist;
            if (cover <= 0.0f) continue;
            if (cover > 1.0f) cover = 1.0f;
            uint8_t a = (uint8_t)((uint32_t)alpha * (uint8_t)(cover * 255.0f) / 255u);
            if (!a) continue;
            d[y * GFX_W + x] = gfx_mix(d[y * GFX_W + x], c, a);
        }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom)
{
    if (h <= 0) return;
    for (int j = 0; j < h; j++) {
        uint16_t c = gfx_mix(top, bottom, (uint8_t)((j * 255) / (h > 1 ? h - 1 : 1)));
        gfx_hline(x, y + j, w, c);
    }
}

void gfx_tint_rect(int x, int y, int w, int h, uint16_t c, uint8_t alpha)
{
    int y0 = y < 0 ? 0 : y, y1 = y + h; if (y1 > GFX_H) y1 = GFX_H;
    int x0 = x < 0 ? 0 : x, x1 = x + w; if (x1 > GFX_W) x1 = GFX_W;
    uint16_t *d = surface();
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++)
            d[j * GFX_W + i] = gfx_mix(d[j * GFX_W + i], c, alpha);
}

/* ------------------------------------------------------------------ text */

static const Glyph *s_last_font_glyph;
static uint16_t     s_last_cp;

const Glyph *font_find(const Font *f, uint16_t cp)
{
    /* linear-scan cache: text is highly repetitive (digits, same labels) */
    if (s_last_font_glyph && s_last_cp == cp) return s_last_font_glyph;

    int lo = 0, hi = (int)f->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->g[mid].cp == cp) {
            s_last_font_glyph = &f->g[mid];
            s_last_cp = cp;
            return s_last_font_glyph;
        }
        if (f->g[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

int utf8_decode(const char *s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)s;
    if (!p[0]) { *cp = 0; return 0; }
    if (p[0] < 0x80) { *cp = p[0]; return 1; }
    if ((p[0] & 0xE0) == 0xC0) { *cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); return 2; }
    if ((p[0] & 0xF0) == 0xE0) { *cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); return 3; }
    if ((p[0] & 0xF8) == 0xF0) { *cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); return 4; }
    *cp = '?';
    return 1;
}

int gfx_text_w(const Font *f, const char *s)
{
    int w = 0;
    while (*s) {
        uint32_t cp; int n = utf8_decode(s, &cp);
        if (!n) break;
        const Glyph *g = font_find(f, (uint16_t)cp);
        w += g ? g->adv : (f->g[0].adv);
        s += n;
    }
    return w;
}

int gfx_text_h(const Font *f) { return f->line_h; }

void gfx_text(const Font *f, int x, int y, uint16_t c, const char *s)
{
    gfx_text_alpha(f, x, y, c, ALIGN_L, s, 255);
}

void gfx_text_a(const Font *f, int x, int y, uint16_t c, int align, const char *s)
{
    gfx_text_alpha(f, x, y, c, align, s, 255);
}

void gfx_text_alpha(const Font *f, int x, int y, uint16_t c, int align, const char *s, uint8_t alpha)
{
    if (align != ALIGN_L) {
        int w = gfx_text_w(f, s);
        x -= (align == ALIGN_C) ? w / 2 : w;
    }
    int pen = x;
    uint16_t *d = surface();
    const Glyph *fallback = &f->g[0];

    while (*s) {
        uint32_t cp; int n = utf8_decode(s, &cp);
        if (!n) break;
        const Glyph *g = font_find(f, (uint16_t)cp);
        if (!g) g = fallback;

        int bx = pen + g->bx;
        int by = y + g->by;   /* by is already relative to the line box top */
        int stride = (g->w + 1) / 2;
        const uint8_t *bmp = f->pool + g->off;

        for (int j = 0; j < g->h; j++) {
            int yy = by + j;
            if ((unsigned)yy >= GFX_H) continue;
            const uint8_t *row = bmp + j * stride;
            uint16_t *dst = d + yy * GFX_W;
            for (int i = 0; i < g->w; i++) {
                int xx = bx + i;
                if ((unsigned)xx >= GFX_W) continue;
                uint8_t b = row[i >> 1];
                uint8_t a4 = (i & 1) ? (b & 0x0F) : (b >> 4);
                if (!a4) continue;
                uint32_t a = (a4 * 17u * alpha) / 255u;
                if (!a) continue;
                dst[xx] = gfx_mix(dst[xx], c, (uint8_t)a);
            }
        }
        pen += g->adv;
        s += n;
    }
}
