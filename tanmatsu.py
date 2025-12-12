"""
Tanmatsu Board Helper Library for CircuitPython

This library provides a Python interface to interact with the Tanmatsu board's
coprocessor (keyboard, backlight, LEDs) and audio codec (ES8156).

Copyright (c) 2024 Nicolai Electronics

Sources and references:
- Badge Team ESP32 Component Badge BSP: https://github.com/badgeteam/esp32-component-badge-bsp
  * tanmatsu_hardware.h - Hardware pin definitions and constants
  * tanmatsu_coprocessor.c - Coprocessor register addresses and communication protocol
  * Hardware documentation and schematics
- Nicolai Electronics ES8156 Component: https://github.com/Nicolai-Electronics/esp32-component-es8156
  * es8156_regs.h - Correct register addresses
  * es8156.c - Proper initialization sequence
- ES8156 audio codec datasheet and register documentation
- M5Stack Tab5 board definition (base reference for ESP32-P4 port)

Register addresses are from tanmatsu_coprocessor.c in the Badge Team BSP:
- Coprocessor I2C address: 0x5F
- ES8156 I2C address: 0x08
- I2C protocol: Write register address, then read/write data

Volume range: 0-180 dB (based on BSP implementation: value = 180.0 * (percentage / 100.0))

Note on keyboard input to sys.stdin:
CircuitPython does not provide a Python API to inject characters directly into sys.stdin.
To use the Tanmatsu keyboard as a USB keyboard, use USB HID device mode:
1. Enable USB HID: usb_hid.enable((usb_hid.Device.KEYBOARD,))
2. Use keyboard_to_hid_report() to convert Tanmatsu keyboard state to HID reports
3. Send reports via keyboard_device.send_report()
"""

import time
from micropython import const

# Coprocessor I2C address
COPROCESSOR_ADDRESS = const(0x5F)

# ES8156 Audio Codec I2C address
ES8156_ADDRESS = const(0x08)

# Coprocessor register addresses (from tanmatsu_coprocessor.c)
REG_FW_VERSION_0 = const(0x00)  # Firmware version LSB
REG_FW_VERSION_1 = const(0x01)  # Firmware version MSB
REG_KEYBOARD_0 = const(0x02)  # Keyboard state (9 bytes)
REG_DISPLAY_BACKLIGHT = const(0x0B)  # Display backlight (0-255)
REG_KEYBOARD_BACKLIGHT = const(0x0C)  # Keyboard backlight (0-255)
REG_INTERRUPT = const(0x0D)  # Interrupt status register
REG_LED_BRIGHTNESS = const(0x0E)  # LED brightness (0-255)
REG_INPUT = const(0x0F)  # Input states (SD card, headphone, power button)
REG_OUTPUT = const(0x10)  # Output states (amplifier, camera GPIO)

# PMIC/Battery registers (from tanmatsu_coprocessor.c - decimal addresses)
REG_PMIC_COMM_FAULT = const(106)  # 0x6A - PMIC communication fault
REG_PMIC_FAULT = const(107)  # 0x6B - PMIC faults
REG_PMIC_ADC_CONTROL = const(108)  # 0x6C - PMIC ADC control
REG_PMIC_ADC_VBAT_0 = const(109)  # 0x6D - Battery voltage LSB
REG_PMIC_ADC_VBAT_1 = const(110)  # 0x6E - Battery voltage MSB
REG_PMIC_ADC_VSYS_0 = const(111)  # 0x6F - System voltage LSB
REG_PMIC_ADC_VSYS_1 = const(112)  # 0x70 - System voltage MSB
REG_PMIC_ADC_TS_0 = const(113)  # 0x71 - Temperature sensor LSB
REG_PMIC_ADC_TS_1 = const(114)  # 0x72 - Temperature sensor MSB
REG_PMIC_ADC_VBUS_0 = const(115)  # 0x73 - USB voltage LSB
REG_PMIC_ADC_VBUS_1 = const(116)  # 0x74 - USB voltage MSB
REG_PMIC_ADC_ICHGR_0 = const(117)  # 0x75 - Charging current LSB
REG_PMIC_ADC_ICHGR_1 = const(118)  # 0x76 - Charging current MSB
REG_PMIC_CHARGING_CONTROL = const(119)  # 0x77 - Charging control
REG_PMIC_CHARGING_STATUS = const(120)  # 0x78 - Charging status
REG_PMIC_OTG_CONTROL = const(121)  # 0x79 - OTG control

# =============================================================================
# ES8156 register addresses - CORRECTED from Nicolai Electronics es8156_regs.h
# https://github.com/Nicolai-Electronics/esp32-component-es8156/blob/main/es8156_regs.h
# =============================================================================
ES8156_REG_PAGE_SELECT = const(0xFC)  # Page select register
ES8156_REG_CHIP_ID1 = const(0xFD)  # Chip ID high byte (page 0)
ES8156_REG_CHIP_ID0 = const(0xFE)  # Chip ID low byte (page 0)
ES8156_REG_CHIP_VERSION = const(0xFF)  # Chip version (page 0)

# Page 0 registers - CORRECTED addresses
ES8156_REG_RESET_CONTROL = const(0x00)
ES8156_REG_MAIN_CLOCK_CONTROL = const(0x01)
ES8156_REG_MODE_CONFIG = const(0x02)
ES8156_REG_MASTER_LRCK_DIVIDER_1 = const(0x03)
ES8156_REG_MASTER_LRCK_DIVIDER_0 = const(0x04)
ES8156_REG_MASTER_CLOCK_CONTROL = const(0x05)
ES8156_REG_NFS_CONFIG = const(0x06)
ES8156_REG_MISC_CONTROL_1 = const(0x07)
ES8156_REG_CLOCK_OFF = const(0x08)  # Was 0x07 - FIXED
ES8156_REG_MISC_CONTROL_2 = const(0x09)  # Was 0x08 - FIXED
ES8156_REG_TIME_CONTROL_1 = const(0x0A)  # Was 0x09 - FIXED
ES8156_REG_TIME_CONTROL_2 = const(0x0B)  # Was 0x0A - FIXED
ES8156_REG_CHIP_STATUS = const(0x0C)  # Was 0x0B - FIXED
ES8156_REG_P2S_CONTROL = const(0x0D)  # Was 0x0C - FIXED
ES8156_REG_DAC_COUNTER_PARAMETER = const(0x10)  # Was 0x0D - FIXED (note: gap at 0x0E-0x0F)
ES8156_REG_SDP_INTERFACE_CONFIG = const(0x11)  # Was 0x0E - FIXED
ES8156_REG_AUTOMUTE_CONTROL = const(0x12)  # Was 0x0F - FIXED
ES8156_REG_MUTE_CONTROL = const(0x13)  # Was 0x10 - FIXED
ES8156_REG_VOLUME_CONTROL = const(0x14)  # Was 0x11 - FIXED! Main volume register
ES8156_REG_ALC_CONFIG_1 = const(0x15)  # Was 0x12 - FIXED
ES8156_REG_ALC_CONFIG_2 = const(0x16)  # Was 0x13 - FIXED
ES8156_REG_ALC_LEVEL = const(0x17)  # Was 0x14 - FIXED
ES8156_REG_MISC_CONTROL_3 = const(0x18)  # Was 0x15 - FIXED
ES8156_REG_EQ_CONTROL_1 = const(0x19)  # Was 0x16 - FIXED
ES8156_REG_EQ_CONFIG_2 = const(0x1A)  # Was 0x17 - FIXED
# Note: gap at 0x1B-0x1F
ES8156_REG_ANALOG_SYSTEM_1 = const(0x20)  # Was 0x18 - FIXED!
ES8156_REG_ANALOG_SYSTEM_2 = const(0x21)  # Was 0x19 - FIXED!
ES8156_REG_ANALOG_SYSTEM_3 = const(0x22)  # Was 0x1A - FIXED!
ES8156_REG_ANALOG_SYSTEM_4 = const(0x23)  # Was 0x1B - FIXED!
ES8156_REG_ANALOG_SYSTEM_5 = const(0x24)  # Was 0x1C - FIXED!
ES8156_REG_ANALOG_SYSTEM_6 = const(0x25)  # Was 0x1D - FIXED!


class KeyboardState:
    """Represents the state of all keyboard keys."""
    
    def __init__(self, raw_bytes):
        """Initialize from 9 raw bytes from coprocessor."""
        self.raw = bytearray(raw_bytes[:9])
        self._parse_keys()
    
    def _parse_keys(self):
        """Parse the raw bytes into individual key states."""
        # Map keys based on the C struct layout
        byte0 = self.raw[0]
        byte1 = self.raw[1]
        byte2 = self.raw[2]
        byte3 = self.raw[3]
        byte4 = self.raw[4]
        byte5 = self.raw[5]
        byte6 = self.raw[6]
        byte7 = self.raw[7]
        byte8 = self.raw[8]
        
        # Row 1
        self.key_esc = bool(byte0 & 0x01)
        self.key_f1 = bool(byte0 & 0x02)
        self.key_f2 = bool(byte0 & 0x04)
        self.key_f3 = bool(byte0 & 0x08)
        self.key_tilde = bool(byte0 & 0x10)
        self.key_1 = bool(byte0 & 0x20)
        self.key_2 = bool(byte0 & 0x40)
        self.key_3 = bool(byte0 & 0x80)
        
        # Row 2
        self.key_tab = bool(byte1 & 0x01)
        self.key_q = bool(byte1 & 0x02)
        self.key_w = bool(byte1 & 0x04)
        self.key_e = bool(byte1 & 0x08)
        self.key_fn = bool(byte1 & 0x10)
        self.key_a = bool(byte1 & 0x20)
        self.key_s = bool(byte1 & 0x40)
        self.key_d = bool(byte1 & 0x80)
        
        # Row 3
        self.key_shift_l = bool(byte2 & 0x01)
        self.key_z = bool(byte2 & 0x02)
        self.key_x = bool(byte2 & 0x04)
        self.key_c = bool(byte2 & 0x08)
        self.key_ctrl = bool(byte2 & 0x10)
        self.key_meta = bool(byte2 & 0x20)
        self.key_alt_l = bool(byte2 & 0x40)
        self.key_backslash = bool(byte2 & 0x80)
        
        # Row 4
        self.key_4 = bool(byte3 & 0x01)
        self.key_5 = bool(byte3 & 0x02)
        self.key_6 = bool(byte3 & 0x04)
        self.key_7 = bool(byte3 & 0x08)
        self.key_r = bool(byte3 & 0x10)
        self.key_t = bool(byte3 & 0x20)
        self.key_y = bool(byte3 & 0x40)
        self.key_u = bool(byte3 & 0x80)
        
        # Row 5
        self.key_f = bool(byte4 & 0x01)
        self.key_g = bool(byte4 & 0x02)
        self.key_h = bool(byte4 & 0x04)
        self.key_j = bool(byte4 & 0x08)
        self.key_v = bool(byte4 & 0x10)
        self.key_b = bool(byte4 & 0x20)
        self.key_n = bool(byte4 & 0x40)
        self.key_m = bool(byte4 & 0x80)
        
        # Row 6
        self.key_f4 = bool(byte5 & 0x01)
        self.key_f5 = bool(byte5 & 0x02)
        self.key_f6 = bool(byte5 & 0x04)
        self.key_backspace = bool(byte5 & 0x08)
        self.key_9 = bool(byte5 & 0x10)
        self.key_0 = bool(byte5 & 0x20)
        self.key_minus = bool(byte5 & 0x40)
        self.key_equals = bool(byte5 & 0x80)
        
        # Row 7
        self.key_o = bool(byte6 & 0x01)
        self.key_p = bool(byte6 & 0x02)
        self.key_sqbracket_open = bool(byte6 & 0x04)
        self.key_sqbracket_close = bool(byte6 & 0x08)
        self.key_l = bool(byte6 & 0x10)
        self.key_semicolon = bool(byte6 & 0x20)
        self.key_quote = bool(byte6 & 0x40)
        self.key_return = bool(byte6 & 0x80)
        
        # Row 8
        self.key_dot = bool(byte7 & 0x01)
        self.key_slash = bool(byte7 & 0x02)
        self.key_up = bool(byte7 & 0x04)
        self.key_shift_r = bool(byte7 & 0x08)
        self.key_alt_r = bool(byte7 & 0x10)
        self.key_left = bool(byte7 & 0x20)
        self.key_down = bool(byte7 & 0x40)
        self.key_right = bool(byte7 & 0x80)
        
        # Row 9
        self.key_8 = bool(byte8 & 0x01)
        self.key_i = bool(byte8 & 0x02)
        self.key_k = bool(byte8 & 0x04)
        self.key_comma = bool(byte8 & 0x08)
        self.key_space_l = bool(byte8 & 0x10)
        self.key_space_m = bool(byte8 & 0x20)
        self.key_space_r = bool(byte8 & 0x40)
        self.key_volume_up = bool(byte8 & 0x80)
    
    def get_pressed_keys(self):
        """Return a list of currently pressed key names."""
        pressed = []
        for attr_name in dir(self):
            if attr_name.startswith('key_') and getattr(self, attr_name):
                pressed.append(attr_name[4:])  # Remove 'key_' prefix
        return pressed
    
    def __repr__(self):
        pressed = self.get_pressed_keys()
        if pressed:
            return f"KeyboardState(pressed={pressed})"
        return "KeyboardState(no keys pressed)"


class Coprocessor:
    """Interface to the Tanmatsu coprocessor for keyboard, backlight, and LED control."""
    
    def __init__(self, i2c, interrupt_pin=None):
        """
        Initialize the coprocessor interface.
        
        :param i2c: I2C bus object (from board.I2C())
        :param interrupt_pin: Optional DigitalInOut pin for interrupt (GPIO1)
        """
        self.i2c = i2c
        self.address = COPROCESSOR_ADDRESS
        self.interrupt_pin = interrupt_pin
        self._last_keyboard_state = None
        
        # Verify communication
        try:
            version = self.get_firmware_version()
            print(f"Tanmatsu coprocessor firmware version: {version}")
        except Exception as e:
            raise RuntimeError(f"Failed to communicate with coprocessor: {e}")
    
    def _read_register(self, register, length=1):
        """
        Read from a coprocessor register.
        Protocol: Write register address, then read data (repeated start).
        """
        buffer = bytearray(length)
        # CircuitPython I2C requires locking
        import time
        timeout = 0.1  # 100ms timeout
        start = time.monotonic()
        while not self.i2c.try_lock():
            if time.monotonic() - start > timeout:
                raise RuntimeError(f"I2C bus locked, cannot read register 0x{register:02X}")
            time.sleep(0.001)  # 1ms
        
        try:
            # Use writeto_then_readfrom for repeated start (write register, then read data)
            write_buf = bytes([register])
            self.i2c.writeto_then_readfrom(self.address, write_buf, buffer)
        except Exception as e:
            self.i2c.unlock()
            raise RuntimeError(f"I2C read error at register 0x{register:02X}: {e}")
        
        self.i2c.unlock()
        return buffer
    
    def _write_register(self, register, value):
        """
        Write to a coprocessor register.
        Protocol: Write register address followed by data byte(s).
        """
        if isinstance(value, int):
            value = bytes([value])
        elif isinstance(value, (bytes, bytearray)):
            pass
        else:
            value = bytes(value)
        
        # CircuitPython I2C requires locking
        import time
        timeout = 0.1  # 100ms timeout
        start = time.monotonic()
        while not self.i2c.try_lock():
            if time.monotonic() - start > timeout:
                raise RuntimeError(f"I2C bus locked, cannot write register 0x{register:02X}")
            time.sleep(0.001)  # 1ms
        
        try:
            self.i2c.writeto(self.address, bytes([register]) + value)
        except Exception as e:
            self.i2c.unlock()
            raise RuntimeError(f"I2C write error at register 0x{register:02X}: {e}")
        
        self.i2c.unlock()
    
    def get_firmware_version(self):
        """Get the coprocessor firmware version."""
        data = self._read_register(REG_FW_VERSION_0, 2)
        return (data[1] << 8) | data[0]
    
    def get_keyboard_state(self):
        """Get the current keyboard state."""
        data = self._read_register(REG_KEYBOARD_0, 9)
        return KeyboardState(data)
    
    def check_keyboard(self):
        """
        Check for keyboard changes and return new state if changed.
        Returns (changed, new_state) tuple.
        """
        new_state = self.get_keyboard_state()
        changed = (self._last_keyboard_state is None or 
                  new_state.raw != self._last_keyboard_state.raw)
        self._last_keyboard_state = new_state
        return changed, new_state
    
    def get_display_backlight(self):
        """Get display backlight brightness (0-255)."""
        data = self._read_register(REG_DISPLAY_BACKLIGHT)
        return data[0]
    
    def set_display_backlight(self, brightness):
        """
        Set display backlight brightness.
        
        :param brightness: Brightness value 0-255 (0 = off, 255 = max)
        """
        brightness = max(0, min(255, int(brightness)))
        self._write_register(REG_DISPLAY_BACKLIGHT, brightness)
    
    def get_keyboard_backlight(self):
        """Get keyboard backlight brightness (0-255)."""
        data = self._read_register(REG_KEYBOARD_BACKLIGHT)
        return data[0]
    
    def set_keyboard_backlight(self, brightness):
        """
        Set keyboard backlight brightness.
        
        :param brightness: Brightness value 0-255 (0 = off, 255 = max)
        """
        brightness = max(0, min(255, int(brightness)))
        self._write_register(REG_KEYBOARD_BACKLIGHT, brightness)
    
    def get_led_brightness(self):
        """Get LED brightness (0-255)."""
        data = self._read_register(REG_LED_BRIGHTNESS)
        return data[0]
    
    def set_led_brightness(self, brightness):
        """
        Set LED brightness.
        
        :param brightness: Brightness value 0-255 (0 = off, 255 = max)
        """
        brightness = max(0, min(255, int(brightness)))
        self._write_register(REG_LED_BRIGHTNESS, brightness)
    
    def get_interrupt(self):
        """
        Get interrupt status.
        Returns dict with: keyboard, input, pmic
        """
        data = self._read_register(REG_INTERRUPT)
        return {
            'keyboard': bool(data[0] & 0x01),
            'input': bool(data[0] & 0x02),
            'pmic': bool(data[0] & 0x04),
        }
    
    def get_inputs(self):
        """
        Get input states.
        Returns dict with: sd_card_detect, headphone_detect, power_button
        """
        data = self._read_register(REG_INPUT)
        return {
            'sd_card_detect': bool(data[0] & 0x01),
            'headphone_detect': bool(data[0] & 0x02),
            'power_button': bool(data[0] & 0x04),
        }
    
    def get_outputs(self):
        """
        Get output states.
        Returns dict with: amplifier_enable, camera_gpio0
        """
        data = self._read_register(REG_OUTPUT)
        return {
            'amplifier_enable': bool(data[0] & 0x01),
            'camera_gpio0': bool(data[0] & 0x02),
        }
    
    def set_outputs(self, amplifier_enable=None, camera_gpio0=None):
        """
        Set output states.
        
        :param amplifier_enable: Enable/disable amplifier
        :param camera_gpio0: Set camera GPIO0 state
        """
        current = self.get_outputs()
        value = 0
        if amplifier_enable is not None:
            value |= (1 if amplifier_enable else 0)
        else:
            value |= (1 if current['amplifier_enable'] else 0)
        
        if camera_gpio0 is not None:
            value |= (2 if camera_gpio0 else 0)
        else:
            value |= (2 if current['camera_gpio0'] else 0)
        
        self._write_register(REG_OUTPUT, value)
    
    def enable_amplifier(self, enable=True):
        """
        Enable or disable the audio amplifier.
        
        :param enable: True to enable amplifier, False to disable
        """
        self.set_outputs(amplifier_enable=enable)
    
    def disable_amplifier(self):
        """Disable the audio amplifier."""
        self.enable_amplifier(False)
    
    def get_battery_voltage(self):
        """
        Get battery voltage in millivolts.
        Returns voltage as uint16_t value.
        """
        data = self._read_register(REG_PMIC_ADC_VBAT_0, 2)
        return data[0] | (data[1] << 8)
    
    def get_system_voltage(self):
        """
        Get system voltage in millivolts.
        Returns voltage as uint16_t value.
        """
        data = self._read_register(REG_PMIC_ADC_VSYS_0, 2)
        return data[0] | (data[1] << 8)
    
    def get_charging_status(self):
        """
        Get charging status.
        Returns dict with:
        - battery_attached: bool
        - usb_attached: bool
        - charging_disabled: bool
        - charging_status: int (0=not charging, 1=pre-charging, 2=fast charging, 3=charge done)
        """
        data = self._read_register(REG_PMIC_CHARGING_STATUS)
        status_byte = data[0]
        
        # Bit layout (from C code):
        # Bit 0: battery_attached
        # Bit 1: usb_attached
        # Bit 2: charging_disabled
        # Bits 3-4: charging_status (2 bits, 0-3)
        battery_attached = bool(status_byte & 0x01)
        usb_attached = bool(status_byte & 0x02)
        charging_disabled = bool(status_byte & 0x04)
        charging_status = (status_byte >> 3) & 0x03  # Bits 3-4 (2 bits)
        
        return {
            'battery_attached': battery_attached,
            'usb_attached': usb_attached,
            'charging_disabled': charging_disabled,
            'charging_status': charging_status,
            'status_text': ['Not Charging', 'Pre-Charging', 'Fast Charging', 'Charge Done'][charging_status]
        }


class ES8156:
    """Interface to the ES8156 audio codec.
    
    Based on Nicolai Electronics esp32-component-es8156 and Badge Team BSP.
    Register addresses corrected from es8156_regs.h.
    """
    
    def __init__(self, i2c):
        """
        Initialize the ES8156 audio codec interface.
        
        :param i2c: I2C bus object (from board.I2C())
        """
        self.i2c = i2c
        self.address = ES8156_ADDRESS
        self._current_page = None  # Track current page
        
        # Verify communication - try to read chip ID but don't fail if it's wrong
        # (page selection might need initialization)
        try:
            chip_id = self.get_chip_id()
            # Expected chip ID is 0x8156
            if chip_id != 0x8156 and chip_id != 0:
                print(f"Warning: ES8156 chip ID is 0x{chip_id:04X}, expected 0x8156")
        except Exception as e:
            # Don't fail initialization if chip ID check fails - volume control might still work
            print(f"Warning: ES8156 chip ID check failed: {e}")
    
    def _select_page(self, page):
        """Select ES8156 register page (0 or 1)."""
        if self._current_page == page:
            return
        
        # Write to page select register (0xFC)
        # Bit 0: 0 = page 0, 1 = page 1
        page_value = 1 if page == 1 else 0
        self._write_register_direct(ES8156_REG_PAGE_SELECT, page_value)
        self._current_page = page
    
    def _read_register_direct(self, register):
        """Read from an ES8156 register directly (no page selection)."""
        buffer = bytearray(1)
        # CircuitPython I2C requires locking
        import time
        timeout = 0.1  # 100ms timeout
        start = time.monotonic()
        while not self.i2c.try_lock():
            if time.monotonic() - start > timeout:
                raise RuntimeError(f"I2C bus locked, cannot read register 0x{register:02X}")
            time.sleep(0.001)  # 1ms
        
        try:
            # Use writeto_then_readfrom for repeated start (write register, then read data)
            write_buf = bytes([register])
            self.i2c.writeto_then_readfrom(self.address, write_buf, buffer)
        except Exception as e:
            self.i2c.unlock()
            raise RuntimeError(f"I2C read error: {e}")
        
        self.i2c.unlock()
        return buffer[0]
    
    def _write_register_direct(self, register, value):
        """Write to an ES8156 register directly (no page selection)."""
        # CircuitPython I2C requires locking
        import time
        timeout = 0.1  # 100ms timeout
        start = time.monotonic()
        while not self.i2c.try_lock():
            if time.monotonic() - start > timeout:
                raise RuntimeError(f"I2C bus locked, cannot write register 0x{register:02X}")
            time.sleep(0.001)  # 1ms
        
        try:
            self.i2c.writeto(self.address, bytes([register, value]))
        except Exception as e:
            self.i2c.unlock()
            raise RuntimeError(f"I2C write error: {e}")
        
        self.i2c.unlock()
    
    def _read_register(self, register, page=0):
        """Read from an ES8156 register with page selection."""
        self._select_page(page)
        return self._read_register_direct(register)
    
    def _write_register(self, register, value, page=0):
        """Write to an ES8156 register with page selection."""
        self._select_page(page)
        self._write_register_direct(register, value)
    
    def get_chip_id(self):
        """Get the ES8156 chip ID (should be 0x8156)."""
        # Chip ID is on page 0, registers 0xFD (ID1) and 0xFE (ID0)
        self._select_page(0)
        id1 = self._read_register_direct(ES8156_REG_CHIP_ID1)
        id0 = self._read_register_direct(ES8156_REG_CHIP_ID0)
        chip_id = (id1 << 8) | id0
        return chip_id
    
    def get_chip_version(self):
        """Get the ES8156 chip version."""
        self._select_page(0)
        return self._read_register_direct(ES8156_REG_CHIP_VERSION)
    
    def get_volume(self):
        """
        Get the current volume setting.
        Returns volume (0-180, where 0 is mute and 180 is safe max).
        """
        return self._read_register(ES8156_REG_VOLUME_CONTROL, page=0)
    
    def set_volume(self, volume_db):
        """
        Set the volume.
        
        :param volume_db: Volume (0-180, where 0 is mute and 180 is safe max)
        Based on BSP: value = 180.0 * (percentage / 100.0)
        """
        volume_db = max(0, min(180, int(volume_db)))
        self._write_register(ES8156_REG_VOLUME_CONTROL, volume_db, page=0)
    
    def set_volume_percent(self, percent):
        """
        Set volume as a percentage.
        
        :param percent: Volume percentage (0-100)
        """
        volume_db = int((percent / 100.0) * 180)
        self.set_volume(volume_db)
    
    def mute(self):
        """Mute the audio output."""
        self.set_volume(0)
    
    def unmute(self, volume_db=90):
        """
        Unmute and set volume.
        
        :param volume_db: Volume (default 90, which is 50%)
        """
        self.set_volume(volume_db)
    
    def configure_for_i2s(self, sample_rate=44100, bit_depth=16):
        """
        Configure ES8156 for I2S audio input.
        
        This follows the initialization sequence from es8156_configure() in
        https://github.com/Nicolai-Electronics/esp32-component-es8156/blob/main/es8156.c
        
        :param sample_rate: Sample rate in Hz (e.g., 44100, 48000)
        :param bit_depth: Bit depth (16 or 24)
        """
        # Configuration sequence from es8156_configure() in badge BSP
        # Reference: https://github.com/Nicolai-Electronics/esp32-component-es8156/blob/main/es8156.c
        
        # REG 0x02 - Mode Config
        # Bit 2: SOFT_MODE_SEL = 1 (use MCLK from external source)
        self._write_register(ES8156_REG_MODE_CONFIG, 0x04, page=0)
        
        # REG 0x20 - Analog System 1
        # S6_SEL=2, S2_SEL=2, S3_SEL=2
        # (2 << 4) | (2 << 2) | 2 = 0x2A
        self._write_register(ES8156_REG_ANALOG_SYSTEM_1, 0x2A, page=0)
        
        # REG 0x21 - Analog System 2
        # VSEL = 0x1C, VREF_RMPDN1 = 1
        # 0x1C | (1 << 5) = 0x3C
        self._write_register(ES8156_REG_ANALOG_SYSTEM_2, 0x3C, page=0)
        
        # REG 0x22 - Analog System 3
        # All bits 0 (no mute, no HP switch)
        self._write_register(ES8156_REG_ANALOG_SYSTEM_3, 0x00, page=0)
        
        # REG 0x24 - Analog System 5
        # LPVREFBUF=1, LPHPCOM=1, LPDACVRP=1, LPDAC=0
        self._write_register(ES8156_REG_ANALOG_SYSTEM_5, 0x07, page=0)
        
        # REG 0x23 - Analog System 4
        # VROI=1
        self._write_register(ES8156_REG_ANALOG_SYSTEM_4, 0x04, page=0)
        
        # REG 0x0A - Time Control 1
        self._write_register(ES8156_REG_TIME_CONTROL_1, 0x01, page=0)
        
        # REG 0x0B - Time Control 2
        self._write_register(ES8156_REG_TIME_CONTROL_2, 0x01, page=0)
        
        # REG 0x11 - SDP Interface Config
        # I2S format, 16-bit word length
        # Protocol bits [1:0] = 00 (I2S)
        # Word length bits [6:4]: 011 = 16-bit
        if bit_depth == 24:
            sdp_config = 0x00  # 24-bit
        elif bit_depth == 32:
            sdp_config = 0x40  # 32-bit
        else:
            sdp_config = 0x30  # 16-bit (default)
        self._write_register(ES8156_REG_SDP_INTERFACE_CONFIG, sdp_config, page=0)
        
        # REG 0x0D - P2S Control
        # P2S_SDOUT_MUTEB=1, LRCK_1STCNT=1
        self._write_register(ES8156_REG_P2S_CONTROL, 0x14, page=0)
        
        # REG 0x18 - Misc Control 3
        # All 0 (no DAC RAM clear, no inversion)
        self._write_register(ES8156_REG_MISC_CONTROL_3, 0x00, page=0)
        
        # REG 0x08 - Clock Off (enable all clocks)
        # MCLK_ON=1, DAC_MCLK_ON=1, ANA_CLK_ON=1, EXT_SCLKLRCK_ON=1, MASTER_CLK_ON=1, P2S_CLK_ON=1
        self._write_register(ES8156_REG_CLOCK_OFF, 0x3F, page=0)
        
        # REG 0x00 - Reset Control
        # First: SEQ_DIS=1 (disable internal power sequence)
        self._write_register(ES8156_REG_RESET_CONTROL, 0x02, page=0)
        
        # Then: CSM_ON=1, SEQ_DIS=1 (enable chip state machine)
        self._write_register(ES8156_REG_RESET_CONTROL, 0x03, page=0)
        
        # REG 0x25 - Analog System 6 (Power Control)
        # VMIDSEL=2, everything else powered on (no PDN bits set)
        # (2 << 4) = 0x20
        self._write_register(ES8156_REG_ANALOG_SYSTEM_6, 0x20, page=0)
        
        # Set initial volume (100 is a reasonable default)
        self._write_register(ES8156_REG_VOLUME_CONTROL, 100, page=0)
        
        # Small delay to let settings take effect
        time.sleep(0.01)
    
    def set_mute(self, mute=True):
        """
        Set mute state using the mute control register.
        This is separate from volume control.
        
        :param mute: True to mute, False to unmute
        """
        # Mute control register (0x13 on page 0)
        # Bit 1: LCH_DSM_SMUTE - Left channel soft mute
        # Bit 2: RCH_DSM_SMUTE - Right channel soft mute
        mute_value = 0x06 if mute else 0x00
        self._write_register(ES8156_REG_MUTE_CONTROL, mute_value, page=0)
    
    def is_muted(self):
        """
        Check if audio is muted via mute control register.
        
        :return: True if muted, False if not muted
        """
        mute_reg = self._read_register(ES8156_REG_MUTE_CONTROL, page=0)
        return (mute_reg & 0x06) != 0
    
    def reset(self):
        """
        Reset the ES8156 codec.
        Based on es8156_reset() from badge BSP.
        """
        # Reset control: RST_DIG=1, RST_DAC_DIG=1, RST_MSTGEN=1
        self._write_register(ES8156_REG_RESET_CONTROL, 0x1C, page=0)
        time.sleep(0.01)  # 10ms delay
        # Enable chip state machine
        self._write_register(ES8156_REG_RESET_CONTROL, 0x01, page=0)
        # Reset the page tracking since reset may have changed state
        self._current_page = None
    
    def standby(self):
        """
        Put codec into standby mode (low power, no pop noise).
        Based on es8156_standby_nopop() from badge BSP.
        """
        # Set volume to 0
        self._write_register(ES8156_REG_VOLUME_CONTROL, 0, page=0)
        
        # EQ off, config write
        self._write_register(ES8156_REG_EQ_CONTROL_1, 0x02, page=0)
        
        # Power down analog with VMIDSEL=2
        # PDN_ANA=1, VMIDSEL=2 -> 0x80 | 0x20 = 0xA0
        self._write_register(ES8156_REG_ANALOG_SYSTEM_6, 0xA0, page=0)
        
        # DAC RAM clear
        self._write_register(ES8156_REG_MISC_CONTROL_3, 0x01, page=0)
        
        # Misc control 2: DLL on
        self._write_register(ES8156_REG_MISC_CONTROL_2, 0x02, page=0)
        
        # Misc control 2: PUPDN off
        self._write_register(ES8156_REG_MISC_CONTROL_2, 0x01, page=0)
        
        # Disable all clocks
        self._write_register(ES8156_REG_CLOCK_OFF, 0x00, page=0)
    
    def powerdown(self):
        """
        Fully power down the codec.
        Based on es8156_powerdown() from badge BSP.
        Note: This may cause a pop noise. Use standby() for quiet shutdown.
        """
        # Set volume to 0
        self._write_register(ES8156_REG_VOLUME_CONTROL, 0, page=0)
        
        # EQ off
        self._write_register(ES8156_REG_EQ_CONTROL_1, 0x02, page=0)
        
        # Analog mute
        self._write_register(ES8156_REG_ANALOG_SYSTEM_3, 0x01, page=0)
        
        # Power down analog (PDN_ANA=1, PDN_DAC=1)
        self._write_register(ES8156_REG_ANALOG_SYSTEM_6, 0x81, page=0)
        
        # DAC RAM clear
        self._write_register(ES8156_REG_MISC_CONTROL_3, 0x01, page=0)
        
        # Misc control 2
        self._write_register(ES8156_REG_MISC_CONTROL_2, 0x02, page=0)
        self._write_register(ES8156_REG_MISC_CONTROL_2, 0x01, page=0)
        
        # Disable all clocks
        self._write_register(ES8156_REG_CLOCK_OFF, 0x00, page=0)
        
        # Wait for power down
        time.sleep(0.5)
        
        # Full power down (all PDN bits set)
        self._write_register(ES8156_REG_ANALOG_SYSTEM_6, 0x87, page=0)
    
    def get_chip_status(self):
        """
        Get chip status register.
        Returns status byte with various status flags.
        """
        return self._read_register(ES8156_REG_CHIP_STATUS, page=0)
    
    def set_clock_mode(self, mode='auto'):
        """
        Set clock mode.
        
        :param mode: Clock mode string:
            - 'auto': Automatic clock detection
            - 'manual': Manual clock configuration
        """
        if mode == 'auto':
            self._write_register(ES8156_REG_MAIN_CLOCK_CONTROL, 0x00, page=0)
        elif mode == 'manual':
            self._write_register(ES8156_REG_MAIN_CLOCK_CONTROL, 0x01, page=0)
    
    def set_dac_mode(self, mode='normal'):
        """
        Set DAC mode.
        
        :param mode: Mode string:
            - 'normal': Normal DAC operation
            - 'mute': Mute mode
            - 'standby': Standby mode
        """
        mode_values = {
            'normal': 0x00,
            'mute': 0x01,
            'standby': 0x02,
        }
        if mode not in mode_values:
            raise ValueError(f"Invalid mode: {mode}. Must be one of {list(mode_values.keys())}")
        self._write_register(ES8156_REG_MODE_CONFIG, mode_values[mode], page=0)
    
    def read_register(self, register, page=0):
        """
        Read a raw register value.
        
        :param register: Register address (0x00-0xFF)
        :param page: Register page (0 or 1)
        :return: Register value as integer
        """
        return self._read_register(register, page)
    
    def write_register(self, register, value, page=0):
        """
        Write a raw register value.
        
        :param register: Register address (0x00-0xFF)
        :param value: Value to write (0-255)
        :param page: Register page (0 or 1)
        """
        self._write_register(register, value, page)
    
    def dump_registers(self):
        """Dump key registers for debugging."""
        print("ES8156 Register Dump:")
        print("-" * 50)
        regs = [
            (0x00, "RESET_CONTROL"),
            (0x01, "MAIN_CLOCK_CONTROL"),
            (0x02, "MODE_CONFIG"),
            (0x08, "CLOCK_OFF"),
            (0x09, "MISC_CONTROL_2"),
            (0x0C, "CHIP_STATUS"),
            (0x11, "SDP_INTERFACE_CONFIG"),
            (0x13, "MUTE_CONTROL"),
            (0x14, "VOLUME_CONTROL"),
            (0x20, "ANALOG_SYSTEM_1"),
            (0x21, "ANALOG_SYSTEM_2"),
            (0x22, "ANALOG_SYSTEM_3"),
            (0x23, "ANALOG_SYSTEM_4"),
            (0x24, "ANALOG_SYSTEM_5"),
            (0x25, "ANALOG_SYSTEM_6"),
            (0xFD, "CHIP_ID1"),
            (0xFE, "CHIP_ID0"),
            (0xFF, "CHIP_VERSION"),
        ]
        for addr, name in regs:
            try:
                val = self._read_register(addr, page=0)
                print(f"  0x{addr:02X} {name:22s} = 0x{val:02X} ({val:3d})")
            except Exception as e:
                print(f"  0x{addr:02X} {name:22s} = ERROR: {e}")
        print("-" * 50)


# USB HID Keycode mapping (HID Usage IDs for keyboard)
# Based on USB HID Usage Tables
HID_KEYCODE_MAP = {
    'esc': 0x29, 'f1': 0x3A, 'f2': 0x3B, 'f3': 0x3C, 'f4': 0x3D, 'f5': 0x3E, 'f6': 0x3F,
    'tilde': 0x35, '1': 0x1E, '2': 0x1F, '3': 0x20, '4': 0x21, '5': 0x22, '6': 0x23,
    '7': 0x24, '8': 0x25, '9': 0x26, '0': 0x27, 'minus': 0x2D, 'equals': 0x2E,
    'tab': 0x2B, 'q': 0x14, 'w': 0x1A, 'e': 0x08, 'r': 0x15, 't': 0x17, 'y': 0x1C,
    'u': 0x18, 'i': 0x0C, 'o': 0x12, 'p': 0x13, 'sqbracket_open': 0x2F, 'sqbracket_close': 0x30,
    'backslash': 0x31, 'a': 0x04, 's': 0x16, 'd': 0x07, 'f': 0x09, 'g': 0x0A, 'h': 0x0B,
    'j': 0x0D, 'k': 0x0E, 'l': 0x0F, 'semicolon': 0x33, 'quote': 0x34, 'return': 0x28,
    'shift_l': 0xE1, 'z': 0x1D, 'x': 0x1B, 'c': 0x06, 'v': 0x19, 'b': 0x05, 'n': 0x11,
    'm': 0x10, 'comma': 0x36, 'dot': 0x37, 'slash': 0x38, 'shift_r': 0xE5,
    'ctrl': 0xE0, 'meta': 0xE3, 'alt_l': 0xE2, 'alt_r': 0xE6, 'fn': None,  # FN key has no HID code
    'space_l': 0x2C, 'space_m': 0x2C, 'space_r': 0x2C,  # All map to space
    'backspace': 0x2A,
    'up': 0x52, 'down': 0x51, 'left': 0x50, 'right': 0x4F,
    'volume_up': None,  # Consumer control, not standard keyboard
}


def keyboard_to_hid_report(keyboard_state, previous_state=None):
    """
    Convert Tanmatsu KeyboardState to USB HID keyboard report.
    
    :param keyboard_state: KeyboardState object from Coprocessor.get_keyboard_state()
    :param previous_state: Optional previous KeyboardState for change detection
    :return: Tuple of (report_bytes, modifiers, changed)
        - report_bytes: 8-byte HID keyboard report (modifiers + reserved + 6 keycodes)
        - modifiers: Modifier byte (bit flags for Ctrl, Shift, Alt, etc.)
        - changed: True if state changed from previous_state
    
    USB HID Keyboard Report Format:
    Byte 0: Modifiers (bit flags: Ctrl=0x01, Shift=0x02, Alt=0x04, GUI/Meta=0x08)
    Byte 1: Reserved (always 0)
    Bytes 2-7: Keycodes (up to 6 simultaneous keys, 0x00 = no key)
    
    Example usage:
        import usb_hid
        usb_hid.enable((usb_hid.Device.KEYBOARD,))
        keyboard_device = usb_hid.devices[0]
        
        coprocessor = tanmatsu.Coprocessor(i2c)
        prev_state = None
        
        while True:
            state = coprocessor.get_keyboard_state()
            report, modifiers, changed = tanmatsu.keyboard_to_hid_report(state, prev_state)
            if changed:
                keyboard_device.send_report(report)
            prev_state = state
            time.sleep(0.01)
    """
    report = bytearray(8)
    report[1] = 0  # Reserved byte
    
    modifiers = 0
    keycodes = []
    
    # Map Tanmatsu keys to HID keycodes
    key_attr_map = {
        'esc': 'esc', 'f1': 'f1', 'f2': 'f2', 'f3': 'f3', 'f4': 'f4', 'f5': 'f5', 'f6': 'f6',
        'tilde': 'tilde', '1': '1', '2': '2', '3': '3', '4': '4', '5': '5', '6': '6',
        '7': '7', '8': '8', '9': '9', '0': '0', 'minus': 'minus', 'equals': 'equals',
        'tab': 'tab', 'q': 'q', 'w': 'w', 'e': 'e', 'r': 'r', 't': 't', 'y': 'y',
        'u': 'u', 'i': 'i', 'o': 'o', 'p': 'p', 'sqbracket_open': 'sqbracket_open',
        'sqbracket_close': 'sqbracket_close', 'backslash': 'backslash',
        'a': 'a', 's': 's', 'd': 'd', 'f': 'f', 'g': 'g', 'h': 'h',
        'j': 'j', 'k': 'k', 'l': 'l', 'semicolon': 'semicolon', 'quote': 'quote',
        'return': 'return', 'shift_l': 'shift_l', 'z': 'z', 'x': 'x', 'c': 'c',
        'v': 'v', 'b': 'b', 'n': 'n', 'm': 'm', 'comma': 'comma', 'dot': 'dot',
        'slash': 'slash', 'shift_r': 'shift_r', 'ctrl': 'ctrl', 'meta': 'meta',
        'alt_l': 'alt_l', 'alt_r': 'alt_r', 'space_l': 'space_l', 'space_m': 'space_m',
        'space_r': 'space_r', 'backspace': 'backspace',
        'up': 'up', 'down': 'down', 'left': 'left', 'right': 'right',
    }
    
    # Process modifier keys first
    if keyboard_state.key_ctrl:
        modifiers |= 0x01  # Left Control
    if keyboard_state.key_shift_l or keyboard_state.key_shift_r:
        modifiers |= 0x02  # Shift
    if keyboard_state.key_alt_l or keyboard_state.key_alt_r:
        modifiers |= 0x04  # Alt
    if keyboard_state.key_meta:
        modifiers |= 0x08  # GUI/Meta
    
    # Process regular keys
    for attr_name, key_name in key_attr_map.items():
        if getattr(keyboard_state, f'key_{attr_name}', False):
            hid_code = HID_KEYCODE_MAP.get(key_name)
            if hid_code is not None and hid_code not in keycodes:
                # Modifier keys are already handled, skip them in keycode list
                if hid_code not in (0xE0, 0xE1, 0xE2, 0xE3, 0xE5, 0xE6):  # Modifier codes
                    if len(keycodes) < 6:  # HID supports max 6 simultaneous keys
                        keycodes.append(hid_code)
    
    # Fill report
    report[0] = modifiers
    for i, keycode in enumerate(keycodes[:6]):
        report[2 + i] = keycode
    
    # Check if state changed
    changed = True
    if previous_state is not None:
        changed = (keyboard_state.raw != previous_state.raw)
    
    return bytes(report), modifiers, changed


# Keymap for converting Tanmatsu keyboard to ASCII characters
# Maps key attribute name -> (normal_char, shift_char)
TANMATSU_KEYMAP = {
    'esc': ('\x1b', '\x1b'),  # Escape
    'f1': ('\x1bOP', '\x1bOP'),  # F1
    'f2': ('\x1bOQ', '\x1bOQ'),  # F2
    'f3': ('\x1bOR', '\x1bOR'),  # F3
    'f4': ('\x1bOS', '\x1bOS'),  # F4
    'f5': ('\x1b[15~', '\x1b[15~'),  # F5
    'f6': ('\x1b[17~', '\x1b[17~'),  # F6
    'tilde': ('`', '~'),
    '1': ('1', '!'),
    '2': ('2', '@'),
    '3': ('3', '#'),
    '4': ('4', '$'),
    '5': ('5', '%'),
    '6': ('6', '^'),
    '7': ('7', '&'),
    '8': ('8', '*'),
    '9': ('9', '('),
    '0': ('0', ')'),
    'minus': ('-', '_'),
    'equals': ('=', '+'),
    'tab': ('\t', '\t'),
    'q': ('q', 'Q'),
    'w': ('w', 'W'),
    'e': ('e', 'E'),
    'r': ('r', 'R'),
    't': ('t', 'T'),
    'y': ('y', 'Y'),
    'u': ('u', 'U'),
    'i': ('i', 'I'),
    'o': ('o', 'O'),
    'p': ('p', 'P'),
    'sqbracket_open': ('[', '{'),
    'sqbracket_close': (']', '}'),
    'backslash': ('\\', '|'),
    'a': ('a', 'A'),
    's': ('s', 'S'),
    'd': ('d', 'D'),
    'f': ('f', 'F'),
    'g': ('g', 'G'),
    'h': ('h', 'H'),
    'j': ('j', 'J'),
    'k': ('k', 'K'),
    'l': ('l', 'L'),
    'semicolon': (';', ':'),
    'quote': ("'", '"'),
    'return': ('\r', '\r'),
    'z': ('z', 'Z'),
    'x': ('x', 'X'),
    'c': ('c', 'C'),
    'v': ('v', 'V'),
    'b': ('b', 'B'),
    'n': ('n', 'N'),
    'm': ('m', 'M'),
    'comma': (',', '<'),
    'dot': ('.', '>'),
    'slash': ('/', '?'),
    'space_l': (' ', ' '),
    'space_m': (' ', ' '),
    'space_r': (' ', ' '),
    'backspace': ('\x08', '\x08'),  # Backspace
    'up': ('\x1b[A', '\x1b[A'),  # Up arrow
    'down': ('\x1b[B', '\x1b[B'),  # Down arrow
    'left': ('\x1b[D', '\x1b[D'),  # Left arrow
    'right': ('\x1b[C', '\x1b[C'),  # Right arrow
}


def test_keyboard_detection(coprocessor):
    """
    Test function to debug keyboard detection.
    Prints pressed keys to help diagnose issues.
    
    :param coprocessor: Coprocessor instance
    """
    state = coprocessor.get_keyboard_state()
    pressed = state.get_pressed_keys()
    if pressed:
        print(f"Pressed keys: {pressed}")
        print(f"Raw bytes: {bytes(state.raw).hex()}")
    return state

# Module-level counter for tracking injected characters (for first-char duplication workaround)
_chars_injected = 0

# Global state for automatic polling
_auto_poll_coprocessor = None
_auto_poll_prev_state = None
_auto_poll_active = False

def _auto_poll_keyboard():
    """Background polling function for automatic keyboard input.
    This is called automatically by the C background callback system.
    """
    global _auto_poll_coprocessor, _auto_poll_prev_state, _auto_poll_active
    
    # Safety checks - return early if not ready
    if not _auto_poll_active:
        return
    if _auto_poll_coprocessor is None:
        return
    
    # Try to access I2C - if it fails, disable polling silently
    try:
        changed, state = inject_keyboard_to_stdin(_auto_poll_coprocessor, _auto_poll_prev_state, debug=False)
        _auto_poll_prev_state = state
    except (AttributeError, RuntimeError, OSError, TypeError):
        # I2C errors, missing attributes, or type errors - disable polling
        _auto_poll_active = False
    except Exception:
        # Any other exception - disable polling to prevent crash loop
        # Don't print exceptions here as it can cause crashes in background callbacks
        _auto_poll_active = False

def start_auto_keyboard(coprocessor=None, i2c=None):
    """
    Start automatic keyboard polling that injects keys into sys.stdin.
    This works automatically in the background - no manual polling needed!
    
    Note: Characters are buffered and will appear when the REPL becomes idle
    (this is how CircuitPython's REPL works - it only reads stdin when idle).
    
    :param coprocessor: Optional Coprocessor instance (if None, creates one)
    :param i2c: I2C bus object (if None, uses board.I2C())
    :return: The coprocessor instance
    
    Example:
        import tanmatsu
        import tanmatsu_keyboard
        
        tanmatsu_keyboard.attach_serial()
        coprocessor = tanmatsu.start_auto_keyboard()
        # Now keyboard input works automatically!
        # Type in REPL - characters will appear when REPL is idle
    """
    import tanmatsu_keyboard
    
    global _auto_poll_coprocessor, _auto_poll_prev_state, _auto_poll_active
    
    tanmatsu_keyboard.attach_serial()
    
    if coprocessor is None:
        if i2c is None:
            import board
            i2c = board.I2C()
        coprocessor = Coprocessor(i2c)
    
    _auto_poll_coprocessor = coprocessor
    _auto_poll_prev_state = None
    _auto_poll_active = True
    
    # Reset character counter
    global _chars_injected
    _chars_injected = 0
    
    # Test that coprocessor works before setting up polling
    # This ensures I2C is ready and prevents crashes during boot
    try:
        # Do a test read to make sure I2C is working
        test_state = coprocessor.get_keyboard_state()
        _auto_poll_prev_state = test_state
    except Exception:
        # If initial read fails, don't set up polling
        # This prevents crashes during boot when I2C might not be ready
        _auto_poll_active = False
        return coprocessor
    
    # Add a small delay to ensure everything is initialized
    # This is especially important when running from code.py during boot
    import time
    time.sleep(0.1)  # 100ms delay
    
    # Set up automatic polling via background callbacks
    # Use polling mode since interrupts aren't working reliably
    # Pass True as second argument to enable polling mode
    try:
        tanmatsu_keyboard.set_poll_callback(_auto_poll_keyboard, True)
    except Exception:
        # If setting callback fails, disable polling
        _auto_poll_active = False
    
    return coprocessor

def poll_keyboard():
    """
    Poll the keyboard once and inject any pressed keys into sys.stdin.
    Call this periodically (e.g., every 10ms) for automatic keyboard input.
    
    Example:
        while True:
            tanmatsu.poll_keyboard()
            import time
            time.sleep(0.01)
    """
    global _auto_poll_coprocessor, _auto_poll_prev_state, _auto_poll_active
    
    if not _auto_poll_active or _auto_poll_coprocessor is None:
        return
    
    try:
        changed, state = inject_keyboard_to_stdin(_auto_poll_coprocessor, _auto_poll_prev_state, debug=False)
        _auto_poll_prev_state = state
    except Exception:
        # If polling fails, disable auto-poll
        _auto_poll_active = False

def stop_auto_keyboard():
    """Stop automatic keyboard polling."""
    global _auto_poll_active
    _auto_poll_active = False
    import tanmatsu_keyboard
    tanmatsu_keyboard.set_poll_callback(None)


def inject_keyboard_to_stdin(coprocessor, previous_state=None, debug=False):
    """
    Poll Tanmatsu keyboard and inject pressed keys into sys.stdin.
    
    This function reads the current keyboard state, converts pressed keys to
    ASCII characters, and injects them into the serial input stream (sys.stdin).
    
    :param coprocessor: Coprocessor instance
    :param previous_state: Optional KeyboardState for change detection
    :param debug: If True, print debug information about injected characters
    :return: Tuple of (changed, new_state) where changed is True if keys changed
    
    Example usage:
        import tanmatsu
        import tanmatsu_keyboard
        import board
        import time
        
        # Enable keyboard input injection
        tanmatsu_keyboard.attach_serial()
        
        i2c = board.I2C()
        coprocessor, audio = tanmatsu.init_tanmatsu(i2c)
        prev_state = None
        
        while True:
            changed, state = tanmatsu.inject_keyboard_to_stdin(coprocessor, prev_state)
            prev_state = state
            time.sleep(0.01)  # Poll at ~100Hz
    """
    try:
        import tanmatsu_keyboard
    except ImportError:
        raise RuntimeError("tanmatsu_keyboard module not available. Make sure you're running on Tanmatsu hardware.")
    
    new_state = coprocessor.get_keyboard_state()
    
    # Check if state changed
    changed = (previous_state is None or new_state.raw != previous_state.raw)
    
    if debug:
        pressed = new_state.get_pressed_keys()
        if pressed:
            print(f"State: pressed={pressed}, changed={changed}, prev_state={previous_state is not None}")
    
    if not changed:
        return False, new_state
    
    # Determine if shift is pressed
    shift_pressed = new_state.key_shift_l or new_state.key_shift_r
    ctrl_pressed = new_state.key_ctrl
    alt_pressed = new_state.key_alt_l or new_state.key_alt_r
    
    # Find newly pressed keys (keys that are pressed now but weren't before)
    # Iterate over known keys in the keymap for efficiency and reliability
    for key_name in TANMATSU_KEYMAP.keys():
        attr_name = f'key_{key_name}'
        if not hasattr(new_state, attr_name):
            continue
        
        is_pressed_now = getattr(new_state, attr_name)
        # Compare against previous state - if None, treat as not pressed before
        was_pressed_before = getattr(previous_state, attr_name) if previous_state is not None else False
        
        # Only process keys that are newly pressed (not held)
        if is_pressed_now and not was_pressed_before:
            # Handle special modifier combinations
            if ctrl_pressed and key_name == 'c':
                # Ctrl+C - send interrupt character
                tanmatsu_keyboard.write_char(3)  # Ctrl+C
                continue
            
            if key_name not in TANMATSU_KEYMAP:
                # Key not in keymap - skip it
                continue
            
            normal_char, shift_char = TANMATSU_KEYMAP[key_name]
            char_to_send = shift_char if shift_pressed else normal_char
            
            # Handle Ctrl modifier (convert to control character)
            if ctrl_pressed and len(char_to_send) == 1 and 'a' <= char_to_send <= 'z':
                # Convert letter to control character (Ctrl+A = 1, Ctrl+B = 2, etc.)
                char_to_send = chr(ord(char_to_send) - ord('a') + 1)
            
            # Handle Alt modifier (prepend escape)
            if alt_pressed and len(char_to_send) == 1:
                tanmatsu_keyboard.write_char(0x1b)  # Escape
            
            # Debug: print what we're sending
            if debug:
                print(f"Injecting: {repr(char_to_send)} for key {key_name} (prev_state={previous_state is not None})")
            
            # Send the character(s)
            # Workaround: Duplicate the first character injected to protect against loss
            # This handles the case where REPL initialization consumes the first char
            global _chars_injected
            
            # Always inject the character
            tanmatsu_keyboard.write_string(char_to_send)
            _chars_injected += 1
            
            # If this is the very first character, duplicate it
            if _chars_injected == 1:
                tanmatsu_keyboard.write_string(char_to_send)
            # Only inject first key per call to avoid multiple injections
            break
    
    return True, new_state




# Convenience function to create both interfaces
def init_tanmatsu(i2c=None, interrupt_pin=None):
    """
    Initialize Tanmatsu interfaces.
    
    :param i2c: I2C bus object (if None, uses board.I2C())
    :param interrupt_pin: Optional interrupt pin for coprocessor
    :return: Tuple of (coprocessor, audio_codec) objects
    """
    if i2c is None:
        import board
        i2c = board.I2C()
    
    coprocessor = Coprocessor(i2c, interrupt_pin)
    audio_codec = ES8156(i2c)
    
    return coprocessor, audio_codec


def get_i2s_out():
    """
    Get an I2SOut object configured for Tanmatsu's ES8156 codec.
    
    This returns a CircuitPython I2SOut object that can be used with
    audiocore (WAV, MP3, etc.) for audio playback.
    
    :return: audiobusio.I2SOut object configured for Tanmatsu I2S pins
    
    Example:
        import tanmatsu
        import audiocore
        import board
        
        i2c = board.I2C()
        coprocessor, audio = tanmatsu.init_tanmatsu(i2c)
        
        # Enable amplifier
        coprocessor.enable_amplifier(True)
        
        # Configure codec for I2S
        audio.configure_for_i2s()
        
        # Set volume
        audio.set_volume_percent(50)
        
        # Get I2S output object
        i2s = tanmatsu.get_i2s_out()
        
        # Play a WAV file
        with open("sound.wav", "rb") as wav_file:
            wave = audiocore.WaveFile(wav_file)
            i2s.play(wave)
            while i2s.playing:
                pass
        
        # Play MP3 (if supported)
        # with open("music.mp3", "rb") as mp3_file:
        #     mp3 = audiocore.MP3Decoder(mp3_file)
        #     i2s.play(mp3, loop=True)
        #     # ... control playback as needed
        
        i2s.deinit()
    """
    try:
        import audiobusio
        import board
    except ImportError:
        try:
            # Fallback to audioio if audiobusio not available
            import audioio
            import board
            i2s_module = audioio
        except ImportError:
            raise RuntimeError(
                "Audio libraries not available. "
                "Make sure CIRCUITPY_AUDIOBUSIO is enabled in mpconfigboard.mk. "
                "Add: CIRCUITPY_AUDIOBUSIO = 1"
            )
    else:
        i2s_module = audiobusio
    
    # Try to find I2S pins - Tanmatsu uses I2S_BCLK, I2S_WS, I2S_DOUT
    try:
        # Tanmatsu board has I2S pins defined
        if hasattr(board, 'I2S_BCLK') and hasattr(board, 'I2S_WS') and hasattr(board, 'I2S_DOUT'):
            i2s = i2s_module.I2SOut(
                board.I2S_BCLK,      # Bit clock
                board.I2S_WS,        # Word select (LR clock)
                board.I2S_DOUT       # Data output
            )
        elif hasattr(board, 'I2S_BIT_CLOCK') and hasattr(board, 'I2S_WORD_SELECT') and hasattr(board, 'I2S_DATA'):
            # Fallback to common I2S pin names
            i2s = i2s_module.I2SOut(
                board.I2S_BIT_CLOCK,
                board.I2S_WORD_SELECT,
                board.I2S_DATA
            )
        else:
            # If I2S pins aren't defined, raise error
            raise AttributeError("I2S pins not defined in board module. Tanmatsu needs I2S_BCLK, I2S_WS, I2S_DOUT")
    except (AttributeError, ValueError) as e:
        raise RuntimeError(
            f"I2S not configured on this board. "
            f"Tanmatsu board should have I2S_BCLK, I2S_WS, and I2S_DOUT pins defined. "
            f"Error: {e}"
        )
    
    return i2s


# Example usage code (commented out - uncomment to use)
"""
Example: Using Tanmatsu ES8156 codec with standard CircuitPython audio APIs

import board
import tanmatsu
import audiocore
import math
import time

# Initialize I2C and Tanmatsu interfaces
i2c = board.I2C()
coprocessor, audio = tanmatsu.init_tanmatsu(i2c)

# Enable the audio amplifier
coprocessor.enable_amplifier(True)

# Configure the codec for I2S audio
audio.configure_for_i2s(sample_rate=44100, bit_depth=16)

# Set volume to 50%
audio.set_volume_percent(50)

# Get I2S output object
i2s = tanmatsu.get_i2s_out()

# Example 1: Play a tone using audiocore.RawSample
frequency = 440  # A4 note
sample_rate = 44100
duration = 1.0
samples_per_cycle = int(sample_rate / frequency)

# Generate one cycle of sine wave
cycle_buffer = bytearray(samples_per_cycle * 2)  # 16-bit samples
for i in range(samples_per_cycle):
    sample = math.sin(2 * math.pi * i / samples_per_cycle)
    sample_int = int(sample * 32767 * 0.5)  # 50% volume
    cycle_buffer[i * 2] = sample_int & 0xFF
    cycle_buffer[i * 2 + 1] = (sample_int >> 8) & 0xFF

# Create RawSample and play
wave = audiocore.RawSample(cycle_buffer, sample_rate=sample_rate, channel_count=1)
i2s.play(wave, loop=True)
time.sleep(duration)
i2s.stop()

# Example 2: Play a WAV file using audiocore.WaveFile
try:
    with open("sound.wav", "rb") as wav_file:
        wave = audiocore.WaveFile(wav_file)
        i2s.play(wave)
        while i2s.playing:
            pass
except FileNotFoundError:
    print("WAV file not found. Make sure 'sound.wav' is in your CIRCUITPY drive.")

# Example 3: Play MP3 (if supported)
# try:
#     with open("music.mp3", "rb") as mp3_file:
#         mp3 = audiocore.MP3Decoder(mp3_file)
#         i2s.play(mp3, loop=True)
#         # ... control playback as needed
#         # i2s.stop()  # Stop when done
# except FileNotFoundError:
#     print("MP3 file not found.")

# Clean up
i2s.deinit()

# When finished, you can disable the amplifier to save power
# coprocessor.disable_amplifier()
"""


def example_audio_setup():
    """
    Complete example showing how to set up audio with the Tanmatsu ES8156 codec.
    
    This example demonstrates:
    1. Initializing the Tanmatsu interfaces
    2. Enabling the audio amplifier
    3. Configuring the ES8156 codec for I2S
    4. Setting volume
    5. Getting an I2S output object for use with audiocore
    
    Run this example:
        import tanmatsu
        coprocessor, audio, i2s = tanmatsu.example_audio_setup()
        
    Then use i2s with standard CircuitPython audio APIs:
        import audiocore
        with open("sound.wav", "rb") as f:
            wave = audiocore.WaveFile(f)
            i2s.play(wave)
            while i2s.playing:
                pass
    """
    import board
    
    print("Initializing Tanmatsu...")
    i2c = board.I2C()
    coprocessor, audio = init_tanmatsu(i2c)
    
    print("Enabling amplifier...")
    coprocessor.enable_amplifier(True)
    
    print("Configuring ES8156 codec for I2S...")
    audio.configure_for_i2s(sample_rate=44100, bit_depth=16)
    
    print("Setting volume to 50%...")
    audio.set_volume_percent(50)
    
    print("Getting I2S output object...")
    i2s = get_i2s_out()
    
    print("Audio setup complete! Use i2s with audiocore for playback.")
    return coprocessor, audio, i2s
