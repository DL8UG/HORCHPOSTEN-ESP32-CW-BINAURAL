#include "audio_io.h"

#include <math.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "dsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define DMA_DESC    4
#define FADE_N      (DSP_FS / 100)                      /* 10 ms */
#define DMA_MS      (DMA_DESC * AUDIO_BLOCK * 1000 / DSP_FS)

static const char *TAG = "audio";
static i2s_chan_handle_t s_tx, s_rx;
static volatile float s_gain = 1.0f;

/* output fader: set by the UI task, s_level moved by the audio task */
static volatile bool s_mute, s_duck;
static volatile float s_level;

/*
 * Capture for the auto pitch. The UI task numbers each request; the
 * audio task keeps its own position and reports the number it filled.
 * A request that timed out can't fill or end a later one.
 */
static float *volatile s_cap_buf;       /* NULL = no request */
static volatile size_t s_cap_n;
static volatile unsigned s_cap_id, s_cap_done_id;
static SemaphoreHandle_t s_cap_done;

esp_err_t audio_io_init(const board_i2s_pins_t *pins)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESC;
    chan_cfg.dma_frame_num = AUDIO_BLOCK;
    chan_cfg.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx, &s_rx));

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DSP_FS),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCK,
            .ws = pins->ws,
            .dout = pins->dout,
            .din = BOARD_I2S_DIN,
        },
    };
    /* APLL gives an exact MCLK of 256 * 16 kHz for the codec */
    std.clk_cfg.clk_src = I2S_CLK_SRC_APLL;
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
    s_cap_done = xSemaphoreCreateBinary();
    return ESP_OK;
}

void audio_set_digital_gain(int db)
{
    s_gain = powf(10.0f, db / 20.0f);
}

void audio_set_mute(bool mute)
{
    s_mute = mute;
}

void audio_duck(bool duck)
{
    s_duck = duck;
}

void audio_wait_fader(void)
{
    float target = s_mute || s_duck ? 0.0f : 1.0f;
    for (int ms = 0; s_level != target && ms < 100; ms++)
        vTaskDelay(pdMS_TO_TICKS(1));
    /* the blocks already in the DMA buffers still have to play */
    vTaskDelay(pdMS_TO_TICKS(DMA_MS + 4));
}

bool audio_capture(float *buf, size_t n, int timeout_ms)
{
    unsigned id = s_cap_id + 1;
    s_cap_n = n;
    s_cap_id = id;
    s_cap_buf = buf;            /* the audio task starts filling now */
    TickType_t start = xTaskGetTickCount(), wait = pdMS_TO_TICKS(timeout_ms);
    bool ok = false;
    for (TickType_t used = 0; !ok && used < wait; used = xTaskGetTickCount() - start)
        ok = xSemaphoreTake(s_cap_done, wait - used) == pdTRUE && s_cap_done_id == id;
    s_cap_buf = NULL;
    return ok;
}

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t rx[2 * AUDIO_BLOCK], tx[2 * AUDIO_BLOCK];
    static float in[AUDIO_BLOCK], out[2 * AUDIO_BLOCK];
#if CONFIG_HORCH_INPUT_RIGHT
    const int ch = 1;
#else
    const int ch = 0;
#endif
    unsigned overruns = 0;
    unsigned cap_id = 0;        /* request being filled */
    size_t cap_pos = 0;
    for (;;) {
        size_t got = 0;
        if (i2s_channel_read(s_rx, rx, sizeof(rx), &got, portMAX_DELAY) != ESP_OK
            || got != sizeof(rx)) {
            if (++overruns % 100 == 1)
                ESP_LOGW(TAG, "short read (%u bytes)", (unsigned)got);
            continue;
        }
        float g = s_gain / 32768.0f;
        for (int i = 0; i < AUDIO_BLOCK; i++)
            in[i] = rx[2 * i + ch] * g;

        float *cap = s_cap_buf;
        unsigned id = s_cap_id;
        if (cap && id != s_cap_done_id) {
            if (id != cap_id) {
                cap_id = id;
                cap_pos = 0;
            }
            size_t n = s_cap_n;
            for (int i = 0; i < AUDIO_BLOCK && cap_pos < n; i++)
                cap[cap_pos++] = in[i];
            if (cap_pos >= n) {
                s_cap_done_id = id;
                xSemaphoreGive(s_cap_done);
            }
        }

        dsp_process(in, out, AUDIO_BLOCK);
        float target = s_mute || s_duck ? 0.0f : 1.0f, lv = s_level;
        for (int i = 0; i < AUDIO_BLOCK; i++) {
            if (lv != target)
                lv = target > lv ? fminf(lv + 1.0f / FADE_N, target)
                                 : fmaxf(lv - 1.0f / FADE_N, target);
            tx[2 * i] = (int16_t)lrintf(out[2 * i] * lv * 32767.0f);
            tx[2 * i + 1] = (int16_t)lrintf(out[2 * i + 1] * lv * 32767.0f);
        }
        s_level = lv;
        size_t put = 0;
        i2s_channel_write(s_tx, tx, sizeof(tx), &put, portMAX_DELAY);
    }
}

void audio_start(void)
{
    /* core 1, above the UI; the signal chain needs about 15 % of a core */
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 20, NULL, 1);
}
