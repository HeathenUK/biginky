/**
 * @file es8156_regs.h
 * @brief ES8156 Audio Codec Register Definitions
 * 
 * Based on Nicolai Electronics ESP32 Component ES8156
 * https://github.com/Nicolai-Electronics/esp32-component-es8156
 * 
 * SPDX-License-Identifier: MIT
 */

#pragma once

// ============================================================================
// ES8156 Register Addresses (Page 0)
// ============================================================================

#define ES8156_REG_RESET_CONTROL          0x00
#define ES8156_REG_MAIN_CLOCK_CONTROL     0x01
#define ES8156_REG_MODE_CONFIG_1          0x02
#define ES8156_REG_MASTER_LRCK_DIVIDER_1  0x03
#define ES8156_REG_MASTER_LRCK_DIVIDER_0  0x04
#define ES8156_REG_MASTER_CLOCK_CONTROL   0x05
#define ES8156_REG_NFS_CONFIG             0x06
#define ES8156_REG_MISC_CONTROL_1         0x07
#define ES8156_REG_CLOCK_OFF              0x08
#define ES8156_REG_MISC_CONTROL_2         0x09
#define ES8156_REG_TIME_CONTROL_1         0x0A
#define ES8156_REG_TIME_CONTROL_2         0x0B
#define ES8156_REG_CHIP_STATUS            0x0C
#define ES8156_REG_P2S_CONTROL            0x0D
#define ES8156_REG_DAC_COUNTER_PARAMETER  0x10
#define ES8156_REG_SDP_INTERFACE_CONFIG_1 0x11
#define ES8156_REG_AUTOMUTE_CONTROL       0x12
#define ES8156_REG_MUTE_CONTROL           0x13
#define ES8156_REG_VOLUME_CONTROL         0x14
#define ES8156_REG_ALC_CONFIG_1           0x15
#define ES8156_REG_ALC_CONFIG_2           0x16
#define ES8156_REG_ALC_LEVEL              0x17
#define ES8156_REG_MISC_CONTROL_3         0x18
#define ES8156_REG_EQ_CONTROL_1           0x19
#define ES8156_REG_EQ_CONFIG_2            0x1A
#define ES8156_REG_ANALOG_SYSTEM_1        0x20
#define ES8156_REG_ANALOG_SYSTEM_2        0x21
#define ES8156_REG_ANALOG_SYSTEM_3        0x22
#define ES8156_REG_ANALOG_SYSTEM_4        0x23
#define ES8156_REG_ANALOG_SYSTEM_5        0x24
#define ES8156_REG_ANALOG_SYSTEM_6        0x25
#define ES8156_REG_PAGE_SELECT            0xFC
#define ES8156_REG_CHIP_ID1               0xFD
#define ES8156_REG_CHIP_ID0               0xFE
#define ES8156_REG_CHIP_VERSION           0xFF

// ============================================================================
// Register 0x00 - Reset Control
// ============================================================================
#define ES8156_REG00_CSM_ON_BIT           (1 << 0)  // Chip state machine enable
#define ES8156_REG00_SEQ_DIS_BIT          (1 << 1)  // Disable internal power sequence
#define ES8156_REG00_RST_DIG_BIT          (1 << 2)  // Reset digital
#define ES8156_REG00_RST_DAC_DIG_BIT      (1 << 3)  // Reset DAC digital
#define ES8156_REG00_RST_MSTGEN_BIT       (1 << 4)  // Reset master generator
#define ES8156_REG00_RST_REGS_BIT         (1 << 5)  // Reset registers

// ============================================================================
// Register 0x01 - Main Clock Control
// ============================================================================
#define ES8156_REG01_CLK_DAC_DIV_MASK     0x0F      // DAC clock divider [3:0]
#define ES8156_REG01_OSR128_SEL_BIT       (1 << 5)  // OSR 128 select
#define ES8156_REG01_MULTP_FACTOR_SHIFT   6         // Multiplier factor [7:6]
#define ES8156_REG01_MULTP_FACTOR_MASK    (0x03 << ES8156_REG01_MULTP_FACTOR_SHIFT)

// ============================================================================
// Register 0x02 - Mode Config 1
// ============================================================================
#define ES8156_REG02_MS_MODE_BIT          (1 << 0)  // Master/Slave mode
#define ES8156_REG02_SPEED_MODE_BIT       (1 << 1)  // Speed mode
#define ES8156_REG02_SOFT_MODE_SEL_BIT    (1 << 2)  // Soft mode select
#define ES8156_REG02_EQ_HIGH_MODE_BIT     (1 << 3)  // EQ high mode
#define ES8156_REG02_SCLK_INV_MODE_BIT    (1 << 4)  // SCLK invert
#define ES8156_REG02_SCLKLRCK_TRI_BIT     (1 << 5)  // SCLK/LRCK tristate
#define ES8156_REG02_ISCLKLRCK_SEL_BIT    (1 << 6)  // Internal SCLK/LRCK select
#define ES8156_REG02_MCLK_SEL_BIT         (1 << 7)  // MCLK source select

// ============================================================================
// Register 0x08 - Clock Off
// ============================================================================
#define ES8156_REG08_MCLK_ON_BIT          (1 << 0)  // MCLK on
#define ES8156_REG08_DAC_MCLK_ON_BIT      (1 << 1)  // DAC MCLK on
#define ES8156_REG08_ANA_CLK_ON_BIT       (1 << 2)  // Analog clock on
#define ES8156_REG08_EXT_SCLKLRCK_ON_BIT  (1 << 3)  // External SCLK/LRCK on
#define ES8156_REG08_MASTER_CLK_ON_BIT    (1 << 4)  // Master clock on
#define ES8156_REG08_P2S_CLK_ON_BIT       (1 << 5)  // P2S clock on

// ============================================================================
// Register 0x09 - Misc Control 2
// ============================================================================
#define ES8156_REG09_PUPDN_OFF_BIT        (1 << 0)  // Power up/down off
#define ES8156_REG09_DLL_ON_BIT           (1 << 1)  // DLL on

// ============================================================================
// Register 0x0D - P2S Control
// ============================================================================
#define ES8156_REG0D_P2S_SDOUT_TRI_BIT    (1 << 0)  // P2S SDOUT tristate
#define ES8156_REG0D_P2S_SDOUT_SEL_BIT    (1 << 1)  // P2S SDOUT select
#define ES8156_REG0D_P2S_SDOUT_MUTEB_BIT  (1 << 2)  // P2S SDOUT mute bar
#define ES8156_REG0D_P2S_NFS_FLAGOFF_BIT  (1 << 3)  // P2S NFS flag off
#define ES8156_REG0D_LRCK_1STCNT_SHIFT    4
#define ES8156_REG0D_LRCK_1STCNT_MASK     (0x0F << ES8156_REG0D_LRCK_1STCNT_SHIFT)

// ============================================================================
// Register 0x11 - SDP Interface Config 1
// ============================================================================
#define ES8156_REG11_SP_PROTOCOL_MASK     0x03      // Serial port protocol [1:0]
#define ES8156_REG11_SP_PROTOCOL_I2S      0x00      // I2S format
#define ES8156_REG11_SP_PROTOCOL_LJ       0x01      // Left-justified
#define ES8156_REG11_SP_PROTOCOL_RJ       0x02      // Right-justified
#define ES8156_REG11_SP_PROTOCOL_DSP      0x03      // DSP/PCM format
#define ES8156_REG11_SP_LRP_BIT           (1 << 2)  // LR polarity
#define ES8156_REG11_SP_MUTE_BIT          (1 << 3)  // Serial port mute
#define ES8156_REG11_SP_WL_SHIFT          4         // Word length [6:4]
#define ES8156_REG11_SP_WL_MASK           (0x07 << ES8156_REG11_SP_WL_SHIFT)
#define ES8156_REG11_SP_WL_24BIT          (0x00 << ES8156_REG11_SP_WL_SHIFT)
#define ES8156_REG11_SP_WL_20BIT          (0x01 << ES8156_REG11_SP_WL_SHIFT)
#define ES8156_REG11_SP_WL_18BIT          (0x02 << ES8156_REG11_SP_WL_SHIFT)
#define ES8156_REG11_SP_WL_16BIT          (0x03 << ES8156_REG11_SP_WL_SHIFT)
#define ES8156_REG11_SP_WL_32BIT          (0x04 << ES8156_REG11_SP_WL_SHIFT)

// ============================================================================
// Register 0x13 - Mute Control
// ============================================================================
#define ES8156_REG13_AM_ENA_BIT           (1 << 0)  // Automute enable
#define ES8156_REG13_LCH_DSM_SMUTE_BIT    (1 << 1)  // Left channel DSM soft mute
#define ES8156_REG13_RCH_DSM_SMUTE_BIT    (1 << 2)  // Right channel DSM soft mute
#define ES8156_REG13_AM_MUTE_FLAG_BIT     (1 << 3)  // Automute mute flag (read-only)
#define ES8156_REG13_AM_DSMMUTE_ENA_BIT   (1 << 4)  // Automute DSM mute enable
#define ES8156_REG13_AM_ACLKOFF_ENA_BIT   (1 << 5)  // Automute analog clock off enable
#define ES8156_REG13_AM_ATTENU6_ENA_BIT   (1 << 6)  // Automute 6dB attenuation enable
#define ES8156_REG13_INTOUT_CLIPEN_BIT    (1 << 7)  // Clip interrupt output enable

// ============================================================================
// Register 0x14 - Volume Control
// ============================================================================
// 0x00 = mute
// 0xBF (191) = 0dB (recommended max for normal use)
// 0xFF (255) = +32dB (risk of clipping)
// Tanmatsu BSP uses 180 max: value = 180.0 * (percentage / 100.0)

// ============================================================================
// Register 0x18 - Misc Control 3
// ============================================================================
#define ES8156_REG18_DAC_RAM_CLR_BIT      (1 << 0)  // DAC RAM clear
#define ES8156_REG18_DSM_DITHERON_BIT     (1 << 1)  // DSM dither on
#define ES8156_REG18_RCH_INV_BIT          (1 << 2)  // Right channel invert
#define ES8156_REG18_LCH_INV_BIT          (1 << 3)  // Left channel invert
#define ES8156_REG18_CHN_CROSS_SHIFT      4         // Channel crossover [5:4]
#define ES8156_REG18_CHN_CROSS_MASK       (0x03 << ES8156_REG18_CHN_CROSS_SHIFT)
#define ES8156_REG18_P2S_DPATH_SEL_BIT    (1 << 6)  // P2S data path select
#define ES8156_REG18_P2S_DATA_BITNUM_BIT  (1 << 7)  // P2S data bit number

// ============================================================================
// Register 0x19 - EQ Control 1
// ============================================================================
#define ES8156_REG19_EQ_ON_BIT            (1 << 0)  // EQ on
#define ES8156_REG19_EQ_CFG_WR_BIT        (1 << 1)  // EQ config write
#define ES8156_REG19_EQ_CFG_RD_BIT        (1 << 2)  // EQ config read
#define ES8156_REG19_EQ_RST_BIT           (1 << 3)  // EQ reset

// ============================================================================
// Register 0x20 - Analog System 1
// ============================================================================
#define ES8156_REG20_S6_SEL_MASK          0x03      // S6 select [1:0]
#define ES8156_REG20_S2_SEL_SHIFT         2
#define ES8156_REG20_S2_SEL_MASK          (0x03 << ES8156_REG20_S2_SEL_SHIFT)
#define ES8156_REG20_S3_SEL_SHIFT         4
#define ES8156_REG20_S3_SEL_MASK          (0x03 << ES8156_REG20_S3_SEL_SHIFT)

// ============================================================================
// Register 0x21 - Analog System 2
// ============================================================================
#define ES8156_REG21_VSEL_MASK            0x1F      // VSEL [4:0]
#define ES8156_REG21_VREF_RMPDN1_BIT      (1 << 5)  // VREF ramp down 1
#define ES8156_REG21_VREF_RMPDN2_BIT      (1 << 6)  // VREF ramp down 2

// ============================================================================
// Register 0x22 - Analog System 3
// ============================================================================
#define ES8156_REG22_OUT_MUTE_BIT         (1 << 0)  // Output mute
#define ES8156_REG22_SWRMPSEL_BIT         (1 << 1)  // Software ramp select
#define ES8156_REG22_HPSW_BIT             (1 << 3)  // Headphone switch

// ============================================================================
// Register 0x23 - Analog System 4
// ============================================================================
#define ES8156_REG23_HPCOM_REF1_BIT       (1 << 0)
#define ES8156_REG23_HPCOM_REF2_BIT       (1 << 1)
#define ES8156_REG23_VROI_BIT             (1 << 2)
#define ES8156_REG23_DAC_IBIAS_SW_BIT     (1 << 3)
#define ES8156_REG23_VMIDLVL_SHIFT        4
#define ES8156_REG23_VMIDLVL_MASK         (0x03 << ES8156_REG23_VMIDLVL_SHIFT)
#define ES8156_REG23_IBIAS_SW_SHIFT       6
#define ES8156_REG23_IBIAS_SW_MASK        (0x03 << ES8156_REG23_IBIAS_SW_SHIFT)

// ============================================================================
// Register 0x24 - Analog System 5
// ============================================================================
#define ES8156_REG24_LPVREFBUF_BIT        (1 << 0)  // Low power VREF buffer
#define ES8156_REG24_LPHPCOM_BIT          (1 << 1)  // Low power HP common
#define ES8156_REG24_LPDACVRP_BIT         (1 << 2)  // Low power DAC VRP
#define ES8156_REG24_LPDAC_BIT            (1 << 3)  // Low power DAC

// ============================================================================
// Register 0x25 - Analog System 6 (Power Control)
// ============================================================================
#define ES8156_REG25_PDN_DAC_BIT          (1 << 0)  // Power down DAC
#define ES8156_REG25_PDN_VREFBUF_BIT      (1 << 1)  // Power down VREF buffer
#define ES8156_REG25_PDN_DACVREFGEN_BIT   (1 << 2)  // Power down DAC VREF generator
#define ES8156_REG25_ENHPCOM_BIT          (1 << 3)  // Enable HP common
#define ES8156_REG25_VMIDSEL_SHIFT        4
#define ES8156_REG25_VMIDSEL_MASK         (0x03 << ES8156_REG25_VMIDSEL_SHIFT)
#define ES8156_REG25_ENREFR_BIT           (1 << 6)  // Enable reference resistor
#define ES8156_REG25_PDN_ANA_BIT          (1 << 7)  // Power down analog (master)

// ============================================================================
// Chip identification
// ============================================================================
#define ES8156_CHIP_ID_HIGH               0x81
#define ES8156_CHIP_ID_LOW                0x56
