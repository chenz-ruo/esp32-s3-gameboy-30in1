/*
 * games_react.c - reaction family: impulse control, rhythm ring, moving zone.
 *
 * These three share the same shape: a timed stimulus, one button, a metric and
 * a grade. Keeping them in one file makes the differences obvious.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "esp_random.h"
#include "game.h"
#include "gfx.h"
#include "font.h"
#include "ui.h"
#include "audio.h"
#include "bsp.h"

static inline uint32_t since(uint32_t t0, uint32_t now) { return now - t0; }

/* ===================================================== 1. 抢跑抑制 GO/NO-GO */

#define NG_TRIALS    10
#define NG_GAP_MIN   600000u
#define NG_GAP_SPAN  900000u
#define NG_WINDOW_MS 900u

static struct {
    int      trial;
    bool     nogo;
    bool     answered;
    bool     correct;
    int      hits;
    int      rt_sum;
    int      rt_n;
    uint32_t t_state;
    uint32_t show_at;
    uint8_t  grade;
    int32_t  acc;
    int      flash;         /* 0 none, 1 good, 2 bad */
    uint32_t t_flash;
} ng;

static uint8_t acc_grade(int32_t a)
{
    if (a >= 90) return 0;
    if (a >= 80) return 1;
    if (a >= 70) return 2;
    if (a >= 55) return 3;
    return 4;
}

static void ng_next(uint32_t now)
{
    if (ng.trial >= NG_TRIALS) {
        ng.acc = ng.hits * 100 / NG_TRIALS;
        ng.grade = acc_grade(ng.acc);
        return;
    }
    ng.nogo = (esp_random() % 100) < 35;      /* 35% no-go */
    ng.answered = false;
    ng.correct = false;
    ng.t_state = now;
    ng.show_at = now + NG_GAP_MIN + (esp_random() % NG_GAP_SPAN);
}

static void ng_start(void)
{
    memset(&ng, 0, sizeof ng);
    ng_next(bsp_us32());
}

static void ng_finish_trial(uint32_t now)
{
    ng.trial++;
    ng.flash = ng.correct ? 1 : 2;
    ng.t_flash = now;
    ng.t_state = now;
    ng.show_at = now + 700000u;   /* feedback dwell */
}

static void ng_update(uint32_t now)
{
    if (ng.trial >= NG_TRIALS) return;

    if (ng.flash) {
        if (since(ng.t_flash, now) > 550000u) {
            ng.flash = 0;
            ng_next(now);
        }
        return;
    }

    int32_t dt = (int32_t)(now - ng.show_at);
    if (dt < 0 || ng.answered) return;          /* gap, or already answered */

    if ((uint32_t)dt / 1000u > NG_WINDOW_MS) {
        /* window closed: withholding is correct only on a stop signal */
        ng.correct = ng.nogo;
        if (ng.nogo) ng.hits++;
        audio_tone(ng.nogo ? 1100 : 300, 70);
        ng_finish_trial(now);
    }
}

static void ng_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (ng.flash || ng.trial >= NG_TRIALS) return;
    if ((int32_t)(now - ng.show_at) < 0) {
        /* pressed during the gap: counts as a false start on a go trial */
        ng.correct = false;
        ng_finish_trial(now);
        return;
    }
    if (ng.answered) return;
    ng.answered = true;

    if (ng.nogo) {
        ng.correct = false;                    /* pressed on a stop signal */
    } else {
        int rt = (int)((now - ng.show_at) / 1000u);
        ng.correct = true;
        ng.rt_sum += rt;
        ng.rt_n++;
        ng.hits++;
    }
    ng_finish_trial(now);
}

static void ng_draw(uint32_t now)
{
    gfx_clear(C_BG);

    char meta[24];
    snprintf(meta, sizeof meta, "%d/%d", ng.trial + 1, NG_TRIALS);
    ui_header("抢跑抑制", meta);

    bool shown = (int32_t)(now - ng.show_at) >= 0;

    if (ng.flash) {
        bool good = ng.flash == 1;
        gfx_fill_round(20, 92, 200, 56, 16, good ? C_GREEN : C_RED);
        gfx_text_a(&font_28, GFX_W / 2, 104, C_WHITE, ALIGN_C,
                   ng.nogo ? (good ? "正确" : "误按") : (good ? "正确" : "漏按"));
    } else if (!shown) {
        gfx_ring_soft(GFX_W / 2, 116, 40, 3, C_CYAN, 180);
        gfx_text_a(&font_28, GFX_W / 2, 96, C_TEXT, ALIGN_C, "准备");
        ui_note("绿圈按，红圈不按");
    } else if (ng.nogo) {
        gfx_fill_circle(GFX_W / 2, 116, 46, C_RED);
        ui_note("不要按");
    } else {
        gfx_fill_circle(GFX_W / 2, 116, 46, C_GREEN);
        ui_note("按下去");
    }

}

static bool    ng_done(void)  { return ng.trial >= NG_TRIALS; }
static int32_t ng_value(void) { return ng.acc; }
static uint8_t ng_grade(void) { return ng.grade; }

const Game GAME_NOGO = {
    .name = "抢跑抑制", .tag = "NO-GO",
    .rule = "绿圈按、红圈停，10 回合看正确率",
    .accent = RGB(0xB6, 0x5A, 0x52),
    .metric = METRIC_HIGHER_BETTER, .rounds = NG_TRIALS,
    .start = ng_start, .update = ng_update, .draw = ng_draw, .press = ng_press,
    .finished = ng_done, .value = ng_value, .grade = ng_grade,
};

/* ======================================================== 2. 节奏环 RHYTHM */

#define RH_TAPS    8
#define RH_R0      78.0f
#define RH_RT      26.0f
#define RH_RMIN    6.0f
#define RH_PERIOD  1450000u     /* full shrink, us */
#define RH_DWELL   700000u

static struct {
    int      tap;
    int      phase;      /* 0 = shrinking, 1 = feedback */
    uint32_t t_state;
    int      err_ms[RH_TAPS];
    int      err_n;
    int32_t  avg_err;
    uint8_t  grade;
    int      last;
} rh;

static uint8_t err_grade(int32_t ms)
{
    if (ms < 40) return 0;
    if (ms < 70) return 1;
    if (ms < 110) return 2;
    if (ms < 170) return 3;
    return 4;
}

static void rh_start(void)
{
    memset(&rh, 0, sizeof rh);
    rh.phase = 0;
    rh.t_state = bsp_us32();
}

static float rh_radius(uint32_t el)
{
    float k = (float)el / (float)RH_PERIOD;
    if (k > 1.0f) k = 1.0f;
    return RH_R0 - (RH_R0 - RH_RMIN) * k;
}

static void rh_update(uint32_t now)
{
    if (rh.tap >= RH_TAPS) return;

    if (rh.phase == 0) {
        if (since(rh.t_state, now) > RH_PERIOD) {
            /* missed this ring entirely */
            rh.err_ms[rh.err_n < RH_TAPS ? rh.err_n : RH_TAPS - 1] = 400;
            rh.err_n++;
            rh.last = 400;
            rh.phase = 1;
            rh.t_state = now;
        }
    } else {
        if (since(rh.t_state, now) > RH_DWELL) {
            rh.tap++;
            if (rh.tap >= RH_TAPS) {
                int32_t s = 0;
                for (int i = 0; i < RH_TAPS; i++) s += rh.err_ms[i];
                rh.avg_err = s / RH_TAPS;
                rh.grade = err_grade(rh.avg_err);
            } else {
                rh.phase = 0;
                rh.t_state = now;
            }
        }
    }
}

static void rh_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (rh.phase != 0 || rh.tap >= RH_TAPS) return;

    uint32_t el = since(rh.t_state, now);
    float r = rh_radius(el);
    /* radius error -> milliseconds, using the actual shrink speed.
     * Ring still outside the target means the player was early. */
    float speed = (RH_R0 - RH_RMIN) / (float)RH_PERIOD;   /* px per us */
    int err = (int)(fabsf(r - RH_RT) / speed / 1000.0f);
    if (err > 400) err = 400;
    if (r > RH_RT) err = -err;

    /* store the magnitude: the metric is "average error", and a negative
     * value here would become an unbeatable best score (lower is better). */
    rh.err_ms[rh.err_n < RH_TAPS ? rh.err_n : RH_TAPS - 1] = err < 0 ? -err : err;
    rh.err_n++;
    rh.last = err;
    rh.phase = 1;
    rh.t_state = now;
    audio_tone(abs(err) < 60 ? 1400 : 700, 55);
}

static void rh_draw(uint32_t now)
{
    if (rh.tap >= RH_TAPS) {
        char buf[24];
        gfx_clear(C_BG);
        char meta[24];
        snprintf(meta, sizeof meta, "%d taps", RH_TAPS);
        ui_header("节奏环", meta);

        snprintf(buf, sizeof buf, "%d", (int)rh.avg_err);
        int nw = gfx_text_w(&font_52, buf);
        int x0 = GFX_W / 2 - (nw + 8 + gfx_text_w(&font_18, "ms")) / 2;
        gfx_text(&font_52, x0, 52, C_TEXT, buf);
        gfx_text(&font_18, x0 + nw + 8, 86, C_DIM, "ms");
        gfx_text_a(&font_14, GFX_W / 2, 124, C_DIM, ALIGN_C, "AVERAGE ERROR");

        int cw = ui_chip_w(grade_name(rh.grade));
        ui_chip(GFX_W / 2 - cw / 2, 144, grade_name(rh.grade), C_WHITE, grade_color(rh.grade));
        return;
    }

    gfx_clear(C_BG);
    char meta[24];
    snprintf(meta, sizeof meta, "%d/%d", rh.tap + 1, RH_TAPS);
    ui_header("节奏环", meta);

    const int cx = GFX_W / 2, cy = 118;
    uint32_t el = rh.phase == 0 ? since(rh.t_state, now) : RH_PERIOD;
    float r = rh_radius(el);

    if (rh.phase == 0) ui_note("收缩环碰到内圈时按下");
    gfx_ring_soft(cx, cy, (int)RH_RT, 3, C_DIM, 190);
    gfx_ring_soft(cx, cy, (int)r, 3, C_CYAN, 235);

    if (rh.phase == 1) {
        char b[24];
        snprintf(b, sizeof b, "%+d ms", rh.last);
        gfx_text_a(&font_28, cx, 168, err_grade(abs(rh.last)) <= 1 ? C_GREEN : C_TEXT,
                   ALIGN_C, b);
        ui_note(rh.last < 0 ? "早了一点" : "晚了一点");
    }

}

static bool    rh_done(void)  { return rh.tap >= RH_TAPS; }
static int32_t rh_value(void) { return rh.avg_err; }
static uint8_t rh_grade(void) { return rh.grade; }

const Game GAME_RHYTHM = {
    .name = "节奏环", .tag = "RHYTHM",
    .rule = "收缩环碰到目标圈时按下，8 次取平均误差",
    .accent = RGB(0x4C, 0x86, 0xB5),
    .metric = METRIC_LOWER_BETTER_MS, .rounds = RH_TAPS,
    .start = rh_start, .update = rh_update, .draw = rh_draw, .press = rh_press,
    .finished = rh_done, .value = rh_value, .grade = rh_grade,
};

/* ====================================================== 3. 移动区间 ZONE */

#define ZN_ROUNDS 8
#define ZN_X0     20
#define ZN_X1     220
#define ZN_SPEED  0.42f         /* px per ms */
#define ZN_DWELL  650000u

static struct {
    int      round;
    int      phase;
    uint32_t t_state;
    float    pos;           /* cursor x */
    float    dir;
    int      lo, hi;        /* keeper zone */
    int      score_sum;
    int      last_score;
    int32_t  score;
    uint8_t  grade;
} zn;

static int zone_width(int round)
{
    int w = 58 - round * 4;
    return w < 26 ? 26 : w;
}

static void zn_next(uint32_t now)
{
    int w = zone_width(zn.round);
    zn.lo = ZN_X0 + 10 + (int)(esp_random() % (uint32_t)(ZN_X1 - ZN_X0 - w - 20));
    zn.hi = zn.lo + w;
    zn.pos = (float)ZN_X0;
    zn.dir = 1.0f;
    zn.t_state = now;
    zn.phase = 0;
}

static void zn_start(void)
{
    memset(&zn, 0, sizeof zn);
    zn_next(bsp_us32());
}

static void zn_update(uint32_t now)
{
    if (zn.round >= ZN_ROUNDS) return;

    if (zn.phase == 1) {
        if (since(zn.t_state, now) > ZN_DWELL) {
            zn.round++;
            if (zn.round >= ZN_ROUNDS) {
                zn.score = zn.score_sum / ZN_ROUNDS;
                zn.grade = acc_grade(zn.score);
            } else {
                zn_next(now);
            }
        }
        return;
    }

    uint32_t dt = since(zn.t_state, now);
    zn.t_state = now;
    zn.pos += zn.dir * ZN_SPEED * (float)dt / 1000.0f;
    if (zn.pos > ZN_X1) { zn.pos = ZN_X1; zn.dir = -1.0f; }
    if (zn.pos < ZN_X0) { zn.pos = ZN_X0; zn.dir = 1.0f; }
}

static void zn_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (zn.phase != 0 || zn.round >= ZN_ROUNDS) return;

    int p = (int)zn.pos;
    int sc;
    if (p < zn.lo || p > zn.hi) {
        sc = 0;
    } else {
        int mid = (zn.lo + zn.hi) / 2;
        int half = (zn.hi - zn.lo) / 2;
        int d = abs(p - mid);
        sc = 100 - (half > 0 ? d * 60 / half : 0);   /* centre = 100, edge = 40 */
    }
    zn.last_score = sc;
    zn.score_sum += sc;
    zn.phase = 1;
    zn.t_state = now;
}

static void zn_draw(uint32_t now)
{
    if (zn.round >= ZN_ROUNDS) {
        gfx_clear(C_BG);
        ui_header("移动区间", "DONE");
        char buf[24];
        snprintf(buf, sizeof buf, "%d", (int)zn.score);
        gfx_text(&font_52, 150, 52, C_TEXT, buf);
        gfx_text(&font_14, GFX_W / 2, 120, C_DIM, "SCORE / 100");
        int cw = ui_chip_w(grade_name(zn.grade));
        ui_chip(GFX_W / 2 - cw / 2, 140, grade_name(zn.grade), C_WHITE, grade_color(zn.grade));
        return;
    }

    gfx_clear(C_BG);
    char meta[24];
    snprintf(meta, sizeof meta, "%d/%d", zn.round + 1, ZN_ROUNDS);
    ui_header("移动区间", meta);

    /* track */
    gfx_fill_round(ZN_X0 - 6, 96, ZN_X1 - ZN_X0 + 12, 26, 13, C_PANEL_HI);
    /* keeper zone */
    gfx_fill_round(zn.lo, 96, zn.hi - zn.lo, 26, 13, RGB(0x0E, 0xB5, 0x7A));
    /* cursor */
    int p = (int)zn.pos;
    gfx_fill_round(p - 3, 88, 6, 42, 3, C_TEXT);

    if (zn.phase == 1) {
        char b[24];
        snprintf(b, sizeof b, "%+d", zn.last_score);
        gfx_text_a(&font_52, GFX_W / 2, 132, zn.last_score >= 70 ? C_GREEN : C_TEXT, ALIGN_C, b);
    } else {
        ui_note("光标进入绿区时按下");
    }

}

static bool    zn_done(void)  { return zn.round >= ZN_ROUNDS; }
static int32_t zn_value(void) { return zn.score; }
static uint8_t zn_grade(void) { return zn.grade; }

const Game GAME_ZONE = {
    .name = "移动区间", .tag = "ZONE",
    .rule = "光标进入绿区时按下，越靠中心分越高",
    .accent = RGB(0xB9, 0x8A, 0x4C),
    .metric = METRIC_HIGHER_BETTER, .rounds = ZN_ROUNDS,
    .start = zn_start, .update = zn_update, .draw = zn_draw, .press = zn_press,
    .finished = zn_done, .value = zn_value, .grade = zn_grade,
};
