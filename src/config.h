#pragma once

// Centralized feature toggles for the ESP32-P4 build.
//
// WIFI_ENABLED is true unless DISABLE_WIFI is provided. ENABLE_WIFI_TEST can
// force Wi‑Fi helpers on for targeted builds.
#if !defined(DISABLE_WIFI) || defined(ENABLE_WIFI_TEST)
#define WIFI_ENABLED 1
#else
#define WIFI_ENABLED 0
#endif

// SDMMC_ENABLED is true unless explicitly disabled with DISABLE_SDMMC.
#if !defined(DISABLE_SDMMC)
#define SDMMC_ENABLED 1
#else
#define SDMMC_ENABLED 0
#endif


