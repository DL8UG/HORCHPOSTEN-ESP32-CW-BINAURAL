/*
 * ES8388 (ESP32-Audio-Kit V2.2): I2S slave, 16 bit Philips, 256 fs.
 * Input LIN2/RIN2 = LINE IN jack (the on-board microphones are switched
 * off by the jack when a plug is in). Output LOUT1/ROUT1 and LOUT2/ROUT2
 * are both driven; the speaker amplifier stays off (board_init).
 * Register sequence after the esp-adf / arduino-audiokit driver.
 */
#include "codec.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define R_CONTROL1      0x00
#define R_CONTROL2      0x01
#define R_CHIPPOWER     0x02
#define R_ADCPOWER      0x03
#define R_DACPOWER      0x04
#define R_MASTERMODE    0x08
#define R_ADCCONTROL1   0x09    /* PGA gain, 3 dB steps, L << 4 | R */
#define R_ADCCONTROL2   0x0a    /* input select */
#define R_ADCCONTROL3   0x0b
#define R_ADCCONTROL4   0x0c    /* format */
#define R_ADCCONTROL5   0x0d    /* fs ratio */
#define R_ADCCONTROL8   0x10    /* ADC digital volume L */
#define R_ADCCONTROL9   0x11    /* ADC digital volume R */
#define R_DACCONTROL1   0x17    /* format */
#define R_DACCONTROL2   0x18    /* fs ratio */
#define R_DACCONTROL3   0x19    /* bits 7:6 ramp rate, 5 soft ramp, 2 mute */
#define R_DACCONTROL4   0x1a    /* DAC digital volume L */
#define R_DACCONTROL5   0x1b    /* DAC digital volume R */
#define R_DACCONTROL16  0x26
#define R_DACCONTROL17  0x27
#define R_DACCONTROL20  0x2a
#define R_DACCONTROL21  0x2b
#define R_DACCONTROL23  0x2d
#define R_LOUT1VOL      0x2e    /* 0 = -45 dB .. 30 = 0 dB .. 33 = +4.5 dB, no ramp */
#define R_ROUT1VOL      0x2f
#define R_LOUT2VOL      0x30
#define R_ROUT2VOL      0x31

#define ADC_INPUT_LIN2_RIN2  0x50
#define DAC_OUTPUT_ALL       0x3c
#define DAC_SOFT_RAMP        0x60    /* 0.5 dB per 32 LRCK: 1.5 dB in 6 ms */
#define OUT_VOL_MIN          3       /* -40.5 dB at volume 0 */

static esp_err_t es_init(void)
{
    static const uint8_t seq[][2] = {
        { R_DACCONTROL3, DAC_SOFT_RAMP | 0x04 },    /* mute while setting up */
        { R_CONTROL2, 0x50 },
        { R_CHIPPOWER, 0x00 },
        { 0x35, 0xa0 },             /* internal DLL off: better at low fs */
        { 0x37, 0xd0 },
        { 0x39, 0xd0 },
        { R_MASTERMODE, 0x00 },     /* slave */
        { R_DACPOWER, 0xc0 },
        { R_CONTROL1, 0x12 },
        { R_DACCONTROL1, 0x18 },    /* 16 bit, I2S */
        { R_DACCONTROL2, 0x02 },    /* single speed, 256 fs */
        { R_DACCONTROL16, 0x00 },
        { R_DACCONTROL17, 0x90 },   /* left DAC to left mixer only, 0 dB */
        { R_DACCONTROL20, 0x90 },   /* right DAC to right mixer only */
        { R_DACCONTROL21, 0x80 },   /* ADC and DAC share LRCK */
        { R_DACCONTROL23, 0x00 },
        { R_DACCONTROL4, 0x00 },    /* DAC digital 0 dB, fixed */
        { R_DACCONTROL5, 0x00 },
        { R_LOUT1VOL, 0 },          /* -45 dB until the volume is set */
        { R_ROUT1VOL, 0 },
        { R_LOUT2VOL, 0 },
        { R_ROUT2VOL, 0 },
        { R_DACPOWER, DAC_OUTPUT_ALL },
        { R_ADCPOWER, 0xff },
        { R_ADCCONTROL1, 0x00 },    /* PGA 0 dB, set later */
        { R_ADCCONTROL2, ADC_INPUT_LIN2_RIN2 },
        { R_ADCCONTROL3, 0x02 },
        { R_ADCCONTROL4, 0x0c },    /* 16 bit, I2S */
        { R_ADCCONTROL5, 0x02 },    /* single speed, 256 fs */
        { R_ADCCONTROL8, 0x00 },    /* ADC digital 0 dB */
        { R_ADCCONTROL9, 0x00 },
        { R_ADCPOWER, 0x09 },
        /* start */
        { R_DACCONTROL21, 0x80 },
        { R_CHIPPOWER, 0xf0 },      /* restart the state machine */
        { R_CHIPPOWER, 0x00 },
        { R_ADCPOWER, 0x00 },       /* ADC and line in on */
        { R_DACPOWER, DAC_OUTPUT_ALL },
    };
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
        err |= codec_write8(seq[i][0], seq[i][1]);
    vTaskDelay(pdMS_TO_TICKS(10));
    return err ? ESP_FAIL : ESP_OK;
}

/*
 * The volume is set in the output stage: 30 -> +4.5 dB, 0 -> -40.5 dB in
 * 1.5 dB steps. The analog noise of the codec comes before this stage,
 * so it goes down with the volume. (With the stage fixed at +4.5 dB and
 * the volume in the DAC, the hiss stayed at full level at any volume,
 * measured on the Audio Kit V2.2.) The stage has no ramp: a hard step.
 */
static esp_err_t es_set_volume(int vol)
{
    uint8_t v = (uint8_t)(OUT_VOL_MIN + vol);
    esp_err_t err = ESP_OK;
    for (uint8_t r = R_LOUT1VOL; r <= R_ROUT2VOL; r++)
        err |= codec_write8(r, v);
    return err ? ESP_FAIL : ESP_OK;
}

static esp_err_t es_set_mute(bool mute)
{
    uint8_t r;
    esp_err_t err = codec_read8(R_DACCONTROL3, &r);
    if (err)
        return err;
    r = (r & ~0x04) | (mute ? 0x04 : 0x00);
    return codec_write8(R_DACCONTROL3, r);
}

static esp_err_t es_set_input_gain(int db)
{
    int n = db / 3;
    if (n < 0) n = 0;
    if (n > 8) n = 8;
    return codec_write8(R_ADCCONTROL1, (uint8_t)(n << 4 | n));
}

const codec_ops_t codec_es8388_ops = {
    .init = es_init,
    .set_volume = es_set_volume,
    .set_mute = es_set_mute,
    .set_input_gain = es_set_input_gain,
};
