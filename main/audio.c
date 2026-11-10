/*
 * audio.c - tone engine on top of the vendor I2S + ES8311 drivers.
 *
 * A single task keeps the I2S DMA fed at all times: it writes silence when
 * nothing is playing, so the codec never sees an underrun (which clicks).
 */
#include "audio.h"

#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs.h"
#include "esp_timer.h"

#include "myi2s.h"
#include "es8311.h"

static const char *TAG = "audio";

#define FS        44100
#define FRAMES    512                       /* 11.6 ms per buffer */
#define CH        2
#define TWO_PI    6.28318530718f
#define RAMP      96                        /* ~2 ms de-click ramp */

extern i2s_chan_handle_t tx_handle;         /* from myi2s.c */

/* ---- chiptune sequencer ---------------------------------------------
 * tracks.c holds 30 hand-written 16-step loops, one per game, each with its
 * own root note, tempo and pulse width.  Square wave on purpose: it is the
 * Game Boy voice.  A one-shot SFX (audio_tone) always wins over the loop. */
#include "tracks.h"

static int16_t s_buf[FRAMES * CH];
static volatile int  s_bgm_id = -1;
static const int8_t *s_pat;
static uint8_t  s_pat_len = 16;
static uint8_t  s_root = 40;
static uint8_t  s_duty = 4;          /* pulse width 1..7 */
static uint32_t s_step_us = 125000;      /* 8th note at 240 bpm */
static uint32_t s_step_t0;
static uint8_t  s_step_i;
static volatile uint16_t s_req_hz;
static volatile uint16_t s_req_ms;
static volatile int      s_vol = 70;
static nvs_handle_t      s_nvs;
static bool              s_nvs_ok;
static volatile bool     s_ready;
static int               s_dbg;

static void audio_task(void *arg)
{
    (void)arg;
    float phase = 0.0f, dphase = 0.0f, s_phase = 0.0f;
    int   total = 0, left = 0;

    while (1) {
        /* advance the pattern clock */
        uint16_t bgm_hz = 0;
        if (s_bgm_id >= 0 && s_pat && s_pat_len && s_step_us) {
            uint32_t now = (uint32_t)(esp_timer_get_time());
            if (now - s_step_t0 >= s_step_us) {
                s_step_t0 += s_step_us;
                s_step_i = (uint8_t)((s_step_i + 1) % s_pat_len);
            }
            int8_t semi = s_pat[s_step_i];
            if (semi >= 0) {
                float f = 440.0f * powf(2.0f, ((float)s_root + semi - 69.0f) / 12.0f);
                bgm_hz = (uint16_t)f;
            }
        }

        uint16_t hz = s_req_hz;
        if (hz && left <= 0) {
            s_req_hz = 0;
            total = left = (int)((uint32_t)s_req_ms * FS / 1000u);
            if (total < 64) total = 64;
            dphase = TWO_PI * (float)hz / (float)FS;
            phase = 0.0f;
        }

        float amp = (float)s_vol / 100.0f * 0.80f;   /* headroom: never clip */

        for (int i = 0; i < FRAMES; i++) {
            int32_t v = 0;
            if (left > 0) {
                int done = total - left;
                float env = 1.0f;
                if (done < RAMP)          env = (float)done / RAMP;
                else if (left < RAMP)     env = (float)left / RAMP;

                v = (int32_t)(sinf(phase) * 32767.0f * amp * env);
                phase += dphase;
                if (phase > TWO_PI) phase -= TWO_PI;
                left--;
            }
            /* background: square wave, quieter so it sits under SFX */
            if (bgm_hz && left <= 0) {
                s_phase += (float)bgm_hz / (float)FS;
                if (s_phase > 1.0f) s_phase -= 1.0f;
                float duty = (float)s_duty / 8.0f;
                v += (s_phase < duty ? 1 : -1) * (int32_t)(9000.0f * ((float)s_vol / 100.0f));
            }
            s_buf[i * CH + 0] = (int16_t)v;
            s_buf[i * CH + 1] = (int16_t)v;
        }

        size_t written = 0;
        esp_err_t we = i2s_channel_write(tx_handle, s_buf, sizeof s_buf, &written, portMAX_DELAY);
        if (s_dbg < 3) {
            s_dbg++;
            ESP_LOGW(TAG, "i2s write rc=%d bytes=%u left=%d", (int)we, (unsigned)written, left);
        }
    }
}

void audio_init(void)
{
    if (nvs_open("box1aud", NVS_READWRITE, &s_nvs) == ESP_OK) {
        s_nvs_ok = true;
        int32_t v = 0;
        if (nvs_get_i32(s_nvs, "vol", &v) == ESP_OK) s_vol = (int)v;
        /* A value this low is never deliberate - it comes from a key reading
         * as held at boot, which walks the volume down 10% per boot. */
        if (s_vol < 40) {
            s_vol = 80;
            nvs_set_i32(s_nvs, "vol", s_vol);
            nvs_commit(s_nvs);
        }
    }

    esp_err_t e = myi2s_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "i2s init failed %d", (int)e);
        return;
    }
    e = es8311_init(FS);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "es8311 init failed %d", (int)e);
        return;
    }
    /* es8311_init() leaves the DAC muted (es8311_set_voice_mute(1)) and the
     * volume at 0 - unmute here or the speaker stays silent. */
    /* Fixed DAC gain (the vendor uses 65) and volume control in the digital
     * domain only: chaining both attenuations made 40% effectively silent. */
    es8311_set_voice_volume(65);
    es8311_set_voice_mute(0);
    s_ready = true;

    BaseType_t ok = xTaskCreate(audio_task, "audio", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "audio task created=%d vol=%d", (int)ok, s_vol);
    ESP_LOGI(TAG, "speaker up, volume %d", s_vol);
}

void audio_tone(uint16_t hz, uint16_t ms)
{
    s_req_ms = ms ? ms : 30;
    s_req_hz = hz;
}

void audio_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_vol = pct;
    (void)s_ready;   /* DAC gain stays fixed; volume is digital */
    if (s_nvs_ok) {
        nvs_set_i32(s_nvs, "vol", pct);
        nvs_commit(s_nvs);
    }
}

int audio_get_volume(void) { return s_vol; }

void audio_bgm(int id)
{
    if (id < 0) { audio_bgm_stop(); return; }
    if (id >= TRACK_COUNT) id = TRACK_COUNT - 1;
    const Track *t = &TRACKS[id];
    if (!t->steps || !t->len || !t->step_us) { s_bgm_id = -1; return; }
    s_root = t->root;
    s_pat = t->steps;
    s_pat_len = t->len;
    s_step_us = t->step_us;
    s_duty = t->duty;
    s_step_i = 0;
    s_step_t0 = (uint32_t)(esp_timer_get_time());
    s_bgm_id = id;
}

void audio_bgm_stop(void) { s_bgm_id = -1; }
