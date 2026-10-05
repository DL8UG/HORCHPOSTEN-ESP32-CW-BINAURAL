/* Status LED (GPIO22) until the display arrives: short blink codes. */
#pragma once

void led_init(void);
void led_blink(int count);  /* count short blinks */
void led_error(void);       /* one long blink */
