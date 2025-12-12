#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)

#include "es8311_simple.h"

// Register map (subset) from Espressif esp-adf / audio_hal ES8311 driver
static constexpr uint8_t ES8311_RESET_REG00 = 0x00;
static constexpr uint8_t ES8311_CLK_MANAGER_REG01 = 0x01;
static constexpr uint8_t ES8311_CLK_MANAGER_REG02 = 0x02;
static constexpr uint8_t ES8311_CLK_MANAGER_REG03 = 0x03;
static constexpr uint8_t ES8311_CLK_MANAGER_REG04 = 0x04;
static constexpr uint8_t ES8311_CLK_MANAGER_REG05 = 0x05;
static constexpr uint8_t ES8311_CLK_MANAGER_REG06 = 0x06;
static constexpr uint8_t ES8311_CLK_MANAGER_REG07 = 0x07;
static constexpr uint8_t ES8311_CLK_MANAGER_REG08 = 0x08;

static constexpr uint8_t ES8311_SDPIN_REG09 = 0x09;
static constexpr uint8_t ES8311_SDPOUT_REG0A = 0x0A;

static constexpr uint8_t ES8311_SYSTEM_REG0B = 0x0B;
static constexpr uint8_t ES8311_SYSTEM_REG0C = 0x0C;
static constexpr uint8_t ES8311_SYSTEM_REG0D = 0x0D;
static constexpr uint8_t ES8311_SYSTEM_REG0E = 0x0E;
static constexpr uint8_t ES8311_SYSTEM_REG10 = 0x10;
static constexpr uint8_t ES8311_SYSTEM_REG11 = 0x11;
static constexpr uint8_t ES8311_SYSTEM_REG12 = 0x12;
static constexpr uint8_t ES8311_SYSTEM_REG13 = 0x13;
static constexpr uint8_t ES8311_SYSTEM_REG14 = 0x14;

static constexpr uint8_t ES8311_ADC_REG15 = 0x15;
static constexpr uint8_t ES8311_ADC_REG16 = 0x16;
static constexpr uint8_t ES8311_ADC_REG17 = 0x17;
static constexpr uint8_t ES8311_ADC_REG1B = 0x1B;
static constexpr uint8_t ES8311_ADC_REG1C = 0x1C;

static constexpr uint8_t ES8311_DAC_REG31 = 0x31;
static constexpr uint8_t ES8311_DAC_REG32 = 0x32;
static constexpr uint8_t ES8311_DAC_REG37 = 0x37;

static constexpr uint8_t ES8311_GPIO_REG44 = 0x44;
static constexpr uint8_t ES8311_GP_REG45 = 0x45;

static constexpr uint8_t ES8311_CHD1_REGFD = 0xFD;
static constexpr uint8_t ES8311_CHD2_REGFE = 0xFE;
static constexpr uint8_t ES8311_CHVER_REGFF = 0xFF;

struct coeff_div {
  uint32_t mclk;
  uint32_t rate;
  uint8_t pre_div;
  uint8_t pre_multi;
  uint8_t adc_div;
  uint8_t dac_div;
  uint8_t fs_mode;
  uint8_t lrck_h;
  uint8_t lrck_l;
  uint8_t bclk_div;
  uint8_t adc_osr;
  uint8_t dac_osr;
};

// Copied from Espressif esp-adf `es8311.c` (esp_codec_dev) coefficient table.
static const coeff_div kCoeff[] = {
  // mclk      rate   pre_div mult adc_div dac_div fs lrch lrcl bckdiv osr_adc osr_dac
  {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {5644800,  44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {2822400,  44100, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {1411200,  44100, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {6144000,  48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {3072000,  48000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {1536000,  48000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
  {12288000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
  {12288000, 8000,  0x06, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
};

static int find_coeff(uint32_t mclk, uint32_t rate) {
  for (int i = 0; i < (int)(sizeof(kCoeff) / sizeof(kCoeff[0])); i++) {
    if (kCoeff[i].mclk == mclk && kCoeff[i].rate == rate) return i;
  }
  return -1;
}

bool ES8311Simple::begin(TwoWire& wire, uint8_t i2c_addr_7bit, const Pins& pins, const Clocking& clk) {
  wire_ = &wire;
  addr7_ = i2c_addr_7bit;
  pins_ = pins;
  clk_ = clk;

  if (clk_.mclk_div == 0) clk_.mclk_div = 256;
  paSetup();
  return openInit();
}

bool ES8311Simple::probe(uint8_t* id1, uint8_t* id2, uint8_t* ver) {
  uint8_t v = 0;
  if (id1) {
    if (!readReg(ES8311_CHD1_REGFD, v)) return false;
    *id1 = v;
  }
  if (id2) {
    if (!readReg(ES8311_CHD2_REGFE, v)) return false;
    *id2 = v;
  }
  if (ver) {
    if (!readReg(ES8311_CHVER_REGFF, v)) return false;
    *ver = v;
  }
  return true;
}

bool ES8311Simple::configureI2S(int sample_rate_hz, int bits_per_sample) {
  if (!setBitsPerSample(bits_per_sample)) return false;
  if (!configFmtI2S()) return false;
  if (!configSampleRate(sample_rate_hz)) return false;
  return true;
}

bool ES8311Simple::startDac() {
  // Sequence from esp-adf `es8311_start()`
  uint8_t regv = 0x80;
  if (clk_.master_mode) regv |= 0x40;
  else regv &= (uint8_t)~0x40;
  if (!writeReg(ES8311_RESET_REG00, regv)) return false;

  regv = 0x3F;
  if (clk_.use_mclk) regv &= (uint8_t)~0x80;
  else regv |= 0x80;
  if (clk_.invert_mclk) regv |= 0x40;
  else regv &= (uint8_t)~0x40;
  if (!writeReg(ES8311_CLK_MANAGER_REG01, regv)) return false;

  uint8_t dac_iface = 0, adc_iface = 0;
  if (!readReg(ES8311_SDPIN_REG09, dac_iface)) return false;
  if (!readReg(ES8311_SDPOUT_REG0A, adc_iface)) return false;
  dac_iface &= 0xBF; // clear bit6
  adc_iface &= 0xBF; // clear bit6
  if (!writeReg(ES8311_SDPIN_REG09, dac_iface)) return false;
  if (!writeReg(ES8311_SDPOUT_REG0A, adc_iface)) return false;

  if (!writeReg(ES8311_ADC_REG17, 0xBF)) return false;
  if (!writeReg(ES8311_SYSTEM_REG0E, 0x02)) return false;
  if (!writeReg(ES8311_SYSTEM_REG12, 0x00)) return false; // enable DAC
  if (!writeReg(ES8311_SYSTEM_REG14, 0x1A)) return false;

  // DMIC enable/disable (bit6 in SYSTEM_REG14)
  uint8_t sys14 = 0;
  if (!readReg(ES8311_SYSTEM_REG14, sys14)) return false;
  if (clk_.digital_mic) sys14 |= 0x40;
  else sys14 &= (uint8_t)~0x40;
  if (!writeReg(ES8311_SYSTEM_REG14, sys14)) return false;

  if (!writeReg(ES8311_SYSTEM_REG0D, 0x01)) return false;
  if (!writeReg(ES8311_ADC_REG15, 0x40)) return false;
  if (!writeReg(ES8311_DAC_REG37, 0x08)) return false;
  if (!writeReg(ES8311_GP_REG45, 0x00)) return false;

  paSet(true);
  // unmute by default
  setMute(false);
  return true;
}

bool ES8311Simple::stopAll() {
  // Minimal suspend-like sequence (based on esp-adf es8311_suspend)
  (void)writeReg(ES8311_DAC_REG32, 0x00);
  (void)writeReg(ES8311_ADC_REG17, 0x00);
  (void)writeReg(ES8311_SYSTEM_REG0E, 0xFF);
  (void)writeReg(ES8311_SYSTEM_REG12, 0x02);
  (void)writeReg(ES8311_SYSTEM_REG14, 0x00);
  (void)writeReg(ES8311_SYSTEM_REG0D, 0xFA);
  (void)writeReg(ES8311_ADC_REG15, 0x00);
  (void)writeReg(ES8311_CLK_MANAGER_REG02, 0x10);
  (void)writeReg(ES8311_RESET_REG00, 0x00);
  (void)writeReg(ES8311_RESET_REG00, 0x1F);
  (void)writeReg(ES8311_CLK_MANAGER_REG01, 0x30);
  (void)writeReg(ES8311_CLK_MANAGER_REG01, 0x00);
  (void)writeReg(ES8311_GP_REG45, 0x00);
  (void)writeReg(ES8311_SYSTEM_REG0D, 0xFC);
  (void)writeReg(ES8311_CLK_MANAGER_REG02, 0x00);
  paSet(false);
  return true;
}

bool ES8311Simple::setMute(bool mute) {
  uint8_t regv = 0;
  if (!readReg(ES8311_DAC_REG31, regv)) return false;
  regv &= 0x9F;
  if (mute) regv |= 0x60;
  return writeReg(ES8311_DAC_REG31, regv);
}

bool ES8311Simple::setDacVolumeReg(uint8_t reg) {
  return writeReg(ES8311_DAC_REG32, reg);
}

bool ES8311Simple::setDacVolumePercent(int percent_0_100) {
  if (percent_0_100 < 0) percent_0_100 = 0;
  if (percent_0_100 > 100) percent_0_100 = 100;
  uint8_t reg = (uint8_t)((percent_0_100 * 255) / 100);
  return setDacVolumeReg(reg);
}

bool ES8311Simple::writeReg(uint8_t reg, uint8_t val) {
  if (!wire_) return false;
  wire_->beginTransmission(addr7_);
  wire_->write(reg);
  wire_->write(val);
  return wire_->endTransmission() == 0;
}

bool ES8311Simple::readReg(uint8_t reg, uint8_t& val) {
  if (!wire_) return false;
  wire_->beginTransmission(addr7_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) return false;
  int n = wire_->requestFrom((int)addr7_, 1);
  if (n != 1) return false;
  val = (uint8_t)wire_->read();
  return true;
}

bool ES8311Simple::openInit() {
  // Enhance ES8311 I2C noise immunity (write twice)
  if (!writeReg(ES8311_GPIO_REG44, 0x08)) return false;
  if (!writeReg(ES8311_GPIO_REG44, 0x08)) return false;

  if (!writeReg(ES8311_CLK_MANAGER_REG01, 0x30)) return false;
  if (!writeReg(ES8311_CLK_MANAGER_REG02, 0x00)) return false;
  if (!writeReg(ES8311_CLK_MANAGER_REG03, 0x10)) return false;
  if (!writeReg(ES8311_ADC_REG16, 0x24)) return false;
  if (!writeReg(ES8311_CLK_MANAGER_REG04, 0x10)) return false;
  if (!writeReg(ES8311_CLK_MANAGER_REG05, 0x00)) return false;
  if (!writeReg(ES8311_SYSTEM_REG0B, 0x00)) return false;
  if (!writeReg(ES8311_SYSTEM_REG0C, 0x00)) return false;
  if (!writeReg(ES8311_SYSTEM_REG10, 0x1F)) return false;
  if (!writeReg(ES8311_SYSTEM_REG11, 0x7F)) return false;
  if (!writeReg(ES8311_RESET_REG00, 0x80)) return false;

  uint8_t regv = 0;
  if (!readReg(ES8311_RESET_REG00, regv)) return false;
  if (clk_.master_mode) regv |= 0x40;
  else regv &= (uint8_t)~0x40;
  if (!writeReg(ES8311_RESET_REG00, regv)) return false;

  // Clock source selection for MCLK
  regv = 0x3F;
  if (clk_.use_mclk) regv &= (uint8_t)~0x80;
  else regv |= 0x80;
  if (clk_.invert_mclk) regv |= 0x40;
  else regv &= (uint8_t)~0x40;
  if (!writeReg(ES8311_CLK_MANAGER_REG01, regv)) return false;

  // SCLK inverted or not
  if (!readReg(ES8311_CLK_MANAGER_REG06, regv)) return false;
  if (clk_.invert_sclk) regv |= 0x20;
  else regv &= (uint8_t)~0x20;
  if (!writeReg(ES8311_CLK_MANAGER_REG06, regv)) return false;

  if (!writeReg(ES8311_SYSTEM_REG13, 0x10)) return false;
  if (!writeReg(ES8311_ADC_REG1B, 0x0A)) return false;
  if (!writeReg(ES8311_ADC_REG1C, 0x6A)) return false;

  if (!clk_.no_dac_ref) {
    // set internal reference signal (ADCL + DACR)
    if (!writeReg(ES8311_GPIO_REG44, 0x58)) return false;
  } else {
    if (!writeReg(ES8311_GPIO_REG44, 0x08)) return false;
  }

  paSet(true);
  return true;
}

bool ES8311Simple::configFmtI2S() {
  // ES_I2S_NORMAL in esp-adf: clear low 2 bits for both ADC/DAC port regs
  uint8_t dac_iface = 0, adc_iface = 0;
  if (!readReg(ES8311_SDPIN_REG09, dac_iface)) return false;
  if (!readReg(ES8311_SDPOUT_REG0A, adc_iface)) return false;
  dac_iface &= 0xFC;
  adc_iface &= 0xFC;
  if (!writeReg(ES8311_SDPIN_REG09, dac_iface)) return false;
  if (!writeReg(ES8311_SDPOUT_REG0A, adc_iface)) return false;
  return true;
}

bool ES8311Simple::setBitsPerSample(int bits) {
  uint8_t dac_iface = 0, adc_iface = 0;
  if (!readReg(ES8311_SDPIN_REG09, dac_iface)) return false;
  if (!readReg(ES8311_SDPOUT_REG0A, adc_iface)) return false;

  // esp-adf logic:
  // - 16-bit: set 0x0c
  // - 24-bit: clear 0x1c
  // - 32-bit: set 0x10
  if (bits == 24) {
    dac_iface &= (uint8_t)~0x1C;
    adc_iface &= (uint8_t)~0x1C;
  } else if (bits == 32) {
    dac_iface |= 0x10;
    adc_iface |= 0x10;
  } else { // default 16
    dac_iface |= 0x0C;
    adc_iface |= 0x0C;
  }

  if (!writeReg(ES8311_SDPIN_REG09, dac_iface)) return false;
  if (!writeReg(ES8311_SDPOUT_REG0A, adc_iface)) return false;
  return true;
}

bool ES8311Simple::configSampleRate(int sample_rate_hz) {
  const uint32_t mclk_hz = (uint32_t)sample_rate_hz * (uint32_t)clk_.mclk_div;
  const int idx = find_coeff(mclk_hz, (uint32_t)sample_rate_hz);
  if (idx < 0) {
    // Not found in our minimal table: user likely set an unsupported SR or MCLK multiple.
    return false;
  }
  const coeff_div& c = kCoeff[idx];

  uint8_t regv = 0;
  // CLK_MANAGER_REG02: pre_div (bits 7:5) and pre_multi selector (bits 4:3)
  if (!readReg(ES8311_CLK_MANAGER_REG02, regv)) return false;
  regv &= 0x07;
  regv |= (uint8_t)((c.pre_div - 1) << 5);

  uint8_t datmp = 0;
  switch (c.pre_multi) {
    case 1: datmp = 0; break;
    case 2: datmp = 1; break;
    case 4: datmp = 2; break;
    case 8: datmp = 3; break;
    default: datmp = 0; break;
  }
  regv |= (uint8_t)(datmp << 3);
  if (!writeReg(ES8311_CLK_MANAGER_REG02, regv)) return false;

  // REG05: adc_div and dac_div
  regv = 0;
  regv |= (uint8_t)((c.adc_div - 1) << 4);
  regv |= (uint8_t)((c.dac_div - 1) << 0);
  if (!writeReg(ES8311_CLK_MANAGER_REG05, regv)) return false;

  // REG03: fs_mode and adc_osr
  if (!readReg(ES8311_CLK_MANAGER_REG03, regv)) return false;
  regv &= 0x80;
  regv |= (uint8_t)(c.fs_mode << 6);
  regv |= (uint8_t)(c.adc_osr << 0);
  if (!writeReg(ES8311_CLK_MANAGER_REG03, regv)) return false;

  // REG04: dac_osr
  if (!readReg(ES8311_CLK_MANAGER_REG04, regv)) return false;
  regv &= 0x80;
  regv |= (uint8_t)(c.dac_osr << 0);
  if (!writeReg(ES8311_CLK_MANAGER_REG04, regv)) return false;

  // REG07/08: lrck divider high/low
  if (!readReg(ES8311_CLK_MANAGER_REG07, regv)) return false;
  regv &= 0xC0;
  regv |= (uint8_t)(c.lrck_h << 0);
  if (!writeReg(ES8311_CLK_MANAGER_REG07, regv)) return false;
  if (!writeReg(ES8311_CLK_MANAGER_REG08, (uint8_t)(c.lrck_l << 0))) return false;

  // REG06: bclk divider (keep invert flag as configured earlier)
  if (!readReg(ES8311_CLK_MANAGER_REG06, regv)) return false;
  regv &= 0xE0;
  if (c.bclk_div < 19) regv |= (uint8_t)((c.bclk_div - 1) << 0);
  else regv |= (uint8_t)(c.bclk_div << 0);
  if (clk_.invert_sclk) regv |= 0x20;
  if (!writeReg(ES8311_CLK_MANAGER_REG06, regv)) return false;

  return true;
}

void ES8311Simple::paSetup() {
  if (pins_.pa_enable_gpio < 0) return;
  pinMode(pins_.pa_enable_gpio, OUTPUT);
  // Match typical board bring-up: enable PA as early as possible.
  // (Codec outputs are muted/quiet until configured anyway.)
  paSet(true);
}

void ES8311Simple::paSet(bool enable) {
  if (pins_.pa_enable_gpio < 0) return;
  const bool level = pins_.pa_active_high ? enable : !enable;
  digitalWrite(pins_.pa_enable_gpio, level ? HIGH : LOW);
}

#endif // ESP32 || ARDUINO_ARCH_ESP32

