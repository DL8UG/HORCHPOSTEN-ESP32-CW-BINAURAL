/*
 * Audio codec of the board: ES8388 or AC101, both I2S slave with
 * 16 bit Philips I2S at DSP_FS and MCLK = 256 * fs from the ESP32.
 */
#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define CODEC_VOL_MAX   30      /* volume steps, about 1.5 dB each */

typedef enum {
    CODEC_NONE = 0,
    CODEC_ES8388,
    CODEC_AC101,
} codec_type_t;

typedef struct {
    esp_err_t (*init)(void);    /* MCLK must already run */
    esp_err_t (*set_volume)(int vol);           /* 0..CODEC_VOL_MAX */
    esp_err_t (*set_mute)(bool mute);
    esp_err_t (*set_input_gain)(int db);        /* 0..24; NOT_SUPPORTED -> digital */
} codec_ops_t;

/* Find the codec on the I2C bus (or take the one set in menuconfig). */
codec_type_t codec_probe(void);

/* Initialise the probed codec; call after the I2S clocks run. */
esp_err_t codec_init(void);

esp_err_t codec_set_volume(int vol);
esp_err_t codec_set_mute(bool mute);
esp_err_t codec_set_input_gain(int db);
const char *codec_name(codec_type_t t);

/* register access for the drivers */
esp_err_t codec_write8(uint8_t reg, uint8_t val);
esp_err_t codec_read8(uint8_t reg, uint8_t *val);
esp_err_t codec_write16(uint8_t reg, uint16_t val);
esp_err_t codec_read16(uint8_t reg, uint16_t *val);

extern const codec_ops_t codec_es8388_ops;
extern const codec_ops_t codec_ac101_ops;
