/*
 * audio.h - speaker feedback through the ES8311 codec.
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

void audio_init(void);

/* Fire a short tone. Safe to call from the game loop; never blocks. */
void audio_tone(uint16_t hz, uint16_t ms);

/* 0..100, persisted in NVS. */
void audio_set_volume(int pct);
int  audio_get_volume(void);

/* Per-game background loop. id < 0 stops it. SFX always cuts through. */
void audio_bgm(int id);
void audio_bgm_stop(void);

#endif
