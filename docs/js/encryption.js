// Encryption and password management module

// Initialize session encryption key (derived from session ID)
async function initSessionEncryptionKey() {
    if (sessionEncryptionKey) return sessionEncryptionKey;
    
    // Generate a session-specific key from a random value stored in sessionStorage
    let sessionId = sessionStorage.getItem('biginky_session_id');
    if (!sessionId) {
        // Generate new session ID
        sessionId = Array.from(crypto.getRandomValues(new Uint8Array(32)))
            .map(b => b.toString(16).padStart(2, '0')).join('');
        sessionStorage.setItem('biginky_session_id', sessionId);
    }
    
    // Derive encryption key from session ID using PBKDF2
    const encoder = new TextEncoder();
    const sessionIdData = encoder.encode(sessionId);
    const salt = encoder.encode('biginky_session_salt_v1');
    
    const keyMaterial = await crypto.subtle.importKey(
        'raw',
        sessionIdData,
        { name: 'PBKDF2' },
        false,
        ['deriveBits', 'deriveKey']
    );
    
    sessionEncryptionKey = await crypto.subtle.deriveKey(
        {
            name: 'PBKDF2',
            salt: salt,
            iterations: 100000,
            hash: 'SHA-256'
        },
        keyMaterial,
        { name: 'AES-GCM', length: 256 },
        false,
        ['encrypt', 'decrypt']
    );
    
    return sessionEncryptionKey;
}

// Encrypt password for storage
async function encryptPasswordForStorage(password) {
    const key = await initSessionEncryptionKey();
    const encoder = new TextEncoder();
    const passwordData = encoder.encode(password);
    
    // Generate random IV for GCM
    const iv = crypto.getRandomValues(new Uint8Array(12));
    
    // Encrypt
    const encrypted = await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv: iv },
        key,
        passwordData
    );
    
    // Combine IV + ciphertext and base64 encode
    const combined = new Uint8Array(iv.length + encrypted.byteLength);
    combined.set(iv, 0);
    combined.set(new Uint8Array(encrypted), iv.length);
    
    // Convert to base64 (avoid spread operator for large arrays)
    let binaryString = '';
    for (let i = 0; i < combined.length; i++) {
        binaryString += String.fromCharCode(combined[i]);
    }
    return btoa(binaryString);
}

// Decrypt password from storage
async function decryptPasswordFromStorage(encryptedBase64) {
    try {
        const key = await initSessionEncryptionKey();
        
        // Decode base64
        const combined = Uint8Array.from(atob(encryptedBase64), c => c.charCodeAt(0));
        
        // Extract IV and ciphertext
        const iv = combined.slice(0, 12);
        const ciphertext = combined.slice(12);
        
        // Decrypt
        const decrypted = await crypto.subtle.decrypt(
            { name: 'AES-GCM', iv: iv },
            key,
            ciphertext
        );
        
        // Convert to string
        const decoder = new TextDecoder();
        return decoder.decode(decrypted);
    } catch (e) {
        console.error('Failed to decrypt password:', e);
        return null;
    }
}

// Derive HMAC key from password using HMAC-SHA256 with salt
async function deriveHMACKey(password) {
    const encoder = new TextEncoder();
    const salt = encoder.encode('biginky_hmac_key_v1');
    const passwordData = encoder.encode(password);
    
    // Import password as HMAC key
    const key = await crypto.subtle.importKey(
        'raw',
        passwordData,
        { name: 'HMAC', hash: 'SHA-256' },
        false,
        ['sign']
    );
    
    // Compute HMAC(password, salt) to derive key
    const derivedKey = await crypto.subtle.sign('HMAC', key, salt);
    return new Uint8Array(derivedKey);
}

// Derive encryption key from password using HMAC-SHA256 with salt
async function deriveEncryptionKey(password) {
    const encoder = new TextEncoder();
    const salt = encoder.encode('biginky_enc_key_v1');
    const passwordData = encoder.encode(password);
    
    // Debug: log password length and first few bytes
    console.log('deriveEncryptionKey: password length =', password.length);
    console.log('deriveEncryptionKey: password bytes (first 20) =', Array.from(passwordData.slice(0, 20)).map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
    console.log('deriveEncryptionKey: salt =', Array.from(salt).map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
    
    // Import password as HMAC key
    const key = await crypto.subtle.importKey(
        'raw',
        passwordData,
        { name: 'HMAC', hash: 'SHA-256' },
        false,
        ['sign']
    );
    
    // Compute HMAC(password, salt) to derive key
    const derivedKey = await crypto.subtle.sign('HMAC', key, salt);
    const keyArray = new Uint8Array(derivedKey);
    
    // Ensure we have exactly 32 bytes (256 bits) for AES-256
    // HMAC-SHA256 always produces 32 bytes, but let's be explicit
    const finalKey = keyArray.length === 32 ? keyArray : keyArray.slice(0, 32);
    
    // Debug: log derived key length and first 16 bytes
    console.log('deriveEncryptionKey: derived key length =', keyArray.length, 'bytes');
    console.log('deriveEncryptionKey: final key length =', finalKey.length, 'bytes');
    console.log('deriveEncryptionKey: derived key (first 16 bytes) =', Array.from(finalKey.slice(0, 16)).map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
    
    return finalKey;
}

// Encrypt a message using AES-256-CBC
async function encryptMessage(plaintext) {
    if (!webUIPassword) {
        console.error('No password configured for encryption');
        return null;
    }
    
    try {
        // Derive encryption key from password
        const encryptionKey = await deriveEncryptionKey(webUIPassword);
        
        // Ensure key is exactly 32 bytes (256 bits) for AES-256
        if (encryptionKey.length !== 32) {
            console.error('Invalid encryption key length:', encryptionKey.length, '(expected 32 bytes for AES-256)');
            return null;
        }
        
        // Import key for AES-CBC
        const key = await crypto.subtle.importKey(
            'raw',
            encryptionKey,
            { name: 'AES-CBC', length: 256 },
            false,
            ['encrypt']
        );
        
        // Generate random IV (16 bytes)
        const iv = crypto.getRandomValues(new Uint8Array(16));
        
        // Encrypt
        const encoder = new TextEncoder();
        const plaintextData = encoder.encode(plaintext);
        const ciphertext = await crypto.subtle.encrypt(
            { name: 'AES-CBC', iv: iv },
            key,
            plaintextData
        );
        
        // Combine IV + ciphertext and base64 encode
        const combined = new Uint8Array(16 + ciphertext.byteLength);
        combined.set(iv, 0);
        combined.set(new Uint8Array(ciphertext), 16);
        
        // Base64 encode
        const binaryString = String.fromCharCode.apply(null, combined);
        const base64 = btoa(binaryString);
        
        return base64;
    } catch (e) {
        console.error('Encryption error:', e);
        return null;
    }
}

// Decrypt a message using AES-256-CBC
// Accepts either: (ciphertextBase64) for legacy format with IV prepended
//                 or (payloadBase64, ivBase64) for new format with separate IV
async function decryptMessage(payloadBase64, ivBase64) {
    if (!webUIPassword) {
        console.error('No password configured for decryption');
        return null;
    }
    
    // Diagnostic: Log password info (first few chars only for security)
    console.log('decryptMessage: password length =', webUIPassword ? webUIPassword.length : 0);
    console.log('decryptMessage: password preview =', webUIPassword ? webUIPassword.substring(0, 3) + '...' : 'null');
    
    try {
        let iv, ciphertext;
        
        // Diagnostic: Log payload base64 length
        console.log('decryptMessage: payloadBase64 length:', payloadBase64 ? payloadBase64.length : 0);
        console.log('decryptMessage: ivBase64 length:', ivBase64 ? ivBase64.length : 0);
        
        if (ivBase64) {
            // New format: IV and payload are separate
            // Remove any whitespace/newlines from base64 strings
            const cleanIvBase64 = ivBase64.replace(/\s/g, '');
            const cleanPayloadBase64 = payloadBase64.replace(/\s/g, '');
            
            const ivBinaryString = atob(cleanIvBase64);
            iv = new Uint8Array(ivBinaryString.length);
            for (let i = 0; i < ivBinaryString.length; i++) {
                iv[i] = ivBinaryString.charCodeAt(i);
            }
            
            const payloadBinaryString = atob(cleanPayloadBase64);
            ciphertext = new Uint8Array(payloadBinaryString.length);
            for (let i = 0; i < payloadBinaryString.length; i++) {
                ciphertext[i] = payloadBinaryString.charCodeAt(i);
            }
        } else {
            // Legacy format: IV is prepended to ciphertext
            // Remove any whitespace/newlines from base64 string
            const cleanPayloadBase64 = payloadBase64.replace(/\s/g, '');
            const binaryString = atob(cleanPayloadBase64);
            const combined = new Uint8Array(binaryString.length);
            for (let i = 0; i < binaryString.length; i++) {
                combined[i] = binaryString.charCodeAt(i);
            }
            
            // Extract IV (first 16 bytes) and ciphertext
            iv = combined.slice(0, 16);
            ciphertext = combined.slice(16);
        }
        
        // Diagnostic: Log decoded lengths
        console.log('decryptMessage: IV length (decoded):', iv.length);
        console.log('decryptMessage: ciphertext length (decoded):', ciphertext.length);
        console.log('decryptMessage: ciphertext length % 16:', ciphertext.length % 16);
        
        // Validate IV length
        if (iv.length !== 16) {
            console.error('Invalid IV length:', iv.length, '(expected 16)');
            return null;
        }
        
        // Validate ciphertext is not empty
        if (ciphertext.length === 0) {
            console.error('Ciphertext is empty');
            return null;
        }
        
        // Validate ciphertext length is multiple of 16 (AES block size)
        if (ciphertext.length % 16 !== 0) {
            console.error('Invalid ciphertext length (not multiple of 16):', ciphertext.length, 'remainder:', ciphertext.length % 16);
            console.error('This will cause Web Crypto API to fail. Ciphertext may be truncated or corrupted.');
            return null;
        }
        
        // Derive encryption key from password
        const encryptionKey = await deriveEncryptionKey(webUIPassword);
        
        // Ensure key is exactly 32 bytes (256 bits) for AES-256
        if (encryptionKey.length !== 32) {
            console.error('Invalid encryption key length:', encryptionKey.length, '(expected 32 bytes for AES-256)');
            return null;
        }
        
        // Import key for AES-CBC
        const key = await crypto.subtle.importKey(
            'raw',
            encryptionKey,
            { name: 'AES-CBC', length: 256 },
            false,
            ['decrypt']
        );
        
        // Decrypt
        let plaintext;
        try {
            console.log('decryptMessage: Attempting decryption with ciphertext length:', ciphertext.length, 'bytes');
            plaintext = await crypto.subtle.decrypt(
                { name: 'AES-CBC', iv: iv },
                key,
                ciphertext
            );
            console.log('decryptMessage: Decryption successful, plaintext length:', plaintext.byteLength, 'bytes');
        } catch (decryptError) {
            console.error('crypto.subtle.decrypt failed:', decryptError);
            console.error('Decryption failure details:');
            console.error('  - IV length:', iv.length);
            console.error('  - Ciphertext length:', ciphertext.length);
            console.error('  - Ciphertext length % 16:', ciphertext.length % 16);
            console.error('  - Error name:', decryptError.name);
            console.error('  - Error message:', decryptError.message);
            
            // Fallback: try multiple strategies to recover from decryption failure
            // This handles cases where the ciphertext might have extra blocks or padding issues
            let fallbackSucceeded = false;
            
            // Strategy 1: Try removing last block (16 bytes)
            if (ciphertext.length >= 32 && !fallbackSucceeded) {
                console.log('decryptMessage: Attempting fallback 1 - removing last block (16 bytes)');
                try {
                    const fallbackCiphertext = ciphertext.slice(0, ciphertext.length - 16);
                    plaintext = await crypto.subtle.decrypt(
                        { name: 'AES-CBC', iv: iv },
                        key,
                        fallbackCiphertext
                    );
                    console.log('decryptMessage: Fallback 1 successful, plaintext length:', plaintext.byteLength, 'bytes');
                    fallbackSucceeded = true;
                } catch (fallbackError) {
                    console.log('Fallback 1 failed, trying next strategy...');
                }
            }
            
            // Strategy 2: Try removing last 2 blocks (32 bytes)
            if (ciphertext.length >= 48 && !fallbackSucceeded) {
                console.log('decryptMessage: Attempting fallback 2 - removing last 2 blocks (32 bytes)');
                try {
                    const fallbackCiphertext = ciphertext.slice(0, ciphertext.length - 32);
                    plaintext = await crypto.subtle.decrypt(
                        { name: 'AES-CBC', iv: iv },
                        key,
                        fallbackCiphertext
                    );
                    console.log('decryptMessage: Fallback 2 successful, plaintext length:', plaintext.byteLength, 'bytes');
                    fallbackSucceeded = true;
                } catch (fallbackError) {
                    console.log('Fallback 2 failed, trying next strategy...');
                }
            }
            
            // Strategy 3: Try removing bytes that aren't a multiple of 16 (1-15 bytes)
            if (ciphertext.length >= 17 && !fallbackSucceeded) {
                for (let removeBytes = 1; removeBytes <= 15 && !fallbackSucceeded; removeBytes++) {
                    const testLength = ciphertext.length - removeBytes;
                    if (testLength > 0 && testLength % 16 === 0) {
                        console.log(`decryptMessage: Attempting fallback 3 - removing ${removeBytes} bytes`);
                        try {
                            const fallbackCiphertext = ciphertext.slice(0, testLength);
                            plaintext = await crypto.subtle.decrypt(
                                { name: 'AES-CBC', iv: iv },
                                key,
                                fallbackCiphertext
                            );
                            console.log(`decryptMessage: Fallback 3 successful (removed ${removeBytes} bytes), plaintext length:`, plaintext.byteLength, 'bytes');
                            fallbackSucceeded = true;
                            break;
                        } catch (fallbackError) {
                            // Continue trying
                        }
                    }
                }
            }
            
            if (!fallbackSucceeded) {
                console.error('All fallback decryption strategies failed');
                // This usually means the key is wrong or the ciphertext is corrupted
                return null;
            }
        }
        
        // Remove PKCS7 padding from ArrayBuffer BEFORE converting to string
        // Padding bytes might not be valid UTF-8, so we must remove them first
        const plaintextArray = new Uint8Array(plaintext);
        if (plaintextArray.length === 0) {
            console.error('Decrypted plaintext is empty');
            return null;
        }
        
        // Debug: log first few bytes of decrypted data
        const debugBytes = Array.from(plaintextArray.slice(0, Math.min(20, plaintextArray.length)));
        console.log('Decrypted bytes (first 20):', debugBytes.map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
        
        // Helper function to check if a Uint8Array contains valid JSON
        const tryDecodeAsJSON = (dataArray) => {
            try {
                const decoder = new TextDecoder('utf-8', { fatal: false });
                const jsonStr = decoder.decode(dataArray);
                if (jsonStr.trim().startsWith('{') && jsonStr.trim().endsWith('}')) {
                    JSON.parse(jsonStr);  // Validate it's actually valid JSON
                    return jsonStr;
                }
            } catch (e) {
                // Not valid JSON
            }
            return null;
        };
        
        // Get padding value (last byte)
        const padValue = plaintextArray[plaintextArray.length - 1];
        
        // Try to remove padding
        let unpaddedArray;
        let paddingValid = false;
        
        // Validate padding (must be between 1 and 16)
        if (padValue >= 1 && padValue <= 16) {
            // Verify all padding bytes are the same
            const paddingStart = plaintextArray.length - padValue;
            if (paddingStart >= 0) {
                let allSame = true;
                for (let i = paddingStart; i < plaintextArray.length; i++) {
                    if (plaintextArray[i] !== padValue) {
                        allSame = false;
                        break;
                    }
                }
                if (allSame) {
                    // Valid padding - remove it
                    unpaddedArray = plaintextArray.slice(0, paddingStart);
                    paddingValid = true;
                }
            }
        }
        
        // If padding validation failed, try fallback: check if data looks like valid JSON
        if (!paddingValid) {
            // Graceful fallback: if the last byte is a curly brace '}' (0x7d), it might be part of the JSON
            // payload rather than padding. Try to find valid JSON by checking different padding scenarios.
            const firstByte = plaintextArray[0];
            const lastByte = plaintextArray[plaintextArray.length - 1];
            
            // Check if data looks like JSON (starts with '{' and/or ends with '}')
            const looksLikeJSON = firstByte === 0x7b || lastByte === 0x7d;  // '{' or '}'
            
            if (looksLikeJSON) {
                // Try to find actual padding by checking from the end
                // Try common padding values (1-16) and see if removing that many bytes gives valid JSON
                let foundValidPadding = false;
                for (let tryPad = 1; tryPad <= 16 && !foundValidPadding; tryPad++) {
                    const tryStart = plaintextArray.length - tryPad;
                    if (tryStart > 0) {
                        const tryUnpadded = plaintextArray.slice(0, tryStart);
                        // Check if all padding bytes are the same
                        let allSame = true;
                        for (let i = tryStart; i < plaintextArray.length; i++) {
                            if (plaintextArray[i] !== tryPad) {
                                allSame = false;
                                break;
                            }
                        }
                        if (allSame) {
                            // Try to decode as JSON
                            const jsonStr = tryDecodeAsJSON(tryUnpadded);
                            if (jsonStr) {
                                // Valid JSON! Use this padding
                                unpaddedArray = tryUnpadded;
                                paddingValid = true;
                                foundValidPadding = true;
                                console.log('Padding validation fallback succeeded with padding value:', tryPad);
                            }
                        }
                    }
                }
                
                // If last byte is '}' and we haven't found valid padding, try treating it as part of JSON
                // This handles the case where the final char is a curly brace rather than padding
                if (!foundValidPadding && lastByte === 0x7d) {
                    const jsonStr = tryDecodeAsJSON(plaintextArray);
                    if (jsonStr) {
                        // Valid JSON! Use it as-is
                        unpaddedArray = plaintextArray;
                        paddingValid = true;
                        console.log('Graceful fallback: treating final curly brace as part of JSON payload');
                    }
                }
            }
            
            // If still no valid padding found, try to decode anyway
            if (!paddingValid) {
                const jsonStr = tryDecodeAsJSON(plaintextArray);
                if (jsonStr) {
                    // Valid JSON - use it as-is (Web Crypto API already removed padding)
                    unpaddedArray = plaintextArray;
                    paddingValid = true;
                } else {
                    // Only log error if JSON parsing also fails
                    console.error('Invalid padding value:', padValue, '(expected 1-16)');
                    console.error('Plaintext length:', plaintextArray.length);
                    console.error('Last 16 bytes:', Array.from(plaintextArray.slice(-16)).map(b => '0x' + b.toString(16).padStart(2, '0')).join(' '));
                    console.error('Decryption returned empty result - data is not valid JSON');
                    return null;
                }
            }
        }
        
        // Convert to string
        const decoder = new TextDecoder('utf-8', { fatal: false });
        const decoded = decoder.decode(unpaddedArray);
        
        return decoded;
    } catch (e) {
        console.error('Decryption error:', e);
        return null;
    }
}

// Compute HMAC-SHA256 of a message using the derived HMAC key
async function computeHMAC(message) {
    if (!webUIPassword) {
        console.error('No password configured for HMAC computation');
        return null;
    }
    
    try {
        // Derive HMAC key from password
        const hmacKey = await deriveHMACKey(webUIPassword);
        
        // Import derived key
        const key = await crypto.subtle.importKey(
            'raw',
            hmacKey,
            { name: 'HMAC', hash: 'SHA-256' },
            false,
            ['sign']
        );
        
        // Sign the message
        const encoder = new TextEncoder();
        const messageData = encoder.encode(message);
        const signature = await crypto.subtle.sign('HMAC', key, messageData);
        
        // Convert to hex string
        const hashArray = Array.from(new Uint8Array(signature));
        const hashHex = hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
        return hashHex;
    } catch (e) {
        console.error('HMAC computation error:', e);
        return null;
    }
}

// Verify HMAC signature of a message
async function verifyHMAC(message, providedHMAC) {
    if (!webUIPassword) {
        console.warn('No password configured for HMAC verification');
        return false;
    }
    
    const computedHMAC = await computeHMAC(message);
    if (!computedHMAC) {
        return false;
    }
    
    // Constant-time comparison
    if (computedHMAC.length !== providedHMAC.length) {
        return false;
    }
    
    let result = 0;
    for (let i = 0; i < computedHMAC.length; i++) {
        result |= computedHMAC.charCodeAt(i) ^ providedHMAC.charCodeAt(i);
    }
    return result === 0;
}

