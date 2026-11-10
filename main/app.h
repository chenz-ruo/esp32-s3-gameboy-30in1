/*
 * app.h - shell entry points used by main.c
 */
#ifndef APP_H
#define APP_H

#include <stdint.h>

void app_init(void);
void app_frame(uint32_t now_us);
void app_short_press(uint32_t now_us, uint32_t held_us);
void app_long_press(uint32_t now_us);
void app_move(int dir);
const char *app_where(void);
bool app_in_menu(void);
bool app_animating(void);
void app_clear_records(void);

#endif
