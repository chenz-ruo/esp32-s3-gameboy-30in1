/*
 * games_batch3.c - counting, matching, interference, arithmetic, rule flip.
 * Batch of five; same template as the rest of the pack.
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

static uint8_t acc_grade3(int32_t a)
{
    if (a >= 90) return 0;
    if (a >= 80) return 1;
    if (a >= 70) return 2;
    if (a >= 55) return 3;
    return 4;
}

/* ================================================ 计数 COUNT */

#define CT_ROUNDS 5
#define CT_SHOW   320000u      /* per flash */
#define CT_GAP    170000u
#define CT_INPUT  2000000u     /* window to reproduce the count */

static struct {
    int      round, phase;   /* 0 flashing, 1 counting, 2 input, 3 feedback */
    uint32_t t_state;
    int      target, shown, taps;
    int      hits;
    int32_t  acc;
    uint8_t  grade;
    bool     last_ok;
} ct;

static int ct_new_target(void) { return 3 + (int)(esp_random() % 4); }

static void ct_start(void)
{
    memset(&ct, 0, sizeof ct);
    ct.target = ct_new_target();
    ct.phase = 0;
    ct.t_state = bsp_us32();
}

static void ct_update(uint32_t now)
{
    if (ct.round >= CT_ROUNDS) return;
    uint32_t el = since(ct.t_state, now);

    switch (ct.phase) {
    case 0:                                   /* flash target times */
        if (el >= CT_SHOW) {
            ct.shown++;
            ct.t_state = now;
            ct.phase = 10;                    /* gap */
        }
        break;
    case 10:
        if (el >= CT_GAP) {
            if (ct.shown >= ct.target) {
                ct.phase = 1;                 /* prompt for input */
                ct.taps = 0;
            } else {
                ct.phase = 0;
            }
            ct.t_state = now;
        }
        break;
    case 1:
        if (el > 900000u) { ct.phase = 2; ct.t_state = now; }
        break;
    case 2:
        if (el > CT_INPUT) {
            ct.last_ok = (ct.taps == ct.target);
            if (ct.last_ok) ct.hits++;
            ct.phase = 3;
            ct.t_state = now;
        }
        break;
    case 3:
        if (el > 800000u) {
            ct.round++;
            if (ct.round >= CT_ROUNDS) {
                ct.acc = ct.hits * 100 / CT_ROUNDS;
                ct.grade = acc_grade3(ct.acc);
            } else {
                ct.target = ct_new_target();
                ct.shown = 0;
                ct.phase = 0;
                ct.t_state = now;
            }
        }
        break;
    default: break;
    }
}

static void ct_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (ct.phase != 1 && ct.phase != 2) return;
    if (ct.phase == 1) { ct.phase = 2; ct.t_state = now; }
    ct.taps++;
    audio_tone(900 + ct.taps * 40, 25);
}

static void ct_draw(uint32_t now)
{
    char buf[32];
    gfx_clear(C_BG);

    if (ct.round >= CT_ROUNDS) {
        ui_header("计数", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)ct.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(ct.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(ct.grade), C_INK, grade_color(ct.grade));
        ui_note("数对闪了几次，再按同样次数");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", ct.round + 1, CT_ROUNDS);
    ui_header("计数", buf);

    if (ct.phase == 0 || ct.phase == 10) {
        uint16_t c = (ct.phase == 0 && since(ct.t_state, now) < CT_SHOW) ? C_AMBER : C_PANEL_HI;
        gfx_fill_circle(GFX_W / 2, 118, 44, c);
        ui_note("看着闪光的次数");
    } else if (ct.phase == 1 || ct.phase == 2) {
        snprintf(buf, sizeof buf, "%d", ct.taps);
        gfx_text_a(&font_52, GFX_W / 2, 88, C_TEXT, ALIGN_C, buf);
        ui_note("按同样次数，够了就等它结束");
    } else {
        gfx_text_a(&font_28, GFX_W / 2, 104, ct.last_ok ? C_GREEN : C_RED, ALIGN_C,
                   ct.last_ok ? "正确" : "不对");
        snprintf(buf, sizeof buf, "应为 %d 次", ct.target);
        gfx_text_a(&font_18, GFX_W / 2, 148, C_DIM, ALIGN_C, buf);
    }
}

static bool    ct_done(void)  { return ct.round >= CT_ROUNDS; }
static int32_t ct_value(void) { return ct.acc; }
static uint8_t ct_grade(void) { return ct.grade; }

const Game GAME_COUNT_IT = {
    .name = "计数", .tag = "COUNT",
    .rule = "看光圈闪几次，再按同样次数，5 回合",
    .accent = RGB(0xC9, 0x8B, 0x5E),
    .metric = METRIC_HIGHER_BETTER, .rounds = CT_ROUNDS,
    .start = ct_start, .update = ct_update, .draw = ct_draw, .press = ct_press,
    .finished = ct_done, .value = ct_value, .grade = ct_grade,
};

/* ================================================ 延迟匹配 MATCH */

#define MT_ROUNDS 10
#define MT_SHOW   700000u
#define MT_DELAY  900000u
#define MT_ANS    1600u

static struct {
    int round, phase;     /* 0 first, 1 blank, 2 second, 3 feedback */
    uint32_t t_state;
    int a, b;
    int hits;
    int32_t acc;
    uint8_t grade;
    bool last_ok;
} mt;

static void mt_next(uint32_t now)
{
    mt.a = (int)(esp_random() % 5);
    /* half the time the second symbol matches */
    if (esp_random() % 2) mt.b = mt.a;
    else {
        do { mt.b = (int)(esp_random() % 5); } while (mt.b == mt.a);
    }
    mt.phase = 0;
    mt.t_state = now;
}

static void mt_start(void)
{
    memset(&mt, 0, sizeof mt);
    mt_next(bsp_us32());
}

static void mt_update(uint32_t now)
{
    if (mt.round >= MT_ROUNDS) return;
    uint32_t el = since(mt.t_state, now);

    if (mt.phase == 0 && el > MT_SHOW) { mt.phase = 1; mt.t_state = now; return; }
    if (mt.phase == 1 && el > MT_DELAY) { mt.phase = 2; mt.t_state = now; return; }
    if (mt.phase == 2 && el / 1000u > MT_ANS) {
        mt.last_ok = false;
        mt.phase = 3;
        mt.t_state = now;
        return;
    }
    if (mt.phase == 3 && el > 500000u) {
        mt.round++;
        if (mt.round >= MT_ROUNDS) {
            mt.acc = mt.hits * 100 / MT_ROUNDS;
            mt.grade = acc_grade3(mt.acc);
        } else {
            mt_next(now);
        }
    }
}

static void mt_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (mt.phase != 2) return;
    mt.last_ok = (mt.a == mt.b);      /* press = "same" */
    if (mt.last_ok) mt.hits++;
    mt.phase = 3;
    mt.t_state = now;
    audio_tone(mt.last_ok ? 1300 : 300, 55);
}

static void mt_draw(uint32_t now)
{
    char buf[24];
    static const char *sym[5] = { "●", "▲", "■", "◆", "★" };
    gfx_clear(C_BG);

    if (mt.round >= MT_ROUNDS) {
        ui_header("延迟匹配", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)mt.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(mt.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(mt.grade), C_INK, grade_color(mt.grade));
        ui_note("隔一会儿再判断是否相同");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", mt.round + 1, MT_ROUNDS);
    ui_header("延迟匹配", buf);

    if (mt.phase == 0) {
        gfx_text_a(&font_52, GFX_W / 2, 92, C_AMBER, ALIGN_C, sym[mt.a]);
        ui_note("记住这个符号");
    } else if (mt.phase == 1) {
        gfx_text_a(&font_18, GFX_W / 2, 120, C_DIMMER, ALIGN_C, "…");
        ui_note("稍等");
    } else if (mt.phase == 2) {
        gfx_text_a(&font_52, GFX_W / 2, 92, C_TEXT, ALIGN_C, sym[mt.b]);
        ui_note("和刚才一样就按，不一样就别按");
    } else {
        gfx_text_a(&font_28, GFX_W / 2, 110, mt.last_ok ? C_GREEN : C_RED, ALIGN_C,
                   mt.last_ok ? "对" : "错");
        ui_note(mt.a == mt.b ? "其实是一样的" : "其实不一样");
    }
}

static bool    mt_done(void)  { return mt.round >= MT_ROUNDS; }
static int32_t mt_value(void) { return mt.acc; }
static uint8_t mt_grade(void) { return mt.grade; }

const Game GAME_MATCH = {
    .name = "延迟匹配", .tag = "MATCH",
    .rule = "先记符号，隔一会儿再判断是否相同，10 回合",
    .accent = RGB(0x7C, 0x6B, 0xB0),
    .metric = METRIC_HIGHER_BETTER, .rounds = MT_ROUNDS,
    .start = mt_start, .update = mt_update, .draw = mt_draw, .press = mt_press,
    .finished = mt_done, .value = mt_value, .grade = mt_grade,
};

/* ============================================== 抗干扰 STROOP */

#define ST_ROUNDS 10
#define ST_ANS    1500u

static struct {
    int round, phase;
    uint32_t t_state;
    int word, ink;      /* 0 red, 1 green, 2 blue */
    int hits;
    int32_t acc;
    uint8_t grade;
    bool last_ok;
} st;

static const char *st_name_cn[3] = { "红", "绿", "蓝" };
static uint16_t st_color(int i)
{
    return i == 0 ? C_RED : (i == 1 ? C_GREEN : C_CYAN);
}

static void st_next(uint32_t now)
{
    st.word = (int)(esp_random() % 3);
    st.ink = (int)(esp_random() % 3);
    st.phase = 0;
    st.t_state = now;
}

static void st_start(void)
{
    memset(&st, 0, sizeof st);
    st_next(bsp_us32());
}

static void st_update(uint32_t now)
{
    if (st.round >= ST_ROUNDS) return;
    if (st.phase == 0 && since(st.t_state, now) / 1000u > ST_ANS) {
        st.last_ok = (st.word != st.ink);   /* press = "ink matches word" */
        st.phase = 1;
        st.t_state = now;
    }
    if (st.phase == 1 && since(st.t_state, now) > 550000u) {
        st.round++;
        if (st.round >= ST_ROUNDS) {
            st.acc = st.hits * 100 / ST_ROUNDS;
            st.grade = acc_grade3(st.acc);
        } else {
            st_next(now);
        }
    }
}

static void st_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (st.phase != 0 || st.round >= ST_ROUNDS) return;
    bool same = (st.word == st.ink);
    st.last_ok = same;
    if (st.last_ok) st.hits++;
    st.phase = 1;
    st.t_state = now;
    audio_tone(st.last_ok ? 1300 : 300, 55);
}

static void st_draw(uint32_t now)
{
    char buf[24];
    gfx_clear(C_BG);

    if (st.round >= ST_ROUNDS) {
        ui_header("抗干扰", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)st.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(st.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(st.grade), C_INK, grade_color(st.grade));
        ui_note("看字的颜色，不是字义");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", st.round + 1, ST_ROUNDS);
    ui_header("抗干扰", buf);

    gfx_text_a(&font_52, GFX_W / 2, 86, st_color(st.ink), ALIGN_C, st_name_cn[st.word]);

    if (st.phase == 0) ui_note("字的颜色和字义一致才按");
    else ui_note(st.last_ok ? "对" : "错");
}

static bool    st_done(void)  { return st.round >= ST_ROUNDS; }
static int32_t st_value(void) { return st.acc; }
static uint8_t st_grade(void) { return st.grade; }

const Game GAME_STROOP = {
    .name = "抗干扰", .tag = "STROOP",
    .rule = "字的颜色和字义一致就按，不一致别按，10 回合",
    .accent = RGB(0xB6, 0x5A, 0x52),
    .metric = METRIC_HIGHER_BETTER, .rounds = ST_ROUNDS,
    .start = st_start, .update = st_update, .draw = st_draw, .press = st_press,
    .finished = st_done, .value = st_value, .grade = st_grade,
};

/* ================================================ 心算 MATH */

#define MA_ROUNDS 10
#define MA_ANS    2200u

static struct {
    int round, phase;
    uint32_t t_state;
    int a, b, shown;
    bool correct;
    int hits;
    int32_t acc;
    uint8_t grade;
    bool last_ok;
} ma;

static void ma_next(uint32_t now)
{
    ma.a = 2 + (int)(esp_random() % 18);
    ma.b = 2 + (int)(esp_random() % 18);
    ma.correct = (esp_random() % 2) != 0;
    int real = ma.a + ma.b;
    ma.shown = ma.correct ? real : real + ((int)(esp_random() % 5) + 1) * ((esp_random() % 2) ? 1 : -1);
    if (ma.shown == real) ma.shown = real + 1;
    ma.phase = 0;
    ma.t_state = now;
}

static void ma_start(void)
{
    memset(&ma, 0, sizeof ma);
    ma_next(bsp_us32());
}

static void ma_update(uint32_t now)
{
    if (ma.round >= MA_ROUNDS) return;
    if (ma.phase == 0 && since(ma.t_state, now) / 1000u > MA_ANS) {
        ma.last_ok = false;
        ma.phase = 1;
        ma.t_state = now;
    }
    if (ma.phase == 1 && since(ma.t_state, now) > 550000u) {
        ma.round++;
        if (ma.round >= MA_ROUNDS) {
            ma.acc = ma.hits * 100 / MA_ROUNDS;
            ma.grade = acc_grade3(ma.acc);
        } else {
            ma_next(now);
        }
    }
}

static void ma_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (ma.phase != 0 || ma.round >= MA_ROUNDS) return;
    ma.last_ok = ma.correct;            /* press = "equation is right" */
    if (ma.last_ok) ma.hits++;
    ma.phase = 1;
    ma.t_state = now;
    audio_tone(ma.last_ok ? 1300 : 300, 55);
}

static void ma_draw(uint32_t now)
{
    char buf[32];
    gfx_clear(C_BG);

    if (ma.round >= MA_ROUNDS) {
        ui_header("心算", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)ma.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(ma.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(ma.grade), C_INK, grade_color(ma.grade));
        ui_note("等式对就按，错就别按");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", ma.round + 1, MA_ROUNDS);
    ui_header("心算", buf);

    snprintf(buf, sizeof buf, "%d+%d", ma.a, ma.b);
    gfx_text_a(&font_52, GFX_W / 2, 74, C_TEXT, ALIGN_C, buf);
    snprintf(buf, sizeof buf, "= %d", ma.shown);
    gfx_text_a(&font_52, GFX_W / 2, 132, C_DIM, ALIGN_C, buf);

    if (ma.phase == 0) ui_note("算式成立就按，不成立别按");
    else ui_note(ma.last_ok ? "对" : "错");
}

static bool    ma_done(void)  { return ma.round >= MA_ROUNDS; }
static int32_t ma_value(void) { return ma.acc; }
static uint8_t ma_grade(void) { return ma.grade; }

const Game GAME_MATH = {
    .name = "心算", .tag = "MATH",
    .rule = "两位数加法，等式成立就按，10 回合",
    .accent = RGB(0xC0, 0x94, 0x57),
    .metric = METRIC_HIGHER_BETTER, .rounds = MA_ROUNDS,
    .start = ma_start, .update = ma_update, .draw = ma_draw, .press = ma_press,
    .finished = ma_done, .value = ma_value, .grade = ma_grade,
};

/* ============================================== 规则反转 FLIP */

#define FL_ROUNDS 12
#define FL_ANS    1500u

static struct {
    int round, phase;
    uint32_t t_state;
    int dir;            /* 0 left, 1 right */
    bool invert;        /* rule flips every 4 rounds */
    int hits;
    int32_t acc;
    uint8_t grade;
    bool last_ok;
} fl;

static void fl_next(uint32_t now)
{
    fl.dir = (int)(esp_random() % 2);
    fl.invert = (fl.round / 4) % 2;     /* yes/no flips mid-game */
    fl.phase = 0;
    fl.t_state = now;
}

static void fl_start(void)
{
    memset(&fl, 0, sizeof fl);
    fl_next(bsp_us32());
}

static void fl_update(uint32_t now)
{
    if (fl.round >= FL_ROUNDS) return;
    if (fl.phase == 0 && since(fl.t_state, now) / 1000u > FL_ANS) {
        fl.last_ok = false;
        fl.phase = 1;
        fl.t_state = now;
    }
    if (fl.phase == 1 && since(fl.t_state, now) > 500000u) {
        fl.round++;
        if (fl.round >= FL_ROUNDS) {
            fl.acc = fl.hits * 100 / FL_ROUNDS;
            fl.grade = acc_grade3(fl.acc);
        } else {
            fl_next(now);
        }
    }
}

static void fl_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (fl.phase != 0 || fl.round >= FL_ROUNDS) return;
    /* normal rule: press on right arrow; inverted: press on left arrow */
    bool should = fl.invert ? (fl.dir == 0) : (fl.dir == 1);
    fl.last_ok = should;
    if (should) fl.hits++;
    fl.phase = 1;
    fl.t_state = now;
    audio_tone(should ? 1300 : 300, 55);
}

static void fl_draw(uint32_t now)
{
    char buf[32];
    gfx_clear(C_BG);

    if (fl.round >= FL_ROUNDS) {
        ui_header("规则反转", "DONE");
        snprintf(buf, sizeof buf, "%d", (int)fl.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(fl.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(fl.grade), C_INK, grade_color(fl.grade));
        ui_note("中途规则会反转");
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", fl.round + 1, FL_ROUNDS);
    ui_header("规则反转", buf);

    gfx_text_a(&font_52, GFX_W / 2, 96, C_TEXT, ALIGN_C, fl.dir ? "→" : "←");

    if (fl.phase == 0) {
        ui_note(fl.invert ? "反转期：箭头朝左才按" : "箭头朝右就按");
    } else {
        ui_note(fl.last_ok ? "对" : "错");
    }
}

static bool    fl_done(void)  { return fl.round >= FL_ROUNDS; }
static int32_t fl_value(void) { return fl.acc; }
static uint8_t fl_grade(void) { return fl.grade; }

const Game GAME_FLIP = {
    .name = "规则反转", .tag = "FLIP",
    .rule = "先按右箭头，中途规则反转改成按左，12 回合",
    .accent = RGB(0x4C, 0x86, 0xB5),
    .metric = METRIC_HIGHER_BETTER, .rounds = FL_ROUNDS,
    .start = fl_start, .update = fl_update, .draw = fl_draw, .press = fl_press,
    .finished = fl_done, .value = fl_value, .grade = fl_grade,
};
