#pragma once
#include "esp_err.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AUDIO_CODEC_I2S_ROLE_MASTER = 0,
    AUDIO_CODEC_I2S_ROLE_SLAVE = 1,
} audio_codec_i2s_role_t;

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t mclk_multiple;
} audio_codec_i2s_clk_t;

typedef struct {
    int role;
    audio_codec_i2s_clk_t clk_cfg;
} audio_codec_i2s_cfg_t;

typedef struct audio_codec_data_if_t {
    audio_codec_i2s_cfg_t cfg;
} audio_codec_data_if_t;

typedef struct {
    uint8_t addr;
    int port;
} audio_codec_i2c_cfg_t;

typedef struct audio_codec_ctrl_if_t {
    audio_codec_i2c_cfg_t cfg;
} audio_codec_ctrl_if_t;

typedef struct {
    const audio_codec_ctrl_if_t* ctrl_if;
    int pa_pin;
} audio_codec_es8311_cfg_t;

typedef struct audio_codec_if_t {
    audio_codec_es8311_cfg_t cfg;
} audio_codec_if_t;

#define AUDIO_CODEC_I2S_CFG_DEFAULT() ((audio_codec_i2s_cfg_t){ .role = AUDIO_CODEC_I2S_ROLE_MASTER, .clk_cfg = {0, 0} })
#define AUDIO_CODEC_I2C_CFG_DEFAULT() ((audio_codec_i2c_cfg_t){ .addr = 0, .port = 0 })
#define AUDIO_CODEC_ES8311_DEFAULT_CFG() ((audio_codec_es8311_cfg_t){ .ctrl_if = NULL, .pa_pin = -1 })

const audio_codec_data_if_t* audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t* cfg);
const audio_codec_ctrl_if_t* audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t* cfg);
const audio_codec_if_t* audio_codec_new_es8311(const audio_codec_es8311_cfg_t* cfg);
void audio_codec_delete_i2s_data(const audio_codec_data_if_t* intf);
void audio_codec_delete_i2c_ctrl(const audio_codec_ctrl_if_t* intf);
void audio_codec_delete_codec(const audio_codec_if_t* intf);

#ifdef __cplusplus
}
#endif
