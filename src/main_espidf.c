/*
 * ESP32-P4 SD Card Test (ESP-IDF with exFAT support)
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_ldo_regulator.h"

static const char *TAG = "SD_TEST";

// Pin definitions (from build flags or defaults)
#ifndef PIN_SD_CLK
#define PIN_SD_CLK    43
#endif
#ifndef PIN_SD_CMD
#define PIN_SD_CMD    44
#endif
#ifndef PIN_SD_D0
#define PIN_SD_D0     39
#endif
#ifndef PIN_SD_D1
#define PIN_SD_D1     40
#endif
#ifndef PIN_SD_D2
#define PIN_SD_D2     41
#endif
#ifndef PIN_SD_D3
#define PIN_SD_D3     42
#endif
#ifndef PIN_SD_POWER
#define PIN_SD_POWER  45
#endif

static sdmmc_card_t *card = NULL;
static esp_ldo_channel_handle_t ldo_handle = NULL;

// Enable LDO channel 4 for SD card pull-ups
static esp_err_t enable_ldo_vo4(void)
{
    if (ldo_handle != NULL) {
        ESP_LOGI(TAG, "LDO_VO4 already enabled");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Enabling LDO_VO4 (3.3V for SD pull-ups)...");
    
    esp_ldo_channel_config_t ldo_config = {
        .chan_id = 4,
        .voltage_mv = 3300,
        .flags = {
            .adjustable = false,
            .owned_by_hw = false,
        }
    };
    
    esp_err_t ret = esp_ldo_acquire_channel(&ldo_config, &ldo_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire LDO_VO4: %s", esp_err_to_name(ret));
        esp_ldo_dump(stdout);
        return ret;
    }
    
    ESP_LOGI(TAG, "LDO_VO4 enabled at 3.3V");
    return ESP_OK;
}

// Enable SD card power via GPIO45 -> MOSFET
static void sd_power_on(void)
{
    ESP_LOGI(TAG, "Enabling SD card power (GPIO%d LOW)...", PIN_SD_POWER);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_SD_POWER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_SD_POWER, 0);  // LOW = MOSFET ON = SD powered
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "SD card power enabled");
}

static void sd_power_off(void)
{
    ESP_LOGI(TAG, "Disabling SD card power (GPIO%d HIGH)...", PIN_SD_POWER);
    gpio_set_level(PIN_SD_POWER, 1);  // HIGH = MOSFET OFF
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "SD card power disabled");
}

// Initialize SD card
static esp_err_t sd_init(void)
{
    ESP_LOGI(TAG, "=== Initializing SD Card ===");
    ESP_LOGI(TAG, "Pins: CLK=%d, CMD=%d, D0=%d, D1=%d, D2=%d, D3=%d",
             PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2, PIN_SD_D3);
    ESP_LOGI(TAG, "Power control: GPIO%d (active LOW)", PIN_SD_POWER);

    // Step 1: Enable LDO for pull-ups
    enable_ldo_vo4();

    // Step 2: Power on SD card
    sd_power_on();

    // Configure SDMMC host
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 20 MHz

    // Configure slot with internal pull-ups
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // 4-bit mode
    slot_config.clk = PIN_SD_CLK;
    slot_config.cmd = PIN_SD_CMD;
    slot_config.d0 = PIN_SD_D0;
    slot_config.d1 = PIN_SD_D1;
    slot_config.d2 = PIN_SD_D2;
    slot_config.d3 = PIN_SD_D3;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Internal pull-ups ENABLED via SDMMC_SLOT_FLAG_INTERNAL_PULLUP");
    ESP_LOGI(TAG, "Trying 4-bit mode at %d kHz...", host.max_freq_khz);

    // Mount filesystem (with exFAT support!)
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Mount failed: %s (0x%x)", esp_err_to_name(ret), ret);
        if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "Timeout - check if card is inserted");
        } else if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Filesystem mount failed - check if card is formatted (FAT32/exFAT)");
        }
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted successfully!");
    sdmmc_card_print_info(stdout, card);
    
    return ESP_OK;
}

// List files in directory
static void sd_list(const char *path)
{
    ESP_LOGI(TAG, "=== Listing: %s ===", path);
    
    DIR *dir = opendir(path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory");
        return;
    }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL && count < 50) {
        char fullpath[300];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(fullpath, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                printf("  [DIR]  %s/\n", entry->d_name);
            } else {
                if (st.st_size >= 1024 * 1024) {
                    printf("  %6.2f MB  %s\n", (float)st.st_size / (1024 * 1024), entry->d_name);
                } else if (st.st_size >= 1024) {
                    printf("  %6.2f KB  %s\n", (float)st.st_size / 1024, entry->d_name);
                } else {
                    printf("  %6lld B   %s\n", (long long)st.st_size, entry->d_name);
                }
            }
        }
        count++;
    }
    closedir(dir);
    printf("=== %d items ===\n", count);
}

// Read test
static void sd_read_test(void)
{
    ESP_LOGI(TAG, "=== Read Speed Test ===");
    
    // Create test file
    FILE *f = fopen("/sdcard/test.bin", "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to create test file");
        return;
    }
    
    uint8_t *buf = malloc(4096);
    if (buf == NULL) {
        fclose(f);
        return;
    }
    memset(buf, 0xAA, 4096);
    
    // Write 1MB
    for (int i = 0; i < 256; i++) {
        fwrite(buf, 1, 4096, f);
    }
    fclose(f);
    ESP_LOGI(TAG, "Created 1MB test file");
    
    // Read test
    f = fopen("/sdcard/test.bin", "rb");
    if (f == NULL) {
        free(buf);
        return;
    }
    
    int64_t start = esp_timer_get_time();
    size_t total = 0;
    while (fread(buf, 1, 4096, f) == 4096) {
        total += 4096;
    }
    int64_t elapsed = esp_timer_get_time() - start;
    fclose(f);
    
    float speed = (float)total / elapsed;  // bytes/us = MB/s
    ESP_LOGI(TAG, "Read %zu bytes in %lld us = %.2f MB/s", total, elapsed, speed);
    
    // Cleanup
    remove("/sdcard/test.bin");
    free(buf);
}

void app_main(void)
{
    printf("\n\n");
    printf("========================================\n");
    printf("  ESP32-P4 SD Card Test (ESP-IDF)\n");
    printf("  exFAT Support: ENABLED\n");
    printf("========================================\n\n");

    // Initialize SD card
    if (sd_init() == ESP_OK) {
        // List root directory
        sd_list("/sdcard");
        
        // Run read test
        sd_read_test();
    }

    printf("\nCommands: Press key + Enter\n");
    printf("  m = mount SD card\n");
    printf("  u = unmount SD card\n");
    printf("  l = list files\n");
    printf("  t = speed test\n");
    printf("  p = power cycle\n");
    printf("\n");

    // Simple command loop
    char cmd;
    while (1) {
        if (scanf(" %c", &cmd) == 1) {
            switch (cmd) {
                case 'm':
                    if (card == NULL) {
                        sd_init();
                    } else {
                        ESP_LOGI(TAG, "Already mounted");
                    }
                    break;
                case 'u':
                    if (card != NULL) {
                        esp_vfs_fat_sdcard_unmount("/sdcard", card);
                        card = NULL;
                        ESP_LOGI(TAG, "Unmounted");
                    }
                    break;
                case 'l':
                    if (card != NULL) {
                        sd_list("/sdcard");
                    } else {
                        ESP_LOGE(TAG, "Not mounted");
                    }
                    break;
                case 't':
                    if (card != NULL) {
                        sd_read_test();
                    } else {
                        ESP_LOGE(TAG, "Not mounted");
                    }
                    break;
                case 'p':
                    if (card != NULL) {
                        esp_vfs_fat_sdcard_unmount("/sdcard", card);
                        card = NULL;
                    }
                    sd_power_off();
                    vTaskDelay(pdMS_TO_TICKS(100));
                    sd_power_on();
                    vTaskDelay(pdMS_TO_TICKS(50));
                    sd_init();
                    break;
                default:
                    printf("Unknown command: %c\n", cmd);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
