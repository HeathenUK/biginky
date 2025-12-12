/**
 * @file es8156_simple.h
 * @brief Simple ES8156 Audio Codec Driver for Arduino/ESP32
 * 
 * This is a minimal ES8156 bring-up helper based on the Nicolai Electronics
 * and Badge Team ESP32 components for Tanmatsu.
 * 
 * References:
 * - https://github.com/Nicolai-Electronics/esp32-component-es8156
 * - https://github.com/badgeteam/esp32-component-badge-bsp
 * 
 * Notes:
 * - ES8156 7-bit I2C address is typically 0x08 or 0x09 (check your board)
 * - This helper focuses on DAC playback (speaker/headphone out)
 * - The ES8156 is a DIFFERENT chip than ES8311! Different registers!
 */

#pragma once

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h>
#include <Wire.h>

class ES8156Simple {
public:
    struct Pins {
        int pa_enable_gpio = -1;      // Power amplifier enable, active-high unless pa_active_high=false
        bool pa_active_high = true;
    };

    struct Clocking {
        bool master_mode = false;     // codec I2S master? Usually false (ESP32 is master)
        bool use_mclk = true;         // Use external MCLK (vs internal PLL from SCLK)
        bool invert_mclk = false;
        bool invert_sclk = false;
    };

    /**
     * @brief Initialize the ES8156 codec
     * 
     * @param wire Reference to TwoWire (I2C) instance
     * @param i2c_addr_7bit 7-bit I2C address (typically 0x08 or 0x09)
     * @param pins Pin configuration for PA enable
     * @param clk Clocking configuration
     * @return true if initialization successful
     */
    bool begin(TwoWire& wire, uint8_t i2c_addr_7bit, const Pins& pins, const Clocking& clk);

    /**
     * @brief Probe the ES8156 and read chip ID
     * 
     * @param id_high Pointer to receive high byte of chip ID (0x81 expected)
     * @param id_low Pointer to receive low byte of chip ID (0x56 expected)
     * @param ver Pointer to receive chip version
     * @return true if probe successful
     */
    bool probe(uint8_t* id_high = nullptr, uint8_t* id_low = nullptr, uint8_t* ver = nullptr);

    /**
     * @brief Configure I2S format and sample rate
     * 
     * @param sample_rate_hz Sample rate (e.g., 44100, 48000)
     * @param bits_per_sample Bits per sample (16, 24, or 32)
     * @return true if configuration successful
     */
    bool configureI2S(int sample_rate_hz, int bits_per_sample);

    /**
     * @brief Start the DAC (enables audio output)
     * @return true if successful
     */
    bool startDac();

    /**
     * @brief Stop all audio and power down
     * @return true if successful
     */
    bool stopAll();

    /**
     * @brief Set DAC mute state
     * @param mute true to mute, false to unmute
     * @return true if successful
     */
    bool setMute(bool mute);

    /**
     * @brief Set DAC volume using raw register value
     * 
     * @param reg Volume register value (0x00=mute, 0xBF=0dB, 0xFF=+32dB)
     *            Tanmatsu BSP uses max 180 (0xB4) for safety
     * @return true if successful
     */
    bool setDacVolumeReg(uint8_t reg);

    /**
     * @brief Set DAC volume as percentage (0-100)
     * 
     * @param percent_0_100 Volume percentage
     * @return true if successful
     */
    bool setDacVolumePercent(int percent_0_100);

    /**
     * @brief Set DAC volume with mapping to safe range
     * 
     * Maps UI percentage (0-100) to a restricted codec percentage range
     * to avoid inaudible/too-loud extremes.
     * 
     * @param ui_percent_0_100 User-facing 0-100 percentage
     * @param min_percent Minimum codec percentage (e.g., 20)
     * @param max_percent Maximum codec percentage (e.g., 70)
     * @return true if successful
     */
    bool setDacVolumePercentMapped(int ui_percent_0_100, int min_percent, int max_percent);

    /**
     * @brief Enable or disable PA (power amplifier) output
     * @param enable true to enable PA
     */
    void paSet(bool enable);

    // Debug helpers
    void setTrace(bool enable) { trace_ = enable; }
    bool dumpRegisters(uint8_t start_reg = 0x00, uint8_t end_reg = 0x25);

private:
    TwoWire* wire_ = nullptr;
    uint8_t addr7_ = 0x08;
    Pins pins_{};
    Clocking clk_{};
    bool trace_ = false;

    bool writeReg(uint8_t reg, uint8_t val);
    bool readReg(uint8_t reg, uint8_t& val);

    bool initCodec();
    bool configSampleRate(int sample_rate_hz);
    bool setBitsPerSample(int bits);

    void paSetup();
};

#endif // ESP32 || ARDUINO_ARCH_ESP32
