/*
 * Everything the user can set, in one place. The UI task owns it and
 * tells listeners about each change (serial log now, a display later).
 */
#pragma once

#include <stdbool.h>
#include "dsp.h"

#define UI_GAIN_STEP_DB 6
#define UI_GAIN_MAX_DB  24

typedef struct {
    dsp_params_t dsp;
    int volume;         /* 0..CODEC_VOL_MAX */
    int in_gain_db;     /* 0..UI_GAIN_MAX_DB */
    bool mute;
} ui_state_t;

typedef enum {
    UI_EV_CHANGED,      /* a setting changed */
    UI_EV_AUTO_OK,      /* auto pitch found a tone */
    UI_EV_AUTO_FAIL,    /* auto pitch found nothing */
} ui_event_t;

typedef void (*ui_listener_t)(ui_event_t ev, const ui_state_t *st);

/* register a listener (up to 4) */
void ui_add_listener(ui_listener_t cb);
