/*
 * game.h - the contract every game implements.
 *
 * A game owns its own state machine. The shell only routes input, gives it a
 * microsecond clock and draws the chrome around it; that keeps 30 games from
 * each re-inventing the loop.
 */
#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

/* Metric a game reports, which decides how "best" is compared. */
typedef enum {
    METRIC_LOWER_BETTER_MS = 0,   /* reaction time, error, ...        */
    METRIC_HIGHER_BETTER,         /* score, streak, accuracy(0-100)   */
} MetricKind;

typedef struct Game {
    const char *name;       /* 中文名, 显示在菜单与结算              */
    const char *tag;        /* 4-6 字母标签                          */
    const char *rule;       /* 一句话规则, 引导页显示                */
    uint16_t    accent;     /* 该游戏的主题色                        */
    MetricKind  metric;
    uint8_t     rounds;     /* 一局多少回合, 用于进度显示            */

    void (*start)(void);                              /* 开局(可重复调用) */
    void (*update)(uint32_t now_us);
    void (*draw)(uint32_t now_us);
    void (*press)(uint32_t now_us, uint32_t held_us);  /* K2 按下时        */
    bool (*finished)(void);
    int32_t (*value)(void);                           /* 本局指标          */
    uint8_t (*grade)(void);                           /* 0..4              */
} Game;

extern const Game *const GAMES[];
extern const int GAME_COUNT;

#define GRADE_COUNT 5
const char *grade_name(uint8_t g);
uint16_t    grade_color(uint8_t g);
uint8_t     grade_from_ms(int32_t ms);

#endif
