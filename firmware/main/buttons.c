#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define POLL_MS     10
#define DEBOUNCE_N  3       /* stable polls */

static QueueHandle_t s_q;

static void buttons_task(void *arg)
{
    (void)arg;
    bool state[BOARD_KEY_COUNT] = { 0 }, long_sent[BOARD_KEY_COUNT] = { 0 };
    int stable[BOARD_KEY_COUNT] = { 0 }, held_ms[BOARD_KEY_COUNT] = { 0 };
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        for (int k = 0; k < BOARD_KEY_COUNT; k++) {
            bool down = gpio_get_level(board_keys[k]) == 0;     /* active low */
            if (down != state[k]) {
                if (++stable[k] < DEBOUNCE_N)
                    continue;
                state[k] = down;
                stable[k] = 0;
                if (down) {
                    held_ms[k] = 0;
                    long_sent[k] = false;
                } else if (!long_sent[k]) {
                    button_event_t ev = { .key = k, .long_press = false };
                    xQueueSend(s_q, &ev, 0);
                }
            } else {
                stable[k] = 0;
                if (down && !long_sent[k] && (held_ms[k] += POLL_MS) >= BUTTONS_LONG_MS) {
                    long_sent[k] = true;
                    button_event_t ev = { .key = k, .long_press = true };
                    xQueueSend(s_q, &ev, 0);
                }
            }
        }
    }
}

void buttons_init(void)
{
    for (int k = 0; k < BOARD_KEY_COUNT; k++) {
        gpio_config_t c = {
            .pin_bit_mask = 1ULL << board_keys[k],
            .mode = GPIO_MODE_INPUT,
            /* GPIO36 has no internal pull-up; the board has external ones */
            .pull_up_en = board_keys[k] < GPIO_NUM_34 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        };
        gpio_config(&c);
    }
    s_q = xQueueCreate(8, sizeof(button_event_t));
    xTaskCreate(buttons_task, "buttons", 2048, NULL, 5, NULL);
}

bool buttons_get(button_event_t *ev, int timeout_ms)
{
    return xQueueReceive(s_q, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
