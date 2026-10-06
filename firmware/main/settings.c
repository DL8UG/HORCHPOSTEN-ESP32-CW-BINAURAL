#include "settings.h"

#include <string.h>

#include "codec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#define NS          "horch"
#define KEY         "state"
#define VERSION     1
#define SAVE_DELAY_US (3 * 1000 * 1000)

static const char *TAG = "settings";

typedef struct {
    uint32_t version;
    ui_state_t st;
} stored_t;

static ui_state_t s_pending;
static int64_t s_due;           /* 0 = nothing to save */
static ui_state_t s_saved;      /* what the flash holds */
static bool s_have_saved;

/* field by field: a struct compare would include the padding */
static bool same(const ui_state_t *a, const ui_state_t *b)
{
    const dsp_params_t *x = &a->dsp, *y = &b->dsp;
    return x->mode == y->mode && x->filter == y->filter && x->width == y->width
        && x->pitch_hz == y->pitch_hz && x->agc == y->agc && x->swap == y->swap
        && a->volume == b->volume && a->in_gain_db == b->in_gain_db;
}

void settings_defaults(ui_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->dsp.mode = DSP_MODE_PITCH;
    st->dsp.filter = DSP_FILTER_500;
    st->dsp.width = DSP_WIDTH_MEDIUM;
    st->dsp.pitch_hz = 600;
    st->dsp.agc = true;
    st->dsp.swap = false;
    st->volume = 20;
    st->in_gain_db = 0;
    st->mute = false;
}

bool settings_load(ui_state_t *st)
{
    settings_defaults(st);
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
        return false;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK)
        return false;
    stored_t s;
    size_t len = sizeof(s);
    err = nvs_get_blob(h, KEY, &s, &len);
    nvs_close(h);
    if (err != ESP_OK || len != sizeof(s) || s.version != VERSION) {
        ESP_LOGI(TAG, "no stored settings, defaults");
        return false;
    }
    /* range checks: a damaged entry must not give silly values, a bad
     * field keeps its default; mute is never stored, because silence with
     * no sign of it after a restart looks like a fault */
    st->dsp = s.st.dsp;
    dsp_params_sanitize(&st->dsp);
    if (s.st.volume >= 0 && s.st.volume <= CODEC_VOL_MAX)
        st->volume = s.st.volume;
    if (s.st.in_gain_db >= 0 && s.st.in_gain_db <= UI_GAIN_MAX_DB
        && s.st.in_gain_db % UI_GAIN_STEP_DB == 0)
        st->in_gain_db = s.st.in_gain_db;
    s_saved = *st;
    s_have_saved = true;
    return true;
}

void settings_changed(const ui_state_t *st)
{
    s_pending = *st;
    s_pending.mute = false;
    /* back to what is stored (AGC twice, pitch up and down): nothing to
     * write, and no audio gap for it */
    if (s_have_saved && same(&s_pending, &s_saved))
        s_due = 0;
    else
        s_due = esp_timer_get_time() + SAVE_DELAY_US;
}

bool settings_due(void)
{
    return s_due && esp_timer_get_time() >= s_due;
}

void settings_save(void)
{
    s_due = 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK)
        return;
    stored_t s = { .version = VERSION, .st = s_pending };
    esp_err_t err = nvs_set_blob(h, KEY, &s, sizeof(s));
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        s_saved = s_pending;
        s_have_saved = true;
    }
    ESP_LOGI(TAG, "saved (%s)", esp_err_to_name(err));
}
