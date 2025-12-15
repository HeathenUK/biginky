#include "esp_audio_simple_player.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_err.h"
#include <stdlib.h>
#include <string.h>

struct esp_codec_dev {
    esp_codec_dev_cfg_t cfg;
    int volume;
};

esp_codec_dev_handle_t esp_codec_dev_new(const esp_codec_dev_cfg_t* cfg) {
    if (!cfg) {
        return NULL;
    }
    struct esp_codec_dev* dev = (struct esp_codec_dev*)calloc(1, sizeof(struct esp_codec_dev));
    if (!dev) {
        return NULL;
    }
    dev->cfg = *cfg;
    dev->volume = 0;
    return dev;
}

esp_err_t esp_codec_dev_delete(esp_codec_dev_handle_t handle) {
    free(handle);
    return ESP_OK;
}

esp_err_t esp_codec_dev_set_out_vol(esp_codec_dev_handle_t handle, int volume) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->volume = volume;
    return ESP_OK;
}

static audio_codec_data_if_t g_i2s_data;
static audio_codec_ctrl_if_t g_ctrl_if;
static audio_codec_if_t g_codec_if;

const audio_codec_data_if_t* audio_codec_new_i2s_data(const audio_codec_i2s_cfg_t* cfg) {
    if (cfg) {
        g_i2s_data.cfg = *cfg;
    }
    return &g_i2s_data;
}

const audio_codec_ctrl_if_t* audio_codec_new_i2c_ctrl(const audio_codec_i2c_cfg_t* cfg) {
    if (cfg) {
        g_ctrl_if.cfg = *cfg;
    }
    return &g_ctrl_if;
}

const audio_codec_if_t* audio_codec_new_es8311(const audio_codec_es8311_cfg_t* cfg) {
    if (cfg) {
        g_codec_if.cfg = *cfg;
    }
    return &g_codec_if;
}

void audio_codec_delete_i2s_data(const audio_codec_data_if_t* intf) {
    (void)intf;
}

void audio_codec_delete_i2c_ctrl(const audio_codec_ctrl_if_t* intf) {
    (void)intf;
}

void audio_codec_delete_codec(const audio_codec_if_t* intf) {
    (void)intf;
}

struct esp_audio_simple_player {
    esp_audio_simple_player_cfg_t cfg;
    esp_audio_state_t state;
    int volume;
};

esp_audio_simple_player_handle_t esp_audio_simple_player_create(const esp_audio_simple_player_cfg_t* cfg) {
    if (!cfg || !cfg->out) {
        return NULL;
    }
    struct esp_audio_simple_player* p = (struct esp_audio_simple_player*)calloc(1, sizeof(struct esp_audio_simple_player));
    if (!p) {
        return NULL;
    }
    p->cfg = *cfg;
    p->state = ESP_AUDIO_STATE_STOPPED;
    p->volume = 0;
    return p;
}

esp_err_t esp_audio_simple_player_destroy(esp_audio_simple_player_handle_t handle) {
    free(handle);
    return ESP_OK;
}

esp_err_t esp_audio_simple_player_set_volume(esp_audio_simple_player_handle_t handle, int volume) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->volume = volume;
    return ESP_OK;
}

esp_err_t esp_audio_simple_player_play(esp_audio_simple_player_handle_t handle, const char* uri) {
    if (!handle || !uri) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->state = ESP_AUDIO_STATE_RUNNING;
    // Stub implementation: actual decoding is unavailable without esp-gmf.
    // Mark as stopped immediately to avoid blocking callers.
    handle->state = ESP_AUDIO_STATE_STOPPED;
    return ESP_OK;
}

esp_err_t esp_audio_simple_player_stop(esp_audio_simple_player_handle_t handle) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    handle->state = ESP_AUDIO_STATE_STOPPED;
    return ESP_OK;
}

esp_audio_state_t esp_audio_simple_player_get_state(esp_audio_simple_player_handle_t handle) {
    if (!handle) {
        return ESP_AUDIO_STATE_ERROR;
    }
    return handle->state;
}

