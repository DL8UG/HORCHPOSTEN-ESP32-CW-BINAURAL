/*
 * AC101 (older ESP32-Audio-Kit): I2S slave, 16 bit Philips, 16 kHz.
 * The PLL makes the 24.576 MHz system clock from MCLK = 4.096 MHz
 * (256 fs): 4.096 * 18 / (1 * 3), values from the Linux ac101 driver
 * (respeaker/seeed-voicecard). Input = line in, output = headphones.
 * Not tested on hardware yet.
 */
#include "codec.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define R_CHIP_AUDIO_RS     0x00
#define R_PLL_CTRL1         0x01
#define R_PLL_CTRL2         0x02
#define R_SYSCLK_CTRL       0x03
#define R_MOD_CLK_ENA       0x04
#define R_MOD_RST_CTRL      0x05
#define R_I2S_SR_CTRL       0x06
#define R_I2S1LCK_CTRL      0x10
#define R_I2S1_SDOUT_CTRL   0x11
#define R_I2S1_SDIN_CTRL    0x12
#define R_I2S1_MXR_SRC      0x13
#define R_ADC_DIG_CTRL      0x40
#define R_DAC_DIG_CTRL      0x48
#define R_DAC_VOL_CTRL      0x49    /* 0xa0a0 = 0 dB, 0 = muted */
#define R_DAC_MXR_SRC       0x4c
#define R_ADC_APC_CTRL      0x50
#define R_ADC_SRC           0x51
#define R_ADC_SRCBST_CTRL   0x52
#define R_OMIXER_DACA_CTRL  0x53
#define R_OMIXER_SR         0x54
#define R_HPOUT_CTRL        0x56    /* bits 9:4 headphone volume */
#define R_SPKOUT_CTRL       0x58

static const char *TAG = "ac101";

static esp_err_t ac_init(void)
{
    if (codec_write16(R_CHIP_AUDIO_RS, 0x0123) != ESP_OK) {
        ESP_LOGE(TAG, "reset failed");
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    static const struct { uint8_t reg; uint16_t val; } seq[] = {
        { R_SPKOUT_CTRL, 0xe880 },      /* speaker off */
        { R_PLL_CTRL1, 0x014f },        /* M = 1 */
        { R_PLL_CTRL2, 0x8120 },        /* PLL on, N = 18 */
        { R_SYSCLK_CTRL, 0x8b08 },      /* PLL from MCLK1, AIF1 and SYS clock from PLL */
        { R_MOD_CLK_ENA, 0x800c },
        { R_MOD_RST_CTRL, 0x800c },
        { R_I2S_SR_CTRL, 0x3000 },      /* 16 kHz */
        { R_I2S1LCK_CTRL, 0x8850 },     /* slave, 16 bit, I2S */
        { R_I2S1_SDOUT_CTRL, 0xc000 },
        { R_I2S1_SDIN_CTRL, 0xc000 },
        { R_I2S1_MXR_SRC, 0x2200 },
        { R_ADC_SRCBST_CTRL, 0xccc4 },
        { R_ADC_SRC, 0x0408 },          /* line in left/right */
        { R_ADC_DIG_CTRL, 0x8000 },
        { R_ADC_APC_CTRL, 0xbb00 },     /* both ADCs on, no microphone bias */
        { R_DAC_MXR_SRC, 0xcc00 },
        { R_DAC_DIG_CTRL, 0x8000 },
        { R_DAC_VOL_CTRL, 0x0000 },     /* muted until the settings are applied */
        { R_OMIXER_SR, 0x0081 },
        { R_OMIXER_DACA_CTRL, 0xff80 }, /* headphone output path */
        { R_HPOUT_CTRL, 0xc3c1 },
        { R_HPOUT_CTRL, 0xcb00 },
    };
    esp_err_t err = ESP_OK;
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
        err |= codec_write16(seq[i].reg, seq[i].val);
    vTaskDelay(pdMS_TO_TICKS(100));
    err |= codec_write16(R_HPOUT_CTRL, 0xf800);    /* headphone amplifier on, volume 0 */
    return err ? ESP_FAIL : ESP_OK;
}

static esp_err_t ac_set_volume(int vol)
{
    uint16_t r;
    esp_err_t err = codec_read16(R_HPOUT_CTRL, &r);
    if (err)
        return err;
    uint16_t v = (uint16_t)(vol * 2 + 3);  /* 0..63 */
    r = (r & ~(0x3f << 4)) | (v << 4);
    return codec_write16(R_HPOUT_CTRL, r);
}

static esp_err_t ac_set_mute(bool mute)
{
    return codec_write16(R_DAC_VOL_CTRL, mute ? 0x0000 : 0xa0a0);
}

static esp_err_t ac_set_input_gain(int db)
{
    (void)db;
    return ESP_ERR_NOT_SUPPORTED;   /* done digitally in the audio task */
}

const codec_ops_t codec_ac101_ops = {
    .init = ac_init,
    .set_volume = ac_set_volume,
    .set_mute = ac_set_mute,
    .set_input_gain = ac_set_input_gain,
};
