/*
 * main.c - entry point, frame pacing, key routing, serial protocol.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "bsp.h"
#include "gfx.h"
#include "app.h"
#include "audio.h"

static const char *TAG = "main";

#define LONG_PRESS_US 600000u
#define FRAME_US      16667u

/* ---------------------------------------------------------- screenshots */

static void dump_frame(void)
{
    const uint16_t *fb = gfx_onscreen();
    const int n = GFX_W * GFX_H;
    static uint8_t out[GFX_W * GFX_H / 2];
    int len = 0;

    for (int i = 0; i < n;) {
        uint16_t v = fb[i];
        int run = 1;
        while (i + run < n && fb[i + run] == v && run < 255) run++;
        out[len++] = (uint8_t)run;
        out[len++] = (uint8_t)(v & 0xFF);
        out[len++] = (uint8_t)(v >> 8);
        i += run;
    }

    uint8_t hdr[10] = { 'S', 'S', GFX_W & 0xFF, GFX_W >> 8, GFX_H & 0xFF, GFX_H >> 8,
                        (uint8_t)(len & 0xFF), (uint8_t)(len >> 8),
                        (uint8_t)(len >> 16), (uint8_t)(len >> 24) };
    uart_write_bytes(UART_NUM_0, hdr, sizeof hdr);
    uart_write_bytes(UART_NUM_0, (const char *)out, len);
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(8000));
}

/* ------------------------------------------------------------ key router */

static bool     s_k2_prev;
static uint32_t s_k2_t;
static bool     s_k2_long;

/* K0/K1 do double duty: list scrolling in the menu, volume during play. */
static void step(int dir)
{
    if (app_in_menu()) {
        app_move(dir);
    } else {
        audio_set_volume(audio_get_volume() + (dir > 0 ? 10 : -10));
        audio_tone(1000, 25);
    }
}

static void keys_service(uint32_t now)
{
    bool k2 = bsp_key_down(KEY_INDEX_K2);

    if (k2 && !s_k2_prev) {
        s_k2_t = now;
        s_k2_long = false;
    }
    if (k2 && !s_k2_long && (now - s_k2_t) >= LONG_PRESS_US) {
        s_k2_long = true;
        app_long_press(now);
    }
    if (!k2 && s_k2_prev) {
        if (!s_k2_long) app_short_press(now, now - s_k2_t);
        s_k2_long = false;
    }
    s_k2_prev = k2;

    bsp_keys_poll();   /* debounce + beep service */

    /* K0/K1: scroll the menu (hold accelerates), adjust volume during play */
    static bool     p0, p1;
    static uint32_t t0, t1, next_rep;
    bool k0 = bsp_key_down(KEY_INDEX_K0);
    bool k1 = bsp_key_down(KEY_INDEX_K1);

    /* K0+K1+K2 held together wipes every record (1 s, menu only) */
    static uint32_t combo_t;
    static bool combo_done;
    if (k0 && k1 && k2) {
        if (!combo_t) combo_t = now;
        if (!combo_done && (now - combo_t) > 1000000u) {
            combo_done = true;
            if (app_in_menu()) {
                app_clear_records();
                audio_tone(1400, 140);
            }
        }
        s_k2_prev = true;      /* keep the K2 long-press from firing */
        s_k2_long = true;
        p0 = k0; p1 = k1;
        return;
    }
    combo_t = 0;
    combo_done = false;

    if (k0 && !p0) { step(+1); t0 = now; next_rep = now + 300000u; }
    if (k1 && !p1) { step(-1); t1 = now; next_rep = now + 300000u; }

    if (app_in_menu() && (k0 || k1) && (int32_t)(now - next_rep) >= 0) {
        uint32_t held = now - (k0 ? t0 : t1);
        step(k0 ? +1 : -1);
        next_rep = now + (held > 900000u ? 26000u : 60000u);
    }
    p0 = k0;
    p1 = k1;
}

/* ------------------------------------------------------------------ app */

/* ---- portrait feasibility card -------------------------------------
 * Bypasses the game shell. 12 rows, eased scroll and the same band flush the
 * real menu uses, so what you see here is what the menu will feel like. */
#define PORTRAIT_CARD 0
#define PT_ROWS   12
#define PT_PITCH  44
#define PT_TOP    40

static void portrait_card(void)
{
    int   sel = 0;
    float off_f = 0.0f;
    uint32_t t_prev = bsp_us32();
    const int total = PT_ROWS * PT_PITCH;
    const int view = GFX_H - PT_TOP;

    while (1) {
        uint32_t now = bsp_us32();
        float dt = (float)(now - t_prev) / 1e6f;
        t_prev = now;
        if (dt > 0.1f) dt = 0.1f;

        if (bsp_key_down(KEY_INDEX_K0) && sel < PT_ROWS - 1) { sel++; vTaskDelay(pdMS_TO_TICKS(130)); }
        if (bsp_key_down(KEY_INDEX_K1) && sel > 0)            { sel--; vTaskDelay(pdMS_TO_TICKS(130)); }

        float target = (float)sel * PT_PITCH - (float)(view - PT_PITCH) / 2.0f;
        if (target < 0) target = 0;
        if (target > (float)(total - view)) target = (float)(total - view);

        float k = 1.0f - expf(-dt / 0.085f);
        off_f += (target - off_f) * k;

        gfx_clear(C_BG);
        int off = (int)lroundf(off_f);
        for (int i = 0; i < PT_ROWS; i++) {
            int y = PT_TOP + i * PT_PITCH - off;
            if (y < -PT_PITCH || y > GFX_H) continue;
            bool cov = (i == sel);
            gfx_fill_round(12, y, GFX_W - 24, PT_PITCH - 8, 10,
                           cov ? RGB(0x0E, 0xB5, 0x7A) : C_PANEL);
            char b[8];
            snprintf(b, sizeof b, "%d", i + 1);
            gfx_text(&font_28, 28, y + 6, cov ? C_WHITE : C_TEXT, b);
        }
        gfx_present_band(0, GFX_H);
        vTaskDelay(1);
    }
}

void app_main(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    bsp_init();
    bsp_backlight(1);
    bsp_backlight_level(100);  /* WRDISBV has no effect on this module */

    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0));
    uart_vfs_dev_use_driver(UART_NUM_0);

#if PORTRAIT_CARD
    portrait_card();          /* never returns */
#endif

    audio_init();
    audio_tone(880, 130);
    vTaskDelay(pdMS_TO_TICKS(220));
    audio_tone(1320, 170);

    app_init();
    ESP_LOGI(TAG, "GAMEBOY ready");

    uint32_t next = bsp_us32();
    while (1) {
        uint32_t now = bsp_us32();
        keys_service(now);
        app_frame(now);

        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, 0) == 1) {
            switch (c) {
            case 's': case 'S': dump_frame(); break;
            case 'p': app_short_press(bsp_us32(), 40000); break;   /* tap  */
            case 'P': app_long_press(bsp_us32()); break;           /* hold */
            case 'n': app_move(+1); break;
            case 'i': printf(">where=%s\n", app_where()); break;
            default: break;
            }
        }

        /* While the menu animates, the blocking flush already paces the loop:
         * adding a 60 Hz sleep on top only adds jitter. */
        if (app_animating()) {
            next = bsp_us32();
            continue;
        }

        next += FRAME_US;
        int32_t wait = (int32_t)(next - bsp_us32());
        if (wait > 1000) vTaskDelay(pdMS_TO_TICKS(wait / 1000));
        else if (wait <= 0) next = bsp_us32();
        else taskYIELD();
    }
}
