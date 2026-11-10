/*
 * bsp.h - board support: 8080 LCD, XL9555 keys/backlight/beep, time base.
 */
#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "driver/i2c_master.h"

void bsp_init(void);
i2c_master_bus_handle_t bsp_i2c_bus(void);

/* LCD: push a rectangle from the caller's RGB565 buffer. */
void bsp_lcd_flush(int x, int y, int w, int h, const uint16_t *px);
void bsp_lcd_wait(void);
uint32_t bsp_lcd_trans_count(void);
void bsp_lcd_raw_fill(uint16_t color, uint16_t *buf);
void bsp_lcd_raw_fill_alt(uint16_t color, uint16_t *buf);
void bsp_lcd_disp_off(void);
void bsp_lcd_disp_on(void);
void bsp_backlight(int on);
void bsp_backlight_level(int pct);

/* Buzzer on the expander (active buzzer, on/off only). */
void bsp_beep(int ms);

/* Raw key level, true = pressed. idx: 0 = K0, 1 = K1, 2 = K2. */
bool bsp_key_down(int idx);

/* Debounced key polling; call at 5-10 ms. Returns a bitmask of key events. */
#define KEY_EV_K0 (1u << 0)
#define KEY_EV_K1 (1u << 1)
#define KEY_EV_K2 (1u << 2)
uint32_t bsp_keys_poll(void);

uint32_t bsp_ms(void);
uint32_t bsp_us32(void);

#endif
