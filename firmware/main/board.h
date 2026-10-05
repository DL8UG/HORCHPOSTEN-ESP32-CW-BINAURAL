/*
 * Pins of the ESP32-Audio-Kit (ESP32-A1S). Two variants exist: V2.2 with
 * an ES8388 codec and an older one with an AC101; they differ in the I2S
 * word select and data out pins.
 * Source: board definitions of pschatzmann/arduino-audiokit.
 */
#pragma once

#include "driver/gpio.h"

#define BOARD_I2C_SDA       GPIO_NUM_33
#define BOARD_I2C_SCL       GPIO_NUM_32
#define BOARD_I2S_MCLK      GPIO_NUM_0
#define BOARD_I2S_BCK       GPIO_NUM_27
#define BOARD_I2S_DIN       GPIO_NUM_35
#define BOARD_PA_ENABLE     GPIO_NUM_21     /* speaker amplifier, kept off */
#define BOARD_LED           GPIO_NUM_22

#define BOARD_KEY_COUNT     6
/* KEY1..KEY6; KEY2 (GPIO13) depends on the DIP switch setting */
extern const gpio_num_t board_keys[BOARD_KEY_COUNT];

typedef struct {
    gpio_num_t ws;
    gpio_num_t dout;
} board_i2s_pins_t;

extern const board_i2s_pins_t board_i2s_es8388;
extern const board_i2s_pins_t board_i2s_ac101;

/* speaker amplifier off: the output is for headphones only */
void board_init(void);
