/**
 * @file webui_crypto.cpp
 * @brief Web UI encryption and authentication functions implementation
 * 
 * Extracted from main_esp32p4_test.cpp as part of Priority 1 refactoring.
 * Provides encryption, decryption, HMAC validation, and password management.
 */

#include "webui_crypto.h"
#include <Arduino.h>
#include <Preferences.h>
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <esp_random.h>

// External references to global state in main file
extern Preferences authPrefs;  // NVS preferences for web UI authentication
extern bool g_is_cold_boot;    // Flag to indicate this is a cold boot

bool deriveKeysFromPassword(const String& password, uint8_t* hmacKey, uint8_t* encryptionKey) {
    if (password.length() == 0) {
        Serial.println("ERROR: Cannot derive keys from empty password");
        return false;
    }
    
    // Use HMAC-SHA256 to derive keys (simpler than PBKDF2, still secure)
    // Salt for HMAC key: "biginky_hmac_key_v1"
    // Salt for encryption key: "biginky_enc_key_v1"
    const char* hmacSalt = "biginky_hmac_key_v1";
    const char* encSalt = "biginky_enc_key_v1";
    
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr) {
        Serial.println("ERROR: Failed to get SHA256 MD info for key derivation");
        return false;
    }
    
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    
    // Derive HMAC key
    if (mbedtls_md_setup(&ctx, md_info, 1) != 0) {  // 1 = HMAC mode
        Serial.println("ERROR: Failed to setup HMAC context for key derivation");
        mbedtls_md_free(&ctx);
        return false;
    }
    
    if (mbedtls_md_hmac_starts(&ctx, (const unsigned char*)password.c_str(), password.length()) != 0) {
        Serial.println("ERROR: Failed to start HMAC for key derivation");
        mbedtls_md_free(&ctx);
        return false;
    }
    
    if (mbedtls_md_hmac_update(&ctx, (const unsigned char*)hmacSalt, strlen(hmacSalt)) != 0) {
        Serial.println("ERROR: Failed to update HMAC for key derivation");
        mbedtls_md_free(&ctx);
        return false;
    }
    
    if (mbedtls_md_hmac_finish(&ctx, hmacKey) != 0) {
        Serial.println("ERROR: Failed to finish HMAC for key derivation");
        mbedtls_md_free(&ctx);
        return false;
    }
    
    // Derive encryption key (reuse context)
    if (mbedtls_md_hmac_starts(&ctx, (const unsigned char*)password.c_str(), password.length()) != 0) {
        Serial.println("ERROR: Failed to start HMAC for encryption key derivation");
        mbedtls_md_free(&ctx);
        return false;
    }
    
    if (mbedtls_md_hmac_update(&ctx, (const unsigned char*)encSalt, strlen(encSalt)) != 0) {
        Serial.println("ERROR: Failed to update HMAC for encryption key derivation");
        mbedtls_md_free(&ctx);
        return false;
    }
    
    if (mbedtls_md_hmac_finish(&ctx, encryptionKey) != 0) {
        Serial.println("ERROR: Failed to finish HMAC for encryption key derivation");
        mbedtls_md_free(&ctx);
        return false;
    }
    
    mbedtls_md_free(&ctx);
    return true;
}

String computeHMAC(const String& message) {
    if (!isWebUIPasswordSet()) {
        Serial.println("ERROR: Cannot compute HMAC - password not set");
        return "";
    }
    
    // Get password from NVS
    if (!authPrefs.begin("webui_auth", true)) {
        Serial.println("ERROR: Failed to open NVS for HMAC computation");
        return "";
    }
    String password = authPrefs.getString("password", "");
    authPrefs.end();
    
    if (password.length() == 0) {
        Serial.println("ERROR: Password is empty");
        return "";
    }
    
    // Derive HMAC key from password
    uint8_t hmacKey[32];
    uint8_t dummyEncKey[32];  // Not used for HMAC
    if (!deriveKeysFromPassword(password, hmacKey, dummyEncKey)) {
        Serial.println("ERROR: Failed to derive HMAC key");
        return "";
    }
    
    // Compute HMAC-SHA256 using derived key
    uint8_t hmac[32];  // HMAC-SHA256 produces 32 bytes
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == nullptr) {
        Serial.println("ERROR: Failed to get SHA256 MD info");
        return "";
    }
    
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    
    if (mbedtls_md_setup(&ctx, md_info, 1) != 0) {  // 1 = HMAC mode
        Serial.println("ERROR: Failed to setup HMAC context");
        mbedtls_md_free(&ctx);
        return "";
    }
    
    if (mbedtls_md_hmac_starts(&ctx, hmacKey, 32) != 0) {
        Serial.println("ERROR: Failed to start HMAC");
        mbedtls_md_free(&ctx);
        return "";
    }
    
    if (mbedtls_md_hmac_update(&ctx, (const unsigned char*)message.c_str(), message.length()) != 0) {
        Serial.println("ERROR: Failed to update HMAC");
        mbedtls_md_free(&ctx);
        return "";
    }
    
    if (mbedtls_md_hmac_finish(&ctx, hmac) != 0) {
        Serial.println("ERROR: Failed to finish HMAC");
        mbedtls_md_free(&ctx);
        return "";
    }
    
    mbedtls_md_free(&ctx);
    
    // Convert to hex string
    String hexHMAC = "";
    for (int i = 0; i < 32; i++) {
        if (hmac[i] < 16) hexHMAC += "0";
        hexHMAC += String(hmac[i], HEX);
    }
    return hexHMAC;
}

bool validateWebUIHMAC(const String& message, const String& providedHMAC) {
    // If no password is set, reject all requests (require setup)
    if (!isWebUIPasswordSet()) {
        Serial.println("ERROR: Web UI password not configured - rejecting request");
        return false;
    }
    
    if (providedHMAC.length() != 64) {
        Serial.println("ERROR: Invalid HMAC format (must be 64 hex characters)");
        return false;
    }
    
    String computedHMAC = computeHMAC(message);
    if (computedHMAC.length() == 0) {
        Serial.println("ERROR: Failed to compute HMAC for validation");
        return false;
    }
    
    // Constant-time comparison to prevent timing attacks
    bool valid = true;
    for (int i = 0; i < 64; i++) {
        if (computedHMAC[i] != providedHMAC[i]) {
            valid = false;
        }
    }
    
    if (valid) {
        Serial.println("HMAC validation successful");
    } else {
        Serial.println("HMAC validation failed");
    }
    
    return valid;
}

String encryptMessage(const String& plaintext) {
    if (!isWebUIPasswordSet()) {
        Serial.println("ERROR: Cannot encrypt - password not set");
        return "";
    }
    
    // Get password from NVS
    if (!authPrefs.begin("webui_auth", true)) {
        Serial.println("ERROR: Failed to open NVS for encryption");
        return "";
    }
    String password = authPrefs.getString("password", "");
    authPrefs.end();
    
    if (password.length() == 0) {
        Serial.println("ERROR: Password is empty");
        return "";
    }
    
    // Derive encryption key from password
    uint8_t dummyHmacKey[32];
    uint8_t encryptionKey[32];
    if (!deriveKeysFromPassword(password, dummyHmacKey, encryptionKey)) {
        Serial.println("ERROR: Failed to derive encryption key");
        return "";
    }
    
    // Generate random IV (16 bytes for AES-128/256 CBC)
    uint8_t iv[16];
    esp_fill_random(iv, 16);
    
    // Prepare plaintext
    size_t plaintextLen = plaintext.length();
    size_t paddedLen = ((plaintextLen + 15) / 16) * 16;  // PKCS7 padding
    uint8_t* paddedPlaintext = (uint8_t*)malloc(paddedLen);
    if (!paddedPlaintext) {
        Serial.println("ERROR: Failed to allocate memory for padded plaintext");
        return "";
    }
    
    memcpy(paddedPlaintext, plaintext.c_str(), plaintextLen);
    // PKCS7 padding
    uint8_t padValue = paddedLen - plaintextLen;
    for (size_t i = plaintextLen; i < paddedLen; i++) {
        paddedPlaintext[i] = padValue;
    }
    
    // Encrypt using AES-256-CBC
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    if (mbedtls_aes_setkey_enc(&aes, encryptionKey, 256) != 0) {
        Serial.println("ERROR: Failed to set AES encryption key");
        mbedtls_aes_free(&aes);
        free(paddedPlaintext);
        return "";
    }
    
    uint8_t* ciphertext = (uint8_t*)malloc(paddedLen);
    if (!ciphertext) {
        Serial.println("ERROR: Failed to allocate memory for ciphertext");
        mbedtls_aes_free(&aes);
        free(paddedPlaintext);
        return "";
    }
    
    if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, iv, paddedPlaintext, ciphertext) != 0) {
        Serial.println("ERROR: AES encryption failed");
        mbedtls_aes_free(&aes);
        free(paddedPlaintext);
        free(ciphertext);
        return "";
    }
    
    mbedtls_aes_free(&aes);
    free(paddedPlaintext);
    
    // Combine IV + ciphertext and base64 encode
    size_t totalLen = 16 + paddedLen;
    uint8_t* combined = (uint8_t*)malloc(totalLen);
    if (!combined) {
        Serial.println("ERROR: Failed to allocate memory for combined data");
        free(ciphertext);
        return "";
    }
    
    memcpy(combined, iv, 16);
    memcpy(combined + 16, ciphertext, paddedLen);
    free(ciphertext);
    
    // Base64 encode
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t base64Len = ((totalLen + 2) / 3) * 4;
    char* base64 = (char*)malloc(base64Len + 1);
    if (!base64) {
        Serial.println("ERROR: Failed to allocate memory for base64");
        free(combined);
        return "";
    }
    
    size_t base64Idx = 0;
    for (size_t i = 0; i < totalLen; i += 3) {
        uint32_t value = (combined[i] << 16) | ((i + 1 < totalLen ? combined[i + 1] : 0) << 8) | (i + 2 < totalLen ? combined[i + 2] : 0);
        base64[base64Idx++] = base64_chars[(value >> 18) & 0x3F];
        base64[base64Idx++] = base64_chars[(value >> 12) & 0x3F];
        base64[base64Idx++] = (i + 1 < totalLen) ? base64_chars[(value >> 6) & 0x3F] : '=';
        base64[base64Idx++] = (i + 2 < totalLen) ? base64_chars[value & 0x3F] : '=';
    }
    base64[base64Idx] = '\0';
    
    free(combined);
    String result = String(base64);
    free(base64);
    
    return result;
}

String encryptAndFormatMessage(const String& plaintext) {
    if (!isWebUIPasswordSet()) {
        Serial.println("ERROR: Cannot format message - password not set");
        return "";
    }
    
    // Check if encryption is enabled
    bool useEncryption = isEncryptionEnabled();
    String payload;
    String iv;
    
    if (useEncryption) {
        // Get password from NVS
        if (!authPrefs.begin("webui_auth", true)) {
            Serial.println("ERROR: Failed to open NVS for encryption");
            return "";
        }
        String password = authPrefs.getString("password", "");
        authPrefs.end();
        
        if (password.length() == 0) {
            Serial.println("ERROR: Password is empty");
            return "";
        }
        
        // Derive encryption key from password
        uint8_t dummyHmacKey[32];
        uint8_t encryptionKey[32];
        if (!deriveKeysFromPassword(password, dummyHmacKey, encryptionKey)) {
            Serial.println("ERROR: Failed to derive encryption key");
            return "";
        }
        
        // Generate random IV (16 bytes for AES-256 CBC)
        uint8_t ivBytes[16];
        esp_fill_random(ivBytes, 16);
        
        // Save a copy of the original IV (mbedtls_aes_crypt_cbc modifies it in place)
        uint8_t ivOriginal[16];
        memcpy(ivOriginal, ivBytes, 16);
        
        // Prepare plaintext
        size_t plaintextLen = plaintext.length();
        size_t paddedLen = ((plaintextLen + 15) / 16) * 16;  // PKCS7 padding
        uint8_t* paddedPlaintext = (uint8_t*)malloc(paddedLen);
        if (!paddedPlaintext) {
            Serial.println("ERROR: Failed to allocate memory for padded plaintext");
            return "";
        }
        
        memcpy(paddedPlaintext, plaintext.c_str(), plaintextLen);
        // PKCS7 padding
        uint8_t padValue = paddedLen - plaintextLen;
        for (size_t i = plaintextLen; i < paddedLen; i++) {
            paddedPlaintext[i] = padValue;
        }
        
        // Encrypt using AES-256-CBC
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        
        if (mbedtls_aes_setkey_enc(&aes, encryptionKey, 256) != 0) {
            Serial.println("ERROR: Failed to set AES encryption key");
            mbedtls_aes_free(&aes);
            free(paddedPlaintext);
            return "";
        }
        
        uint8_t* ciphertext = (uint8_t*)malloc(paddedLen);
        if (!ciphertext) {
            Serial.println("ERROR: Failed to allocate memory for ciphertext");
            mbedtls_aes_free(&aes);
            free(paddedPlaintext);
            return "";
        }
        
        if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, paddedLen, ivBytes, paddedPlaintext, ciphertext) != 0) {
            Serial.println("ERROR: AES encryption failed");
            mbedtls_aes_free(&aes);
            free(paddedPlaintext);
            free(ciphertext);
            return "";
        }
        
        mbedtls_aes_free(&aes);
        free(paddedPlaintext);
        
        // Base64 encode IV and ciphertext separately using mbedTLS
        // Use the ORIGINAL IV (before mbedtls_aes_crypt_cbc modified it)
        // Encode IV (16 bytes -> 24 base64 chars + null terminator)
        char ivBase64[25];
        size_t ivBase64Len = 0;
        if (mbedtls_base64_encode((unsigned char*)ivBase64, 25, &ivBase64Len, ivOriginal, 16) != 0) {
            Serial.println("ERROR: Failed to base64 encode IV");
            free(ciphertext);
            return "";
        }
        ivBase64[ivBase64Len] = '\0';
        iv = String(ivBase64);
        
        // Encode ciphertext
        size_t ciphertextBase64Len = ((paddedLen + 2) / 3) * 4 + 1;
        char* ciphertextBase64 = (char*)malloc(ciphertextBase64Len);
        if (!ciphertextBase64) {
            Serial.println("ERROR: Failed to allocate memory for ciphertext base64");
            free(ciphertext);
            return "";
        }
        
        size_t ciphertextBase64ActualLen = 0;
        if (mbedtls_base64_encode((unsigned char*)ciphertextBase64, ciphertextBase64Len, &ciphertextBase64ActualLen, ciphertext, paddedLen) != 0) {
            Serial.println("ERROR: Failed to base64 encode ciphertext");
            free(ciphertext);
            free(ciphertextBase64);
            return "";
        }
        ciphertextBase64[ciphertextBase64ActualLen] = '\0';
        payload = String(ciphertextBase64);
        
        free(ciphertext);
        free(ciphertextBase64);
    } else {
        // Encryption disabled: just base64 encode the plaintext
        payload = base64Encode(plaintext);
        if (payload.length() == 0) {
            Serial.println("ERROR: Failed to base64 encode plaintext");
            return "";
        }
    }
    
    // Build message JSON (without hmac field) for HMAC computation
    String messageForHMAC;
    if (useEncryption) {
        messageForHMAC.reserve(50 + iv.length() + payload.length());
        messageForHMAC = "{\"encrypted\":true,\"iv\":\"";
        messageForHMAC += iv;
        messageForHMAC += "\",\"payload\":\"";
        messageForHMAC += payload;
        messageForHMAC += "\"}";
    } else {
        messageForHMAC.reserve(40 + payload.length());
        messageForHMAC = "{\"encrypted\":false,\"payload\":\"";
        messageForHMAC += payload;
        messageForHMAC += "\"}";
    }
    
    // Compute HMAC (always required)
    String hmac = computeHMAC(messageForHMAC);
    if (hmac.length() == 0) {
        Serial.println("ERROR: Failed to compute HMAC for message");
        return "";
    }
    
    // Build final JSON with HMAC
    String result;
    result.reserve(messageForHMAC.length() + 20 + hmac.length());
    if (useEncryption) {
        result = "{\"encrypted\":true,\"iv\":\"";
        result += iv;
        result += "\",\"payload\":\"";
        result += payload;
        result += "\",\"hmac\":\"";
        result += hmac;
        result += "\"}";
    } else {
        result = "{\"encrypted\":false,\"payload\":\"";
        result += payload;
        result += "\",\"hmac\":\"";
        result += hmac;
        result += "\"}";
    }
    
    return result;
}

String decryptMessage(const String& ciphertext) {
    if (!isWebUIPasswordSet()) {
        Serial.println("ERROR: Cannot decrypt - password not set");
        return "";
    }
    
    // Get password from NVS
    if (!authPrefs.begin("webui_auth", true)) {
        Serial.println("ERROR: Failed to open NVS for decryption");
        return "";
    }
    String password = authPrefs.getString("password", "");
    authPrefs.end();
    
    if (password.length() == 0) {
        Serial.println("ERROR: Password is empty");
        return "";
    }
    
    // Derive encryption key from password
    uint8_t dummyHmacKey[32];
    uint8_t encryptionKey[32];
    if (!deriveKeysFromPassword(password, dummyHmacKey, encryptionKey)) {
        Serial.println("ERROR: Failed to derive encryption key");
        return "";
    }
    
    // Base64 decode
    // Remove any whitespace/newlines from base64 string
    String cleanCiphertext = ciphertext;
    cleanCiphertext.replace("\n", "");
    cleanCiphertext.replace("\r", "");
    cleanCiphertext.replace(" ", "");
    cleanCiphertext.trim();
    
    size_t ciphertextLen = cleanCiphertext.length();
    Serial.printf("Base64 input length: %d bytes (after cleaning)\n", ciphertextLen);
    
    // Base64 encoding: 4 chars -> 3 bytes, but we need to account for padding
    // Calculate max possible decoded length
    size_t decodedLen = ((ciphertextLen + 3) / 4) * 3;
    uint8_t* decoded = (uint8_t*)malloc(decodedLen);
    if (!decoded) {
        Serial.println("ERROR: Failed to allocate memory for decoded data");
        return "";
    }
    
    size_t decodedIdx = 0;
    for (size_t i = 0; i < ciphertextLen; i += 4) {
        if (i + 4 > ciphertextLen) {
            // Handle incomplete last group
            break;
        }
        
        uint32_t value = 0;
        int padding = 0;
        
        for (int j = 0; j < 4; j++) {
            char c = cleanCiphertext.charAt(i + j);
            if (c == '=') {
                padding++;
                value <<= 6;
            } else if (c >= 'A' && c <= 'Z') {
                value = (value << 6) | (c - 'A');
            } else if (c >= 'a' && c <= 'z') {
                value = (value << 6) | (c - 'a' + 26);
            } else if (c >= '0' && c <= '9') {
                value = (value << 6) | (c - '0' + 52);
            } else if (c == '+') {
                value = (value << 6) | 62;
            } else if (c == '/') {
                value = (value << 6) | 63;
            } else {
                // Invalid character - skip this group
                Serial.printf("ERROR: Invalid base64 character at position %d: '%c' (0x%02x)\n", i + j, c, c);
                free(decoded);
                return "";
            }
        }
        
        int bytes = 3 - padding;
        for (int j = 0; j < bytes && decodedIdx < decodedLen; j++) {
            decoded[decodedIdx++] = (value >> (8 * (2 - j))) & 0xFF;
        }
    }
    
    Serial.printf("Base64 decoded: %d bytes input -> %d bytes output\n", ciphertextLen, decodedIdx);
    
    if (decodedIdx < 16) {
        Serial.printf("ERROR: Decoded data too short (need at least 16 bytes for IV, got %d)\n", decodedIdx);
        free(decoded);
        return "";
    }
    
    // Extract IV (first 16 bytes)
    uint8_t iv[16];
    memcpy(iv, decoded, 16);
    
    size_t ciphertextDataLen = decodedIdx - 16;
    Serial.printf("Ciphertext data length: %d bytes (after removing 16-byte IV)\n", ciphertextDataLen);
    
    if (ciphertextDataLen % 16 != 0) {
        Serial.printf("ERROR: Ciphertext length not multiple of 16 bytes (got %d bytes, remainder: %d)\n", 
                     ciphertextDataLen, ciphertextDataLen % 16);
        free(decoded);
        return "";
    }
    
    // Decrypt using AES-256-CBC
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    
    if (mbedtls_aes_setkey_dec(&aes, encryptionKey, 256) != 0) {
        Serial.println("ERROR: Failed to set AES decryption key");
        mbedtls_aes_free(&aes);
        free(decoded);
        return "";
    }
    
    uint8_t* plaintext = (uint8_t*)malloc(ciphertextDataLen);
    if (!plaintext) {
        Serial.println("ERROR: Failed to allocate memory for plaintext");
        mbedtls_aes_free(&aes);
        free(decoded);
        return "";
    }
    
    if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ciphertextDataLen, iv, decoded + 16, plaintext) != 0) {
        Serial.println("ERROR: AES decryption failed");
        mbedtls_aes_free(&aes);
        free(decoded);
        free(plaintext);
        return "";
    }
    
    mbedtls_aes_free(&aes);
    free(decoded);
    
    // Remove PKCS7 padding
    if (ciphertextDataLen == 0) {
        free(plaintext);
        return "";
    }
    
    uint8_t padValue = plaintext[ciphertextDataLen - 1];
    if (padValue == 0 || padValue > 16) {
        Serial.println("ERROR: Invalid padding value");
        free(plaintext);
        return "";
    }
    
    size_t plaintextLen = ciphertextDataLen - padValue;
    String result = String((char*)plaintext, plaintextLen);
    free(plaintext);
    
    return result;
}

bool isWebUIPasswordSet() {
    if (!authPrefs.begin("webui_auth", true)) {
        return false;
    }
    String password = authPrefs.getString("password", "");
    authPrefs.end();
    return (password.length() > 0);
}

bool setWebUIPassword(const String& password) {
    if (password.length() == 0) {
        Serial.println("ERROR: Cannot set empty password");
        return false;
    }
    
    if (password.length() < 8) {
        Serial.println("WARNING: Password is less than 8 characters - consider using a stronger password");
    }
    
    if (!authPrefs.begin("webui_auth", false)) {
        Serial.println("ERROR: Failed to open NVS for password storage");
        return false;
    }
    
    bool success = authPrefs.putString("password", password);
    authPrefs.end();
    
    if (success) {
        Serial.println("Web UI password set successfully (stored as HMAC key)");
    } else {
        Serial.println("ERROR: Failed to store password in NVS");
    }
    
    return success;
}

void requireWebUIPasswordSetup() {
    if (!isWebUIPasswordSet()) {
        Serial.println("\n========================================");
        Serial.println("CRITICAL: Web UI password not configured!");
        Serial.println("========================================");
        Serial.println("The GitHub Pages web UI will NOT work until a password is set.");
        Serial.println("To set the password:");
        Serial.println("  1. Connect to the device's local WiFi UI");
        Serial.println("  2. Navigate to Settings > Web UI Password");
        Serial.println("  3. Set a password (minimum 8 characters recommended)");
        Serial.println("  4. The password will be used as HMAC key for message signing");
        Serial.println("  5. Password is NEVER transmitted - only HMAC signatures are sent");
        Serial.println("========================================\n");
    } else {
        if (g_is_cold_boot) {
            Serial.println("Web UI password is configured - GitHub Pages UI is enabled");
        }
    }
}

bool isEncryptionEnabled() {
    if (!authPrefs.begin("webui_auth", true)) {
        // Default to true (encryption enabled) if NVS access fails
        return true;
    }
    // Default to true if key doesn't exist (backward compatible)
    bool enabled = authPrefs.getBool("encryption_enabled", true);
    authPrefs.end();
    return enabled;
}

bool setEncryptionEnabled(bool enabled) {
    if (!authPrefs.begin("webui_auth", false)) {
        Serial.println("ERROR: Failed to open NVS for encryption setting storage");
        return false;
    }
    
    bool success = authPrefs.putBool("encryption_enabled", enabled);
    authPrefs.end();
    
    if (success) {
        Serial.printf("Encryption %s for MQTT messages\n", enabled ? "enabled" : "disabled");
    } else {
        Serial.println("ERROR: Failed to store encryption setting in NVS");
    }
    
    return success;
}

String base64Encode(const String& plaintext) {
    if (plaintext.length() == 0) {
        return "";
    }
    
    // Use mbedTLS base64 encoding
    size_t plaintextLen = plaintext.length();
    size_t base64Len = ((plaintextLen + 2) / 3) * 4 + 1;
    char* base64 = (char*)malloc(base64Len);
    if (!base64) {
        Serial.println("ERROR: Failed to allocate memory for base64 encoding");
        return "";
    }
    
    size_t actualLen = 0;
    if (mbedtls_base64_encode((unsigned char*)base64, base64Len, &actualLen, 
                              (const unsigned char*)plaintext.c_str(), plaintextLen) != 0) {
        Serial.println("ERROR: Failed to base64 encode");
        free(base64);
        return "";
    }
    
    base64[actualLen] = '\0';
    String result = String(base64);
    free(base64);
    return result;
}

String base64Decode(const String& encoded) {
    if (encoded.length() == 0) {
        return "";
    }
    
    // Remove whitespace
    String cleanEncoded = encoded;
    cleanEncoded.replace("\n", "");
    cleanEncoded.replace("\r", "");
    cleanEncoded.replace(" ", "");
    cleanEncoded.trim();
    
    size_t encodedLen = cleanEncoded.length();
    if (encodedLen == 0) {
        return "";
    }
    
    // Calculate max decoded length
    size_t decodedLen = ((encodedLen + 3) / 4) * 3;
    uint8_t* decoded = (uint8_t*)malloc(decodedLen);
    if (!decoded) {
        Serial.println("ERROR: Failed to allocate memory for base64 decoding");
        return "";
    }
    
    size_t actualLen = 0;
    if (mbedtls_base64_decode(decoded, decodedLen, &actualLen,
                              (const unsigned char*)cleanEncoded.c_str(), encodedLen) != 0) {
        Serial.println("ERROR: Failed to base64 decode");
        free(decoded);
        return "";
    }
    
    String result = String((char*)decoded, actualLen);
    free(decoded);
    return result;
}


