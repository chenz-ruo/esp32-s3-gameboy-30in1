/*
 * games_misc.c - precision / endurance: silent stopwatch and a tap sprint.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_random.h"
#include "game.h"
#include "gfx.h"
#include "font.h"
#include "ui.h"
#include "bsp.h"
#include "audio.h"

static inline uint32_t since(uint32_t t0, uint32_t now) { return now - t0; }

/* ================================================== 停表 STOPWATCH */

#define SW_ROUNDS 3
#define SW_MAX_MS 9000

static struct {
    int      round;
    int      phase;      /* 0 running, 1 feedback */
    uint32_t t_state;
    int      target_ms;
    int      err[SW_ROUNDS];
    int      err_n;
    int32_t  avg;
    uint8_t  grade;
    int      last;
} sw;

static uint8_t sw_grade(int32_t ms)
{
    if (ms < 100) return 0;
    if (ms < 200) return 1;
    if (ms < 350) return 2;
    if (ms < 600) return 3;
    return 4;
}

static void sw_next(uint32_t now)
{
    sw.target_ms = 3000 + (int)(esp_random() % 5000);
    sw.t_state = now;
    sw.phase = 0;
}

static void sw_start(void)
{
    memset(&sw, 0, sizeof sw);
    sw_next(bsp_us32());
}

static void sw_update(uint32_t now)
{
    if (sw.round >= SW_ROUNDS) return;

    if (sw.phase == 1) {
        if (since(sw.t_state, now) > 900000u) {
            sw.round++;
            if (sw.round >= SW_ROUNDS) {
                int32_t s = 0;
                for (int i = 0; i < SW_ROUNDS; i++) s += sw.err[i];
                sw.avg = s / SW_ROUNDS;
                sw.grade = sw_grade(sw.avg);
            } else {
                sw_next(now);
            }
        }
        return;
    }
    /* ran past the target without a press: count a miss */
    if ((int)(since(sw.t_state, now) / 1000u) > SW_MAX_MS) {
        sw.err[sw.err_n < SW_ROUNDS ? sw.err_n : SW_ROUNDS - 1] = SW_MAX_MS;
        sw.err_n++;
        sw.last = SW_MAX_MS;
        sw.phase = 1;
        sw.t_state = now;
    }
}

static void sw_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (sw.phase != 0 || sw.round >= SW_ROUNDS) return;

    int el = (int)(since(sw.t_state, now) / 1000u);
    int e = el - sw.target_ms;
    if (e < 0) e = -e;
    if (e > SW_MAX_MS) e = SW_MAX_MS;

    sw.err[sw.err_n < SW_ROUNDS ? sw.err_n : SW_ROUNDS - 1] = e;
    sw.err_n++;
    sw.last = e;
    sw.phase = 1;
    sw.t_state = now;
    audio_tone(e < 200 ? 1400 : 700, 55);
}

static void sw_draw(uint32_t now)
{
    char buf[32];
    gfx_clear(C_BG);

    if (sw.round >= SW_ROUNDS) {
        ui_header("停表", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)sw.avg);
        int nw = gfx_text_w(&font_52, buf);
        int x0 = GFX_W / 2 - (nw + 8 + gfx_text_w(&font_18, "ms")) / 2;
        gfx_text(&font_52, x0, 52, C_TEXT, buf);
        gfx_text(&font_18, x0 + nw + 8, 86, C_DIM, "ms");
        gfx_text_a(&font_14, GFX_W / 2, 124, C_DIM, ALIGN_C, "AVERAGE ERROR");
        int cw = ui_chip_w(grade_name(sw.grade));
        ui_chip(GFX_W / 2 - cw / 2, 144, grade_name(sw.grade), C_WHITE, grade_color(sw.grade));
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", sw.round + 1, SW_ROUNDS);
    ui_header("停表", buf);

    if (sw.phase == 0) {
        snprintf(buf, sizeof buf, "%d.%02d", sw.target_ms / 1000, (sw.target_ms / 10) % 100);
        gfx_text_a(&font_52, GFX_W / 2, 62, C_TEXT, ALIGN_C, buf);
        gfx_text_a(&font_18, GFX_W / 2, 128, C_DIM, ALIGN_C, "秒");
        /* the clock is deliberately invisible: stop it by feel */
        uint32_t el = since(sw.t_state, now);
        int pct = (int)(el / 1000u * 100 / SW_MAX_MS);
        if (pct > 100) pct = 100;
        gfx_fill_round(20, 168, 200, 8, 4, C_PANEL_HI);
        gfx_fill_round(20, 168, 200 * pct / 100, 8, 4, C_AMBER);
        ui_note("到目标秒数就按，屏幕不给提示");
    } else {
        snprintf(buf, sizeof buf, "%+d ms", sw.last);
        gfx_text_a(&font_28, GFX_W / 2, 96, sw_grade(sw.last) <= 1 ? C_GREEN : C_TEXT, ALIGN_C, buf);
        gfx_text_a(&font_14, GFX_W / 2, 146, C_DIM, ALIGN_C, "误差");
    }

}

static bool    sw_done(void)  { return sw.round >= SW_ROUNDS; }
static int32_t sw_value(void) { return sw.avg; }
static uint8_t sw_grade_fn(void) { return sw.grade; }

const Game GAME_STOPWATCH = {
    .name = "停表", .tag = "STOPWATCH",
    .rule = "凭感觉在目标秒数按下，3 次取平均误差",
    .accent = RGB(0x7C, 0x6B, 0xB0),
    .metric = METRIC_LOWER_BETTER_MS, .rounds = SW_ROUNDS,
    .start = sw_start, .update = sw_update, .draw = sw_draw, .press = sw_press,
    .finished = sw_done, .value = sw_value, .grade = sw_grade_fn,
};

/* ==================================================== 连打 TAP SPRINT */

#define TS_MS     5000u
#define TS_WINDOW 900000u

static struct {
    int      taps;
    int      phase;      /* 0 countdown, 1 running, 2 feedback */
    uint32_t t_state;
    uint32_t start_at;
    int32_t  score;
    uint8_t  grade;
} ts;

static uint8_t tap_grade(int32_t n)
{
    if (n >= 60) return 0;
    if (n >= 50) return 1;
    if (n >= 40) return 2;
    if (n >= 28) return 3;
    return 4;
}

static void ts_start(void)
{
    memset(&ts, 0, sizeof ts);
    ts.phase = 0;
    ts.t_state = bsp_us32();
    ts.start_at = ts.t_state + TS_WINDOW;
}

static void ts_update(uint32_t now)
{
    if (ts.phase == 0 && (int32_t)(now - ts.start_at) >= 0) {
        ts.phase = 1;
        ts.t_state = now;
        audio_tone(1200, 60);
    } else if (ts.phase == 1 && since(ts.t_state, now) >= TS_MS * 1000u) {
        ts.phase = 2;
        ts.score = ts.taps;
        ts.grade = tap_grade(ts.score);
        ts.t_state = now;
    }
}

static void ts_press(uint32_t now, uint32_t held_us)
{
    (void)now; (void)held_us;
    if (ts.phase != 1) return;
    ts.taps++;
    if ((ts.taps % 10) == 0) audio_tone(1500, 30);
    else audio_tone(950, 18);
}

static void ts_draw(uint32_t now)
{
    char buf[24];
    gfx_clear(C_BG);

    if (ts.phase == 2) {
        ui_header("连打", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)ts.score);
        gfx_text(&font_52, 130, 52, C_TEXT, buf);
        gfx_text_a(&font_14, GFX_W / 2, 124, C_DIM, ALIGN_C, "TAPS IN 5 s");
        int cw = ui_chip_w(grade_name(ts.grade));
        ui_chip(GFX_W / 2 - cw / 2, 144, grade_name(ts.grade), C_WHITE, grade_color(ts.grade));
        return;
    }

    ui_header("连打", ts.phase == 0 ? "READY" : "GO");

    if (ts.phase == 0) {
        int left = (int)((int32_t)(ts.start_at - now) / 1000);
        if (left < 0) left = 0;
        snprintf(buf, sizeof buf, "%d", (left / 1000) + 1);
        gfx_text_a(&font_52, GFX_W / 2, 80, C_AMBER, ALIGN_C, buf);
        ui_note("准备好，马上开始");
    } else {
        snprintf(buf, sizeof buf, "%d", ts.taps);
        gfx_text_a(&font_52, GFX_W / 2, 64, C_TEXT, ALIGN_C, buf);
        int pct = (int)(since(ts.t_state, now) / 1000u * 100 / TS_MS);
        if (pct > 100) pct = 100;
        gfx_fill_round(20, 148, 200, 10, 5, C_PANEL_HI);
        gfx_fill_round(20, 148, 200 * (100 - pct) / 100, 10, 5, C_GREEN);
    }

}

static bool    ts_done(void)  { return ts.phase == 2; }
static int32_t ts_value(void) { return ts.score; }
static uint8_t ts_grade_fn(void) { return ts.grade; }

const Game GAME_TAPSPRINT = {
    .name = "连打", .tag = "TAP",
    .rule = "5 秒内尽量多按，看手速",
    .accent = RGB(0x4A, 0x9A, 0xB0),
    .metric = METRIC_HIGHER_BETTER, .rounds = 1,
    .start = ts_start, .update = ts_update, .draw = ts_draw, .press = ts_press,
    .finished = ts_done, .value = ts_value, .grade = ts_grade_fn,
};
