/*
 * games_quickfire.c - one judgement engine, many games.
 *
 * Every game here is "show a stimulus, decide press / don't press". The
 * lifecycle is shared; a game only supplies a stimulus generator and a rule
 * string, which keeps each variant about 20 lines instead of 120.
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

#define QF_ROUNDS  10
#define QF_ANS     1700u
#define QF_DWELL   520000u

typedef struct {
    const char *big;      /* main stimulus line            */
    const char *sub;      /* optional second line          */
    uint16_t    col;
    uint16_t    col2;
    bool        answer;   /* true: the correct move is press */
} QFRound;

typedef struct {
    const char *name;
    const char *tag;
    const char *rule;
    uint16_t    accent;
    void      (*gen)(int round, QFRound *r);
    bool      (*judge)(uint32_t el_ms);   /* NULL: use r.answer */
    uint8_t   arg;                        /* generator parameter */
} QFDef;

static const QFDef *s_def;

static struct {
    int      round, phase;    /* 0 answering, 1 feedback */
    uint32_t t_state;
    QFRound  r;
    int      hits;
    int32_t  acc;
    uint8_t  grade;
    bool     last_ok;
} qf;

static uint8_t qf_grade(int32_t a)
{
    if (a >= 90) return 0;
    if (a >= 80) return 1;
    if (a >= 70) return 2;
    if (a >= 55) return 3;
    return 4;
}

static void qf_next(uint32_t now)
{
    s_def->gen(qf.round, &qf.r);
    qf.phase = 0;
    qf.t_state = now;
}

static void qf_start_common(void)
{
    memset(&qf, 0, sizeof qf);
    qf_next(bsp_us32());
}

static void qf_update(uint32_t now)
{
    if (qf.round >= QF_ROUNDS) return;
    if (qf.phase == 0 && since(qf.t_state, now) / 1000u > QF_ANS) {
        uint32_t el = since(qf.t_state, now) / 1000u;
        bool should = s_def->judge ? s_def->judge(el) : qf.r.answer;
        qf.last_ok = !should;           /* time-out = "didn't press" */
        qf.phase = 1;
        qf.t_state = now;
        return;
    }
    if (qf.phase == 1 && since(qf.t_state, now) > QF_DWELL) {
        qf.round++;
        if (qf.round >= QF_ROUNDS) {
            qf.acc = qf.hits * 100 / QF_ROUNDS;
            qf.grade = qf_grade(qf.acc);
        } else {
            qf_next(now);
        }
    }
}

static void qf_press(uint32_t now, uint32_t held_us)
{
    (void)held_us;
    if (qf.round >= QF_ROUNDS || qf.phase != 0) return;
    uint32_t el = since(qf.t_state, now) / 1000u;
    bool should = s_def->judge ? s_def->judge(el) : qf.r.answer;
    qf.last_ok = should;
    if (qf.last_ok) qf.hits++;
    qf.phase = 1;
    qf.t_state = now;
    audio_tone(qf.last_ok ? 1300 : 300, 55);
}

static void qf_draw(uint32_t now)
{
    char buf[24];
    gfx_clear(C_BG);

    if (qf.round >= QF_ROUNDS) {
        ui_header(s_def->name, "DONE");
        snprintf(buf, sizeof buf, "%d", (int)qf.acc);
        int nw = gfx_text_w(&font_52, buf);
        gfx_text(&font_52, GFX_W / 2 - nw / 2, 76, C_TEXT, buf);
        gfx_text_a(&font_18, GFX_W / 2, 140, C_DIM, ALIGN_C, "正确率 %");
        int cw = ui_chip_w(grade_name(qf.grade));
        ui_chip(GFX_W / 2 - cw / 2, 170, grade_name(qf.grade), C_INK, grade_color(qf.grade));
        ui_note(s_def->rule);
        return;
    }

    snprintf(buf, sizeof buf, "%d/%d", qf.round + 1, QF_ROUNDS);
    ui_header(s_def->name, buf);

    if (qf.phase == 0) {
        gfx_text_a(&font_52, GFX_W / 2, 74, qf.r.col, ALIGN_C, qf.r.big);
        if (qf.r.sub) gfx_text_a(&font_52, GFX_W / 2, 134, qf.r.col2, ALIGN_C, qf.r.sub);
        ui_note(s_def->rule);
    } else {
        gfx_text_a(&font_52, GFX_W / 2, 74, qf.r.col, ALIGN_C, qf.r.big);
        if (qf.r.sub) gfx_text_a(&font_52, GFX_W / 2, 134, qf.r.col2, ALIGN_C, qf.r.sub);
        gfx_text_a(&font_18, GFX_W / 2, 210, qf.last_ok ? C_GREEN : C_RED, ALIGN_C,
                   qf.last_ok ? "对" : "错");
    }
}

static bool    qf_done(void)  { return qf.round >= QF_ROUNDS; }
static int32_t qf_value(void) { return qf.acc; }
static uint8_t qf_gy(void)    { return qf.grade; }

/* ---------------------------------------------------------- generators */

/* 15. 数位和: digit sum is even -> press */
static void gen_digitsum(int round, QFRound *r)
{
    (void)round;
    int v = 100 + (int)(esp_random() % 900);
    int sum = (v / 100) + (v / 10 % 10) + (v % 10);
    static char b[8];
    snprintf(b, sizeof b, "%d", v);
    r->big = b; r->sub = NULL; r->col = C_TEXT; r->col2 = C_TEXT;
    r->answer = (sum % 2) == 0;
}

/* 16. 两位同色: two swatches, same colour -> press */
static void gen_swatch(int round, QFRound *r)
{
    (void)round;
    static const char *nm[3] = { "红", "绿", "蓝" };
    int a = (int)(esp_random() % 3);
    int b = a;
    if (esp_random() % 2) { do { b = (int)(esp_random() % 3); } while (b == a); }
    static char ba[4], bb[4];
    snprintf(ba, sizeof ba, "%s", nm[a]);
    snprintf(bb, sizeof bb, "%s", nm[b]);
    r->big = ba; r->sub = bb;
    r->col = a == 0 ? C_RED : (a == 1 ? C_GREEN : C_CYAN);
    r->col2 = b == 0 ? C_RED : (b == 1 ? C_GREEN : C_CYAN);
    r->answer = (a == b);
}

/* 17. 箭头一致: arrow glyph and word agree -> press */
static void gen_arrow(int round, QFRound *r)
{
    (void)round;
    int d = (int)(esp_random() % 2);
    bool same = (esp_random() % 2) != 0;
    int word = same ? d : 1 - d;
    r->big = d ? "→" : "←";
    r->sub = word ? "右" : "左";
    r->col = C_TEXT; r->col2 = C_DIM;
    r->answer = same;
}

/* 18. 连续递增: second number larger -> press */
static void gen_grow(int round, QFRound *r)
{
    (void)round;
    int a = 10 + (int)(esp_random() % 80);
    int b = 10 + (int)(esp_random() % 80);
    static char ba[6], bb[6];
    snprintf(ba, sizeof ba, "%d", a);
    snprintf(bb, sizeof bb, "%d", b);
    r->big = ba; r->sub = bb; r->col = C_TEXT; r->col2 = C_TEXT;
    r->answer = b > a;
}

/* 19. 字母数字: stimulus is a letter -> press */
static void gen_alpha(int round, QFRound *r)
{
    (void)round;
    bool letter = (esp_random() % 2) != 0;
    static char b[4];
    if (letter) snprintf(b, sizeof b, "%c", 'A' + (int)(esp_random() % 26));
    else        snprintf(b, sizeof b, "%d", (int)(esp_random() % 10));
    r->big = b; r->sub = NULL; r->col = C_TEXT; r->col2 = C_TEXT;
    r->answer = letter;
}

/* 20. 偶数色块: digit is even AND colour is warm -> press (two rules) */
static void gen_double(int round, QFRound *r)
{
    (void)round;
    int v = (int)(esp_random() % 9) + 1;
    bool warm = (esp_random() % 2) != 0;
    static char b[4];
    snprintf(b, sizeof b, "%d", v);
    r->big = b; r->sub = NULL;
    r->col = warm ? C_AMBER : C_CYAN;
    r->col2 = C_TEXT;
    r->answer = (v % 2 == 0) && warm;
}

/* 21. 时间感知: elapsed long -> press */
static struct { uint32_t t0; } gt;
static void gen_time(int round, QFRound *r)
{
    (void)round;
    gt.t0 = bsp_us32();
    r->big = "…"; r->sub = NULL; r->col = C_DIMMER; r->col2 = C_TEXT;
    r->answer = false;
}
static bool judge_time(uint32_t el_ms)
{
    /* Was 2000 ms, but the answer window is only QF_ANS (1700 ms) - the
     * correct moment never existed. Keep the threshold inside the window. */
    return el_ms >= 1200;
}

/* 22. 重复符号: same symbol as two rounds ago -> press */
static void gen_repeat(int round, QFRound *r)
{
    static const char *last;
    static const char *sy[4] = { "○", "△", "□", "◇" };
    const char *cur;
    if (round > 0 && (esp_random() % 3) == 0 && last) cur = last;
    else cur = sy[esp_random() % 4];
    r->big = cur; r->sub = NULL; r->col = C_TEXT; r->col2 = C_TEXT;
    r->answer = (round > 0) && (cur == last);
    last = cur;
}

/* ---------------------------------------------------------- instances */

#define QF_BOILER(G, DEF, GEN)                          \
    static void G##_start(void) { s_def = &DEF; qf_start_common(); } \
    const Game G = {                                    \
        .name = DEF.name, .tag = DEF.tag, .rule = DEF.rule, \
        .accent = DEF.accent, .metric = METRIC_HIGHER_BETTER, \
        .rounds = QF_ROUNDS, .start = G##_start,        \
        .update = qf_update, .draw = qf_draw, .press = qf_press, \
        .finished = qf_done, .value = qf_value, .grade = qf_gy, \
    }

static const QFDef d_sum = { "数位和", "DIGSUM", "各位数字之和是偶数就按", RGB(0x4C, 0x86, 0xB5), gen_digitsum , NULL, 0 };
static const QFDef d_sw  = { "同色判断", "SWATCH", "两个色块颜色相同就按", RGB(0x4F, 0xA8, 0x94), gen_swatch, NULL, 0 };
static const QFDef d_ar  = { "箭头一致", "ARROW", "箭头方向和文字一致就按", RGB(0xC0, 0x94, 0x57), gen_arrow, NULL, 0 };
static const QFDef d_gr  = { "递增判断", "GROW", "第二个数更大就按", RGB(0x3E, 0xA7, 0x83), gen_grow, NULL, 0 };
static const QFDef d_al  = { "字母数字", "ALPHA", "出现字母就按，数字别按", RGB(0xB6, 0x5A, 0x52), gen_alpha, NULL, 0 };
static const QFDef d_db  = { "双重条件", "DOUBLE", "偶数且暖色才按", RGB(0x8A, 0x79, 0xB8), gen_double, NULL, 0 };
static const QFDef d_tm  = { "时间感知", "TIME", "屏幕空白后稍等约 1 秒再按", RGB(0x7C, 0x6B, 0xB0), gen_time, judge_time, 0 };
static const QFDef d_rp  = { "重复符号", "REPEAT", "符号和上一轮相同就按", RGB(0x4A, 0x9A, 0xB0), gen_repeat, NULL, 0 };

QF_BOILER(GAME_DIGSUM, d_sum, gen_digitsum);
QF_BOILER(GAME_SWATCH, d_sw, gen_swatch);
QF_BOILER(GAME_ARROW, d_ar, gen_arrow);
QF_BOILER(GAME_GROW, d_gr, gen_grow);
QF_BOILER(GAME_ALPHA, d_al, gen_alpha);
QF_BOILER(GAME_DOUBLE, d_db, gen_double);
QF_BOILER(GAME_TIME, d_tm, gen_time);
QF_BOILER(GAME_REPEAT, d_rp, gen_repeat);

/* ---- numeric rule bank: one generator, six games ---------------------- */
static void gen_num(int round, QFRound *r)
{
    (void)round;
    int v = 10 + (int)(esp_random() % 89);
    static char b[6];
    snprintf(b, sizeof b, "%d", v);
    r->big = b; r->sub = NULL; r->col = C_TEXT; r->col2 = C_TEXT;

    switch (s_def->arg) {
    case 0: r->answer = v >= 50; break;              /* 大数 */
    case 1: r->answer = (v % 2) == 1; break;         /* 奇数 */
    case 2: r->answer = (v % 10) == 7; break;        /* 尾数 7 */
    case 3: r->answer = (v % 3) == 0; break;         /* 3 的倍数 */
    case 4: r->answer = (v % 10) >= 5; break;        /* 尾数过半 */
    default: r->answer = (v >= 30 && v <= 60); break;/* 区间内 */
    }
}

/* 元音 */
static void gen_vowel(int round, QFRound *r)
{
    (void)round;
    static const char vow[] = "AEIOU";
    bool is_v = (esp_random() % 2) != 0;
    static char b[4];
    if (is_v) snprintf(b, sizeof b, "%c", vow[esp_random() % 5]);
    else {
        char c;
        do { c = 'A' + (char)(esp_random() % 26); } while (strchr(vow, c));
        snprintf(b, sizeof b, "%c", c);
    }
    r->big = b; r->sub = NULL; r->col = C_TEXT; r->col2 = C_TEXT;
    r->answer = is_v;
}

/* 图形：出现圆形就按 */
static void gen_shape(int round, QFRound *r)
{
    (void)round;
    static const char *sy[4] = { "○", "△", "□", "◇" };
    int i = (int)(esp_random() % 4);
    r->big = sy[i];
    r->sub = NULL; r->col = C_TEXT; r->col2 = C_TEXT;
    r->answer = (i == 0);          /* only the circle counts as correct */
}

static const QFDef d_big  = { "大数判断", "BIG",   "数字大于等于 50 就按", RGB(0x3E, 0xA7, 0x83), gen_num, NULL, 0 };
static const QFDef d_odd  = { "奇数判断", "ODD",   "数字是奇数就按",       RGB(0x4C, 0x86, 0xB5), gen_num, NULL, 1 };
static const QFDef d_sev  = { "尾数七",   "SEVEN", "个位是 7 就按",        RGB(0xB9, 0x8A, 0x4C), gen_num, NULL, 2 };
static const QFDef d_three= { "三倍数",   "TIMES3","是 3 的倍数就按",      RGB(0x7C, 0x6B, 0xB0), gen_num, NULL, 3 };
static const QFDef d_high = { "尾数过半", "HIGH",  "个位大于等于 5 就按",  RGB(0x4F, 0xA8, 0x94), gen_num, NULL, 4 };
static const QFDef d_mid  = { "区间判断", "MID",   "在 30 到 60 之间就按", RGB(0xC0, 0x94, 0x57), gen_num, NULL, 5 };
static const QFDef d_vow  = { "元音判断", "VOWEL", "是元音字母就按",       RGB(0x8A, 0x79, 0xB8), gen_vowel, NULL, 0 };
static const QFDef d_shp  = { "图形判断", "SHAPE", "出现圆形就按",         RGB(0x4A, 0x9A, 0xB0), gen_shape, NULL, 0 };

QF_BOILER(GAME_BIG, d_big, gen_num);
QF_BOILER(GAME_ODD, d_odd, gen_num);
QF_BOILER(GAME_SEVEN, d_sev, gen_num);
QF_BOILER(GAME_TIMES3, d_three, gen_num);
QF_BOILER(GAME_HIGH, d_high, gen_num);
QF_BOILER(GAME_MID, d_mid, gen_num);
QF_BOILER(GAME_VOWEL, d_vow, gen_vowel);
QF_BOILER(GAME_SHAPE, d_shp, gen_shape);
