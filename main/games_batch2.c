/*
 * games_batch2.c - precision and judgement: hold, parity, comparison.
 *
 * All three follow the shared template: own state machine, METRIC via value(),
 * grade(), one line of guidance through ui_note().
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_random.h"
#include "game.h"
#include "gfx.h"
#include "font.h"
#include "ui.h"
#include "bsp.h"
#include "audio.h"

static inline uint32_t since(uint32_t t0, uint32_t now) { return now - t0; }

static uint8_t err_grade2(int32_t ms)
{
    if (ms < 100) return 0;
    if (ms < 200) return 1;
    if (ms < 350) return 2;
    if (ms < 600) return 3;
    return 4;
}

/* ================================================== 等长按压 HOLD */

#define HD_ROUNDS 3
#define HD_MAX    9000

static struct {
    int      round;
    int      phase;      /* 0 idle, 1 holding, 2 feedback */
    uint32_t t_down;
    uint32_t t_state;
    int      target_ms;
    int      err[HD_ROUNDS];
    int      n;
    int      last;
    int32_t  avg;
    uint8_t  grade;
} hd;

static void hd_next(uint32_t now)
{
    hd.target_ms = 2000 + (int)(esp_random() % 4000);
    hd.phase = 0;
    hd.t_state = now;
}

static void hd_start(void)
{
    memset(&hd, 0, sizeof hd);
    hd_next(bsp_us32());
}

static void hd_update(uint32_t now)
{
    if (hd.round >= HD_ROUNDS) return;
    if (hd.phase == 2 && since(hd.t_state, now) > 900000u) {
        hd.round++;
        if (hd.round >= HD_ROUNDS) {
            int32_t s = 0;
            for (int i = 0; i < HD_ROUNDS; i++) s += hd.err[i];
            hd.avg = s / HD_ROUNDS;
            hd.grade = err_grade2(hd.avg);
        } else {
            hd_next(now);
        }
    }
}

static void hd_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (hd.round >= HD_ROUNDS) return;
    if (hd.phase == 0) {
        hd.phase = 1;
        hd.t_down = now;
        return;
    }
    if (hd.phase != 1) return;

    int held = (int)(since(hd.t_down, now) / 1000u);
    int e = held - hd.target_ms;
    if (e < 0) e = -e;
    if (e > HD_MAX) e = HD_MAX;
    hd.err[hd.n < HD_ROUNDS ? hd.n : HD_ROUNDS - 1] = e;
    hd.n++;
    hd.last = e;
    hd.phase = 2;
    hd.t_state = now;
    audio_tone(e < 200 ? 1400 : 700, 55);
}

static void hd_draw(uint32_t now)
{
    char buf[32];
    gfx_clear(C_BG);

    if (hd.round >= HD_ROUNDS) {
        ui_header("等长按压", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)hd.avg);
        int nw = gfx_text_w(&font_52, buf);
        int x0 = GFX_W / 2 - (nw + 8 + gfx_text_w(&font_18, "ms")) / 2;
        gfx_text(&font_52, x0, 76, C_TEXT, buf);
        gfx_text(&font_18, x0 + nw + 8, 110, C_DIM, "ms");
        int cw = ui_chip_w(grade_name(hd.grade));
        ui_chip(GFX_W / 2 - cw / 2, 160, grade_name(hd.grade), C_INK, grade_color(hd.grade));
        ui_note("目标时长与实际按压的平均误差");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", hd.round + 1, HD_ROUNDS);
    ui_header("等长按压", buf);

    if (hd.phase == 1) {
        int held = (int)(since(hd.t_down, now) / 1000u);
        snprintf(buf, sizeof buf, "%d.%d", held / 1000, (held / 100) % 10);
        gfx_text_a(&font_52, GFX_W / 2, 100, C_GREEN, ALIGN_C, buf);
        ui_note("保持住，到目标时长再松开");
    } else if (hd.phase == 2) {
        snprintf(buf, sizeof buf, "%+d ms", hd.last);
        gfx_text_a(&font_28, GFX_W / 2, 120, err_grade2(hd.last) <= 1 ? C_GREEN : C_TEXT,
                   ALIGN_C, buf);
        ui_note(hd.last < 200 ? "很准" : "再稳一点");
    } else {
        snprintf(buf, sizeof buf, "%d.%d 秒", hd.target_ms / 1000, (hd.target_ms / 100) % 10);
        gfx_text_a(&font_52, GFX_W / 2, 92, C_TEXT, ALIGN_C, buf);
        ui_note("按住 K2，默数到这个时长再松手");
    }
}

static bool    hd_done(void)  { return hd.round >= HD_ROUNDS; }
static int32_t hd_value(void) { return hd.avg; }
static uint8_t hd_grade(void) { return hd.grade; }

const Game GAME_HOLD = {
    .name = "等长按压", .tag = "HOLD",
    .rule = "按住 K2 默数到目标时长再松开，3 次取平均误差",
    .accent = RGB(0x8A, 0x79, 0xB8),
    .metric = METRIC_LOWER_BETTER_MS, .rounds = HD_ROUNDS,
    .start = hd_start, .update = hd_update, .draw = hd_draw, .press = hd_press,
    .finished = hd_done, .value = hd_value, .grade = hd_grade,
};

/* ================================================== 奇偶判定 PARITY */

#define PA_ROUNDS  10
#define PA_WINDOW  1400u
#define PA_DWELL   600000u

static struct {
    int      round;
    int      phase;      /* 0 showing, 1 answered, 2 gap */
    uint32_t t_state;
    int      value;
    int      hits;
    int32_t  acc;
    uint8_t  grade;
    bool     last_ok;
} pa;

static uint8_t acc_grade2(int32_t a)
{
    if (a >= 90) return 0;
    if (a >= 80) return 1;
    if (a >= 70) return 2;
    if (a >= 55) return 3;
    return 4;
}

static void pa_next(uint32_t now)
{
    pa.value = 10 + (int)(esp_random() % 90);
    if (esp_random() % 2) pa.value |= 1;              /* mix parities */
    pa.phase = 0;
    pa.t_state = now;
}

static void pa_start(void)
{
    memset(&pa, 0, sizeof pa);
    pa_next(bsp_us32());
}

static void pa_update(uint32_t now)
{
    if (pa.round >= PA_ROUNDS) return;

    if (pa.phase == 1 && since(pa.t_state, now) > PA_DWELL) {
        pa.phase = 2;
        pa.t_state = now;
        return;
    }
    if (pa.phase == 2 && since(pa.t_state, now) > 300000u) {
        pa.round++;
        if (pa.round >= PA_ROUNDS) {
            pa.acc = pa.hits * 100 / PA_ROUNDS;
            pa.grade = acc_grade2(pa.acc);
        } else {
            pa_next(now);
        }
        return;
    }
    if (pa.phase == 0 && since(pa.t_state, now) / 1000u > PA_WINDOW) {
        pa.last_ok = false;                            /* timed out */
        pa.phase = 1;
        pa.t_state = now;
    }
}

static void pa_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (pa.round >= PA_ROUNDS || pa.phase != 0) return;
    bool even = (pa.value % 2) == 0;
    pa.last_ok = even;                                 /* press = "even" */
    if (pa.last_ok) pa.hits++;
    pa.phase = 1;
    pa.t_state = now;
    audio_tone(pa.last_ok ? 1300 : 300, 55);
}

static void pa_draw(uint32_t now)
{
    char buf[24];
    gfx_clear(C_BG);

    if (pa.round >= PA_ROUNDS) {
        ui_header("奇偶判定", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)pa.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(pa.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(pa.grade), C_INK, grade_color(pa.grade));
        ui_note("偶数按，奇数不按");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", pa.round + 1, PA_ROUNDS);
    ui_header("奇偶判定", buf);

    snprintf(buf, sizeof buf, "%d", pa.value);
    uint16_t col = C_TEXT;
    if (pa.phase == 1) col = pa.last_ok ? C_GREEN : C_RED;
    gfx_text_a(&font_52, GFX_W / 2, 92, col, ALIGN_C, buf);

    if (pa.phase == 0) {
        ui_note("是偶数就按，奇数别按");
    } else if (pa.phase == 1) {
        ui_note(pa.last_ok ? "对" : "错");
    } else {
        ui_note("");
    }
}

static bool    pa_done(void)  { return pa.round >= PA_ROUNDS; }
static int32_t pa_value(void) { return pa.acc; }
static uint8_t pa_grade(void) { return pa.grade; }

const Game GAME_PARITY = {
    .name = "奇偶判定", .tag = "PARITY",
    .rule = "屏幕给出数字，是偶数就按，奇数别按，10 回合",
    .accent = RGB(0x4F, 0xA8, 0x94),
    .metric = METRIC_HIGHER_BETTER, .rounds = PA_ROUNDS,
    .start = pa_start, .update = pa_update, .draw = pa_draw, .press = pa_press,
    .finished = pa_done, .value = pa_value, .grade = pa_grade,
};

/* =============================================== 大小比较 COMPARE */

#define CM_ROUNDS 10
#define CM_WINDOW 1500u
#define CM_DWELL  600000u

static struct {
    int      round;
    int      phase;
    uint32_t t_state;
    int      a, b;
    int      hits;
    int32_t  acc;
    uint8_t  grade;
    bool     last_ok;
} cm;

static void cm_next(uint32_t now)
{
    cm.a = 10 + (int)(esp_random() % 90);
    do { cm.b = 10 + (int)(esp_random() % 90); } while (cm.b == cm.a);
    cm.phase = 0;
    cm.t_state = now;
}

static void cm_start(void)
{
    memset(&cm, 0, sizeof cm);
    cm_next(bsp_us32());
}

static void cm_update(uint32_t now)
{
    if (cm.round >= CM_ROUNDS) return;
    if (cm.phase == 1 && since(cm.t_state, now) > CM_DWELL) {
        cm.phase = 2;
        cm.t_state = now;
        return;
    }
    if (cm.phase == 2 && since(cm.t_state, now) > 300000u) {
        cm.round++;
        if (cm.round >= CM_ROUNDS) {
            cm.acc = cm.hits * 100 / CM_ROUNDS;
            cm.grade = acc_grade2(cm.acc);
        } else {
            cm_next(now);
        }
        return;
    }
    if (cm.phase == 0 && since(cm.t_state, now) / 1000u > CM_WINDOW) {
        cm.last_ok = false;
        cm.phase = 1;
        cm.t_state = now;
    }
}

static void cm_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (cm.round >= CM_ROUNDS || cm.phase != 0) return;
    cm.last_ok = cm.a > cm.b;
    if (cm.last_ok) cm.hits++;
    cm.phase = 1;
    cm.t_state = now;
    audio_tone(cm.last_ok ? 1300 : 300, 55);
}

static void cm_draw(uint32_t now)
{
    char buf[32];
    gfx_clear(C_BG);

    if (cm.round >= CM_ROUNDS) {
        ui_header("大小比较", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)cm.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(cm.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(cm.grade), C_INK, grade_color(cm.grade));
        ui_note("左边大就按，否则不按");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", cm.round + 1, CM_ROUNDS);
    ui_header("大小比较", buf);

    char la[8], lb[8];
    snprintf(la, sizeof la, "%d", cm.a);
    snprintf(lb, sizeof lb, "%d", cm.b);
    uint16_t cola = C_TEXT, colb = C_TEXT;
    if (cm.phase >= 1) {
        cola = (cm.a > cm.b) ? C_GREEN : C_DIM;
        colb = (cm.b > cm.a) ? C_GREEN : C_DIM;
    }
    gfx_text(&font_52, 26, 96, cola, la);
    gfx_text_a(&font_28, GFX_W / 2, 116, C_DIMMER, ALIGN_C, "vs");
    gfx_text_a(&font_52, GFX_W - 26, 96, colb, ALIGN_R, lb);

    if (cm.phase == 0) ui_note("左边更大就按，否则别按");
    else if (cm.phase == 1) ui_note(cm.last_ok ? "对" : "错");
}

static bool    cm_done(void)  { return cm.round >= CM_ROUNDS; }
static int32_t cm_value(void) { return cm.acc; }
static uint8_t cm_grade(void) { return cm.grade; }

const Game GAME_COMPARE = {
    .name = "大小比较", .tag = "COMPARE",
    .rule = "两个数字同时出现，左边更大就按，10 回合",
    .accent = RGB(0xC0, 0x94, 0x57),
    .metric = METRIC_HIGHER_BETTER, .rounds = CM_ROUNDS,
    .start = cm_start, .update = cm_update, .draw = cm_draw, .press = cm_press,
    .finished = cm_done, .value = cm_value, .grade = cm_grade,
};
