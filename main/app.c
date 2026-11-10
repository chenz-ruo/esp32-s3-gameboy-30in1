/*
 * app.c - the shell around the games: menu, intro, play, result, records.
 *
 * The shell owns nothing game specific. Adding a game is one entry in GAMES[].
 */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "app.h"
#include "game.h"
#include "games.h"
#include "gfx.h"
#include "font.h"
#include "ui.h"
#include "bsp.h"
#include "audio.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "app";

/* --------------------------------------------------------------- registry */

const Game *const GAMES[] = {
    &GAME_REFLEX,
    &GAME_NOGO,
    &GAME_RHYTHM,
    &GAME_ZONE,
    &GAME_STOPWATCH,
    &GAME_TAPSPRINT,
    &GAME_HOLD,
    &GAME_PARITY,
    &GAME_COMPARE,
    &GAME_COUNT_IT,
    &GAME_MATCH,
    &GAME_STROOP,
    &GAME_MATH,
    &GAME_FLIP,
    &GAME_DIGSUM,
    &GAME_SWATCH,
    &GAME_ARROW,
    &GAME_GROW,
    &GAME_ALPHA,
    &GAME_DOUBLE,
    &GAME_TIME,
    &GAME_REPEAT,
    &GAME_BIG,
    &GAME_ODD,
    &GAME_SEVEN,
    &GAME_TIMES3,
    &GAME_HIGH,
    &GAME_MID,
    &GAME_VOWEL,
    &GAME_SHAPE,
};
const int GAME_COUNT = (int)(sizeof GAMES / sizeof GAMES[0]);

/* ----------------------------------------------------------------- grades */

const char *grade_name(uint8_t g)
{
    static const char *n[GRADE_COUNT] = { "闪电", "优秀", "良好", "一般", "迟钝" };
    return n[g < GRADE_COUNT ? g : GRADE_COUNT - 1];
}

uint16_t grade_color(uint8_t g)
{
    static const uint16_t c[GRADE_COUNT] = {
        RGB(0x0E, 0xB5, 0x7A), RGB(0x1B, 0x86, 0xE0), RGB(0xE0, 0x86, 0x0B),
        RGB(0x4E, 0x60, 0x70), RGB(0xD8, 0x3A, 0x30)
    };
    return c[g < GRADE_COUNT ? g : GRADE_COUNT - 1];
}

uint8_t grade_from_ms(int32_t ms)
{
    if (ms < 180) return 0;
    if (ms < 230) return 1;
    if (ms < 285) return 2;
    if (ms < 350) return 3;
    return 4;
}

/* ---------------------------------------------------------------- records */

#define REC_NS "box1rec"

static int32_t  s_best[32];
static uint16_t s_plays[32];
static nvs_handle_t s_nvs;
static bool s_nvs_ok;

static void rec_load(void)
{
    esp_err_t e = nvs_open(REC_NS, NVS_READWRITE, &s_nvs);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "nvs open failed %d, records are volatile", (int)e);
        return;
    }
    s_nvs_ok = true;
    for (int i = 0; i < GAME_COUNT; i++) {
        char k[8];
        snprintf(k, sizeof k, "b%d", i);
        int32_t v = 0;
        if (nvs_get_i32(s_nvs, k, &v) == ESP_OK) s_best[i] = v;
        snprintf(k, sizeof k, "p%d", i);
        uint16_t p = 0;
        if (nvs_get_u16(s_nvs, k, &p) == ESP_OK) s_plays[i] = p;

        /* A negative "lower is better" record is impossible to beat and can
         * only come from an old bug, so drop it instead of freezing the slot. */
        if (GAMES[i]->metric == METRIC_LOWER_BETTER_MS && s_best[i] < 0) {
            s_best[i] = 0;
            s_plays[i] = 0;
        }
    }
}

static bool rec_better(int gi, int32_t v)
{
    if (s_plays[gi] == 0) return true;
    return GAMES[gi]->metric == METRIC_LOWER_BETTER_MS ? v < s_best[gi] : v > s_best[gi];
}

static void rec_store(int gi, int32_t v)
{
    s_plays[gi]++;
    if (rec_better(gi, v) || s_plays[gi] == 1) s_best[gi] = v;
    if (!s_nvs_ok) return;

    char k[8];
    snprintf(k, sizeof k, "b%d", gi);
    nvs_set_i32(s_nvs, k, s_best[gi]);
    snprintf(k, sizeof k, "p%d", gi);
    nvs_set_u16(s_nvs, k, s_plays[gi]);
    nvs_commit(s_nvs);
}

/* ------------------------------------------------------------------ state */

typedef enum { ST_MENU, ST_INTRO, ST_PLAY, ST_RESULT } app_state;

static bool  s_menu_dirty = true;
static bool  s_full_frame = true;
static volatile bool s_animating;
static int   s_sb_y = -1, s_sb_h;
static int   s_sb_draw_y;

static struct {
    app_state st;
    int       sel;
    int       first;      /* first visible row in the menu */
    int       gi;
    int32_t   value;
    uint8_t   grade;
    bool      new_best;
    uint32_t  t_state;
} A;

#define ROWS 7
#define ROW_H 38

static void enter(app_state s, uint32_t now)
{
    A.st = s;
    A.t_state = now;

    /* every game gets its own loop; stop it outside the round */
    if (s == ST_PLAY) audio_bgm(A.gi);
    else audio_bgm_stop();
    if (s == ST_MENU) {
        s_menu_dirty = true;
        s_full_frame = true;   /* the previous screen still owns the panel */
    }
}

void app_init(void)
{
    rec_load();
    memset(&A, 0, sizeof A);
    A.st = ST_MENU;
}

/* --------------------------------------------------------------- drawing */

static void best_text(int gi, char *out, size_t n)
{
    if (s_plays[gi] == 0) { snprintf(out, n, "--"); return; }
    if (GAMES[gi]->metric == METRIC_LOWER_BETTER_MS) snprintf(out, n, "%dms", (int)s_best[gi]);
    else snprintf(out, n, "%d", (int)s_best[gi]);
}

#define LIST_Y0 44
static float s_sel_f;        /* animated selection position (in rows)  */
static float s_scroll_f;     /* animated scroll offset                 */
static int   s_band0, s_band1;
static int   s_prev_hl = -1;

static int row_y(float gi)
{
    return LIST_Y0 + (int)lroundf((gi - s_scroll_f) * ROW_H);
}

static void draw_menu(uint32_t now)
{
    if (A.sel < A.first) A.first = A.sel;
    if (A.sel >= A.first + ROWS) A.first = A.sel - ROWS + 1;

    /* Frame-rate independent easing: k = 1 - exp(-dt/tau). The menu is not
     * paced to 60 Hz, it runs as fast as draw+flush allows, so a per-frame
     * constant would change speed with load and feel uneven. */
    static uint32_t s_last;
    uint32_t dt_us = s_last ? (uint32_t)(now - s_last) : 16000u;
    if (dt_us > 100000u) dt_us = 100000u;
    s_last = now;
    float k = 1.0f - expf(-(float)dt_us / 85000.0f);   /* tau = 85 ms */

    float ds = (float)A.sel - s_sel_f;
    s_sel_f += ds * k;
    if (fabsf(ds) < 0.01f) s_sel_f = (float)A.sel;

    float dc = (float)A.first - s_scroll_f;
    s_scroll_f += dc * k;
    bool scrolling = fabsf(dc) > 0.01f;
    if (!scrolling) s_scroll_f = (float)A.first;

    gfx_clear(C_BG);


    /* cards first */
    for (int gi = 0; gi < GAME_COUNT; gi++) {
        int y = row_y((float)gi);
        if (y < LIST_Y0 - ROW_H || y > GFX_H - 28) continue;
        gfx_fill_round(UI_PAD, y, GFX_W - 2 * UI_PAD, ROW_H - 6, 9, C_LINE);
        gfx_fill_round(UI_PAD + 1, y + 1, GFX_W - 2 * UI_PAD - 2, ROW_H - 8, 8, C_PANEL);
    }

    /* Selection is a lifted surface plus a thin accent bar, not a slab of
     * saturated colour - that is what separates "device UI" from "toy". */
    int hy = row_y(s_sel_f);
    gfx_fill_round(UI_PAD, hy, GFX_W - 2 * UI_PAD, ROW_H - 6, 9, C_PANEL_HI);
    gfx_fill_round(UI_PAD + 3, hy + 6, 3, ROW_H - 18, 1, GAMES[A.sel]->accent);

    /* row content, colour lerped by how far the block covers the row */
    for (int gi = 0; gi < GAME_COUNT; gi++) {
        int y = row_y((float)gi);
        if (y < LIST_Y0 - ROW_H || y > GFX_H - 28) continue;

        /* Hard switch, not a cross-fade: on a light theme a partial blend
         * reads as "the row above went pale" while the block is still moving. */
        bool covered = (gi == (int)lroundf(s_sel_f));
        const Game *g = GAMES[gi];
        uint16_t c_name = covered ? C_TEXT : C_DIM;
        uint16_t c_sub  = covered ? C_DIM : C_DIMMER;

        char idx[16];
        snprintf(idx, sizeof idx, "%02d", gi + 1);
        gfx_text(&font_14, UI_PAD + 8, y + 6, c_sub, idx);
        gfx_text(&font_18, UI_PAD + 38, y + 3, c_name, g->name);

        char b[12];
        best_text(gi, b, sizeof b);
        gfx_text_a(&font_14, GFX_W - UI_PAD - 8, y + 6, c_sub, ALIGN_R, b);
    }


    /* The list has no clip rectangle, so a row sliding out would paint over
     * the header. Repaint the header band last, on top of the rows. */
    gfx_fill_rect(0, 0, GFX_W, LIST_Y0, C_BG);
    {
        ui_wordmark(UI_PAD, 0);
        gfx_fill_rect(UI_PAD, LIST_Y0 - 6, GFX_W - 2 * UI_PAD, 1, C_LINE);
    }

    /* scrollbar: track is static, the thumb is driven by the same eased
     * scroll offset as the rows, so it glides instead of jumping */
    const int SB_X = GFX_W - 7, SB_W = 4;
    const int SB_Y0 = LIST_Y0 - 2, SB_H = GFX_H - SB_Y0 - 6;
    int track_h = SB_H;
    int thumb_h = GAME_COUNT > ROWS ? track_h * ROWS / GAME_COUNT : track_h;
    if (thumb_h < 26) thumb_h = 26;
    int span = GAME_COUNT > ROWS ? GAME_COUNT - ROWS : 0;
    float prog = span > 0 ? (s_scroll_f / (float)span) : 0.0f;
    if (prog < 0) prog = 0;
    if (prog > 1) prog = 1;
    int thumb_y = SB_Y0 + (int)lroundf((track_h - thumb_h) * prog);

    /* always visible: with nothing to scroll the thumb simply fills the track */
    gfx_fill_round(SB_X, SB_Y0, SB_W, SB_H, SB_W / 2, C_PANEL_HI);
    gfx_fill_round(SB_X, thumb_y, SB_W, thumb_h, SB_W / 2, GAMES[A.sel]->accent);
    s_sb_y = thumb_y;
    s_sb_h = thumb_h;

    /* only the band the block swept through changed; flush just that */
    if (s_full_frame) {
        s_band0 = 0;
        s_band1 = GFX_H;
        s_full_frame = false;
    } else if (scrolling) {
        /* rows physically shift: nothing short of a full frame is correct */
        s_band0 = 0;
        s_band1 = GFX_H;
    } else if (s_prev_hl < 0) {
        s_band0 = 0;
        s_band1 = GFX_H;
    } else {
        int lo = s_prev_hl < hy ? s_prev_hl : hy;
        int hi = s_prev_hl > hy ? s_prev_hl : hy;
        if (hi - lo > 3 * ROW_H) {  /* very long jump: full frame is cheaper */
            s_band0 = 0;
            s_band1 = GFX_H;
            s_prev_hl = hy;
            return;
        }
        s_band0 = 0;               /* include the header band, always */
        s_band1 = hi + ROW_H + 6;
    }
    s_prev_hl = hy;
}

static void draw_intro(void)
{
    const Game *g = GAMES[A.gi];
    gfx_clear(C_BG);

    ui_header(g->name, g->tag);

    gfx_text(&font_14, UI_PAD, 58, C_DIM, "规则");
    ui_wrap(g->rule, UI_PAD, 78, GFX_W - 2 * UI_PAD, 22, 3, C_TEXT);

    char b[12];
    best_text(A.gi, b, sizeof b);
    gfx_text(&font_14, UI_PAD, 158, C_DIM, "最佳");
    gfx_text(&font_28, UI_PAD, 174, g->accent, b);

    char p[24];
    snprintf(p, sizeof p, "%d 局", s_plays[A.gi]);
    gfx_text_a(&font_14, GFX_W - UI_PAD, 190, C_DIMMER, ALIGN_R, p);

    gfx_fill_round(UI_PAD, 226, GFX_W - 2 * UI_PAD, 36, 16, g->accent);
    gfx_text_a(&font_18, GFX_W / 2, 233, C_WHITE, ALIGN_C, "按 K2 开始");

}

static void draw_result(void)
{
    const Game *g = GAMES[A.gi];
    gfx_clear(C_BG);

    ui_header(g->name, g->tag);

    char buf[24];
    snprintf(buf, sizeof buf, "%d", (int)A.value);
    int nw = gfx_text_w(&font_52, buf);
    const char *unit = g->metric == METRIC_LOWER_BETTER_MS ? "ms" : "pt";
    int x0 = GFX_W / 2 - (nw + 8 + gfx_text_w(&font_18, unit)) / 2;
    gfx_text(&font_52, x0, 56, C_TEXT, buf);
    gfx_text(&font_18, x0 + nw + 8, 90, C_DIM, unit);

    int cw = ui_chip_w(grade_name(A.grade));
    ui_chip(GFX_W / 2 - cw / 2, 126, grade_name(A.grade), C_WHITE, grade_color(A.grade));

    if (A.new_best)
        gfx_text_a(&font_18, GFX_W / 2, 166, C_AMBER, ALIGN_C, "新纪录");
    else {
        char b[16];
        best_text(A.gi, b, sizeof b);
        char line[32];
        snprintf(line, sizeof line, "最佳 %s", b);
        gfx_text_a(&font_14, GFX_W / 2, 168, C_DIM, ALIGN_C, line);
    }

}

static void draw(uint32_t now)
{
    switch (A.st) {
    case ST_MENU:   break;   /* drawn by draw_menu with a partial flush */
    case ST_INTRO:  draw_intro(); break;
    case ST_PLAY:   GAMES[A.gi]->draw(now); break;
    case ST_RESULT: draw_result(); break;
    }
}

/* ----------------------------------------------------------------- input */

void app_short_press(uint32_t now, uint32_t held_us)
{
    switch (A.st) {
    case ST_MENU:
        A.gi = A.sel;
        GAMES[A.gi]->start();
        audio_tone(760, 60);
        enter(ST_INTRO, now);
        break;
    case ST_INTRO:
        GAMES[A.gi]->start();
        enter(ST_PLAY, now);
        break;
    case ST_PLAY:
        GAMES[A.gi]->press(now, held_us);
        break;
    case ST_RESULT:
        GAMES[A.gi]->start();
        enter(ST_PLAY, now);
        break;
    }
}

void app_long_press(uint32_t now)
{
    if (A.st == ST_MENU) return;
    enter(ST_MENU, now);
}

bool app_in_menu(void) { return A.st == ST_MENU; }
bool app_animating(void) { return s_animating; }

void app_move(int dir)
{
    if (A.st != ST_MENU) return;
    int want = A.sel + dir;
    if (want < 0) want = 0;                       /* clamp, never wrap around */
    if (want >= GAME_COUNT) want = GAME_COUNT - 1;
    if (want == A.sel) return;                    /* already at the end */
    A.sel = want;
    audio_tone(1150, 22);
    s_menu_dirty = true;
}

void app_clear_records(void)
{
    for (int i = 0; i < GAME_COUNT; i++) {
        s_best[i] = 0;
        s_plays[i] = 0;
        if (!s_nvs_ok) continue;
        char k[8];
        snprintf(k, sizeof k, "b%d", i); nvs_erase_key(s_nvs, k);
        snprintf(k, sizeof k, "p%d", i); nvs_erase_key(s_nvs, k);
    }
    if (s_nvs_ok) nvs_commit(s_nvs);
    s_menu_dirty = true;
    s_full_frame = true;
    ESP_LOGW(TAG, "all records cleared");
}

const char *app_where(void)
{
    static char buf[40];
    static const char *st[4] = { "MENU", "INTRO", "PLAY", "RESULT" };
    if (A.st == ST_MENU) return "MENU";
    snprintf(buf, sizeof buf, "%s:%s", st[A.st], GAMES[A.gi]->tag);
    return buf;
}

void app_frame(uint32_t now)
{
    if (A.st == ST_PLAY) {
        const Game *g = GAMES[A.gi];
        g->update(now);
        if (g->finished()) {
            A.value = g->value();
            A.grade = g->grade();
            A.new_best = rec_better(A.gi, A.value);
            rec_store(A.gi, A.value);
            if (A.grade <= 1) audio_tone(1560, 160);
            else if (A.grade <= 3) audio_tone(1046, 110);
            else audio_tone(220, 200);
            enter(ST_RESULT, now);
            ESP_LOGI(TAG, "%s -> %d (best %d)", g->tag, (int)A.value, (int)s_best[A.gi]);
        }
    }
    if (A.st == ST_MENU) {
        s_sb_draw_y = s_sb_y;
        bool animating = fabsf((float)A.sel - s_sel_f) > 0.01f ||
                         fabsf((float)A.first - s_scroll_f) > 0.01f;
        if (animating || s_menu_dirty) {
            draw_menu(now);
            gfx_present_band(s_band0, s_band1);
            if (s_band1 - s_band0 < GFX_H - 8) {
                int a = s_sb_draw_y < 0 ? s_sb_y : s_sb_draw_y;
                int b = s_sb_y;
                int y0 = (a < b ? a : b) - 3;
                int y1 = (a > b ? a : b) + s_sb_h + 3;
                gfx_present_strip(GFX_W - 7, y0, 4, y1 - y0);
            }
            s_menu_dirty = animating;
            s_animating = animating;
        } else {
            s_animating = false;
        }
    } else {
        draw(now);
        gfx_present();
    }
}
