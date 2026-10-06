/*
 * Horchposten - binaural CW headphone processor for the ESP32-Audio-Kit.
 *
 * LINE IN (transceiver audio) -> signal chain (dsp.c) -> headphones.
 * Keys (short / long press):
 *   KEY1 mode            / swap left-right
 *   KEY2 filter width    / AGC on-off
 *   KEY3 pitch -25 Hz    / stereo width
 *   KEY4 pitch +25 Hz    / auto pitch
 *   KEY5 volume -        / input gain
 *   KEY6 volume +        / mute
 */
#include <stdio.h>

#include "audio_io.h"
#include "board.h"
#include "buttons.h"
#include "codec.h"
#include "dsp.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "pitch.h"
#include "settings.h"
#include "ui_state.h"

#define PITCH_STEP_HZ   25
#define UI_TICK_MS      100
#define MAX_LISTENERS   4

static const char *TAG = "horch";

static ui_state_t s_st;
static ui_listener_t s_listeners[MAX_LISTENERS];

void ui_add_listener(ui_listener_t cb)
{
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (!s_listeners[i]) {
            s_listeners[i] = cb;
            return;
        }
    }
}

static void notify(ui_event_t ev)
{
    for (int i = 0; i < MAX_LISTENERS; i++)
        if (s_listeners[i])
            s_listeners[i](ev, &s_st);
}

static void log_listener(ui_event_t ev, const ui_state_t *st)
{
    static const char *widths[] = { "narrow", "medium", "wide" };
    const char *what = ev == UI_EV_AUTO_OK ? "auto pitch: " : ev == UI_EV_AUTO_FAIL
                       ? "auto pitch: no clear tone, " : "";
    int bw = dsp_filter_bw_hz(st->dsp.filter);
    char filt[8];
    if (bw)
        snprintf(filt, sizeof(filt), "%d", bw);
    else
        snprintf(filt, sizeof(filt), "off");
    ESP_LOGI(TAG, "%smode %s, filter %s, pitch %d Hz, width %s, agc %s, swap %s, "
             "vol %d, gain %d dB%s", what, dsp_mode_name(st->dsp.mode), filt,
             st->dsp.pitch_hz, widths[st->dsp.width], st->dsp.agc ? "on" : "off",
             st->dsp.swap ? "on" : "off", st->volume, st->in_gain_db,
             st->mute ? ", MUTED" : "");
}

static void apply_input_gain(void)
{
    if (codec_set_input_gain(s_st.in_gain_db) == ESP_OK)
        audio_set_digital_gain(0);
    else
        audio_set_digital_gain(s_st.in_gain_db);
}

/* the input gain jumps up to 24 dB: hide the step under the fader, and
 * wait until the samples in the DMA buffers have the new gain */
static void change_input_gain(void)
{
    audio_duck(true);
    audio_wait_fader();
    apply_input_gain();
    vTaskDelay(pdMS_TO_TICKS(30));
    audio_duck(false);
}

/* fade first, then the codec mute (a hard step on the AC101) */
static void set_mute(bool mute)
{
    if (mute) {
        audio_set_mute(true);
        audio_wait_fader();
        codec_set_mute(true);
    } else {
        codec_set_mute(false);
        audio_set_mute(false);
    }
}

/* before audio_start: nothing plays yet, the fader starts at 0 */
static void apply_all(void)
{
    dsp_set_params(&s_st.dsp);
    apply_input_gain();
    codec_set_volume(s_st.volume);
    audio_set_mute(s_st.mute);
    codec_set_mute(s_st.mute);      /* last: the codecs start muted */
}

/* the flash write stalls the audio task (cache off): fade out around it */
static void save_settings(void)
{
    if (!settings_due())
        return;
    audio_duck(true);
    audio_wait_fader();
    settings_save();
    audio_duck(false);
}

static void auto_pitch(void)
{
    static float buf[DSP_FS * PITCH_CAPTURE_MS / 1000];
    const size_t n = sizeof(buf) / sizeof(buf[0]);
    float hz;
    if (audio_capture(buf, n, 2 * PITCH_CAPTURE_MS)
        && pitch_detect(buf, n, DSP_FS, DSP_PITCH_MIN, DSP_PITCH_MAX, &hz)) {
        s_st.dsp.pitch_hz = (int)(hz + 0.5f);
        dsp_set_params(&s_st.dsp);
        led_blink(1);
        notify(UI_EV_AUTO_OK);
        settings_changed(&s_st);
    } else {
        led_error();
        notify(UI_EV_AUTO_FAIL);
    }
}

/* returns false if nothing changed (a limit was hit) */
static bool handle(const button_event_t *ev)
{
    dsp_params_t *d = &s_st.dsp;
    bool dsp = true;        /* a signal chain setting changed */
    bool save = true;
    switch (ev->key * 2 + ev->long_press) {
    case 0:     /* KEY1 short: mode */
        d->mode = (d->mode + 1) % DSP_MODE_COUNT;
        led_blink(d->mode + 1);
        break;
    case 1:     /* KEY1 long: swap */
        d->swap = !d->swap;
        led_blink(d->swap ? 2 : 1);
        break;
    case 2:     /* KEY2 short: filter */
        d->filter = (d->filter + 1) % DSP_FILTER_COUNT;
        led_blink(d->filter + 1);
        break;
    case 3:     /* KEY2 long: AGC */
        d->agc = !d->agc;
        led_blink(d->agc ? 2 : 1);
        break;
    case 4:     /* KEY3 short: pitch down */
    case 6:     /* KEY4 short: pitch up */
    {
        /* clamped by the signal chain's own rule, so the limits can be
         * reached from an auto pitch value off the 25 Hz grid */
        dsp_params_t n = *d;
        n.pitch_hz += ev->key == 2 ? -PITCH_STEP_HZ : PITCH_STEP_HZ;
        dsp_params_sanitize(&n);
        if (n.pitch_hz == d->pitch_hz)
            return false;
        d->pitch_hz = n.pitch_hz;
        led_blink(1);
        break;
    }
    case 5:     /* KEY3 long: width */
        d->width = (d->width + 1) % DSP_WIDTH_COUNT;
        led_blink(d->width + 1);
        break;
    case 7:     /* KEY4 long: auto pitch */
        auto_pitch();
        return true;    /* applied and reported there */
    case 8:     /* KEY5 short: volume down */
    case 10:    /* KEY6 short: volume up */
    {
        int v = s_st.volume + (ev->key == 4 ? -1 : 1);
        if (v < 0 || v > CODEC_VOL_MAX)
            return false;
        s_st.volume = v;
        codec_set_volume(v);
        led_blink(1);
        dsp = false;
        break;
    }
    case 9:     /* KEY5 long: input gain */
        s_st.in_gain_db = (s_st.in_gain_db + UI_GAIN_STEP_DB) % (UI_GAIN_MAX_DB + UI_GAIN_STEP_DB);
        change_input_gain();
        led_blink(s_st.in_gain_db / UI_GAIN_STEP_DB + 1);
        dsp = false;
        break;
    case 11:    /* KEY6 long: mute */
        s_st.mute = !s_st.mute;
        set_mute(s_st.mute);
        led_blink(s_st.mute ? 2 : 1);
        dsp = false;
        save = false;       /* mute is not kept over a restart */
        break;
    default:
        return false;
    }
    if (dsp)
        dsp_set_params(d);
    notify(UI_EV_CHANGED);
    if (save)
        settings_changed(&s_st);
    return true;
}

static void fail_forever(const char *why)
{
    ESP_LOGE(TAG, "%s", why);
    for (;;) {
        led_error();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Horchposten %s", esp_app_get_description()->version);
    board_init();
    led_init();
    settings_load(&s_st);

    codec_type_t codec = codec_probe();
    if (codec == CODEC_NONE)
        fail_forever("no audio codec found - is this an ESP32-Audio-Kit?");
    audio_io_init(codec == CODEC_ES8388 ? &board_i2s_es8388 : &board_i2s_ac101);
    if (codec_init() != ESP_OK)
        fail_forever("codec init failed");

    dsp_init(&s_st.dsp);
    apply_all();        /* volume and mute before the first sample */
    audio_start();

    ui_add_listener(log_listener);
    notify(UI_EV_CHANGED);
    buttons_init();
    led_blink(s_st.dsp.mode + 1);

    for (;;) {
        button_event_t ev;
        if (buttons_get(&ev, UI_TICK_MS) && !handle(&ev))
            led_error();
        save_settings();
    }
}
