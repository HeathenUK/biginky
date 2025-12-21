/**
 * @file SIMCom_A7683E.h
 * @brief SIMCom A7683E 4G LTE module driver for Clipper breakout board
 * 
 * This library provides support for the SIMCom A7683E 4G LTE module used in
 * the Pimoroni Clipper LTE 4G Breakout board. It supports AT command communication
 * and network connectivity.
 * 
 * Based on patterns from LilyGo Modem Series examples.
 * 
 * Pinout (SP/CE connector):
 *   GND      - Ground
 *   PWRKEY   - Power key (active LOW, idle HIGH - pulse LOW 50ms to power on)
 *   RX       - UART input to breakout
 *   RESET    - Reset pin (active LOW, idle HIGH - pulse LOW 2.5s to reset)
 *   NETLIGHT - Network status LED output (7mA max)
 *   TX       - UART output from breakout
 *   VDDIO    - IO voltage (3.0-3.6V)
 *   VDD      - Power input (3.7-6.0V)
 * 
 * NOTE: Both PWRKEY and RESET are active LOW but have internal pull-ups to VBAT.
 *       They should ALWAYS be HIGH when not in use. Never leave them LOW.
 */

#ifndef SIMCOM_A7683E_H
#define SIMCOM_A7683E_H

#include <Arduino.h>
#include <HardwareSerial.h>

// Default pin definitions (ESP32-P4)
#ifndef SIMCOM_DEFAULT_PIN_RST
#define SIMCOM_DEFAULT_PIN_RST    24
#endif
#ifndef SIMCOM_DEFAULT_PIN_NETLIGHT
#define SIMCOM_DEFAULT_PIN_NETLIGHT -1  // NETLIGHT disabled by default
#endif
#ifndef SIMCOM_DEFAULT_PIN_PWRKEY
#define SIMCOM_DEFAULT_PIN_PWRKEY 46   // PWRKEY for power-on sequence
#endif
#ifndef SIMCOM_DEFAULT_PIN_RX
#define SIMCOM_DEFAULT_PIN_RX     28
#endif
#ifndef SIMCOM_DEFAULT_PIN_TX
#define SIMCOM_DEFAULT_PIN_TX     29
#endif

// Default UART settings
#define SIMCOM_DEFAULT_UART_BAUD  115200
#define SIMCOM_AT_TIMEOUT_SHORT    5000    // 5 seconds
#define SIMCOM_AT_TIMEOUT_LONG     30000   // 30 seconds
#define SIMCOM_AT_TIMEOUT_CONNECT  60000   // 60 seconds for connection

// Network registration status
enum SIMComNetworkStatus {
    SIMCOM_NET_NOT_REGISTERED = 0,
    SIMCOM_NET_REGISTERED_HOME = 1,
    SIMCOM_NET_SEARCHING = 2,
    SIMCOM_NET_REGISTRATION_DENIED = 3,
    SIMCOM_NET_UNKNOWN = 4,
    SIMCOM_NET_REGISTERED_ROAMING = 5
};

// SIM card status
enum SIMComSimStatus {
    SIMCOM_SIM_ERROR = 0,
    SIMCOM_SIM_READY = 1,
    SIMCOM_SIM_LOCKED = 2
};

class SIMCom_A7683E {
public:
    /**
     * @brief Constructor
     * @param apn Access Point Name (e.g., "internet", "data", carrier-specific)
     * @param serial Serial port to use (default: Serial1)
     * @param reset_pin GPIO pin for RESET (default: SIMCOM_DEFAULT_PIN_RST)
     * @param netlight_pin GPIO pin for NETLIGHT status (optional, -1 to disable)
     * @param pwrkey_pin GPIO pin for PWRKEY (default: SIMCOM_DEFAULT_PIN_PWRKEY, -1 to disable)
     * @param skip_reset Skip hardware reset on init (useful for resuming existing connection)
     * @param apn_username Optional APN username (nullptr if not needed)
     * @param apn_password Optional APN password (nullptr if not needed)
     * @param auth_type Authentication type: 0=none, 1=PAP, 2=CHAP (default: 0)
     */
    SIMCom_A7683E(const char* apn, 
                  HardwareSerial* serial = nullptr,
                  int reset_pin = SIMCOM_DEFAULT_PIN_RST,
                  int netlight_pin = SIMCOM_DEFAULT_PIN_NETLIGHT,
                  int pwrkey_pin = SIMCOM_DEFAULT_PIN_PWRKEY,
                  bool skip_reset = false,
                  const char* apn_username = nullptr,
                  const char* apn_password = nullptr,
                  int auth_type = 0);

    /**
     * @brief Destructor - frees allocated memory
     */
    ~SIMCom_A7683E();

    /**
     * @brief Initialize the module
     * @param rx_pin Optional RX pin (for ESP32, to preserve pin config if Serial already initialized)
     * @param tx_pin Optional TX pin (for ESP32, to preserve pin config if Serial already initialized)
     * @param skip_hardware_reset If true, skip RESET and PWRKEY sequences (module already reset externally)
     * @return true on success, false on failure
     */
    bool begin(int rx_pin = -1, int tx_pin = -1, bool skip_hardware_reset = false);

    /**
     * @brief Check if module is ready to accept AT commands
     * @param timeout_ms Timeout in milliseconds
     * @return true if ready, false on timeout
     */
    bool testAT(uint32_t timeout_ms = 1000);

    /**
     * @brief Wait for module to be ready (alias for testAT)
     * @param timeout_ms Timeout in milliseconds
     * @return true if ready, false on timeout
     */
    bool waitReady(uint32_t timeout_ms = 10000) { return testAT(timeout_ms); }

    /**
     * @brief Get SIM card status
     * @return SIMComSimStatus enum value
     */
    SIMComSimStatus getSimStatus();

    /**
     * @brief Get network registration status
     * @param lte_status Output parameter for LTE registration status
     * @param gsm_status Output parameter for GSM registration status
     * @return true on success, false on failure
     */
    bool getRegistrationStatus(SIMComNetworkStatus* lte_status, SIMComNetworkStatus* gsm_status);

    /**
     * @brief Get network registration status (alias for getRegistrationStatus)
     * @param lte_status Output parameter for LTE registration status
     * @param gsm_status Output parameter for GSM registration status
     * @return true on success, false on failure
     */
    bool getNetworkStatus(SIMComNetworkStatus* lte_status, SIMComNetworkStatus* gsm_status) {
        return getRegistrationStatus(lte_status, gsm_status);
    }

    /**
     * @brief Get signal quality in dBm
     * @return Signal quality in dBm, or -113 if unavailable
     */
    int getSignalQuality();

    /**
     * @brief Connect to cellular network
     * @param timeout_ms Timeout in milliseconds (default: 60000)
     * @return true on success, false on failure
     */
    bool connect(uint32_t timeout_ms = SIMCOM_AT_TIMEOUT_CONNECT);

    /**
     * @brief Disconnect and power down module
     * @return true on success, false on failure
     */
    bool disconnect();

    /**
     * @brief Get SIM card ICCID
     * @param iccid Buffer to store ICCID (must be at least 21 bytes)
     * @return true on success, false on failure
     */
    bool getICCID(char* iccid, size_t len);

    /**
     * @brief Get network time from module (AT+CCLK?)
     * @param time_str Buffer to store time string
     * @param len Buffer length
     * @return true on success, false on failure
     */
    bool getNetworkTime(char* time_str, size_t len);

    /**
     * @brief Get network operator name
     * @param operator_name Buffer to store operator name
     * @param len Buffer length
     * @return true on success, false on failure
     */
    bool getNetworkOperator(char* operator_name, size_t len);

    /**
     * @brief Get SIM card IMSI
     * @param imsi Buffer to store IMSI (must be at least 16 bytes)
     * @param len Buffer length
     * @return true on success, false on failure
     */
    bool getIMSI(char* imsi, size_t len);

    /**
     * @brief Get module firmware version
     * @param version Buffer to store version string
     * @param len Buffer length
     * @return true on success, false on failure
     */
    bool getFirmwareVersion(char* version, size_t len);

    /**
     * @brief Get SMS message count
     * @param unread Output parameter for unread count
     * @param total Output parameter for total count
     * @return true on success, false on failure
     */
    bool getSMSCount(int* unread, int* total);

    /**
     * @brief List SMS messages
     * @param max_messages Maximum number of messages to list (default: 5)
     * @return true on success, false on failure
     */
    bool listSMS(int max_messages = 5);

    /**
     * @brief Fetch SMS messages from a storage with bounded waits.
     * @param storage Storage name ("SM", "ME", or "current" to leave unchanged)
     * @param response Raw response buffer populated with any modem output
     * @param total_timeout_ms Absolute cap for the request
     * @param quiet_timeout_ms How long to wait after the last byte before bailing
     * @return true if any SMS payload was returned, false otherwise
     */
    bool fetchSMSFromStorage(const String& storage,
                             String& response,
                             uint32_t total_timeout_ms = 7000,
                             uint32_t quiet_timeout_ms = 750);

    /**
     * @brief Check if connected to network
     * @return true if connected, false otherwise
     */
    bool isConnected() const { return _connected; }

    /**
     * @brief Get current IP address (after network connection)
     * @return IP address as string, or empty string if not connected
     */
    String getIPAddress() const { return _ip_address; }

    /**
     * @brief Set APN (can be called before connect)
     * @param apn Access Point Name
     * @param username Optional username
     * @param password Optional password
     * @param auth_type Authentication type: 0=none, 1=PAP, 2=CHAP
     * @return true on success, false on failure
     */
    bool setAPN(const char* apn, const char* username = nullptr, const char* password = nullptr, int auth_type = 0);

private:
    const char* _apn;
    HardwareSerial* _serial;
    int _reset_pin;
    int _netlight_pin;
    int _pwrkey_pin;
    bool _connected;
    String _ip_address;
    char* _apn_username;
    char* _apn_password;
    int _auth_type;

    // AT command helpers
    bool sendAT(const char* command, uint32_t timeout_ms = SIMCOM_AT_TIMEOUT_SHORT);
    bool sendAT(const char* command, String& response, uint32_t timeout_ms = SIMCOM_AT_TIMEOUT_SHORT);
    bool sendATBounded(const char* command,
                       String& response,
                       uint32_t total_timeout_ms,
                       uint32_t quiet_timeout_ms);
    int waitResponse(uint32_t timeout_ms = SIMCOM_AT_TIMEOUT_SHORT);
    int waitResponse(uint32_t timeout_ms, String& data);
    int waitResponse(uint32_t timeout_ms, const char* expected);
    void flushUART();
    
    // Internal helpers
    void powerOn();
    void reset();
    bool waitForNetworkRegistration(uint32_t timeout_ms);
};

#endif // SIMCOM_A7683E_H
