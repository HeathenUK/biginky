/**
 * @file webui_crypto.h
 * @brief Web UI encryption and authentication functions
 * 
 * Provides encryption, decryption, HMAC validation, and password management
 * for the GitHub Pages web UI interface.
 */

#ifndef WEBUI_CRYPTO_H
#define WEBUI_CRYPTO_H

#include <Arduino.h>

/**
 * Derive HMAC and encryption keys from a password using HMAC-SHA256
 * @param password The password to derive keys from
 * @param hmacKey Output buffer for HMAC key (32 bytes)
 * @param encryptionKey Output buffer for encryption key (32 bytes)
 * @return true on success, false on failure
 */
bool deriveKeysFromPassword(const String& password, uint8_t* hmacKey, uint8_t* encryptionKey);

/**
 * Compute HMAC-SHA256 of a message using derived HMAC key
 * @param message The message to compute HMAC for
 * @return Hex-encoded HMAC (64 characters) or empty string on failure
 */
String computeHMAC(const String& message);

/**
 * Validate HMAC signature of a message
 * @param message The message to validate
 * @param providedHMAC The HMAC signature to validate against
 * @return true if HMAC matches, false otherwise
 */
bool validateWebUIHMAC(const String& message, const String& providedHMAC);

/**
 * Encrypt a message using AES-256-CBC
 * @param plaintext The message to encrypt
 * @return Base64-encoded ciphertext with IV prepended, or empty string on failure
 */
String encryptMessage(const String& plaintext);

/**
 * Encrypt and format a message as JSON
 * @param plaintext The message to encrypt
 * @return JSON string with format: {"encrypted":true,"iv":"...","payload":"...","hmac":"..."}
 */
String encryptAndFormatMessage(const String& plaintext);

/**
 * Decrypt a message using AES-256-CBC
 * @param ciphertext Base64-encoded ciphertext with IV prepended
 * @return Decrypted plaintext or empty string on failure
 */
String decryptMessage(const String& ciphertext);

/**
 * Check if Web UI password is configured
 * @return true if password is set, false otherwise
 */
bool isWebUIPasswordSet();

/**
 * Set a new Web UI password
 * @param password The password to set (minimum 8 characters recommended)
 * @return true on success, false on failure
 */
bool setWebUIPassword(const String& password);

/**
 * Require password setup at boot if not configured
 * Prints warning messages if password is not set
 */
void requireWebUIPasswordSetup();

#endif // WEBUI_CRYPTO_H


