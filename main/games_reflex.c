/*
 * games_reflex.c - 极限反应: wait for green, tap as fast as you can.
 *
 * The reference implementation for every other game: it owns a small state
 * machine, reports a metric and a grade, and never touches the menu.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "esp_timer.h"
#include "esp_random.h"
#include "game.h"
#include "gfx.h"
#include "font.h"
#include "bsp.h"
#include "ui.h"
#include "audio.h"

#define RF_ROUNDS 5
#define RF_MIN_WAIT  1200000u
#define RF_SPAN_WAIT 1400000u
#define RF_HIT_MS    1100u
#define RF_FALSE_MS  2600u

typedef enum { RF_WAIT, RF_GO, RF_HIT, RF_FALSE, RF_DONE } rf_state;

static struct {
    rf_state st;
    uint32_t t_state;
    uint32_t go_at;
    uint32_t rt[RF_ROUNDS];
    int      round;
    int      last;
    int      false_starts;
    int32_t  avg;
    uint8_t  grade;
} rf;

static uint32_t since(uint32_t t0, uint32_t now) { return now - t0; }

static void rf_release(void)
{
    rf.st = RF_WAIT;
    rf.t_state = bsp_us32();
    rf.go_at = rf.t_state + RF_MIN_WAIT + (esp_random() % RF_SPAN_WAIT);
}

static void rf_start(void)
{
    memset(&rf, 0, sizeof rf);
    rf.round = 0;
    rf_release();
}

static void rf_update(uint32_t now)
{
    switch (rf.st) {
    case RF_WAIT:
        if ((int32_t)(now - rf.go_at) >= 0) {
            rf.st = RF_GO;
            rf.t_state = now;
            audio_tone(1500, 45);
        }
        break;
    case RF_HIT:
        if (since(rf.t_state, now) > RF_HIT_MS * 1000u) {
            if (rf.round + 1 >= RF_ROUNDS) {
                int32_t sum = 0;
                for (int i = 0; i < RF_ROUNDS; i++) sum += rf.rt[i];
                rf.avg = sum / RF_ROUNDS;
                rf.grade = grade_from_ms(rf.avg);
                rf.st = RF_DONE;
            } else {
                rf.round++;
                rf_release();
            }
        }
        break;
    case RF_FALSE:
        if (since(rf.t_state, now) > RF_FALSE_MS * 1000u) rf_release();
        break;
    default:
        break;
    }
}

static void rf_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    switch (rf.st) {
    case RF_WAIT:
        /* jumped the gun: count it, restart the wait */
        rf.false_starts++;
        rf.st = RF_FALSE;
        rf.t_state = now;
        audio_tone(180, 180);
        break;
    case RF_GO: {
        uint32_t rt = (now - rf.t_state) / 1000u;
        if (rt > 5000) rt = 5000;
        rf.rt[rf.round] = rt;
        rf.last = (int)rt;
        rf.st = RF_HIT;
        rf.t_state = now;
        audio_tone(900, 55);
        break;
    }
    default:
        break;
    }
}

static void rf_draw(uint32_t now)
{
    char buf[48];

    switch (rf.st) {
    case RF_WAIT: {
        gfx_clear(C_BG);
        uint32_t el = since(rf.t_state, now);
        float ph = (float)(el % 900000u) / 900000.0f;
        int r = 34 + (int)(ph * 10);

        gfx_ring_soft(GFX_W / 2, 118, r + 6, 3, C_GREEN, 40);
        gfx_ring_soft(GFX_W / 2, 118, r, 3, C_GREEN, 200);
        gfx_text_a(&font_28, GFX_W / 2, 92, C_TEXT, ALIGN_C, "等待绿光");
        ui_note("看到绿光立即按下");

        snprintf(buf, sizeof buf, "%d / %d", rf.round + 1, RF_ROUNDS);
        gfx_text(&font_14, 16, 12, C_DIM, buf);
        break;
    }
    case RF_GO:
        gfx_clear(C_GO);
        gfx_text_a(&font_52, GFX_W / 2, 56, C_INK, ALIGN_C, "GO");
        ui_note("现在按");
        break;

    case RF_HIT: {
        gfx_clear(C_BG);
        snprintf(buf, sizeof buf, "%d", rf.last);
        int nw = gfx_text_w(&font_52, buf);
        int x0 = GFX_W / 2 - (nw + 8 + gfx_text_w(&font_18, "ms")) / 2;
        gfx_text(&font_52, x0, 70, C_TEXT, buf);
        gfx_text(&font_18, x0 + nw + 8, 104, C_DIM, "ms");

        const char *g = grade_name(grade_from_ms(rf.last));
        int cw = ui_chip_w(g);
        ui_chip(GFX_W / 2 - cw / 2, GFX_W / 2, g, C_WHITE, grade_color(grade_from_ms(rf.last)));
        break;
    }
    case RF_FALSE:
        gfx_clear(RGB(0x2A, 0x0A, 0x0A));
        gfx_text_a(&font_28, GFX_W / 2, 96, RGB(0xD8, 0x3A, 0x30), ALIGN_C, "抢跑");
        ui_note("等绿光出现再按，别抢跑");
        break;

    case RF_DONE: {
        gfx_clear(C_BG);
        snprintf(buf, sizeof buf, "%d", (int)rf.avg);
        int nw = gfx_text_w(&font_52, buf);
        int x0 = GFX_W / 2 - (nw + 8 + gfx_text_w(&font_18, "ms")) / 2;
        gfx_text(&font_52, x0, 34, C_TEXT, buf);
        gfx_text(&font_18, x0 + nw + 8, 68, C_DIM, "ms");
        ui_note("5 回合平均反应时间");

        int cw = ui_chip_w(grade_name(rf.grade));
        ui_chip(GFX_W / 2 - cw / 2, 124, grade_name(rf.grade), C_WHITE, grade_color(rf.grade));

        /* five bars, lower is better */
        int bw = 22, gap = 10;
        int x0b = GFX_W / 2 - (RF_ROUNDS * bw + (RF_ROUNDS - 1) * gap) / 2;
        for (int i = 0; i < RF_ROUNDS; i++) {
            int v = rf.rt[i] ? (int)rf.rt[i] : 3000;
            int h = 8 + (3000 - v) * 26 / 3000;
            if (h < 8) h = 8;
            gfx_fill_round(x0b + i * (bw + gap), 200 - h, bw, h, 3,
                           grade_color(grade_from_ms(v)));
        }
        break;
    }
    }
}

static bool rf_finished(void) { return rf.st == RF_DONE; }
static int32_t rf_value(void) { return rf.avg; }
static uint8_t rf_grade(void) { return rf.grade; }

const Game GAME_REFLEX = {
    .name = "极限反应",
    .tag = "REFLEX",
    .rule = "看到绿光立即按下，5 回合取平均",
    .accent = RGB(0x3E, 0xA7, 0x83),
    .metric = METRIC_LOWER_BETTER_MS,
    .rounds = RF_ROUNDS,
    .start = rf_start,
    .update = rf_update,
    .draw = rf_draw,
    .press = rf_press,
    .finished = rf_finished,
    .value = rf_value,
    .grade = rf_grade,
};
