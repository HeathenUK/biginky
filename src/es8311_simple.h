#pragma once

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <Wire.h>

// Minimal ES8311 bring-up helper (based on Espressif esp-adf register sequences).
// Notes:
// - ES8311 7-bit I2C address is commonly 0x18 (0x30 is the 8-bit write address).
// - This helper focuses on DAC playback (speaker/headphone out).

class ES8311Simple {
public:
  struct Pins {
    int pa_enable_gpio = -1;      // Power amplifier enable, active-high unless pa_active_high=false
    bool pa_active_high = true;
  };

  struct Clocking {
    bool master_mode = false; // codec I2S master? Usually false (ESP32 is master)
    bool use_mclk = true;
    bool invert_mclk = false;
    bool invert_sclk = false;
    bool digital_mic = false;
    bool no_dac_ref = false;
    uint16_t mclk_div = 256; // MCLK/LRCK
  };

  bool begin(TwoWire& wire, uint8_t i2c_addr_7bit, const Pins& pins, const Clocking& clk);
  bool probe(uint8_t* id1, uint8_t* id2, uint8_t* ver);

  bool configureI2S(int sample_rate_hz, int bits_per_sample); // e.g. 44100, 16
  bool startDac();
  bool stopAll();

  bool setMute(bool mute);
  bool setDacVolumeReg(uint8_t reg);           // 0x00..0xFF
  bool setDacVolumePercent(int percent_0_100); // convenience mapping

private:
  TwoWire* wire_ = nullptr;
  uint8_t addr7_ = 0x18;
  Pins pins_{};
  Clocking clk_{};

  bool writeReg(uint8_t reg, uint8_t val);
  bool readReg(uint8_t reg, uint8_t& val);

  bool openInit();
  bool configFmtI2S();
  bool setBitsPerSample(int bits);
  bool configSampleRate(int sample_rate_hz);

  void paSetup();
  void paSet(bool enable);
};

#endif // ESP32 || ARDUINO_ARCH_ESP32

