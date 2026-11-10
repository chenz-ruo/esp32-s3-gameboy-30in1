/*
 * bsp.c - ESP32-S3 BOX1 board support.
 *
 * The panel hangs off an 8080 8-bit parallel bus (not SPI): esp_lcd's i80
 * driver does the heavy lifting, we only own the framebuffer.
 */
#include "bsp.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "bsp";

static i2c_master_bus_handle_t s_i2c;
static i2c_master_dev_handle_t s_io_exp;
static esp_lcd_panel_handle_t  s_panel;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_io_handle_t s_io_alt;   /* DC polarity inverted */
static SemaphoreHandle_t       s_lcd_done;
static volatile uint32_t       s_trans_done;
static uint32_t                s_pending_id;

static uint16_t s_out_shadow = 0x0000;   /* XL9555 output port latch */
static int64_t  s_beep_until_us;

/* ------------------------------------------------------------------ i2c */

static esp_err_t xl_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_io_exp, buf, sizeof buf, 50);
}

static esp_err_t xl_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_io_exp, &reg, 1, val, 1, 50);
}

static void xl_write16(uint16_t bits)
{
    s_out_shadow = bits;
    xl_write(2, (uint8_t)(bits & 0xFF));
    xl_write(3, (uint8_t)(bits >> 8));
}

static void xl_pin_set(uint16_t bit, int on)
{
    uint16_t v = on ? (s_out_shadow | bit) : (s_out_shadow & ~bit);
    if (v != s_out_shadow) xl_write16(v);
}

static void xl_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_PIN_SDA,
        .scl_io_num = I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = XL9555_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c, &dev_cfg, &s_io_exp));

    /* beeper is active low, so its idle level is 1 - writing 0 here is what
     * made it sound continuously. */
    uint16_t idle = XL_BEEP_ACTIVE_LOW ? XL_BIT_BEEP : 0x0000;

    xl_write16(idle);
    xl_write(6, XL_CFG_PORT0);
    xl_write(7, XL_CFG_PORT1);
    uint8_t rb0 = 0xFF, rb1 = 0xFF;
    esp_err_t e0 = xl_read(2, &rb0);
    esp_err_t e1 = xl_read(3, &rb1);
    ESP_LOGI(TAG, "xl9555 ready, out readback %02X %02X (err %d/%d)",
             rb0, rb1, (int)e0, (int)e1);
}

/* ------------------------------------------------------------------ lcd */

static bool lcd_trans_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    s_trans_done++;
    xSemaphoreGiveFromISR(s_lcd_done, &woken);
    return woken == pdTRUE;
}

static void lcd_init(void)
{
    s_lcd_done = xSemaphoreCreateBinary();

    /* RD (read strobe) is not part of esp_lcd's i80 bus config, so it is left
     * floating unless we own it. Held low/floating, the panel keeps its data
     * bus in read mode and silently drops every write - the vendor BSP drives
     * it high before creating the bus, and without this the panel stays
     * "garbled" no matter how correct the rest of the init is. */
    gpio_config_t rd = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pin_bit_mask = 1ull << LCD_PIN_RD,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&rd);
    gpio_set_level(LCD_PIN_RD, 1);

    esp_lcd_i80_bus_handle_t i80_bus;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num = LCD_PIN_DC,
        .wr_gpio_num = LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
            LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
        },
        .bus_width = 8,
        .max_transfer_bytes = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &i80_bus));

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags.swap_color_bytes = 1,
        .on_color_trans_done = lcd_trans_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, &io));
    s_io = io;

    /* second IO on the same bus with D/C inverted, to test the one electrical
     * difference between "commands work" and "pixels do not" */
    esp_lcd_panel_io_i80_config_t io_alt = io_cfg;
    io_alt.dc_levels.dc_cmd_level = 1;
    io_alt.dc_levels.dc_data_level = 0;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_alt, &s_io_alt));

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 0, 0));

    /* Match the vendor BSP exactly: this panel needs COLMOD 0x65, while
     * esp_lcd's st7789 driver sends 0x55 for 16 bpp. Getting this wrong
     * produces exactly the "garbled" look we saw. */
    esp_lcd_panel_io_tx_param(io, 0x36, (uint8_t[]){ 0x00 }, 1);
    esp_lcd_panel_io_tx_param(io, 0x3A, (uint8_t[]){ 0x65 }, 1);

    /* native portrait 240x320: no MV rotation, so a row we write is a row the
     * panel scans - the tearing line becomes horizontal instead of vertical */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_LOGI(TAG, "st7789 up (320x240, i80 8-bit), dc=%d wr=%d cs=%d",
             LCD_PIN_DC, LCD_PIN_WR, LCD_PIN_CS);
}

void bsp_lcd_flush(int x, int y, int w, int h, const uint16_t *px)
{
    if (w <= 0 || h <= 0) return;
    esp_err_t e = esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, px);
    /* Record the completion id AFTER queueing: if the previous transfer
     * finished in between, capturing it before would make the wait below
     * return immediately and the next frame would overwrite the buffer
     * while the panel was still reading it (half-updated rows). */
    s_pending_id = s_trans_done + 1;
    static int logged;
    if (e != ESP_OK && logged < 3) {
        logged++;
        ESP_LOGE(TAG, "draw_bitmap failed: %d", (int)e);
    }
}

uint32_t bsp_lcd_trans_count(void) { return s_trans_done; }

/* Raw fill straight over the i80 IO, bypassing esp_lcd's window maths:
 * native 240x320 window, one solid colour over the whole panel. */
void bsp_lcd_raw_fill(uint16_t color, uint16_t *buf)
{
    esp_lcd_panel_io_tx_param(s_io, 0x2A, (uint8_t[]){ 0x00, 0x00, 0x00, 0xEF }, 4);
    esp_lcd_panel_io_tx_param(s_io, 0x2B, (uint8_t[]){ 0x00, 0x00, 0x01, 0x3F }, 4);
    for (int i = 0; i < 240 * 320; i++) buf[i] = color;
    esp_lcd_panel_io_tx_color(s_io, 0x2C, buf, 240 * 320 * 2);
}

void bsp_lcd_raw_fill_alt(uint16_t color, uint16_t *buf)
{
    esp_lcd_panel_io_tx_param(s_io_alt, 0x2A, (uint8_t[]){ 0x00, 0x00, 0x00, 0xEF }, 4);
    esp_lcd_panel_io_tx_param(s_io_alt, 0x2B, (uint8_t[]){ 0x00, 0x00, 0x01, 0x3F }, 4);
    for (int i = 0; i < 240 * 320; i++) buf[i] = color;
    esp_lcd_panel_io_tx_color(s_io_alt, 0x2C, buf, 240 * 320 * 2);
}

/* Raw command test: DISPOFF/DISPON. A working command path blanks and
 * restores the panel, which tells us whether anything is being accepted. */
void bsp_lcd_disp_off(void) { esp_lcd_panel_io_tx_param(s_io, 0x28, NULL, 0); }
void bsp_lcd_disp_on(void)  { esp_lcd_panel_io_tx_param(s_io, 0x29, NULL, 0); }

void bsp_lcd_wait(void)
{
    /* Wait on the completion counter, not the semaphore: a binary semaphore can
     * carry a stale token and return before the transfer finishes, which tears
     * the frame (visible as lagging pixels while the menu slides). */
    int guard = 0;
    while ((int32_t)(s_trans_done - s_pending_id) < 0 && guard++ < 150) vTaskDelay(1);
}

void bsp_init(void)
{
    gpio_config_t boot = {
        .pin_bit_mask = 1ULL << KEY_PIN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&boot);

    xl_init();
    lcd_init();

    /* Both LEDs off: LEDB on GPIO4, LEDR on the expander (P1_0). They are
     * active low on this board, so the idle level is 1. */
    gpio_config_t led = {
        .pin_bit_mask = 1ull << 4,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&led);
    gpio_set_level(4, 1);
    xl_pin_set(XL_BIT_LED_R, 1);
    xl_pin_set(XL_BIT_SPK_EN, 1);     /* active HIGH: 1 enables the amp */
}

i2c_master_bus_handle_t bsp_i2c_bus(void) { return s_i2c; }

void bsp_backlight(int on) { xl_pin_set(XL_BIT_LCD_BL, on ? 1 : 0); }

/* ST7789 WRDISBV: only works if the module wires its backlight driver to the
 * controller's PWM output. Harmless when unsupported. */
void bsp_backlight_level(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    uint8_t v = (uint8_t)(pct * 255 / 100);
    esp_lcd_panel_io_tx_param(s_io, 0x51, &v, 1);
}

static void beep_out(int on)
{
    xl_pin_set(XL_BIT_BEEP, XL_BEEP_ACTIVE_LOW ? !on : on);
}

/* The buzzer is not part of the product: feedback goes through the ES8311
 * speaker path. Keep the pin parked at its idle (silent) level. */
void bsp_beep(int ms)
{
    (void)ms;
}

static void beep_service(void) { }

uint32_t bsp_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
uint32_t bsp_us32(void) { return (uint32_t)esp_timer_get_time(); }

/* ----------------------------------------------------------------- keys */

bool bsp_key_down(int idx)
{
    if (idx == KEY_INDEX_K0) return gpio_get_level(KEY_PIN_BOOT) == 0;

    uint8_t p0 = 0xFF;
    if (xl_read(0, &p0) != ESP_OK) return false;
    if (idx == KEY_INDEX_K1) return (p0 & (XL_BIT_K1 & 0xFF)) == 0;
    return (p0 & (XL_BIT_K2 & 0xFF)) == 0;
}

uint32_t bsp_keys_poll(void)
{
    static bool stable[3];
    static bool raw_last[3];
    static uint32_t t_change[3];
    static bool inited;

    uint32_t now = bsp_ms();
    uint32_t events = 0;

    for (int i = 0; i < 3; i++) {
        bool raw = bsp_key_down(i);
        if (!inited) { raw_last[i] = raw; stable[i] = raw; continue; }
        if (raw != raw_last[i]) { raw_last[i] = raw; t_change[i] = now; }
        else if (raw != stable[i] && (now - t_change[i]) >= 12) {
            stable[i] = raw;
            if (raw) events |= (1u << i);
        }
    }
    inited = true;

    beep_service();
    return events;
}
