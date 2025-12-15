#pragma once

// ESP32-P4 pin mapping for the Waveshare ESP32-P4-WIFI6 board.
//
// Switches (active-low):
//   - A: GPIO25
//   - B: GPIO50
//   - C: GPIO21
//   - D: GPIO51
//
// Legacy RP2350 references retained for context:
//   - Wake button on GPIO1 (active-low to ground)
//   - SD card detect switch on GPIO37
//
// Display SPI (Pico GP -> ESP32-P4 GPIO):
//   SCLK    ->   GPIO3  (was GP10, pin 14)
//   MOSI    ->   GPIO2  (was GP11, pin 15)
//   CS0     ->   GPIO23 (was GP26, pin 31)
//   CS1     ->   GPIO48 (was GP16, pin 21)
//   DC      ->   GPIO26 (was GP22, pin 29)
//   RESET   ->   GPIO22 (was GP27, pin 32)
//   BUSY    ->   GPIO47 (was GP17, pin 22)
//
// RTC I2C pins (internal RTC in use; DS3231 kept for completeness):
//   SDA     ->   GPIO31 (was GP2, pin 4)
//   SCL     ->   GPIO30 (was GP3, pin 5)
//   INT     ->   GPIO46 (was GP18, pin 24)
//
// SDMMC pins (Slot 0 IOMUX): GPIO43/44/39/40/41/42

#ifndef PIN_SPI_SCK
#define PIN_SPI_SCK   3     // GPIO3 = Pico GP10 (pin 14)
#endif
#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI  2     // GPIO2 = Pico GP11 (pin 15)
#endif
#ifndef PIN_CS0
#define PIN_CS0       23    // GPIO23 = Pico GP26 (pin 31)
#endif
#ifndef PIN_CS1
#define PIN_CS1       48    // GPIO48 = Pico GP16 (pin 21)
#endif
#ifndef PIN_DC
#define PIN_DC        26    // GPIO26 = Pico GP22 (pin 29)
#endif
#ifndef PIN_RESET
#define PIN_RESET     22    // GPIO22 = Pico GP27 (pin 32)
#endif
#ifndef PIN_BUSY
#define PIN_BUSY      47    // GPIO47 = Pico GP17 (pin 22)
#endif

// SDMMC SD Card pins
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

// SD Card power control (P-MOSFET Q1 gate)
#ifndef PIN_SD_POWER
#define PIN_SD_POWER  45
#endif

// Audio codec (ES8311) pin definitions (Waveshare ESP32-P4-WIFI6)
#ifndef PIN_CODEC_I2C_SDA
#define PIN_CODEC_I2C_SDA  7
#endif
#ifndef PIN_CODEC_I2C_SCL
#define PIN_CODEC_I2C_SCL  8
#endif
#ifndef PIN_CODEC_I2C_ADDR
#define PIN_CODEC_I2C_ADDR 0x18
#endif

#ifndef PIN_CODEC_MCLK
#define PIN_CODEC_MCLK  13
#endif
#ifndef PIN_CODEC_BCLK
#define PIN_CODEC_BCLK  12   // SCLK (bit clock)
#endif
#ifndef PIN_CODEC_LRCK
#define PIN_CODEC_LRCK  10   // LRCK / WS
#endif
#ifndef PIN_CODEC_DOUT
#define PIN_CODEC_DOUT  9    // ESP32 -> codec SDIN (DSDIN)
#endif
#ifndef PIN_CODEC_DIN
#define PIN_CODEC_DIN   11   // codec DOUT (ASDOUT) -> ESP32 (optional)
#endif
#ifndef PIN_CODEC_PA_EN
#define PIN_CODEC_PA_EN 53   // PA_Ctrl (active high)
#endif

#define PIN_USER_LED 7

