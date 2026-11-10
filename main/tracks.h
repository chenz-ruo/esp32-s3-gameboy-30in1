/* Musical data for the sequencer: one loop per game. */
#ifndef TRACKS_H
#define TRACKS_H

#include <stdint.h>

typedef struct {
    uint32_t     step_us;   /* length of one step, microseconds */
    uint8_t      root;      /* base note (MIDI numbering)    */
    uint8_t      len;       /* steps in the loop             */
    uint8_t      duty;      /* pulse width 1..7 (of 8)       */
    const int8_t *steps;    /* semitone offsets, -1 = rest   */
} Track;

extern const Track TRACKS[];
extern const int   TRACK_COUNT;

#endif
