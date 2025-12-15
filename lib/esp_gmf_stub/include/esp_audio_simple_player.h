#pragma once
#include "esp_err.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_AUDIO_STATE_STOPPED = 0,
    ESP_AUDIO_STATE_RUNNING = 1,
    ESP_AUDIO_STATE_PAUSED = 2,
    ESP_AUDIO_STATE_ERROR = 3,
} esp_audio_state_t;

typedef struct esp_audio_simple_player* esp_audio_simple_player_handle_t;

typedef struct {
    void* in;
    void* out;
    void* monitor;
    int task_prio;
    int task_stack;
    int prefer_sample_rate;
    int prefer_channel;
} esp_audio_simple_player_cfg_t;

#define ESP_AUDIO_SIMPLE_PLAYER_DEFAULT_CFG() ((esp_audio_simple_player_cfg_t){ .in = NULL, .out = NULL, .monitor = NULL, .task_prio = 5, .task_stack = 4096, .prefer_sample_rate = 0, .prefer_channel = 0 })

esp_audio_simple_player_handle_t esp_audio_simple_player_create(const esp_audio_simple_player_cfg_t* cfg);
esp_err_t esp_audio_simple_player_destroy(esp_audio_simple_player_handle_t handle);
esp_err_t esp_audio_simple_player_set_volume(esp_audio_simple_player_handle_t handle, int volume);
esp_err_t esp_audio_simple_player_play(esp_audio_simple_player_handle_t handle, const char* uri);
esp_err_t esp_audio_simple_player_stop(esp_audio_simple_player_handle_t handle);
esp_audio_state_t esp_audio_simple_player_get_state(esp_audio_simple_player_handle_t handle);

#ifdef __cplusplus
}
#endif
