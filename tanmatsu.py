"""
tanmatsu.py - ES8156 Audio Codec Helper Library for Tanmatsu

This module provides a Python interface for the ES8156 DAC audio codec
used in Tanmatsu devices. Based on the Nicolai Electronics and Badge Team
ESP32 component implementations.

References:
- https://github.com/Nicolai-Electronics/esp32-component-es8156
- https://github.com/badgeteam/esp32-component-badge-bsp

Usage (MicroPython):
    from machine import I2C, Pin
    from tanmatsu import ES8156
    
    i2c = I2C(0, sda=Pin(7), scl=Pin(8), freq=400000)
    codec = ES8156(i2c)
    codec.init()
    codec.set_volume(70)  # 0-100%

Usage (CPython with smbus2):
    from smbus2 import SMBus
    from tanmatsu import ES8156
    
    bus = SMBus(1)
    codec = ES8156(bus)
    codec.init()
    codec.set_volume(70)
"""

# ES8156 Register Addresses (Page 0)
ES8156_REG_RESET_CONTROL          = 0x00
ES8156_REG_MAIN_CLOCK_CONTROL     = 0x01
ES8156_REG_MODE_CONFIG_1          = 0x02
ES8156_REG_MASTER_LRCK_DIVIDER_1  = 0x03
ES8156_REG_MASTER_LRCK_DIVIDER_0  = 0x04
ES8156_REG_MASTER_CLOCK_CONTROL   = 0x05
ES8156_REG_NFS_CONFIG             = 0x06
ES8156_REG_MISC_CONTROL_1         = 0x07
ES8156_REG_CLOCK_OFF              = 0x08
ES8156_REG_MISC_CONTROL_2         = 0x09
ES8156_REG_TIME_CONTROL_1         = 0x0A
ES8156_REG_TIME_CONTROL_2         = 0x0B
ES8156_REG_CHIP_STATUS            = 0x0C
ES8156_REG_P2S_CONTROL            = 0x0D
ES8156_REG_DAC_COUNTER_PARAMETER  = 0x10
ES8156_REG_SDP_INTERFACE_CONFIG   = 0x11
ES8156_REG_AUTOMUTE_CONTROL       = 0x12
ES8156_REG_MUTE_CONTROL           = 0x13
ES8156_REG_VOLUME_CONTROL         = 0x14
ES8156_REG_ALC_CONFIG_1           = 0x15
ES8156_REG_ALC_CONFIG_2           = 0x16
ES8156_REG_ALC_LEVEL              = 0x17
ES8156_REG_MISC_CONTROL_3         = 0x18
ES8156_REG_EQ_CONTROL_1           = 0x19
ES8156_REG_EQ_CONFIG_2            = 0x1A
ES8156_REG_ANALOG_SYSTEM_1        = 0x20
ES8156_REG_ANALOG_SYSTEM_2        = 0x21
ES8156_REG_ANALOG_SYSTEM_3        = 0x22
ES8156_REG_ANALOG_SYSTEM_4        = 0x23
ES8156_REG_ANALOG_SYSTEM_5        = 0x24
ES8156_REG_ANALOG_SYSTEM_6        = 0x25
ES8156_REG_PAGE_SELECT            = 0xFC
ES8156_REG_CHIP_ID1               = 0xFD
ES8156_REG_CHIP_ID0               = 0xFE
ES8156_REG_CHIP_VERSION           = 0xFF

# Expected chip ID
ES8156_CHIP_ID = 0x8156

# Default I2C address (7-bit) - some boards use 0x08, others 0x09
ES8156_I2C_ADDR = 0x08


class ES8156:
    """ES8156 Audio DAC Codec Driver
    
    This driver supports the ES8156 DAC used in Tanmatsu devices.
    Note: ES8156 is different from ES8311 - they have different register maps!
    """
    
    def __init__(self, i2c, addr=ES8156_I2C_ADDR, debug=False):
        """Initialize ES8156 driver.
        
        Args:
            i2c: I2C bus object (MicroPython I2C or smbus2.SMBus)
            addr: 7-bit I2C address (default 0x08)
            debug: Enable debug output
        """
        self._i2c = i2c
        self._addr = addr
        self._debug = debug
        self._is_micropython = hasattr(i2c, 'writeto_mem')
        self._initialized = False
        
    def _write_reg(self, reg, value):
        """Write a single register."""
        if self._debug:
            print(f"ES8156 W [0x{reg:02X}] <= 0x{value:02X}")
        
        if self._is_micropython:
            self._i2c.writeto_mem(self._addr, reg, bytes([value]))
        else:
            # smbus2
            self._i2c.write_byte_data(self._addr, reg, value)
    
    def _read_reg(self, reg):
        """Read a single register."""
        if self._is_micropython:
            data = self._i2c.readfrom_mem(self._addr, reg, 1)
            value = data[0]
        else:
            # smbus2
            value = self._i2c.read_byte_data(self._addr, reg)
        
        if self._debug:
            print(f"ES8156 R [0x{reg:02X}] => 0x{value:02X}")
        return value
    
    def probe(self):
        """Probe the ES8156 and verify chip ID.
        
        Returns:
            tuple: (chip_id, version) or (None, None) if not found
        """
        try:
            id_high = self._read_reg(ES8156_REG_CHIP_ID1)
            id_low = self._read_reg(ES8156_REG_CHIP_ID0)
            version = self._read_reg(ES8156_REG_CHIP_VERSION)
            
            chip_id = (id_high << 8) | id_low
            
            if self._debug:
                print(f"ES8156: Chip ID = 0x{chip_id:04X}, Version = 0x{version:02X}")
            
            return chip_id, version
        except Exception as e:
            if self._debug:
                print(f"ES8156: Probe failed - {e}")
            return None, None
    
    def init(self):
        """Initialize the ES8156 codec for audio playback.
        
        This follows the initialization sequence from the Tanmatsu BSP:
        https://github.com/badgeteam/esp32-component-badge-bsp
        
        Returns:
            bool: True if initialization successful
        """
        # Verify chip ID first
        chip_id, _ = self.probe()
        if chip_id != ES8156_CHIP_ID:
            if self._debug:
                print(f"ES8156: Unexpected chip ID 0x{chip_id:04X}, expected 0x{ES8156_CHIP_ID:04X}")
            # Continue anyway - some chips may have different IDs
        
        # Configuration sequence from es8156_configure() in badge BSP
        
        # REG 0x02 - Mode Config 1
        # bit 2: SOFT_MODE_SEL = 1 (use MCLK from external source)
        self._write_reg(ES8156_REG_MODE_CONFIG_1, 0x04)
        
        # REG 0x20 - Analog System 1
        # S6_SEL=2, S2_SEL=2, S3_SEL=2
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_1, 0x2A)  # (2 << 4) | (2 << 2) | 2
        
        # REG 0x21 - Analog System 2
        # VSEL = 0x1C, VREF_RMPDN1 = 1
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_2, 0x3C)  # 0x1C | (1 << 5)
        
        # REG 0x22 - Analog System 3
        # All bits 0 (no mute, no HP switch)
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_3, 0x00)
        
        # REG 0x24 - Analog System 5
        # LPVREFBUF=1, LPHPCOM=1, LPDACVRP=1, LPDAC=0
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_5, 0x07)
        
        # REG 0x23 - Analog System 4
        # VROI=1
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_4, 0x04)
        
        # REG 0x0A - Time Control 1
        self._write_reg(ES8156_REG_TIME_CONTROL_1, 0x01)
        
        # REG 0x0B - Time Control 2
        self._write_reg(ES8156_REG_TIME_CONTROL_2, 0x01)
        
        # REG 0x11 - SDP Interface Config
        # I2S format (0), no mute, 16-bit (0x30)
        self._write_reg(ES8156_REG_SDP_INTERFACE_CONFIG, 0x30)
        
        # REG 0x0D - P2S Control
        # P2S_SDOUT_MUTEB=1, LRCK_1STCNT=1
        self._write_reg(ES8156_REG_P2S_CONTROL, 0x14)
        
        # REG 0x18 - Misc Control 3
        # All 0 (no DAC RAM clear, no inversion)
        self._write_reg(ES8156_REG_MISC_CONTROL_3, 0x00)
        
        # REG 0x08 - Clock Off
        # Enable all clocks: MCLK, DAC_MCLK, ANA_CLK, EXT_SCLKLRCK, MASTER_CLK, P2S_CLK
        self._write_reg(ES8156_REG_CLOCK_OFF, 0x3F)
        
        # REG 0x00 - Reset Control
        # First: SEQ_DIS=1 (disable internal power sequence)
        self._write_reg(ES8156_REG_RESET_CONTROL, 0x02)
        
        # Then: CSM_ON=1, SEQ_DIS=1 (enable chip state machine)
        self._write_reg(ES8156_REG_RESET_CONTROL, 0x03)
        
        # REG 0x25 - Analog System 6 (Power Control)
        # VMIDSEL=2, everything else powered on (no PDN bits set)
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_6, 0x20)
        
        # Set initial volume (0-191 range, 100 is a safe default)
        self._write_reg(ES8156_REG_VOLUME_CONTROL, 100)
        
        self._initialized = True
        
        if self._debug:
            print("ES8156: Initialization complete")
        
        return True
    
    def set_volume(self, percent):
        """Set volume as percentage (0-100).
        
        The ES8156 volume register maps as:
        - 0x00 = mute
        - 0xBF (191) = 0dB  
        - 0xFF (255) = +32dB (not recommended)
        
        Tanmatsu BSP uses max 180 for safety.
        
        Args:
            percent: Volume percentage (0-100)
        """
        if percent < 0:
            percent = 0
        elif percent > 100:
            percent = 100
        
        # Map 0-100% to 0-180 range (Tanmatsu safe range)
        value = int(180 * percent / 100)
        
        if self._debug:
            print(f"ES8156: Set volume {percent}% -> reg value {value}")
        
        self._write_reg(ES8156_REG_VOLUME_CONTROL, value)
    
    def set_volume_raw(self, value):
        """Set volume using raw register value (0-255).
        
        Args:
            value: Raw volume register value
                   0 = mute
                   191 (0xBF) = 0dB
                   255 = +32dB (risk of clipping!)
        """
        if value < 0:
            value = 0
        elif value > 255:
            value = 255
        
        self._write_reg(ES8156_REG_VOLUME_CONTROL, value)
    
    def mute(self, enable=True):
        """Mute or unmute audio output.
        
        Args:
            enable: True to mute, False to unmute
        """
        current = self._read_reg(ES8156_REG_MUTE_CONTROL)
        
        if enable:
            # Set soft mute bits for both channels (bits 1 and 2)
            current |= 0x06
        else:
            # Clear soft mute bits
            current &= ~0x06
        
        self._write_reg(ES8156_REG_MUTE_CONTROL, current)
    
    def standby(self):
        """Put codec into standby mode (low power, no pop noise).
        
        Based on es8156_standby_nopop() from badge BSP.
        """
        # Set volume to 0
        self._write_reg(ES8156_REG_VOLUME_CONTROL, 0)
        
        # EQ off, config write
        self._write_reg(ES8156_REG_EQ_CONTROL_1, 0x02)
        
        # Power down analog with VMIDSEL=2
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_6, 0xA0)  # PDN_ANA=1, VMIDSEL=2
        
        # DAC RAM clear
        self._write_reg(ES8156_REG_MISC_CONTROL_3, 0x01)
        
        # Misc control 2: DLL on
        self._write_reg(ES8156_REG_MISC_CONTROL_2, 0x02)
        
        # Misc control 2: PUPDN off
        self._write_reg(ES8156_REG_MISC_CONTROL_2, 0x01)
        
        # Disable all clocks
        self._write_reg(ES8156_REG_CLOCK_OFF, 0x00)
        
        self._initialized = False
        
        if self._debug:
            print("ES8156: Entered standby mode")
    
    def powerdown(self):
        """Fully power down the codec.
        
        Based on es8156_powerdown() from badge BSP.
        Note: This may cause a pop noise. Use standby() for quiet shutdown.
        """
        import time
        
        # Set volume to 0
        self._write_reg(ES8156_REG_VOLUME_CONTROL, 0)
        
        # EQ off
        self._write_reg(ES8156_REG_EQ_CONTROL_1, 0x02)
        
        # Analog mute
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_3, 0x01)
        
        # Power down analog (PDN_ANA=1, PDN_DAC=1)
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_6, 0x81)
        
        # DAC RAM clear
        self._write_reg(ES8156_REG_MISC_CONTROL_3, 0x01)
        
        # Misc control 2
        self._write_reg(ES8156_REG_MISC_CONTROL_2, 0x02)
        self._write_reg(ES8156_REG_MISC_CONTROL_2, 0x01)
        
        # Disable all clocks
        self._write_reg(ES8156_REG_CLOCK_OFF, 0x00)
        
        # Wait for power down
        time.sleep(0.5)
        
        # Full power down
        self._write_reg(ES8156_REG_ANALOG_SYSTEM_6, 0x87)  # All PDN bits set
        
        self._initialized = False
        
        if self._debug:
            print("ES8156: Powered down")
    
    def reset(self):
        """Reset the codec.
        
        Based on es8156_reset() from badge BSP.
        """
        # Reset digital, DAC digital, and master generator
        self._write_reg(ES8156_REG_RESET_CONTROL, 0x1C)
        
        # Enable chip state machine
        self._write_reg(ES8156_REG_RESET_CONTROL, 0x01)
        
        self._initialized = False
        
        if self._debug:
            print("ES8156: Reset complete")
    
    def dump_registers(self):
        """Dump all registers for debugging."""
        print("ES8156 Register Dump:")
        print("-" * 40)
        
        regs = [
            (0x00, "RESET_CONTROL"),
            (0x01, "MAIN_CLOCK_CONTROL"),
            (0x02, "MODE_CONFIG_1"),
            (0x03, "MASTER_LRCK_DIV_1"),
            (0x04, "MASTER_LRCK_DIV_0"),
            (0x05, "MASTER_CLOCK_CTRL"),
            (0x06, "NFS_CONFIG"),
            (0x07, "MISC_CONTROL_1"),
            (0x08, "CLOCK_OFF"),
            (0x09, "MISC_CONTROL_2"),
            (0x0A, "TIME_CONTROL_1"),
            (0x0B, "TIME_CONTROL_2"),
            (0x0C, "CHIP_STATUS"),
            (0x0D, "P2S_CONTROL"),
            (0x10, "DAC_COUNTER_PARAM"),
            (0x11, "SDP_INTERFACE_CFG"),
            (0x12, "AUTOMUTE_CONTROL"),
            (0x13, "MUTE_CONTROL"),
            (0x14, "VOLUME_CONTROL"),
            (0x15, "ALC_CONFIG_1"),
            (0x16, "ALC_CONFIG_2"),
            (0x17, "ALC_LEVEL"),
            (0x18, "MISC_CONTROL_3"),
            (0x19, "EQ_CONTROL_1"),
            (0x1A, "EQ_CONFIG_2"),
            (0x20, "ANALOG_SYSTEM_1"),
            (0x21, "ANALOG_SYSTEM_2"),
            (0x22, "ANALOG_SYSTEM_3"),
            (0x23, "ANALOG_SYSTEM_4"),
            (0x24, "ANALOG_SYSTEM_5"),
            (0x25, "ANALOG_SYSTEM_6"),
            (0xFC, "PAGE_SELECT"),
            (0xFD, "CHIP_ID1"),
            (0xFE, "CHIP_ID0"),
            (0xFF, "CHIP_VERSION"),
        ]
        
        for addr, name in regs:
            try:
                val = self._read_reg(addr)
                print(f"  0x{addr:02X} {name:20s} = 0x{val:02X} ({val:3d})")
            except Exception as e:
                print(f"  0x{addr:02X} {name:20s} = ERROR: {e}")
        
        print("-" * 40)


# Convenience function for quick setup
def init_audio(i2c, addr=ES8156_I2C_ADDR, volume=70, debug=False):
    """Quick initialization of ES8156 audio codec.
    
    Args:
        i2c: I2C bus object
        addr: I2C address (default 0x08)
        volume: Initial volume percentage (default 70)
        debug: Enable debug output
    
    Returns:
        ES8156: Initialized codec instance, or None on failure
    """
    codec = ES8156(i2c, addr, debug)
    
    chip_id, version = codec.probe()
    if chip_id is None:
        print(f"ES8156: No codec found at address 0x{addr:02X}")
        return None
    
    if chip_id != ES8156_CHIP_ID:
        print(f"ES8156: Warning - unexpected chip ID 0x{chip_id:04X}")
    
    codec.init()
    codec.set_volume(volume)
    
    return codec


# Test code
if __name__ == "__main__":
    print("ES8156 Audio Codec Test")
    print("=" * 40)
    
    # Try MicroPython first
    try:
        from machine import I2C, Pin
        print("Using MicroPython I2C")
        
        # Common pin configurations - adjust for your board
        # Tanmatsu/ESP32-P4: SDA=7, SCL=8
        i2c = I2C(0, sda=Pin(7), scl=Pin(8), freq=400000)
        
        print(f"I2C scan: {[hex(x) for x in i2c.scan()]}")
        
        codec = ES8156(i2c, debug=True)
        chip_id, version = codec.probe()
        
        if chip_id:
            print(f"\nFound ES8156: ID=0x{chip_id:04X}, Version=0x{version:02X}")
            codec.init()
            codec.set_volume(50)
            print("\nRegister dump after init:")
            codec.dump_registers()
        else:
            print("ES8156 not found!")
            
    except ImportError:
        print("MicroPython not available")
        
        # Try smbus2 (Linux/Raspberry Pi)
        try:
            from smbus2 import SMBus
            print("Using smbus2")
            
            bus = SMBus(1)  # Adjust bus number as needed
            codec = ES8156(bus, debug=True)
            chip_id, version = codec.probe()
            
            if chip_id:
                print(f"\nFound ES8156: ID=0x{chip_id:04X}, Version=0x{version:02X}")
                codec.init()
                codec.set_volume(50)
            else:
                print("ES8156 not found!")
                
        except ImportError:
            print("smbus2 not available")
            print("\nTo test, install smbus2: pip install smbus2")
