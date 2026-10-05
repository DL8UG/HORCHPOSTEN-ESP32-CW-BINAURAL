/* Keep the UI state in NVS; written a few seconds after the last change. */
#pragma once

#include <stdbool.h>
#include "ui_state.h"

void settings_defaults(ui_state_t *st);

/* load into *st; false (and defaults) if nothing valid is stored */
bool settings_load(ui_state_t *st);

/* remember that *st changed; saved by settings_tick after the delay */
void settings_changed(const ui_state_t *st);

/* call regularly from the UI task */
void settings_tick(void);
