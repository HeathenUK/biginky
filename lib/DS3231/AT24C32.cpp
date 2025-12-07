/**
 * @file AT24C32.cpp
 * @brief AT24C32 I2C EEPROM driver implementation
 */

#include "AT24C32.h"

AT24C32 eeprom;

AT24C32::AT24C32() : _wire(nullptr), _addr(0), _present(false) {
}

bool AT24C32::begin(TwoWire* wire, uint8_t addr) {
    _wire = wire;
    _addr = addr;
    _present = false;
    
    // Check if EEPROM is present
    _wire->beginTransmission(_addr);
    uint8_t error = _wire->endTransmission();
    
    if (error == 0) {
        _present = true;
        Serial.printf("AT24C32: Found at 0x%02X (%d bytes)\n", _addr, AT24C32_SIZE);
        
        // Check if formatted
        if (!isFormatted()) {
            Serial.println("AT24C32: Not formatted, initializing...");
            format();
        } else {
            Serial.println("AT24C32: Already formatted");
        }
    } else {
        Serial.printf("AT24C32: Not found at 0x%02X\n", _addr);
    }
    
    return _present;
}

void AT24C32::waitForWrite() {
    // EEPROM needs up to 10ms to complete a write
    // Poll until it responds
    uint32_t start = millis();
    int attempts = 0;
    while (millis() - start < 50) {  // Increased timeout
        attempts++;
        _wire->beginTransmission(_addr);
        uint8_t err = _wire->endTransmission();
        if (err == 0) {
            // Add extra delay after write completes for reliability
            delay(5);
            
            // Do a dummy read to verify bus is working
            // This also ensures the EEPROM's address pointer is reset
            _wire->beginTransmission(_addr);
            _wire->write((uint8_t)0);  // Address high byte
            _wire->write((uint8_t)0);  // Address low byte  
            _wire->endTransmission();
            _wire->requestFrom(_addr, (uint8_t)1);
            while (_wire->available()) _wire->read();  // Discard
            
            return;  // Write complete
        }
        delay(1);
    }
    Serial.printf("  [waitForWrite] TIMEOUT after %d attempts!\n", attempts);
}

uint8_t AT24C32::readByte(uint16_t addr) {
    // Debug: print device address to check for corruption
    if (addr == EEPROM_WIFI_SSID) {
        Serial.printf("  [readByte] I2C dev=0x%02X, mem=0x%04X, wire=%p\n", 
                      _addr, addr, (void*)_wire);
    }
    
    _wire->beginTransmission(_addr);
    _wire->write((uint8_t)(addr >> 8));    // High byte
    _wire->write((uint8_t)(addr & 0xFF));  // Low byte
    uint8_t err = _wire->endTransmission();
    
    if (err != 0) {
        Serial.printf("  [readByte] I2C error %d at dev 0x%02X addr 0x%04X\n", err, _addr, addr);
        return 0xFF;
    }
    
    uint8_t received = _wire->requestFrom(_addr, (uint8_t)1);
    if (received != 1) {
        Serial.printf("  [readByte] requestFrom(0x%02X) returned %d (expected 1)\n", _addr, received);
        return 0xFF;
    }
    
    if (_wire->available()) {
        return _wire->read();
    }
    Serial.println("  [readByte] no data available after requestFrom");
    return 0xFF;
}

void AT24C32::writeByte(uint16_t addr, uint8_t value) {
    _wire->beginTransmission(_addr);
    _wire->write((uint8_t)(addr >> 8));
    _wire->write((uint8_t)(addr & 0xFF));
    _wire->write(value);
    _wire->endTransmission();
    waitForWrite();
}

void AT24C32::readBytes(uint16_t addr, uint8_t* buffer, uint16_t len) {
    // Read in chunks to avoid I2C buffer limits
    while (len > 0) {
        uint8_t chunk = (len > 30) ? 30 : len;
        
        _wire->beginTransmission(_addr);
        _wire->write((uint8_t)(addr >> 8));
        _wire->write((uint8_t)(addr & 0xFF));
        _wire->endTransmission();
        
        _wire->requestFrom(_addr, chunk);
        for (uint8_t i = 0; i < chunk && _wire->available(); i++) {
            *buffer++ = _wire->read();
        }
        
        addr += chunk;
        len -= chunk;
    }
}

void AT24C32::writeBytes(uint16_t addr, const uint8_t* buffer, uint16_t len) {
    // Write in pages (32 bytes max, must not cross page boundary)
    while (len > 0) {
        // Calculate bytes until end of current page
        uint8_t pageOffset = addr % AT24C32_PAGE_SIZE;
        uint8_t pageRemaining = AT24C32_PAGE_SIZE - pageOffset;
        uint8_t chunk = (len > pageRemaining) ? pageRemaining : len;
        
        _wire->beginTransmission(_addr);
        _wire->write((uint8_t)(addr >> 8));
        _wire->write((uint8_t)(addr & 0xFF));
        for (uint8_t i = 0; i < chunk; i++) {
            _wire->write(*buffer++);
        }
        _wire->endTransmission();
        waitForWrite();
        
        addr += chunk;
        len -= chunk;
    }
}

uint32_t AT24C32::readUInt32(uint16_t addr) {
    uint8_t data[4];
    readBytes(addr, data, 4);
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | 
           ((uint32_t)data[2] << 8) | data[3];
}

void AT24C32::writeUInt32(uint16_t addr, uint32_t value) {
    uint8_t data[4] = {
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)(value)
    };
    writeBytes(addr, data, 4);
}

uint16_t AT24C32::readUInt16(uint16_t addr) {
    uint8_t data[2];
    readBytes(addr, data, 2);
    return ((uint16_t)data[0] << 8) | data[1];
}

void AT24C32::writeUInt16(uint16_t addr, uint16_t value) {
    uint8_t data[2] = { (uint8_t)(value >> 8), (uint8_t)(value) };
    writeBytes(addr, data, 2);
}

void AT24C32::readString(uint16_t addr, char* buffer, uint16_t maxLen) {
    readBytes(addr, (uint8_t*)buffer, maxLen - 1);
    buffer[maxLen - 1] = '\0';
    // Find actual null terminator
    for (uint16_t i = 0; i < maxLen - 1; i++) {
        if (buffer[i] == '\0' || buffer[i] == 0xFF) {
            buffer[i] = '\0';
            break;
        }
    }
}

void AT24C32::writeString(uint16_t addr, const char* str, uint16_t maxLen) {
    uint16_t len = strlen(str);
    if (len >= maxLen) len = maxLen - 1;
    writeBytes(addr, (const uint8_t*)str, len + 1);  // Include null terminator
}

bool AT24C32::isFormatted() {
    return readUInt32(EEPROM_MAGIC_ADDR) == EEPROM_MAGIC_VALUE;
}

void AT24C32::format() {
    // Write magic number
    writeUInt32(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
    writeByte(EEPROM_VERSION_ADDR, 1);
    
    // Initialize counters
    writeUInt32(EEPROM_BOOT_COUNT, 0);
    writeUInt32(EEPROM_TOTAL_UPTIME, 0);
    writeUInt32(EEPROM_LAST_NTP_SYNC, 0);
    
    // Default sleep: 10 seconds
    writeUInt16(EEPROM_SLEEP_SEC, 10);
    
    // Clear WiFi (write 0xFF to indicate empty)
    writeByte(EEPROM_WIFI_SSID, 0xFF);
    writeByte(EEPROM_WIFI_PSK, 0xFF);
    
    Serial.println("AT24C32: Formatted");
}

uint32_t AT24C32::getBootCount() {
    return readUInt32(EEPROM_BOOT_COUNT);
}

void AT24C32::incrementBootCount() {
    Serial.println("  [incrementBootCount] starting...");
    uint32_t count = getBootCount();
    Serial.printf("  [incrementBootCount] current count=%lu, writing %lu\n", count, count + 1);
    writeUInt32(EEPROM_BOOT_COUNT, count + 1);
    Serial.println("  [incrementBootCount] done");
}

uint32_t AT24C32::getTotalUptime() {
    return readUInt32(EEPROM_TOTAL_UPTIME);
}

void AT24C32::addUptime(uint32_t seconds) {
    uint32_t total = getTotalUptime();
    writeUInt32(EEPROM_TOTAL_UPTIME, total + seconds);
}

uint32_t AT24C32::getLastNtpSync() {
    return readUInt32(EEPROM_LAST_NTP_SYNC);
}

void AT24C32::setLastNtpSync(uint32_t unixTime) {
    writeUInt32(EEPROM_LAST_NTP_SYNC, unixTime);
}

bool AT24C32::hasWifiCredentials() {
    // Try reading up to 3 times in case of I2C bus issues
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            delay(10);  // Wait before retry
            Serial.printf("  [hasWifiCredentials] retry %d...\n", attempt);
        }
        
        uint8_t first = readByte(EEPROM_WIFI_SSID);
        Serial.printf("  [hasWifiCredentials] first byte at 0x%04X = 0x%02X ('%c')\n", 
                      EEPROM_WIFI_SSID, first, (first >= 32 && first < 127) ? first : '?');
        
        // If we got a valid result (not 0xFF indicating I2C error), return it
        if (first != 0xFF) {
            return (first != 0x00);  // 0x00 means empty, anything else means credentials exist
        }
        
        // 0xFF could mean empty OR I2C error - try again
    }
    
    // After 3 attempts of 0xFF, assume no credentials
    return false;
}

bool AT24C32::getWifiCredentials(char* ssid, size_t ssidLen, char* psk, size_t pskLen) {
    if (!hasWifiCredentials()) {
        return false;
    }
    readString(EEPROM_WIFI_SSID, ssid, ssidLen);
    readString(EEPROM_WIFI_PSK, psk, pskLen);
    return true;
}

void AT24C32::setWifiCredentials(const char* ssid, const char* psk) {
    writeString(EEPROM_WIFI_SSID, ssid, 33);
    writeString(EEPROM_WIFI_PSK, psk, 65);
    Serial.printf("AT24C32: Saved WiFi credentials for '%s'\n", ssid);
}

uint16_t AT24C32::getSleepSeconds() {
    uint16_t val = readUInt16(EEPROM_SLEEP_SEC);
    if (val == 0 || val == 0xFFFF) return 10;  // Default
    return val;
}

void AT24C32::setSleepSeconds(uint16_t seconds) {
    writeUInt16(EEPROM_SLEEP_SEC, seconds);
}

bool AT24C32::hasOpenAIKey() {
    uint8_t first = readByte(EEPROM_OPENAI_KEY);
    // Valid API key starts with 's' (from "sk-...")
    return (first == 's');
}

bool AT24C32::getOpenAIKey(char* key, size_t keyLen) {
    if (!hasOpenAIKey()) {
        return false;
    }
    readString(EEPROM_OPENAI_KEY, key, keyLen);
    return true;
}

void AT24C32::setOpenAIKey(const char* key) {
    writeString(EEPROM_OPENAI_KEY, key, 200);
    Serial.printf("AT24C32: Saved OpenAI API key (%d chars)\n", strlen(key));
}

bool AT24C32::hasGetimgKey() {
    uint8_t first = readByte(EEPROM_GETIMG_KEY);
    // Valid API key starts with 'k' (from "key-...")
    return (first == 'k');
}

bool AT24C32::getGetimgKey(char* key, size_t keyLen) {
    if (!hasGetimgKey()) {
        return false;
    }
    readString(EEPROM_GETIMG_KEY, key, keyLen);
    return true;
}

void AT24C32::setGetimgKey(const char* key) {
    writeString(EEPROM_GETIMG_KEY, key, 200);
    Serial.printf("AT24C32: Saved getimg.ai API key (%d chars)\n", strlen(key));
}

bool AT24C32::hasModelsLabKey() {
    // ModelsLab keys are typically alphanumeric strings
    uint8_t first = readByte(EEPROM_MODELSLAB_KEY);
    // Valid if it's an alphanumeric character (not 0x00 or 0xFF)
    return (first >= '0' && first <= '9') || 
           (first >= 'A' && first <= 'Z') || 
           (first >= 'a' && first <= 'z');
}

bool AT24C32::getModelsLabKey(char* key, size_t keyLen) {
    if (!hasModelsLabKey()) {
        return false;
    }
    readString(EEPROM_MODELSLAB_KEY, key, keyLen);
    return true;
}

void AT24C32::setModelsLabKey(const char* key) {
    writeString(EEPROM_MODELSLAB_KEY, key, 200);
    Serial.printf("AT24C32: Saved ModelsLab API key (%d chars)\n", strlen(key));
}

void AT24C32::logTemperature(float temp) {
    Serial.printf("  [logTemperature] temp=%.2f\n", temp);
    
    // Simple circular buffer of temperatures
    // First 2 bytes at TEMP_LOG_START = write index
    uint16_t maxEntries = (EEPROM_TEMP_LOG_SIZE - 2) / 2;  // 2 bytes per temp
    uint16_t index = readUInt16(EEPROM_TEMP_LOG_START);
    
    Serial.printf("  [logTemperature] index=%u, maxEntries=%u\n", index, maxEntries);
    
    if (index >= maxEntries) index = 0;
    
    // Store as fixed-point (temp * 4, gives 0.25 degree resolution)
    int16_t tempFixed = (int16_t)(temp * 4);
    uint16_t addr = EEPROM_TEMP_LOG_START + 2 + (index * 2);
    Serial.printf("  [logTemperature] writing to addr 0x%04X\n", addr);
    writeUInt16(addr, (uint16_t)tempFixed);
    
    // Update index
    Serial.printf("  [logTemperature] updating index to %u\n", index + 1);
    writeUInt16(EEPROM_TEMP_LOG_START, index + 1);
    Serial.println("  [logTemperature] done");
}

uint16_t AT24C32::getTemperatureLogCount() {
    uint16_t maxEntries = (EEPROM_TEMP_LOG_SIZE - 2) / 2;
    uint16_t index = readUInt16(EEPROM_TEMP_LOG_START);
    return (index > maxEntries) ? maxEntries : index;
}

float AT24C32::getLoggedTemperature(uint16_t index) {
    uint16_t maxEntries = (EEPROM_TEMP_LOG_SIZE - 2) / 2;
    if (index >= maxEntries || index >= getTemperatureLogCount()) {
        return 0.0f;  // Invalid index
    }
    uint16_t addr = EEPROM_TEMP_LOG_START + 2 + (index * 2);
    int16_t tempFixed = (int16_t)readUInt16(addr);
    return tempFixed / 4.0f;
}

void AT24C32::printStatus() {
    if (!_present) {
        Serial.println("AT24C32: Not present");
        return;
    }
    
    Serial.println("=== AT24C32 EEPROM Status ===");
    Serial.printf("  Address: 0x%02X\n", _addr);
    Serial.printf("  Size: %d bytes\n", AT24C32_SIZE);
    Serial.printf("  Formatted: %s\n", isFormatted() ? "yes" : "no");
    Serial.printf("  Boot count: %lu\n", getBootCount());
    Serial.printf("  Total uptime: %lu seconds\n", getTotalUptime());
    Serial.printf("  Last NTP sync: %lu\n", getLastNtpSync());
    Serial.printf("  Sleep duration: %u seconds\n", getSleepSeconds());
    Serial.printf("  Has WiFi creds: %s\n", hasWifiCredentials() ? "yes" : "no");
    Serial.printf("  Temp log entries: %u\n", getTemperatureLogCount());
    Serial.println("=============================");
}
