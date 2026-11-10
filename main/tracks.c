/*
 * tracks.c - 30 hand-written chiptune loops, one per game.
 *
 * Values are semitone offsets from the track's root; _ is a rest. Each track
 * has its own root, tempo and pulse width, so no two games share a voice
 * even when the note shapes are similar.
 */
#include "tracks.h"

#define _ (-1)

/* --- reaction: fast, minor pentatonic, driving ----------------------- */
static const int8_t T_REFLEX[]  = { 0,_,12,_,7,_,12,_,10,_,7,_,5,_,7,_ };
static const int8_t T_NOGO[]    = { 0,0,_,12,_,0,0,_,10,_,0,0,_,7,_ };
static const int8_t T_RHYTHM[]  = { 0,7,12,10,7,5,7,_,12,10,7,5,3,5,7,_ };
static const int8_t T_ZONE[]    = { 0,4,7,12,7,4,0,_,7,11,14,11,7,4,0,_ };
static const int8_t T_TAP[]     = { 12,12,7,12,12,7,12,12,10,10,7,10,10,7,5,3 };
static const int8_t T_HOLD[]    = { 0,_,_,_,7,_,_,_,12,_,_,_,7,_,5,_ };

/* --- memory: slower, lyrical, major -------------------------------- */
static const int8_t T_MATCH[]   = { 0,4,7,4,9,7,4,_,5,9,12,9,5,4,2,_ };
static const int8_t T_COUNT[]   = { 0,2,4,7,4,2,0,_,12,9,7,4,7,9,12,_ };
static const int8_t T_REPEAT[]  = { 7,_,7,_,12,_,7,_,5,_,5,_,9,_,5,_ };
static const int8_t T_SWATCH[]  = { 0,7,12,7,0,7,12,7,2,9,14,9,2,9,14,_ };

/* --- judgement: steady quarter pulse, ii-V flavour ------------------ */
static const int8_t T_PARITY[]  = { 0,_,7,_,10,_,7,_,5,_,9,_,12,_,9,_ };
static const int8_t T_COMPARE[] = { 0,4,7,11,7,4,0,_,7,11,14,11,7,4,0,_ };
static const int8_t T_STROOP[]  = { 0,_,6,_,0,_,6,_,1,_,7,_,1,_,7,_ };
static const int8_t T_MATH[]    = { 0,5,9,5,0,5,9,5,3,8,12,8,3,8,12,_ };
static const int8_t T_FLIP[]    = { 0,7,_,7,12,_,7,_,5,12,_,5,10,_,5,_ };
static const int8_t T_DIGSUM[]  = { 0,2,4,5,7,9,11,12,11,9,7,5,4,2,0,_ };

/* --- precision: sparse, tense, wide intervals ---------------------- */
static const int8_t T_STOPWATCH[] = { 12,_,_,_,_,_,7,_,_,_,_,_,3,_,_,_ };
static const int8_t T_ARROW[]     = { 0,_,12,_,0,_,10,_,0,_,7,_,0,_,5,_ };
static const int8_t T_TIMES[]     = { 0,4,8,0,4,8,0,4,8,12,8,4,0,4,8,_ };

/* --- endurance: repetitive, hypnotic ------------------------------- */
static const int8_t T_GROW[]    = { 0,0,7,7,12,12,7,7,0,0,7,7,5,5,3,3 };
static const int8_t T_HIGH[]    = { 12,_,10,_,12,_,7,_,12,_,10,_,15,_,12,_ };
static const int8_t T_MID[]     = { 0,_,4,_,7,_,4,_,0,_,4,_,7,_,4,_ };
static const int8_t T_BIG[]     = { 0,0,_,0,0,_,5,_,0,0,_,0,7,_,5,_ };

/* --- odd / syncopated ---------------------------------------------- */
static const int8_t T_SEVEN[]   = { 0,_,_,7,_,12,_,_,5,_,_,10,_,7,_,_ };
static const int8_t T_ODD[]     = { 1,_,3,_,6,_,3,_,1,_,6,_,10,_,6,_ };
static const int8_t T_DOUBLE[]  = { 0,7,0,12,5,9,5,14,3,10,3,12,2,9,2,7 };
static const int8_t T_VOWEL[]   = { 0,9,12,9,5,12,16,12,0,9,12,9,7,14,19,_ };
static const int8_t T_SHAPE[]   = { 12,0,12,0,10,0,10,0,7,0,7,0,5,0,3,_ };
static const int8_t T_TIME[]    = { 0,_,_,_,_,_,_,_,7,_,_,_,_,_,_,_ };
static const int8_t T_ALPHA[]   = { 0,2,4,7,9,7,4,2,12,9,7,4,2,0,_,_ };

const Track TRACKS[] = {
    /* index matches the game index exactly - no placeholder, a leading empty
     * entry would be picked up by game 0 and the sequencer would deref NULL */

    /* 00 */ { 52000, 40, 16, 2, T_REFLEX },
    /* 01 */ { 70000, 38, 16, 6, T_NOGO },
    /* 02 */ { 58000, 45, 16, 4, T_RHYTHM },
    /* 03 */ { 62000, 43, 16, 3, T_ZONE },
    /* 04 */ { 96000, 40, 16, 2, T_STOPWATCH },
    /* 05 */ { 48000, 47, 16, 5, T_TAP },
    /* 06 */ {110000, 41, 16, 1, T_HOLD },
    /* 07 */ { 66000, 45, 16, 3, T_PARITY },
    /* 08 */ { 72000, 43, 16, 4, T_COMPARE },
    /* 09 */ { 64000, 48, 16, 2, T_COUNT },
    /* 10 */ { 80000, 43, 16, 3, T_MATCH },
    /* 11 */ { 68000, 47, 16, 6, T_STROOP },
    /* 12 */ { 60000, 40, 16, 4, T_MATH },
    /* 13 */ { 56000, 45, 16, 2, T_FLIP },
    /* 14 */ { 54000, 42, 16, 3, T_DIGSUM },
    /* 15 */ { 76000, 44, 16, 5, T_SWATCH },
    /* 16 */ { 61000, 40, 16, 2, T_ARROW },
    /* 17 */ { 59000, 46, 16, 4, T_GROW },
    /* 18 */ { 63000, 41, 16, 3, T_ALPHA },
    /* 19 */ { 57000, 43, 16, 6, T_DOUBLE },
    /* 20 */ {120000, 38, 16, 1, T_TIME },
    /* 21 */ { 74000, 48, 16, 3, T_REPEAT },
    /* 22 */ { 66000, 36, 16, 5, T_BIG },
    /* 23 */ { 55000, 47, 16, 2, T_ODD },
    /* 24 */ { 50000, 44, 16, 4, T_SEVEN },
    /* 25 */ { 46000, 43, 16, 3, T_TIMES },
    /* 26 */ { 52000, 50, 16, 5, T_HIGH },
    /* 27 */ { 84000, 45, 16, 2, T_MID },
    /* 28 */ { 70000, 46, 16, 6, T_VOWEL },
    /* 29 */ { 54000, 42, 16, 3, T_SHAPE },
};

const int TRACK_COUNT = (int)(sizeof TRACKS / sizeof TRACKS[0]);
