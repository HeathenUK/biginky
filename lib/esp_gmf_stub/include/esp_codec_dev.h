#pragma once
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_codec_dev* esp_codec_dev_handle_t;

typedef struct audio_codec_if_t audio_codec_if_t;
typedef struct audio_codec_data_if_t audio_codec_data_if_t;
typedef struct audio_codec_ctrl_if_t audio_codec_ctrl_if_t;

typedef struct esp_codec_dev_cfg {
    const audio_codec_if_t* codec_if;
    const audio_codec_data_if_t* data_if;
    const audio_codec_ctrl_if_t* ctrl_if;
    void* pa_cfg;
} esp_codec_dev_cfg_t;

esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t* cfg);
esp_err_t esp_codec_dev_delete(esp_codec_dev_handle_t handle);
esp_err_t esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume);

#ifdef __cplusplus
}
#endif
