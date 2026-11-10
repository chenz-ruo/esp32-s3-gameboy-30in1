/*
 * ui.h - chrome shared by every screen: header, hint bar, chips.
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "gfx.h"

#define UI_PAD 16

void     ui_header(const char *title, const char *meta);
void     ui_hint(const char *left, const char *right);
void     ui_chip(int x, int y, const char *text, uint16_t fg, uint16_t bg);
int      ui_chip_w(const char *text);
void     ui_note(const char *text);
int      ui_wordmark(int x, int y);   /* returns drawn width */
int      ui_wrap(const char *text, int x, int y, int max_w, int line_h, int max_lines, uint16_t color);

#endif
