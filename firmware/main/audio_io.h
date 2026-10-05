/*
 * I2S full duplex with the codec and the audio task: read a block from
 * the ADC, run the signal chain, write the block to the DAC.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "board.h"
#include "esp_err.h"

#define AUDIO_BLOCK 64      /* frames per block, 4 ms at 16 kHz */

/* start the I2S clocks (MCLK) with the pins of the codec variant */
esp_err_t audio_io_init(const board_i2s_pins_t *pins);

/* start the audio task (after codec_init) */
void audio_start(void);

/* digital input gain in dB, for codecs without an analog PGA */
void audio_set_digital_gain(int db);

/*
 * Record n raw input samples for the auto pitch; blocks up to
 * timeout_ms. Returns false on timeout.
 */
bool audio_capture(float *buf, size_t n, int timeout_ms);
