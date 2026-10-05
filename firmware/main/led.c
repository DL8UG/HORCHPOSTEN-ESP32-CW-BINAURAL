#include "led.h"

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define LED_ON  (CONFIG_HORCH_LED_ACTIVE_LOW ? 0 : 1)
#define LED_OFF (CONFIG_HORCH_LED_ACTIVE_LOW ? 1 : 0)

static QueueHandle_t s_q;

static void led_task(void *arg)
{
    (void)arg;
    int code;
    for (;;) {
        xQueueReceive(s_q, &code, portMAX_DELAY);
        if (code < 0) {
            gpio_set_level(BOARD_LED, LED_ON);
            vTaskDelay(pdMS_TO_TICKS(800));
            gpio_set_level(BOARD_LED, LED_OFF);
        } else {
            for (int i = 0; i < code; i++) {
                gpio_set_level(BOARD_LED, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(120));
                gpio_set_level(BOARD_LED, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(180));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void led_init(void)
{
    gpio_config_t c = { .pin_bit_mask = 1ULL << BOARD_LED, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&c);
    gpio_set_level(BOARD_LED, LED_OFF);
    s_q = xQueueCreate(4, sizeof(int));
    xTaskCreate(led_task, "led", 2048, NULL, 3, NULL);
}

void led_blink(int count)
{
    xQueueSend(s_q, &count, 0);
}

void led_error(void)
{
    int code = -1;
    xQueueSend(s_q, &code, 0);
}
