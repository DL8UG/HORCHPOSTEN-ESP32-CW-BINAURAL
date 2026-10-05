#include "codec.h"

#include "board.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define ES8388_ADDR 0x10
#define AC101_ADDR  0x1a
#define I2C_TIMEOUT_MS 50

static const char *TAG = "codec";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static const codec_ops_t *s_ops;
static codec_type_t s_type;

const char *codec_name(codec_type_t t)
{
    switch (t) {
    case CODEC_ES8388: return "ES8388";
    case CODEC_AC101: return "AC101";
    default: return "none";
    }
}

codec_type_t codec_probe(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed");
        return CODEC_NONE;
    }

#if CONFIG_HORCH_CODEC_ES8388
    s_type = CODEC_ES8388;
#elif CONFIG_HORCH_CODEC_AC101
    s_type = CODEC_AC101;
#else
    if (i2c_master_probe(s_bus, ES8388_ADDR, I2C_TIMEOUT_MS) == ESP_OK)
        s_type = CODEC_ES8388;
    else if (i2c_master_probe(s_bus, AC101_ADDR, I2C_TIMEOUT_MS) == ESP_OK)
        s_type = CODEC_AC101;
    else
        s_type = CODEC_NONE;
#endif
    if (s_type == CODEC_NONE) {
        ESP_LOGE(TAG, "no codec found at 0x%02x or 0x%02x", ES8388_ADDR, AC101_ADDR);
        return s_type;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_type == CODEC_ES8388 ? ES8388_ADDR : AC101_ADDR,
        .scl_speed_hz = 100000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        s_type = CODEC_NONE;
        return s_type;
    }
    s_ops = s_type == CODEC_ES8388 ? &codec_es8388_ops : &codec_ac101_ops;
    ESP_LOGI(TAG, "%s codec", codec_name(s_type));
    return s_type;
}

esp_err_t codec_init(void)
{
    return s_ops ? s_ops->init() : ESP_ERR_INVALID_STATE;
}

esp_err_t codec_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > CODEC_VOL_MAX) vol = CODEC_VOL_MAX;
    return s_ops ? s_ops->set_volume(vol) : ESP_ERR_INVALID_STATE;
}

esp_err_t codec_set_mute(bool mute)
{
    return s_ops ? s_ops->set_mute(mute) : ESP_ERR_INVALID_STATE;
}

esp_err_t codec_set_input_gain(int db)
{
    return s_ops ? s_ops->set_input_gain(db) : ESP_ERR_INVALID_STATE;
}

esp_err_t codec_write8(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, sizeof(b), I2C_TIMEOUT_MS);
}

esp_err_t codec_read8(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

esp_err_t codec_write16(uint8_t reg, uint16_t val)
{
    uint8_t b[3] = { reg, (uint8_t)(val >> 8), (uint8_t)val };
    return i2c_master_transmit(s_dev, b, sizeof(b), I2C_TIMEOUT_MS);
}

esp_err_t codec_read16(uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, b, 2, I2C_TIMEOUT_MS);
    *val = (uint16_t)(b[0] << 8 | b[1]);
    return err;
}
