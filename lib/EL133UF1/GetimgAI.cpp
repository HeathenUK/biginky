/**
 * @file GetimgAI.cpp
 * @brief getimg.ai image generation client implementation
 */

#include "GetimgAI.h"
#include "platform_hal.h"
#include <WiFi.h>
#include "cJSON.h"  // For JSON building

// getimg.ai API endpoint
static const char* GETIMG_HOST = "api.getimg.ai";
static const int GETIMG_PORT = 443;

GetimgAI::GetimgAI() 
    : _apiKey(nullptr), 
      _model(GETIMG_FLUX_SCHNELL),  // Default to fast model
      _format(GETIMG_PNG),
      _width(1024), 
      _height(1024),
      _steps(25),
      _guidance(7.5f),
      _negativePrompt(nullptr) {
    _lastError[0] = '\0';
}

void GetimgAI::begin(const char* apiKey) {
    _apiKey = apiKey;
}

const char* GetimgAI::getModelString() const {
    switch (_model) {
        case GETIMG_SD_1_5:           return "stable-diffusion-v1-5";
        case GETIMG_SD_2_1:           return "stable-diffusion-v2-1";
        case GETIMG_SDXL_1_0:         return "stable-diffusion-xl-v1-0";
        case GETIMG_FLUX_SCHNELL:     return "flux-schnell";
        case GETIMG_FLUX_DEV:         return "flux-dev";
        case GETIMG_REALISTIC_VISION: return "realistic-vision-v5-1";
        case GETIMG_DREAM_SHAPER:     return "dream-shaper-v8";
        default:                      return "flux-schnell";
    }
}

const char* GetimgAI::getEndpoint() const {
    // Different model families use different API endpoints
    switch (_model) {
        case GETIMG_FLUX_SCHNELL:
            return "/v1/flux-schnell/text-to-image";
        case GETIMG_FLUX_DEV:
            return "/v1/flux-dev/text-to-image";
        case GETIMG_SDXL_1_0:
            return "/v1/stable-diffusion-xl/text-to-image";
        case GETIMG_SD_1_5:
        case GETIMG_SD_2_1:
        case GETIMG_REALISTIC_VISION:
        case GETIMG_DREAM_SHAPER:
            return "/v1/stable-diffusion/text-to-image";
        default:
            return "/v1/flux-schnell/text-to-image";
    }
}

const char* GetimgAI::getFormatString() const {
    return (_format == GETIMG_JPEG) ? "jpeg" : "png";
}

const char* GetimgAI::getErrorString(GetimgResult result) {
    switch (result) {
        case GETIMG_OK:                return "OK";
        case GETIMG_ERR_NO_WIFI:       return "WiFi not connected";
        case GETIMG_ERR_CONNECT_FAILED: return "Failed to connect to API";
        case GETIMG_ERR_REQUEST_FAILED: return "HTTP request failed";
        case GETIMG_ERR_RESPONSE_ERROR: return "API returned error";
        case GETIMG_ERR_JSON_PARSE:    return "Failed to parse JSON response";
        case GETIMG_ERR_NO_IMAGE:      return "No image data in response";
        case GETIMG_ERR_BASE64_DECODE: return "Base64 decode failed";
        case GETIMG_ERR_ALLOC_FAILED:  return "Memory allocation failed";
        case GETIMG_ERR_TIMEOUT:       return "Request timeout";
        default:                       return "Unknown error";
    }
}

// Base64 decoding table
static const uint8_t b64_decode_table[128] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
};

size_t GetimgAI::base64Decode(const char* input, size_t inputLen, uint8_t* output) {
    size_t outputLen = 0;
    uint32_t accumulator = 0;
    int bits = 0;
    
    for (size_t i = 0; i < inputLen; i++) {
        char c = input[i];
        
        // Skip whitespace and padding
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') continue;
        
        // Invalid character check
        if (c < 0 || c >= 128) continue;
        uint8_t val = b64_decode_table[(uint8_t)c];
        if (val == 64) continue;  // Invalid character
        
        accumulator = (accumulator << 6) | val;
        bits += 6;
        
        if (bits >= 8) {
            bits -= 8;
            output[outputLen++] = (uint8_t)((accumulator >> bits) & 0xFF);
        }
    }
    
    return outputLen;
}

bool GetimgAI::parseBase64Image(const char* json, char** base64Data, size_t* base64Len) {
    // Look for "image": "base64data..." in the JSON response
    // getimg.ai returns: {"image": "base64encodeddata..."}
    
    const char* imageKey = "\"image\"";
    const char* pos = strstr(json, imageKey);
    
    if (!pos) {
        // Check for error message
        const char* errKey = "\"error\"";
        const char* errPos = strstr(json, errKey);
        if (errPos) {
            // Try to extract error message
            const char* msgKey = "\"message\"";
            const char* msgPos = strstr(json, msgKey);
            if (msgPos) {
                msgPos = strchr(msgPos, ':');
                if (msgPos) {
                    msgPos = strchr(msgPos, '"');
                    if (msgPos) {
                        msgPos++;
                        const char* msgEnd = strchr(msgPos, '"');
                        if (msgEnd) {
                            size_t len = msgEnd - msgPos;
                            if (len >= sizeof(_lastError)) len = sizeof(_lastError) - 1;
                            strncpy(_lastError, msgPos, len);
                            _lastError[len] = '\0';
                        }
                    }
                }
            }
        }
        return false;
    }
    
    // Find the colon after "image"
    pos = strchr(pos, ':');
    if (!pos) return false;
    
    // Find the opening quote
    pos = strchr(pos, '"');
    if (!pos) return false;
    pos++;  // Skip the quote
    
    // Find the closing quote - base64 data can be very long
    const char* end = pos;
    while (*end && *end != '"') end++;
    if (!*end) return false;
    
    size_t len = end - pos;
    
    // Allocate buffer for base64 data (we'll decode in place later)
    *base64Data = (char*)pos;  // Point directly to the data in the response
    *base64Len = len;
    
    return true;
}

GetimgResult GetimgAI::generate(const char* prompt, uint8_t** outData, size_t* outLen, 
                                 uint32_t timeoutMs) {
    if (outData) *outData = nullptr;
    if (outLen) *outLen = 0;
    _lastError[0] = '\0';
    
    // Check WiFi
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(_lastError, sizeof(_lastError), "WiFi not connected");
        return GETIMG_ERR_NO_WIFI;
    }
    
    Serial.println("getimg.ai: Connecting to API...");
    
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate verification
    client.setTimeout(timeoutMs / 1000);
    
    if (!client.connect(GETIMG_HOST, GETIMG_PORT)) {
        snprintf(_lastError, sizeof(_lastError), "Connection to %s failed", GETIMG_HOST);
        return GETIMG_ERR_CONNECT_FAILED;
    }
    
    Serial.printf("getimg.ai: Connected. Model=%s, Size=%dx%d\n",
                  getModelString(), _width, _height);
    
    // Build JSON request body using cJSON
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        snprintf(_lastError, sizeof(_lastError), "Failed to create JSON object");
        client.stop();
        return GETIMG_ERR_ALLOC_FAILED;
    }
    
    // Add prompt (cJSON handles escaping automatically)
    cJSON_AddStringToObject(root, "prompt", prompt);
    
    // Add dimensions
    cJSON_AddNumberToObject(root, "width", _width);
    cJSON_AddNumberToObject(root, "height", _height);
    
    // Add output format
    cJSON_AddStringToObject(root, "output_format", getFormatString());
    
    // Add model-specific parameters
    bool isFluxSchnell = (_model == GETIMG_FLUX_SCHNELL);
    bool isFlux = isFluxSchnell || (_model == GETIMG_FLUX_DEV);
    
    // Non-flux models need model specification and support more parameters
    if (!isFlux) {
        cJSON_AddStringToObject(root, "model", getModelString());
        cJSON_AddNumberToObject(root, "steps", _steps);
        cJSON_AddNumberToObject(root, "guidance", (double)_guidance);
        
        // Negative prompt
        if (_negativePrompt && strlen(_negativePrompt) > 0) {
            cJSON_AddStringToObject(root, "negative_prompt", _negativePrompt);
        }
    }
    
    // Response format - we want base64 data
    cJSON_AddStringToObject(root, "response_format", "b64");
    
    // Convert to string
    char* bodyStr = cJSON_Print(root);
    if (!bodyStr) {
        cJSON_Delete(root);
        snprintf(_lastError, sizeof(_lastError), "Failed to print JSON");
        client.stop();
        return GETIMG_ERR_ALLOC_FAILED;
    }
    
    String body = String(bodyStr);
    free(bodyStr);
    cJSON_Delete(root);
    
    // Get the endpoint for this model
    const char* endpoint = getEndpoint();
    
    // Send HTTP request
    client.print("POST ");
    client.print(endpoint);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(GETIMG_HOST);
    client.print("Authorization: Bearer ");
    client.println(_apiKey);
    client.println("Content-Type: application/json");
    client.println("Accept: application/json");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
    
    Serial.printf("getimg.ai: Request sent to %s (%d bytes), waiting for response...\n", 
                  endpoint, body.length());
    
    // Wait for response with timeout
    uint32_t startTime = millis();
    while (!client.available()) {
        if (millis() - startTime > timeoutMs) {
            snprintf(_lastError, sizeof(_lastError), "Timeout waiting for response");
            client.stop();
            return GETIMG_ERR_TIMEOUT;
        }
        delay(100);
    }
    
    Serial.printf("getimg.ai: Response received after %lu ms\n", millis() - startTime);
    
    // Read response - we need to handle potentially large base64 data
    // First, read headers to get content length
    bool headersEnded = false;
    int statusCode = 0;
    size_t contentLength = 0;
    
    while (client.connected() || client.available()) {
        if (!client.available()) {
            delay(10);
            continue;
        }
        
        String line = client.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) {
            headersEnded = true;
            break;
        }
        
        // Parse status code from first line
        if (statusCode == 0 && line.startsWith("HTTP/")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1).toInt();
                Serial.printf("getimg.ai: HTTP status %d\n", statusCode);
            }
        }
        
        // Get content length
        String lowerLine = line;
        lowerLine.toLowerCase();
        if (lowerLine.startsWith("content-length:")) {
            contentLength = line.substring(15).toInt();
        }
        
        if (millis() - startTime > timeoutMs) {
            snprintf(_lastError, sizeof(_lastError), "Timeout reading headers");
            client.stop();
            return GETIMG_ERR_TIMEOUT;
        }
    }
    
    if (statusCode != 200) {
        // Read error body for details
        String errorBody;
        while (client.available() && errorBody.length() < 1024) {
            errorBody += (char)client.read();
        }
        client.stop();
        
        snprintf(_lastError, sizeof(_lastError), "HTTP error %d", statusCode);
        Serial.printf("getimg.ai: Error response: %s\n", errorBody.c_str());
        return GETIMG_ERR_RESPONSE_ERROR;
    }
    
    Serial.printf("getimg.ai: Content-Length: %zu bytes\n", contentLength);
    
    // Allocate buffer for JSON response (contains base64 image)
    // If no content-length, allocate a reasonable max (8MB for large images)
    size_t bufferSize = contentLength > 0 ? contentLength + 1 : 8 * 1024 * 1024;
    char* responseBuffer = (char*)pmalloc(bufferSize);
    if (!responseBuffer) {
        client.stop();
        snprintf(_lastError, sizeof(_lastError), "Failed to allocate %zu bytes for response", bufferSize);
        return GETIMG_ERR_ALLOC_FAILED;
    }
    
    // Read response body
    size_t bytesRead = 0;
    uint32_t lastProgress = 0;
    
    while ((contentLength == 0 || bytesRead < contentLength) && bytesRead < bufferSize - 1) {
        if (client.available()) {
            int toRead = client.available();
            if (bytesRead + toRead >= bufferSize) {
                toRead = bufferSize - bytesRead - 1;
            }
            int got = client.read((uint8_t*)(responseBuffer + bytesRead), toRead);
            if (got > 0) {
                bytesRead += got;
                
                // Progress indication
                if (bytesRead - lastProgress >= 102400) {
                    Serial.printf("getimg.ai: Received %zu bytes...\n", bytesRead);
                    lastProgress = bytesRead;
                }
            }
        } else if (!client.connected()) {
            break;
        } else {
            delay(10);
        }
        
        if (millis() - startTime > timeoutMs) {
            free(responseBuffer);
            client.stop();
            snprintf(_lastError, sizeof(_lastError), "Timeout reading response body");
            return GETIMG_ERR_TIMEOUT;
        }
    }
    
    responseBuffer[bytesRead] = '\0';
    client.stop();
    
    Serial.printf("getimg.ai: Response complete: %zu bytes\n", bytesRead);
    
    // Parse the base64 image data from JSON
    char* base64Data = nullptr;
    size_t base64Len = 0;
    
    if (!parseBase64Image(responseBuffer, &base64Data, &base64Len)) {
        free(responseBuffer);
        if (_lastError[0] == '\0') {
            snprintf(_lastError, sizeof(_lastError), "Could not find image data in response");
        }
        return GETIMG_ERR_NO_IMAGE;
    }
    
    Serial.printf("getimg.ai: Found base64 data: %zu bytes\n", base64Len);
    
    // Calculate decoded size (base64 is ~4/3 of original)
    size_t decodedMaxSize = (base64Len * 3) / 4 + 4;
    
    // Allocate buffer for decoded image
    uint8_t* imageBuffer = (uint8_t*)pmalloc(decodedMaxSize);
    if (!imageBuffer) {
        free(responseBuffer);
        snprintf(_lastError, sizeof(_lastError), "Failed to allocate %zu bytes for image", decodedMaxSize);
        return GETIMG_ERR_ALLOC_FAILED;
    }
    
    // Decode base64
    size_t decodedLen = base64Decode(base64Data, base64Len, imageBuffer);
    
    // Free the response buffer now that we've decoded
    free(responseBuffer);
    
    if (decodedLen == 0) {
        free(imageBuffer);
        snprintf(_lastError, sizeof(_lastError), "Base64 decode failed");
        return GETIMG_ERR_BASE64_DECODE;
    }
    
    Serial.printf("getimg.ai: Decoded image: %zu bytes\n", decodedLen);
    
    // Verify we got a valid image (check for PNG or JPEG magic bytes)
    bool validImage = false;
    if (decodedLen >= 8) {
        // PNG signature
        static const uint8_t PNG_SIG[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        // JPEG signature  
        static const uint8_t JPEG_SIG[] = {0xFF, 0xD8, 0xFF};
        
        bool isPng = true;
        for (int i = 0; i < 8; i++) {
            if (imageBuffer[i] != PNG_SIG[i]) {
                isPng = false;
                break;
            }
        }
        
        bool isJpeg = (imageBuffer[0] == JPEG_SIG[0] && 
                       imageBuffer[1] == JPEG_SIG[1] && 
                       imageBuffer[2] == JPEG_SIG[2]);
        
        validImage = isPng || isJpeg;
        
        if (isPng) {
            Serial.println("getimg.ai: Valid PNG image");
        } else if (isJpeg) {
            Serial.println("getimg.ai: Valid JPEG image");
        } else {
            Serial.printf("getimg.ai: Warning - unknown format (bytes: %02X %02X %02X %02X)\n",
                         imageBuffer[0], imageBuffer[1], imageBuffer[2], imageBuffer[3]);
        }
    }
    
    if (!validImage) {
        free(imageBuffer);
        snprintf(_lastError, sizeof(_lastError), "Decoded data is not a valid image");
        return GETIMG_ERR_BASE64_DECODE;
    }
    
    *outData = imageBuffer;
    *outLen = decodedLen;
    
    return GETIMG_OK;
}
