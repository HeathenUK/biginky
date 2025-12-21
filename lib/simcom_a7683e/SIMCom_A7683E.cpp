/**
 * @file SIMCom_A7683E.cpp
 * @brief SIMCom A7683E 4G LTE module driver implementation
 * 
 * Based on patterns from LilyGo Modem Series examples.
 * AT Command Reference: A76XX Series AT Command Manual V1.04
 */

#include "SIMCom_A7683E.h"
#include <stdlib.h>

// Power-on pulse width for A7683E (similar to A7670)
#define MODEM_POWERON_PULSE_WIDTH_MS  100

SIMCom_A7683E::SIMCom_A7683E(const char* apn, 
                              HardwareSerial* serial,
                              int reset_pin,
                              int netlight_pin,
                              int pwrkey_pin,
                              bool skip_reset,
                              const char* apn_username,
                              const char* apn_password,
                              int auth_type)
    : _apn(apn)
    , _serial(serial)
    , _reset_pin(reset_pin)
    , _netlight_pin(netlight_pin)
    , _pwrkey_pin(pwrkey_pin)
    , _connected(false)
    , _ip_address("")
    , _apn_username(nullptr)
    , _apn_password(nullptr)
    , _auth_type(auth_type)
{
    // Use Serial1 as default if no serial specified
    if (_serial == nullptr) {
        _serial = &Serial1;
    }
    
    // Allocate and copy username/password if provided
    if (apn_username != nullptr && strlen(apn_username) > 0) {
        _apn_username = (char*)malloc(strlen(apn_username) + 1);
        if (_apn_username != nullptr) {
            strcpy(_apn_username, apn_username);
        }
    }
    
    if (apn_password != nullptr && strlen(apn_password) > 0) {
        _apn_password = (char*)malloc(strlen(apn_password) + 1);
        if (_apn_password != nullptr) {
            strcpy(_apn_password, apn_password);
        }
    }
}

SIMCom_A7683E::~SIMCom_A7683E() {
    if (_apn_username != nullptr) {
        free(_apn_username);
    }
    if (_apn_password != nullptr) {
        free(_apn_password);
    }
}

void SIMCom_A7683E::powerOn() {
    if (_pwrkey_pin < 0) {
        return;
    }
    
    pinMode(_pwrkey_pin, OUTPUT);
    // PWRKEY is active LOW, but idle state is HIGH (pulled up internally to VBAT via 50KΩ)
    // Power on sequence per A7682E manual: Pull LOW for 50ms (min), then return to HIGH
    // PWRKEY should NEVER be left LOW - always return to HIGH after pulse
    digitalWrite(_pwrkey_pin, LOW);   // Assert power-on (active LOW)
    delay(100);  // 50ms minimum per spec, use 100ms for safety
    digitalWrite(_pwrkey_pin, HIGH);  // Release - MUST return to HIGH (idle state)
    // Note: Module needs time to boot after PWRKEY release, but we don't wait here
    // The caller should wait after calling powerOn()
}

void SIMCom_A7683E::reset() {
    if (_reset_pin < 0) {
        return;
    }
    
    pinMode(_reset_pin, OUTPUT);
    // RESET is active LOW, but normal state is HIGH (pulled up internally)
    // Reset sequence: Pull LOW for 2-2.5s, then return to HIGH
    digitalWrite(_reset_pin, LOW);   // Assert reset (active LOW)
    delay(2500);  // 2-2.5s minimum per spec (use 2.5s typical)
    digitalWrite(_reset_pin, HIGH);  // Release reset - return to normal (HIGH)
    // Note: Module needs time to boot after reset release, but we don't wait here
    // The caller should wait after calling reset()
}

void SIMCom_A7683E::flushUART() {
    if (_serial == nullptr) return;
    while (_serial->available()) {
        _serial->read();
    }
}

bool SIMCom_A7683E::begin(int rx_pin, int tx_pin, bool skip_hardware_reset) {
    // Reset connected state - module is being reinitialized
    _connected = false;
    
    // Configure RESET pin (active LOW, but normal/idle state is HIGH)
    // RESET has internal 50KΩ pull-up to VBAT, default HIGH
    // RESET should NEVER be left LOW - always HIGH for normal operation
    if (_reset_pin >= 0) {
        pinMode(_reset_pin, OUTPUT);
        digitalWrite(_reset_pin, HIGH);  // Normal state: HIGH (reset released)
    }
    
    // Configure PWRKEY pin (active LOW, but idle state is HIGH)
    // PWRKEY has internal 50KΩ pull-up to VBAT, default HIGH
    // PWRKEY should NEVER be left LOW - always HIGH when not powering on/off
    if (_pwrkey_pin >= 0) {
        pinMode(_pwrkey_pin, OUTPUT);
        digitalWrite(_pwrkey_pin, HIGH);  // Idle state: HIGH (not asserting power-on)
    }
    
    // Initialize serial (before reset/power operations)
    if (rx_pin >= 0 && tx_pin >= 0) {
        _serial->begin(SIMCOM_DEFAULT_UART_BAUD, SERIAL_8N1, rx_pin, tx_pin);
    } else {
        _serial->begin(SIMCOM_DEFAULT_UART_BAUD);
    }
    delay(100);  // Brief delay for serial to stabilize
    flushUART();  // Clear any garbage
    
    // Do hardware reset and power-on sequences (unless skipped)
    if (!skip_hardware_reset) {
        // Do hardware reset first (following LilyGo pattern - reset before power-on)
        // This ensures clean state even if module was in a bad state
        if (_reset_pin >= 0) {
            Serial.println("SIMCom A7683E: Performing hardware reset...");
            reset();  // This will pulse LOW for 2.5s then back to HIGH
        }
        
        // Power on the module using PWRKEY
        powerOn();
        
        delay(3000);  // Wait for module to start after power-on
    } else {
        Serial.println("SIMCom A7683E: Skipping hardware reset (already done externally)");
        delay(1000);  // Give module time to stabilize after external reset
    }
    
    // Wait for module to be ready
    Serial.println("SIMCom A7683E: Waiting for module ready...");
    int retry = 0;
    bool module_ready = false;
    while (!module_ready && retry < 50) {  // Increased retry count
        flushUART();
        delay(100);
        if (testAT(2000)) {  // Longer timeout
            module_ready = true;
            break;
        }
        Serial.print(".");
        retry++;
        if (retry % 10 == 0) {
            Serial.println();
            Serial.printf("SIMCom A7683E: Still waiting... (attempt %d/50)\n", retry);
        }
        delay(200);
    }
    
    if (!module_ready) {
        Serial.println();
        Serial.println("SIMCom A7683E: ERROR - Module not ready after initialization!");
        return false;
    }
    
    Serial.println();
    Serial.println("SIMCom A7683E: Module ready!");
    
    // Disable echo and wait for response
    flushUART();
    sendAT("ATE0");
    delay(200);
    flushUART();
    
    return true;
}

bool SIMCom_A7683E::testAT(uint32_t timeout_ms) {
    flushUART();
    _serial->print("AT\r");
    _serial->flush();
    return waitResponse(timeout_ms) == 1;
}

int SIMCom_A7683E::waitResponse(uint32_t timeout_ms) {
    String data;
    return waitResponse(timeout_ms, data);
}

int SIMCom_A7683E::waitResponse(uint32_t timeout_ms, String& data) {
    data = "";
    uint32_t start = millis();
    bool found_ok = false;
    bool found_error = false;
    uint32_t last_char_time = millis();
    
    // Common URC prefixes to filter out
    // NOTE: +CREG: and +CEREG: are NOT in this list because we need to keep them
    // when they're responses to AT+CREG? and AT+CEREG? commands
    const char* urc_prefixes[] = {
        "+CGEV:", "+CCIOTOPTI:", "+CMTI:", "+CMT:",
        "+CDS:", "+CBM:", "+CMGS:", "+CDSI:",
        // +CMGL: and +CMGR: are intentionally not filtered to preserve SMS listings
        "+CLIP:", "+CCWA:", "+COLP:", "+CSSI:", "+CSSU:", "+CUSD:",
        "+CRING:", "+RING:", "+NO CARRIER", "+BUSY", "+NO ANSWER"
        // Note: +CME ERROR: and +CMS ERROR: are handled specially below
        // Note: +CREG: and +CEREG: are handled specially - only filter if unsolicited
    };
    
    while (millis() - start < timeout_ms) {
        while (_serial->available()) {
            char c = _serial->read();
            data += c;
            last_char_time = millis();
            
            if (data.endsWith("OK\r\n") || data.endsWith("OK\r")) {
                found_ok = true;
                // Wait a bit more for any trailing URCs
                delay(150);
                // Read any remaining data (URCs might come after OK)
                uint32_t urc_wait_start = millis();
                while (_serial->available() && (millis() - urc_wait_start) < 300) {
                    char c2 = _serial->read();
                    data += c2;
                    last_char_time = millis();
                    urc_wait_start = millis(); // Reset timer on new data
                }
                break;
            }
            if (data.endsWith("ERROR\r\n") || data.endsWith("ERROR\r")) {
                // Check if this ERROR is actually from our command or a URC
                // If we see ERROR right after our command (within a few lines), it's likely our error
                // Otherwise it might be a URC
                found_error = true;
                break;
            }
            // Only treat +CME ERROR or +CMS ERROR as error if it appears before OK
            // If OK appears first, these are just URCs
            if ((data.indexOf("+CME ERROR:") >= 0 || data.indexOf("+CMS ERROR:") >= 0) && 
                data.indexOf("OK") < 0) {
                found_error = true;
                break;
            }
        }
        
        // If we found OK, break out of the loop
        if (found_ok || found_error) {
            break;
        }
        
        // If no data for a while and we have something, check if we should continue
        if (data.length() > 0 && (millis() - last_char_time) > 500) {
            // No more data coming, check if we have OK or ERROR
            if (data.indexOf("OK") >= 0 && !found_ok) {
                // Might have OK but not at the end - wait a bit more
                delay(100);
                continue;
            }
            if (data.indexOf("ERROR") >= 0 && !found_error) {
                break;
            }
        }
        
        delay(1);
    }
    
    // Filter out URCs from the response
    String filtered_data = "";
    int pos = 0;
    while (pos < (int)data.length()) {
        // Check if this line is a URC
        bool is_urc = false;
        int line_end = data.indexOf("\r\n", pos);
        if (line_end < 0) line_end = data.indexOf("\r", pos);
        if (line_end < 0) line_end = data.indexOf("\n", pos);
        if (line_end < 0) line_end = data.length();
        
        String line = data.substring(pos, line_end);
        
        // Check against known URC prefixes
        for (int i = 0; i < (int)(sizeof(urc_prefixes) / sizeof(urc_prefixes[0])); i++) {
            if (line.startsWith(urc_prefixes[i])) {
                is_urc = true;
                break;
            }
        }
        
        // Also check for standalone URC patterns
        if (line.startsWith("+") && (line.indexOf(":") > 0 || line.indexOf("=") > 0)) {
            // Lines starting with + and containing : or = are likely URCs (unless they're our response)
            // But we want to keep responses like "+CPMS: " so check more carefully
            if (!line.startsWith("+CPMS") && !line.startsWith("+CEREG") && 
                !line.startsWith("+CREG") && !line.startsWith("+COPS") &&
                !line.startsWith("+CSQ") && !line.startsWith("+CCLK") &&
                !line.startsWith("+CICCID") && !line.startsWith("+ICCID") &&
                !line.startsWith("+CIMI") && !line.startsWith("+CGDCONT") &&
                !line.startsWith("+CGAUTH") && !line.startsWith("+CPIN") &&
                !line.startsWith("+CFUN") && !line.startsWith("+CMGF") &&
                !line.startsWith("+CMGL") && !line.startsWith("+CMGR")) {
                is_urc = true;
            }
        }
        
        // Special handling for +CREG: and +CEREG:
        // These can be either command responses (keep) or unsolicited URCs (filter)
        // If they appear before we've seen any command response, they're likely URCs
        // But if they're in response to AT+CREG? or AT+CEREG?, we need to keep them
        // For now, we'll keep them if they're in the filtered data (they were responses)
        // and filter them only if they appear as standalone URCs (which is hard to detect)
        // Actually, let's be safe and keep all +CREG: and +CEREG: lines - the parsing
        // function will handle invalid formats
        
        // Filter out +CME ERROR: and +CMS ERROR: if they appear after OK (they're URCs)
        // But keep them if they appear before OK (they're actual command errors)
        if ((line.startsWith("+CME ERROR:") || line.startsWith("+CMS ERROR:")) && found_ok) {
            is_urc = true;  // This is a URC that came after OK, filter it out
        }
        
        if (!is_urc) {
            filtered_data += line;
            if (line_end < (int)data.length()) {
                filtered_data += data.substring(line_end, line_end + (data.charAt(line_end) == '\r' && line_end + 1 < (int)data.length() && data.charAt(line_end + 1) == '\n' ? 2 : 1));
            }
        }
        
        pos = line_end + (line_end < (int)data.length() && data.charAt(line_end) == '\r' && line_end + 1 < (int)data.length() && data.charAt(line_end + 1) == '\n' ? 2 : 1);
        if (pos >= (int)data.length()) break;
    }
    
    data = filtered_data;
    
    return found_ok ? 1 : (found_error ? 0 : -1);
}

int SIMCom_A7683E::waitResponse(uint32_t timeout_ms, const char* expected) {
    String data;
    int res = waitResponse(timeout_ms, data);
    if (res == 1 && data.indexOf(expected) >= 0) {
        return 1;
    }
    return res;
}

bool SIMCom_A7683E::sendAT(const char* command, uint32_t timeout_ms) {
    flushUART();
    _serial->print(command);
    _serial->print("\r");
    _serial->flush();
    return waitResponse(timeout_ms) == 1;
}

bool SIMCom_A7683E::sendAT(const char* command, String& response, uint32_t timeout_ms) {
    flushUART();
    _serial->print(command);
    _serial->print("\r");
    _serial->flush();
    return waitResponse(timeout_ms, response) == 1;
}

bool SIMCom_A7683E::sendATBounded(const char* command,
                       String& response,
                       uint32_t total_timeout_ms,
                       uint32_t quiet_timeout_ms) {
    flushUART();
    _serial->print(command);
    _serial->print("\r");
    _serial->flush();

    uint32_t start = millis();
    uint32_t last_activity = start;
    bool saw_ok = false;

    while ((millis() - start) < total_timeout_ms) {
        bool got_char = false;
        while (_serial->available()) {
            response += char(_serial->read());
            last_activity = millis();
            got_char = true;

            if (response.endsWith("OK\r\n") || response.endsWith("OK\r")) {
                saw_ok = true;
                break;
            }
            if (response.endsWith("ERROR\r\n") || response.endsWith("ERROR\r")) {
                break;
            }
        }

        if (saw_ok) {
            break;
        }

        if (!got_char && response.length() > 0 && (millis() - last_activity) >= quiet_timeout_ms) {
            // We got something but the line went quiet; stop waiting so URCs can't stretch waits forever
            break;
        }

        if (!got_char && response.length() == 0 && (millis() - last_activity) >= quiet_timeout_ms) {
            // No response at all within quiet_timeout_ms
            break;
        }

        delay(5);
    }

    return saw_ok || response.length() > 0;
}

SIMComSimStatus SIMCom_A7683E::getSimStatus() {
    String response;
    if (!sendAT("AT+CPIN?", response)) {
        return SIMCOM_SIM_ERROR;
    }
    
    if (response.indexOf("READY") >= 0) {
        return SIMCOM_SIM_READY;
    }
    if (response.indexOf("SIM PIN") >= 0 || response.indexOf("PIN") >= 0) {
        return SIMCOM_SIM_LOCKED;
    }
    
    return SIMCOM_SIM_ERROR;
}

bool SIMCom_A7683E::getRegistrationStatus(SIMComNetworkStatus* lte_status, SIMComNetworkStatus* gsm_status) {
    if (lte_status == nullptr || gsm_status == nullptr) {
        return false;
    }
    
    // Initialize to unknown in case parsing fails
    *lte_status = SIMCOM_NET_UNKNOWN;
    *gsm_status = SIMCOM_NET_UNKNOWN;
    
    String response;
    
    // Get LTE registration status (CEREG)
    // Format: +CEREG: <n>,<stat> where <n> is URC mode (0/1/2) and <stat> is status (0-5)
    flushUART();
    if (sendAT("AT+CEREG?", response, 3000)) {
        // Debug: print raw response for troubleshooting
        // Serial.printf("DEBUG CEREG response: [%s]\n", response.c_str());
        
        int start = response.indexOf("+CEREG: ");
        if (start >= 0) {
            start += 8;  // Skip "+CEREG: "
            int comma1 = response.indexOf(",", start);
            if (comma1 > start) {
                // Found first comma, status is after it
                int comma2 = response.indexOf(",", comma1 + 1);
                int end = (comma2 > comma1) ? comma2 : response.indexOf("\r", comma1);
                if (end < 0) end = response.indexOf("\n", comma1);
                if (end < 0) end = response.length();
                
                String status_str = response.substring(comma1 + 1, end);
                status_str.trim();
                int status_val = status_str.toInt();
                
                // Validate status is in valid range (0-5)
                if (status_val >= 0 && status_val <= 5) {
                    *lte_status = (SIMComNetworkStatus)status_val;
                } else {
                    Serial.printf("SIMCom A7683E: WARNING - Invalid LTE status value: %d (from: [%s])\n", status_val, status_str.c_str());
                }
            } else {
                Serial.printf("SIMCom A7683E: WARNING - Could not parse CEREG response: [%s]\n", response.c_str());
            }
        } else {
            Serial.printf("SIMCom A7683E: WARNING - No +CEREG: found in response: [%s]\n", response.c_str());
        }
    } else {
        Serial.printf("SIMCom A7683E: WARNING - AT+CEREG? failed. Response: [%s]\n", response.c_str());
    }
    
    // Get GSM registration status (CREG)
    // Format: +CREG: <n>,<stat> where <n> is URC mode (0/1/2) and <stat> is status (0-5)
    flushUART();
    if (sendAT("AT+CREG?", response, 3000)) {
        // Debug: print raw response for troubleshooting
        // Serial.printf("DEBUG CREG response: [%s]\n", response.c_str());
        
        int start = response.indexOf("+CREG: ");
        if (start >= 0) {
            start += 7;  // Skip "+CREG: "
            int comma1 = response.indexOf(",", start);
            if (comma1 > start) {
                // Found first comma, status is after it
                int comma2 = response.indexOf(",", comma1 + 1);
                int end = (comma2 > comma1) ? comma2 : response.indexOf("\r", comma1);
                if (end < 0) end = response.indexOf("\n", comma1);
                if (end < 0) end = response.length();
                
                String status_str = response.substring(comma1 + 1, end);
                status_str.trim();
                int status_val = status_str.toInt();
                
                // Validate status is in valid range (0-5)
                if (status_val >= 0 && status_val <= 5) {
                    *gsm_status = (SIMComNetworkStatus)status_val;
                } else {
                    Serial.printf("SIMCom A7683E: WARNING - Invalid GSM status value: %d (from: [%s])\n", status_val, status_str.c_str());
                }
            } else {
                Serial.printf("SIMCom A7683E: WARNING - Could not parse CREG response: [%s]\n", response.c_str());
            }
        } else {
            Serial.printf("SIMCom A7683E: WARNING - No +CREG: found in response: [%s]\n", response.c_str());
        }
    } else {
        Serial.printf("SIMCom A7683E: WARNING - AT+CREG? failed. Response: [%s]\n", response.c_str());
    }
    
    return true;
}

int SIMCom_A7683E::getSignalQuality() {
    String response;
    if (!sendAT("AT+CSQ", response)) {
        return -113;
    }
    
    int start = response.indexOf("+CSQ: ");
    if (start >= 0) {
        start += 6;
        int comma = response.indexOf(",", start);
        if (comma > start) {
            int rssi = response.substring(start, comma).toInt();
            if (rssi == 99) {
                return -113;  // Not available
            }
            // Convert to dBm: 0-31 maps to -113 to -51 dBm
            return -113 + (rssi * 2);
        }
    }
    
    return -113;
}

bool SIMCom_A7683E::waitForNetworkRegistration(uint32_t timeout_ms) {
    uint32_t start = millis();
    SIMComNetworkStatus lte_status = SIMCOM_NET_UNKNOWN;
    SIMComNetworkStatus gsm_status = SIMCOM_NET_UNKNOWN;
    int best_signal = -113;
    int signal = -113;  // Current signal - declare outside loop for use in timeout message
    uint32_t last_status_print = 0;
    uint32_t last_retry_time = 0;
    int retry_count = 0;
    const int MAX_RETRIES = 2;  // Limit aggressive retries to prevent infinite loops
    const uint32_t RETRY_INTERVAL = 30000;  // 30 seconds between retries (increased from 15s)
    const int MIN_SIGNAL_FOR_RETRY = -110;  // Only retry if signal is reasonable (better than -110 dBm)
    
    Serial.println("SIMCom A7683E: Waiting for network registration...");
    
    while (millis() - start < timeout_ms) {
        getRegistrationStatus(&lte_status, &gsm_status);
        signal = getSignalQuality();
        if (signal > best_signal) {
            best_signal = signal;
        }
        
        // Check if registered (1 = home, 5 = roaming)
        if (lte_status == SIMCOM_NET_REGISTERED_HOME || lte_status == SIMCOM_NET_REGISTERED_ROAMING ||
            gsm_status == SIMCOM_NET_REGISTERED_HOME || gsm_status == SIMCOM_NET_REGISTERED_ROAMING) {
            Serial.printf("SIMCom A7683E: Registered! LTE=%d, GSM=%d, Signal=%d dBm\n", 
                         lte_status, gsm_status, signal);
            return true;
        }
        
        uint32_t elapsed = millis() - start;
        
        // Print status every 5 seconds
        if (elapsed - last_status_print >= 5000) {
            const char* lte_name = (lte_status == 0) ? "Not registered" :
                                  (lte_status == 1) ? "Home" :
                                  (lte_status == 2) ? "Searching" :
                                  (lte_status == 3) ? "Denied" :
                                  (lte_status == 4) ? "Unknown" :
                                  (lte_status == 5) ? "Roaming" : "?";
            
            Serial.printf("  LTE: %s (%d), Signal: %d dBm (best: %d)\n", 
                         lte_name, lte_status, signal, best_signal);
            last_status_print = elapsed;
        } else {
            Serial.print(".");
        }
        
        // Handle different registration states appropriately:
        // - Status 2 (Searching): Module is actively searching, wait patiently
        // - Status 3 (Denied): Registration denied, retries won't help
        // - Status 0 (Not registered) or 4 (Unknown): May benefit from retry, but only if:
        //   * Signal quality is reasonable (better than -110 dBm)
        //   * We haven't exceeded MAX_RETRIES
        //   * Enough time has passed since last retry (exponential backoff)
        
        bool should_retry = false;
        if (retry_count < MAX_RETRIES) {
            // Only retry if status is 0 (not registered) or 4 (unknown)
            // Don't retry if status is 2 (searching) - module is actively working
            // Don't retry if status is 3 (denied) - retries won't help
            if ((lte_status == SIMCOM_NET_NOT_REGISTERED || lte_status == SIMCOM_NET_UNKNOWN) &&
                (gsm_status == SIMCOM_NET_NOT_REGISTERED || gsm_status == SIMCOM_NET_UNKNOWN)) {
                // Check signal quality - only retry if signal is reasonable
                if (signal >= MIN_SIGNAL_FOR_RETRY || best_signal >= MIN_SIGNAL_FOR_RETRY) {
                    // Check if enough time has passed since last retry (exponential backoff)
                    uint32_t time_since_last_retry = (last_retry_time == 0) ? elapsed : (millis() - last_retry_time);
                    uint32_t retry_interval = RETRY_INTERVAL * (1 << retry_count);  // Exponential backoff: 30s, 60s, 120s...
                    
                    if (time_since_last_retry >= retry_interval && elapsed > 20000) {  // Wait at least 20s before first retry
                        should_retry = true;
                    }
                } else {
                    // Signal is too weak - don't retry aggressively
                    if (elapsed - last_status_print >= 10000) {  // Print warning every 10s
                        Serial.printf("\nSIMCom A7683E: Signal too weak (%d dBm) for reliable registration. Waiting...\n", signal);
                        last_status_print = elapsed;  // Reset to avoid spam
                    }
                }
            }
        }
        
        if (should_retry) {
            retry_count++;
            last_retry_time = millis();
            
            Serial.printf("\nSIMCom A7683E: Retry %d/%d - Attempting to trigger registration (signal: %d dBm)...\n", 
                         retry_count, MAX_RETRIES, signal);
            flushUART();
            
            // Try cycling CFUN: set to 0 (minimum), wait, then back to 1 (full)
            Serial.println("SIMCom A7683E: Cycling CFUN (0 -> 1)...");
            sendAT("AT+CFUN=0", 5000);  // Minimum functionality
            delay(2000);
            sendAT("AT+CFUN=1", 10000);  // Full functionality (longer timeout)
            delay(5000);  // Give module more time to reinitialize radio
            
            // Force network selection again
            sendAT("AT+COPS=0", 5000);  // Force automatic network selection
            delay(3000);  // Give module time to start network search
            
            // Re-enable URCs
            sendAT("AT+CEREG=2", 3000);
            delay(500);
            sendAT("AT+CREG=2", 3000);
            delay(500);
            
            // Reset status print timer to show new status after retry
            last_status_print = 0;
        }
        
        delay(1000);
    }
    
    Serial.println();
    Serial.printf("SIMCom A7683E: Registration timeout! LTE=%d, GSM=%d, Signal=%d dBm (best: %d), Retries: %d\n",
                 lte_status, gsm_status, signal, best_signal, retry_count);
    return false;
}

bool SIMCom_A7683E::setAPN(const char* apn, const char* username, const char* password, int auth_type) {
    if (apn == nullptr) {
        return false;
    }
    
    // Set APN
    String cmd = "AT+CGDCONT=1,\"IP\",\"";
    cmd += apn;
    cmd += "\"";
    
    if (!sendAT(cmd.c_str())) {
        Serial.println("SIMCom A7683E: Failed to set APN");
        return false;
    }
    
    Serial.printf("SIMCom A7683E: APN set to: %s\n", apn);
    
    // Set authentication if provided
    if (username != nullptr && password != nullptr && strlen(username) > 0) {
        String auth_cmd = "AT+CGAUTH=1,";
        auth_cmd += auth_type;
        auth_cmd += ",\"";
        auth_cmd += username;
        auth_cmd += "\",\"";
        auth_cmd += password;
        auth_cmd += "\"";
        
        if (sendAT(auth_cmd.c_str())) {
            const char* auth_name = (auth_type == 1) ? "PAP" : (auth_type == 2) ? "CHAP" : "None";
            Serial.printf("SIMCom A7683E: APN authentication set (%s)\n", auth_name);
        }
    }
    
    return true;
}

bool SIMCom_A7683E::connect(uint32_t timeout_ms) {
    uint32_t connect_start = millis();  // Track start time for timeout calculation
    Serial.println("SIMCom A7683E: Connecting to network...");
    
    // Flush any unsolicited messages
    flushUART();
    delay(200);
    
    // Wait for module to be ready (following Pimoroni pattern)
    Serial.println("SIMCom A7683E: Waiting for module ready...");
    int retries = 3;
    bool module_ready = false;
    while (retries > 0 && !module_ready) {
        flushUART();
        delay(100);
        if (testAT(3000)) {
            module_ready = true;
            break;
        }
        Serial.printf("SIMCom A7683E: Module not responding, retrying... (%d attempts left)\n", retries - 1);
        retries--;
        delay(500);
    }
    
    if (!module_ready) {
        Serial.println("SIMCom A7683E: ERROR - Module not responding after retries!");
        return false;
    }
    
    // Disable echo (following Pimoroni pattern)
    sendAT("ATE0");
    delay(200);
    
    // Ensure module is in full functionality mode
    Serial.println("SIMCom A7683E: Setting full functionality mode...");
    flushUART();
    delay(200);
    if (!sendAT("AT+CFUN=1", 5000)) {
        Serial.println("SIMCom A7683E: WARNING - CFUN=1 may have failed, but continuing...");
    }
    Serial.println("SIMCom A7683E: Waiting for module to apply CFUN=1 (5 seconds)...");
    delay(5000);  // Give module more time to fully apply CFUN=1 and initialize radio
    
    // Check CFUN status to verify (but don't fail if it doesn't respond immediately)
    flushUART();
    delay(200);
    String cfun_check;
    if (sendAT("AT+CFUN?", cfun_check, 3000)) {
        // Extract CFUN value from response
        int cfun_pos = cfun_check.indexOf("+CFUN: ");
        if (cfun_pos >= 0) {
            int cfun_val = cfun_check.substring(cfun_pos + 7).toInt();
            Serial.printf("SIMCom A7683E: CFUN status: %d\n", cfun_val);
        } else {
            Serial.printf("SIMCom A7683E: CFUN status: %s\n", cfun_check.c_str());
        }
    }
    
    // Enable network registration URCs (so we get status updates)
    Serial.println("SIMCom A7683E: Enabling network registration URCs...");
    flushUART();
    sendAT("AT+CEREG=2");  // Enable URCs for network registration
    delay(500);
    sendAT("AT+CREG=2");   // Enable URCs for GSM registration
    delay(500);
    
    // Set APN (simpler approach, following Pimoroni - they set APN then wait for registration)
    if (!setAPN(_apn, _apn_username, _apn_password, _auth_type)) {
        Serial.println("SIMCom A7683E: Failed to set APN");
        return false;
    }
    
    // Force automatic network selection
    Serial.println("SIMCom A7683E: Forcing automatic network selection...");
    flushUART();
    delay(200);
    sendAT("AT+COPS=0");  // Automatic network selection
    delay(3000);  // Give module more time to start network search
    
    // Wait a bit before checking registration status (module needs time after COPS=0)
    delay(2000);  // Increased delay to allow module to process COPS=0
    
    // Check current registration status - maybe we're already registered
    // Check multiple times to ensure stability (module might be transitioning)
    SIMComNetworkStatus lte_status, gsm_status;
    bool already_registered = false;
    int stable_checks = 0;
    const int REQUIRED_STABLE_CHECKS = 2;  // Require 2 consecutive stable checks
    
    for (int check = 0; check < 3; check++) {
        if (getRegistrationStatus(&lte_status, &gsm_status)) {
            // If registered (1 = home, 5 = roaming), count as stable
            if (lte_status == SIMCOM_NET_REGISTERED_HOME || lte_status == SIMCOM_NET_REGISTERED_ROAMING ||
                gsm_status == SIMCOM_NET_REGISTERED_HOME || gsm_status == SIMCOM_NET_REGISTERED_ROAMING) {
                stable_checks++;
                if (stable_checks >= REQUIRED_STABLE_CHECKS) {
                    already_registered = true;
                    Serial.printf("SIMCom A7683E: Already registered and stable (LTE=%d, GSM=%d)!\n", 
                                 lte_status, gsm_status);
                    break;
                }
            } else {
                // Not registered or different status - reset counter
                stable_checks = 0;
            }
        }
        
        if (check < 2) {
            delay(1000);  // Wait between checks
        }
    }
    
    if (already_registered) {
        _connected = true;
        _ip_address = "";
        return true;
    }
    
    // Not registered yet - show initial status and wait for registration
    if (getRegistrationStatus(&lte_status, &gsm_status)) {
        Serial.printf("SIMCom A7683E: Initial registration status: LTE=%d, GSM=%d\n", lte_status, gsm_status);
    } else {
        Serial.println("SIMCom A7683E: Could not read initial registration status (this is OK, will check during wait)");
    }
    
    // Wait for network registration (following Pimoroni pattern - poll CEREG? until status 1 or 5)
    // Adjust timeout: subtract time already spent checking status
    uint32_t time_spent = millis() - connect_start;
    uint32_t remaining_timeout = (timeout_ms > time_spent) ? (timeout_ms - time_spent) : 10000;  // At least 10s
    
    if (!waitForNetworkRegistration(remaining_timeout)) {
        Serial.println("SIMCom A7683E: Network registration failed or timed out");
        return false;
    }
    
    Serial.println("SIMCom A7683E: Network registered!");
    
    // Mark as connected - but note: this is just network registration, not PPP connection
    _connected = true;
    _ip_address = "";  // Will be set by PPP if used
    
    return true;
}

bool SIMCom_A7683E::disconnect() {
    if (!_connected) {
        return true;
    }
    
    sendAT("ATH");  // Hang up (if PPP was active)
    // Don't power off module - just disconnect network
    // sendAT("AT+CPOF");  // Power off
    
    _connected = false;
    return true;
}

bool SIMCom_A7683E::getICCID(char* iccid, size_t len) {
    if (iccid == nullptr || len < 21) {
        return false;
    }
    
    String response;
    if (!sendAT("AT+CICCID", response)) {
        return false;
    }
    
    // Parse response: +CICCID: 89860012345678901234
    int start = response.indexOf("+CICCID: ");
    if (start < 0) {
        start = response.indexOf("+ICCID: ");
        if (start < 0) {
            // Try to find raw digits
            for (int i = 0; i < (int)response.length() - 19; i++) {
                if (isdigit(response.charAt(i))) {
                    int j = i;
                    while (j < (int)response.length() && isdigit(response.charAt(j))) {
                        j++;
                    }
                    if (j - i >= 19) {
                        response.substring(i, j).toCharArray(iccid, len);
                        return true;
                    }
                }
            }
            return false;
        }
        start += 8;
    } else {
        start += 9;
    }
    
    int end = start;
    while (end < (int)response.length() && (isdigit(response.charAt(end)) || response.charAt(end) == ' ')) {
        end++;
    }
    
    String iccid_str = response.substring(start, end);
    iccid_str.trim();
    iccid_str.toCharArray(iccid, len);
    return true;
}

bool SIMCom_A7683E::getNetworkTime(char* time_str, size_t len) {
    if (time_str == nullptr || len < 20) {
        return false;
    }
    
    String response;
    if (!sendAT("AT+CCLK?", response)) {
        return false;
    }
    
    int start = response.indexOf("+CCLK: \"");
    if (start < 0) {
        return false;
    }
    
    start += 8;
    int end = response.indexOf("\"", start);
    if (end < 0) {
        return false;
    }
    
    response.substring(start, end).toCharArray(time_str, len);
    return true;
}

bool SIMCom_A7683E::getNetworkOperator(char* operator_name, size_t len) {
    if (operator_name == nullptr || len < 32) {
        return false;
    }
    
    String response;
    if (!sendAT("AT+COPS?", response)) {
        return false;
    }
    
    // Try to get alphanumeric name first
    sendAT("AT+COPS=3,0");  // Set to alphanumeric format
    delay(500);
    
    if (!sendAT("AT+COPS?", response)) {
        return false;
    }
    
    int start = response.indexOf("+COPS: ");
    if (start < 0) {
        return false;
    }
    
    start += 7;
    // Format: +COPS: <mode>[,<format>[,<oper>]]
    int comma1 = response.indexOf(",", start);
    if (comma1 < 0) {
        return false;
    }
    
    int comma2 = response.indexOf(",", comma1 + 1);
    if (comma2 < 0) {
        return false;
    }
    
    int quote_start = response.indexOf("\"", comma2);
    if (quote_start < 0) {
        // Numeric format (MCC+MNC)
        String op_str = response.substring(comma2 + 1);
        op_str.trim();
        op_str.toCharArray(operator_name, len);
        strcat(operator_name, " (MCC+MNC)");
        return true;
    }
    
    quote_start++;
    int quote_end = response.indexOf("\"", quote_start);
    if (quote_end < 0) {
        return false;
    }
    
    response.substring(quote_start, quote_end).toCharArray(operator_name, len);
    return true;
}

bool SIMCom_A7683E::getIMSI(char* imsi, size_t len) {
    if (imsi == nullptr || len < 16) {
        return false;
    }
    
    String response;
    if (!sendAT("AT+CIMI", response)) {
        return false;
    }
    
    // Response is just the IMSI digits
    response.trim();
    // Remove "OK" and whitespace
    int start = 0;
    while (start < (int)response.length() && (response.charAt(start) == '\r' || response.charAt(start) == '\n')) {
        start++;
    }
    
    int end = response.indexOf("\r\nOK");
    if (end < 0) {
        end = response.indexOf("\rOK");
    }
    if (end < 0) {
        end = response.length();
    }
    
    String imsi_str = response.substring(start, end);
    imsi_str.trim();
    imsi_str.toCharArray(imsi, len);
    return true;
}

bool SIMCom_A7683E::getFirmwareVersion(char* version, size_t len) {
    if (version == nullptr || len < 32) {
        return false;
    }
    
    String response;
    if (!sendAT("ATI", response, 10000)) {
        return false;
    }
    
    // Find first non-empty line
    int start = 0;
    while (start < (int)response.length() && 
           (response.charAt(start) == '\r' || response.charAt(start) == '\n' || response.charAt(start) == ' ')) {
        start++;
    }
    
    int end = response.indexOf("\r\n", start);
    if (end < 0) {
        end = response.indexOf("\r", start);
    }
    if (end < 0) {
        end = response.length();
    }
    
    String version_str = response.substring(start, end);
    version_str.trim();
    version_str.toCharArray(version, len);
    return true;
}

bool SIMCom_A7683E::getSMSCount(int* unread, int* total) {
    if (unread == nullptr || total == nullptr) {
        return false;
    }
    
    *unread = 0;
    *total = 0;
    
    // Set text mode
    if (!sendAT("AT+CMGF=1")) {
        Serial.println("SIMCom A7683E: getSMSCount - Failed to set CMGF=1");
        return false;
    }
    delay(200);
    
    // Wait a bit and flush UART to clear any pending URCs
    delay(300);
    flushUART();
    delay(200);
    
    // Verify module is still responding
    if (!testAT(2000)) {
        Serial.println("SIMCom A7683E: getSMSCount - Module not responding before CPMS?");
        return false;
    }
    delay(200);
    
    // Query current storage (don't try to change it - just read what's active)
    // Use longer timeout and retry if we get URCs
    String response;
    int retries = 3;
    bool success = false;
    
    while (retries > 0 && !success) {
        flushUART();
        delay(300);
        
        // Send command manually to get better control
        _serial->print("AT+CPMS?\r");
        _serial->flush();
        delay(200);
        
        // Read response with timeout
        uint32_t start_time = millis();
        response = "";
        bool got_ok = false;
        bool got_error = false;
        
        while ((millis() - start_time) < 5000) {
            while (_serial->available()) {
                char c = _serial->read();
                response += c;
                
                if (response.endsWith("OK\r\n") || response.endsWith("OK\r")) {
                    got_ok = true;
                    break;
                }
                if (response.endsWith("ERROR\r\n") || response.endsWith("ERROR\r")) {
                    got_error = true;
                    break;
                }
            }
            if (got_ok || got_error) break;
            delay(10);
        }
        
        // Check if response contains +CPMS: (the actual response we want)
        if (response.indexOf("+CPMS:") >= 0) {
            success = true;
            break;
        } else {
            Serial.printf("SIMCom A7683E: getSMSCount - CPMS? response missing +CPMS:. Got OK=%d, ERROR=%d. Response: [%s]\n", 
                         got_ok, got_error, response.c_str());
        }
        
        retries--;
        if (retries > 0) {
            Serial.println("SIMCom A7683E: getSMSCount - Retrying...");
            delay(1000);
        }
    }
    
    if (!success) {
        return false;
    }
    
    // Parse: +CPMS: "<storage>",<used1>,<total1>,"<storage>",<used2>,<total2>,"<storage>",<used3>,<total3>
    // Storage can be "SM", "ME", "MT", etc.
    int start = response.indexOf("+CPMS: ");
    if (start < 0) {
        Serial.printf("SIMCom A7683E: getSMSCount - No +CPMS: found. Response: [%s]\n", response.c_str());
        return false;
    }
    
    start += 7;
    // Skip first storage name (e.g., "SM", or "ME")
    int quote1 = response.indexOf("\"", start);
    if (quote1 < 0) {
        Serial.println("SIMCom A7683E: getSMSCount - No quote found after +CPMS:");
        return false;
    }
    
    int comma1 = response.indexOf(",", quote1 + 1);
    if (comma1 < 0) {
        Serial.println("SIMCom A7683E: getSMSCount - No comma after storage name");
        return false;
    }
    
    // Get used (messages in use)
    int comma2 = response.indexOf(",", comma1 + 1);
    if (comma2 < 0) {
        Serial.println("SIMCom A7683E: getSMSCount - No comma after used count");
        return false;
    }
    
    String used_str = response.substring(comma1 + 1, comma2);
    used_str.trim();
    // NOTE: The `unread` output actually returns the number of *used* message slots in
    // the current storage (both read and unread), because AT+CPMS? does not provide an
    // unread count.  The `total` parameter returns the total number of SMS slots in the
    // current storage.  To determine the number of unread messages, parse the results
    // of AT+CMGL="REC UNREAD".
    *unread = used_str.toInt();
    
    // Get total capacity
    int quote2 = response.indexOf("\"", comma2 + 1);
    int comma3 = (quote2 > comma2) ? response.indexOf(",", comma2 + 1) : -1;
    if (comma3 < 0 && quote2 > 0) {
        // Try to find end before next quote
        comma3 = quote2;
    }
    if (comma3 < 0) {
        // Last field, find end of line
        comma3 = response.indexOf("\r", comma2 + 1);
        if (comma3 < 0) comma3 = response.indexOf("\n", comma2 + 1);
    }
    if (comma3 < 0) {
        Serial.println("SIMCom A7683E: getSMSCount - Could not find end of total count");
        return false;
    }
    
    String total_str = response.substring(comma2 + 1, comma3);
    total_str.trim();
    *total = total_str.toInt();
    
    return true;
}

bool SIMCom_A7683E::listSMS(int max_messages) {
    bool found_ok = false;

    (void)max_messages;  // Currently used for informational printing only

    // Set text mode
    if (!sendAT("AT+CMGF=1")) {
        Serial.println("SIMCom A7683E: Failed to set text mode (CMGF=1)");
        return false;
    }

    auto getCurrentStorage = [&]() -> String {
        String current_cpms;
        if (sendAT("AT+CPMS?", current_cpms, 3000)) {
            int cpms_start = current_cpms.indexOf("+CPMS: \"");
            if (cpms_start >= 0) {
                cpms_start += 8;
                int quote_end = current_cpms.indexOf("\"", cpms_start);
                if (quote_end > cpms_start) {
                    return current_cpms.substring(cpms_start, quote_end);
                }
            }
        }
        return String("SM");
    };

    String current_storage = getCurrentStorage();
    Serial.printf("SIMCom A7683E: Current storage is: %s\n", current_storage.c_str());

    auto listFrom = [&](const String& storage) -> bool {
        bool ok = false;
        if (current_storage != storage) {
            Serial.println(String("Switching SMS storage to ") + storage);
            String set_storage_cmd = String("AT+CPMS=\"") + storage + "\",\"" + storage + "\",\"" + storage + "\"";
            String cpms_response;
            if (!sendATBounded(set_storage_cmd.c_str(), cpms_response, 5000, 800)) {
                Serial.printf("SIMCom A7683E: CPMS switch to %s timed out or stayed quiet.\n", storage.c_str());
            }
            current_storage = storage;
        }

        int used_slots = 0;
        int total_slots = 0;
        if (getSMSCount(&used_slots, &total_slots)) {
            Serial.printf("SIMCom A7683E: Storage %s: %d used, %d total\n", storage.c_str(), used_slots, total_slots);
        }

        Serial.printf("SIMCom A7683E: SMS Messages from %s:\n", storage.c_str());

        String response;
        ok = sendATBounded("AT+CMGL=\"ALL\"", response, 10000, 750);
        if (response.length() > 0) {
            Serial.print(response);
        }

        bool has_messages = ok || response.indexOf("+CMGL:") >= 0;
        if (!has_messages) {
            Serial.printf("SIMCom A7683E: No SMS payload returned from %s within bounds.\n", storage.c_str());
        } else {
            found_ok = true;
        }

        return has_messages;
    };

    // List from current, then SM, then ME storages
    listFrom(current_storage);
    if (current_storage != "SM") {
        listFrom("SM");
    }
    if (current_storage != "ME") {
        listFrom("ME");
    }

    return found_ok;
}

bool SIMCom_A7683E::fetchSMSFromStorage(const String& storage,
                                        String& response,
                                        uint32_t total_timeout_ms,
                                        uint32_t quiet_timeout_ms) {
    // Always operate in text mode before listing messages.
    if (!sendAT("AT+CMGF=1")) {
        Serial.println("SIMCom A7683E: Failed to set text mode before fetching SMS");
        return false;
    }

    String current_storage = storage;
    current_storage.toUpperCase();

    if (current_storage != "CURRENT") {
        String cpms_cmd = String("AT+CPMS=\"") + current_storage + "\",\"" + current_storage + "\",\"" + current_storage + "\"";
        String cpms_response;
        if (!sendATBounded(cpms_cmd.c_str(), cpms_response, total_timeout_ms, quiet_timeout_ms)) {
            Serial.printf("SIMCom A7683E: CPMS switch to %s timed out or stayed quiet.\n", current_storage.c_str());
            // If we got no response at all, bail. Otherwise continue and let the caller parse.
            if (cpms_response.length() == 0) {
                return false;
            }
        }
    }

    response = "";
    bool ok = sendATBounded("AT+CMGL=\"ALL\"", response, total_timeout_ms, quiet_timeout_ms);
    if (!ok && response.indexOf("+CMGL:") < 0) {
        Serial.printf("SIMCom A7683E: No SMS payload returned from %s storage.\n", current_storage.c_str());
        return false;
    }

    return response.length() > 0;
}
