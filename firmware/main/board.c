#include "board.h"

const gpio_num_t board_keys[BOARD_KEY_COUNT] = {
    GPIO_NUM_36, GPIO_NUM_13, GPIO_NUM_19, GPIO_NUM_23, GPIO_NUM_18, GPIO_NUM_5,
};

const board_i2s_pins_t board_i2s_es8388 = { .ws = GPIO_NUM_25, .dout = GPIO_NUM_26 };
const board_i2s_pins_t board_i2s_ac101 = { .ws = GPIO_NUM_26, .dout = GPIO_NUM_25 };

void board_init(void)
{
    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << BOARD_PA_ENABLE,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&pa);
    gpio_set_level(BOARD_PA_ENABLE, 0);
}
