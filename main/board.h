/*
 * board.h - ATK ESP32 AI BOX1 (DNESP32S3B) hardware map.
 *
 * Verified against the official IO allocation sheet and the vendor BSP that
 * ships in the A-disk resource pack (10_lcd / 02_key examples):
 *   LCD   : ST7789, 320x240 panel driven in native 240x320 portrait,
 *           8080 8-bit parallel bus (no swap_xy / no mirror)
 *   Keys  : silkscreen K0 = BOOT on GPIO0, K1/K2 on the XL9555 expander
 *   Audio : ES8311 over I2S; BEEP + speaker enable on the expander
 *           (speaker enable is active HIGH, BEEP is active LOW)
 */
#ifndef BOARD_H
#define BOARD_H

/* ---- LCD: 8080 8-bit parallel ---- */
#define LCD_H_RES   320
#define LCD_V_RES   240

#define LCD_PIN_CS  1
#define LCD_PIN_DC  2
#define LCD_PIN_WR  42
#define LCD_PIN_RD  41
#define LCD_PIN_D0  40
#define LCD_PIN_D1  39
#define LCD_PIN_D2  38
#define LCD_PIN_D3  12
#define LCD_PIN_D4  11
#define LCD_PIN_D5  10
#define LCD_PIN_D6   9
#define LCD_PIN_D7  46

/* ---- I2C (shared by the expander, codec and touch) ---- */
#define I2C_PIN_SCL 45
#define I2C_PIN_SDA 48
#define I2C_PORT    0

/* ---- XL9555 expander (address 0x20, 16 bits) ---- */
#define XL9555_ADDR        0x20
#define XL_BIT_AP_INT      0x0001
#define XL_BIT_QMA_INT     0x0002
#define XL_BIT_BEEP        0x0004
/* The vendor BSP names these by expander pin, not by silkscreen:
 *   KEY0_IO (P0_4) is silkscreen K1, KEY1_IO (P0_3) is silkscreen K2,
 *   and silkscreen K0 is the BOOT key on GPIO0. */
#define XL_BIT_K2          0x0008   /* P0_3 -> silkscreen K2 */
#define XL_BIT_K1          0x0010   /* P0_4 -> silkscreen K1 */
#define XL_BIT_SPK_EN      0x0020   /* active HIGH: 1 = amp on */

/* BEEP is driven low to sound: writing 0 at boot made it buzz forever. */
#define XL_BEEP_ACTIVE_LOW 1
#define XL_BIT_CTP_RST     0x0040
#define XL_BIT_LCD_BL      0x0080   /* active high */
#define XL_BIT_LED_R       0x0100
#define XL_BIT_CTP_INT     0x0200

/* direction: 1 = input */
#define XL_CFG_PORT0       0x1B     /* AP/QMA/KEY1/KEY0 inputs            */
#define XL_CFG_PORT1       0xFE     /* CTP_INT input                      */

/* ---- keys ---- */
#define KEY_PIN_BOOT       0        /* silkscreen K0, active low          */
#define KEY_INDEX_K0       0        /* GPIO0  */
#define KEY_INDEX_K1       1        /* XL P0_4 */
#define KEY_INDEX_K2       2        /* XL P0_3 */

#endif
