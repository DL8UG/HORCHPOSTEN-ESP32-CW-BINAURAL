/* Keep the UI state in NVS; written a few seconds after the last change. */
#pragma once

#include <stdbool.h>
#include "ui_state.h"

void settings_defaults(ui_state_t *st);

/* load into *st; false (and defaults) if nothing valid is stored */
bool settings_load(ui_state_t *st);

/* remember that *st changed; due for saving a few seconds later */
void settings_changed(const ui_state_t *st);

/* true when a change waits and the delay is over */
bool settings_due(void);

/* write the last change to flash; stalls both cores while it runs */
void settings_save(void);
