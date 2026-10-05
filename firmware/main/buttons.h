/*
 * The six keys of the board, polled every 10 ms. A press shorter than
 * BUTTONS_LONG_MS is reported at release as short; a longer one is
 * reported once as long while the key is still held.
 */
#pragma once

#include <stdbool.h>
#include "board.h"

#define BUTTONS_LONG_MS 600

typedef struct {
    int key;        /* 0..5 = KEY1..KEY6 */
    bool long_press;
} button_event_t;

void buttons_init(void);

/* wait for the next event; false on timeout */
bool buttons_get(button_event_t *ev, int timeout_ms);
