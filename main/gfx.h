/*
 * gfx.h - 240x320 RGB565 framebuffer in PSRAM, DMA-flushed to the i80 panel.
 *
 * There is one buffer, not two: present() / present_band() / present_strip()
 * start a DMA transfer and the caller must wait for completion before drawing
 * into the affected region again (bsp_lcd_wait() does this for full frames).
 * Dirty-band flushing keeps a scrolling menu cheap on this bus.
 */
#ifndef GFX_H_INCLUDED
#define GFX_H_INCLUDED

#include <stdint.h>
#include "font.h"

#define GFX_W 240
#define GFX_H 320


#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

/* Two palettes, selected at compile time: -DTHEME_DARK=1 flips to the dark
 * scheme. Both keep the same semantic roles, so the drawing code is shared. */
#ifndef THEME_DARK
#define THEME_DARK 1
#endif

#if THEME_DARK
/* Graphite, not blue-black: a neutral base reads as "device", a blue one as
 * "gamer". Text stops at #ECEDEF instead of pure white, and the accents are
 * desaturated so they act as labels rather than neon. */
#define C_BG        RGB(0x0F, 0x10, 0x13)   /* page background            */
#define C_BG2       RGB(0x14, 0x15, 0x1A)
#define C_PANEL     RGB(0x17, 0x19, 0x1E)   /* card surface               */
#define C_PANEL_HI  RGB(0x22, 0x25, 0x2B)   /* tracks, inactive dots      */
#define C_LINE      RGB(0x2B, 0x2E, 0x35)   /* hairlines / borders        */
#define C_TEXT      RGB(0xEC, 0xED, 0xEF)   /* primary text               */
#define C_DIM       RGB(0x9A, 0x9E, 0xA6)   /* secondary text             */
#define C_DIMMER    RGB(0x61, 0x65, 0x6D)   /* tertiary text              */
#define C_GREEN     RGB(0x4F, 0xB0, 0x8A)
#define C_CYAN      RGB(0x5B, 0x93, 0xC7)
#define C_AMBER     RGB(0xC9, 0x97, 0x4E)
#define C_RED       RGB(0xC0, 0x5B, 0x54)
#define C_GO        RGB(0x3F, 0xCB, 0x96)   /* the one place that stays loud */
#else
#define C_BG        RGB(0xEC, 0xF2, 0xF8)   /* page background            */
#define C_BG2       RGB(0xFA, 0xFC, 0xFE)
#define C_PANEL     RGB(0xFF, 0xFF, 0xFF)   /* card surface               */
#define C_PANEL_HI  RGB(0xDC, 0xE5, 0xEE)   /* tracks, inactive dots      */
#define C_LINE      RGB(0xC7, 0xD4, 0xE0)   /* hairlines / borders        */
#define C_TEXT      RGB(0x0D, 0x16, 0x1F)   /* primary text               */
#define C_DIM       RGB(0x4E, 0x60, 0x70)   /* secondary text             */
#define C_DIMMER    RGB(0x86, 0x96, 0xA5)   /* tertiary text              */
#define C_GREEN     RGB(0x0E, 0xB5, 0x7A)
#define C_CYAN      RGB(0x1B, 0x86, 0xE0)
#define C_AMBER     RGB(0xE0, 0x86, 0x0B)
#define C_RED       RGB(0xD8, 0x3A, 0x30)
#define C_GO        RGB(0x1B, 0xD8, 0x8E)
#endif

/* Type that sits on a saturated accent fill: always ink, never white. */
#define C_INK       RGB(0x0A, 0x11, 0x17)
#define C_WHITE     0xFFFF
#define C_BLACK     0x0000

enum { ALIGN_L = 0, ALIGN_C = 1, ALIGN_R = 2 };

/* Back buffer the caller draws into. */
uint16_t *gfx_surface(void);

/* Hands the back buffer to the LCD (async) and flips. */
void gfx_present(void);
void gfx_present_band(int y0, int y1);
void gfx_present_strip(int x, int y, int w, int h);

/* Buffer currently on screen (valid for screenshots). */
const uint16_t *gfx_onscreen(void);

void gfx_clear(uint16_t color);
void gfx_px(int x, int y, uint16_t c);

void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_vline(int x, int y, int h, uint16_t c);
void gfx_fill_rect(int x, int y, int w, int h, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
void gfx_fill_round(int x, int y, int w, int h, int r, uint16_t c);
void gfx_round(int x, int y, int w, int h, int r, uint16_t c);

void gfx_fill_circle(int cx, int cy, int r, uint16_t c);
void gfx_circle(int cx, int cy, int r, uint16_t c);
/* Antialiased annulus; thickness in pixels (float-ish precision via 8.8). */
void gfx_ring_soft(int cx, int cy, int r, int thickness, uint16_t c, uint8_t alpha);
void gfx_disc_soft(int cx, int cy, int r, uint16_t c, uint8_t alpha);

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c);

void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);

/* Text. y is the top of the line box. */
void gfx_text(const Font *f, int x, int y, uint16_t c, const char *s);
void gfx_text_a(const Font *f, int x, int y, uint16_t c, int align, const char *s);
void gfx_text_alpha(const Font *f, int x, int y, uint16_t c, int align, const char *s, uint8_t alpha);
int  gfx_text_w(const Font *f, const char *s);
int  gfx_text_h(const Font *f);

/* Blend a colour over a pixel region with the given alpha (0..255). */
void gfx_tint_rect(int x, int y, int w, int h, uint16_t c, uint8_t alpha);

uint16_t gfx_mix(uint16_t a, uint16_t b, uint8_t t);   /* t=0 -> a, 255 -> b */

#endif
