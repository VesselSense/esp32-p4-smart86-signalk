#include "audio_feedback.hpp"
#include "bsp/esp32_p4_wifi6_touch_lcd_4b.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio";

static esp_codec_dev_handle_t s_spk = NULL;
static bool s_ready = false;

// Pre-generated click samples (8ms at 16kHz = 128 samples)
static const int CLICK_SAMPLE_RATE = 16000;
static const int CLICK_SAMPLES = 128;    // 8ms
static int16_t s_click_buf[128];

void audio_feedback_init(void)
{
    ESP_LOGI(TAG, "Initializing audio...");

    esp_err_t err = bsp_audio_init(NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_audio_init failed: %d", err);
        return;
    }

    s_spk = bsp_audio_codec_speaker_init();
    if (!s_spk) {
        ESP_LOGW(TAG, "Speaker codec init failed — audio disabled");
        return;
    }

    ESP_LOGI(TAG, "Setting volume...");
    esp_codec_dev_set_out_vol(s_spk, 90);

    // Open codec once and leave it open
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = (uint32_t)CLICK_SAMPLE_RATE,
    };
    err = esp_codec_dev_open(s_spk, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Codec open failed: %d", err);
        return;
    }

    // Pre-generate mechanical click: noise burst with fast exponential decay
    srand(42);  // deterministic for consistent sound
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        float t = (float)i / CLICK_SAMPLE_RATE;
        float decay = expf(-t * 800.0f);
        float noise = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;
        s_click_buf[i] = (int16_t)(24000.0f * decay * noise);
    }

    s_ready = true;
    ESP_LOGI(TAG, "Audio ready — speaker open, vol=90");
}

void audio_feedback_click(void)
{
    if (!s_ready) return;
    esp_codec_dev_write(s_spk, s_click_buf, CLICK_SAMPLES * sizeof(int16_t));
}

// Generate and play a frequency sweep (blocking, ~1.5s)
static void play_sweep(float f0, float f1)
{
    if (!s_ready) return;

    const float dur = 1.5f;
    const int n = (int)(CLICK_SAMPLE_RATE * dur);  // 24000 samples
    const int chunk = 512;
    int16_t buf[512];

    float phase = 0.0f;
    for (int i = 0; i < n; i += chunk) {
        int count = (i + chunk <= n) ? chunk : (n - i);
        for (int j = 0; j < count; j++) {
            int idx = i + j;
            float t = (float)idx / n;  // 0..1
            float freq = f0 + (f1 - f0) * t;
            float fade = 1.0f;
            if (idx < 320) fade = (float)idx / 320.0f;           // 20ms fade in
            if (idx > n - 320) fade = (float)(n - idx) / 320.0f; // 20ms fade out
            phase += 2.0f * 3.14159265f * freq / CLICK_SAMPLE_RATE;
            buf[j] = (int16_t)(2000.0f * fade * sinf(phase));  // 10% volume
        }
        esp_codec_dev_write(s_spk, buf, count * sizeof(int16_t));
    }
}

void audio_feedback_engage(void)
{
    play_sweep(500.0f, 1200.0f);
}

void audio_feedback_disengage(void)
{
    play_sweep(1200.0f, 500.0f);
}

void audio_feedback_invalid(void)
{
    if (!s_ready) return;

    // Two 80ms low buzzes at 250Hz with 60ms gap
    const int buzz_samples = CLICK_SAMPLE_RATE * 80 / 1000;   // 1280
    const int gap_samples  = CLICK_SAMPLE_RATE * 60 / 1000;   // 960
    const int fade_samples = CLICK_SAMPLE_RATE * 5 / 1000;    // 80
    int16_t buf[1280];
    int16_t silence[960];
    memset(silence, 0, sizeof(silence));

    for (int i = 0; i < buzz_samples; i++) {
        float fade = 1.0f;
        if (i < fade_samples) fade = (float)i / fade_samples;
        if (i > buzz_samples - fade_samples) fade = (float)(buzz_samples - i) / fade_samples;
        buf[i] = (int16_t)(6000.0f * fade * sinf(2.0f * 3.14159265f * 250.0f * i / CLICK_SAMPLE_RATE));
    }

    esp_codec_dev_write(s_spk, buf, buzz_samples * sizeof(int16_t));
    esp_codec_dev_write(s_spk, silence, gap_samples * sizeof(int16_t));
    esp_codec_dev_write(s_spk, buf, buzz_samples * sizeof(int16_t));
}
