/**
 * @file ModelsLabAI.cpp
 * @brief ModelsLab image generation client implementation
 */

#include "ModelsLabAI.h"
#include <WiFi.h>

// ModelsLab API endpoint
static const char* MODELSLAB_HOST = "modelslab.com";
static const int MODELSLAB_PORT = 443;

ModelsLabAI::ModelsLabAI() 
    : _apiKey(nullptr), 
      _model(MODELSLAB_FLUX_SCHNELL),
      _width(1024), 
      _height(1024),
      _steps(20),
      _guidance(7.5f),
      _negativePrompt(nullptr),
      _scheduler(nullptr) {
    _lastError[0] = '\0';
}

void ModelsLabAI::begin(const char* apiKey) {
    _apiKey = apiKey;
}

const char* ModelsLabAI::getModelString() const {
    switch (_model) {
        case MODELSLAB_FLUX_SCHNELL:     return "flux";
        case MODELSLAB_FLUX_DEV:         return "flux";
        case MODELSLAB_SD_1_5:           return "sd-1.5";
        case MODELSLAB_SD_2_1:           return "sd-2.1";
        case MODELSLAB_SDXL:             return "sdxl";
        case MODELSLAB_SDXL_TURBO:       return "sdxl";
        case MODELSLAB_REALISTIC_VISION: return "realistic-vision-v51";
        case MODELSLAB_DREAMSHAPER:      return "dreamshaper-v8";
        case MODELSLAB_DELIBERATE:       return "deliberate-v3";
        default:                         return "flux";
    }
}

const char* ModelsLabAI::getEndpoint() const {
    // ModelsLab has different endpoints for different model types
    switch (_model) {
        case MODELSLAB_FLUX_SCHNELL:
        case MODELSLAB_FLUX_DEV:
            return "/api/v6/images/text2img";  // Flux endpoint
        default:
            return "/api/v6/images/text2img";  // Standard endpoint
    }
}

const char* ModelsLabAI::getErrorString(ModelsLabResult result) {
    switch (result) {
        case MODELSLAB_OK:                 return "OK";
        case MODELSLAB_ERR_NO_WIFI:        return "WiFi not connected";
        case MODELSLAB_ERR_CONNECT_FAILED: return "Failed to connect to API";
        case MODELSLAB_ERR_REQUEST_FAILED: return "HTTP request failed";
        case MODELSLAB_ERR_RESPONSE_ERROR: return "API returned error";
        case MODELSLAB_ERR_JSON_PARSE:     return "Failed to parse JSON response";
        case MODELSLAB_ERR_NO_IMAGE:       return "No image data in response";
        case MODELSLAB_ERR_DOWNLOAD_FAILED: return "Failed to download image";
        case MODELSLAB_ERR_BASE64_DECODE:  return "Base64 decode failed";
        case MODELSLAB_ERR_ALLOC_FAILED:   return "Memory allocation failed";
        case MODELSLAB_ERR_TIMEOUT:        return "Request timeout";
        case MODELSLAB_ERR_PROCESSING:     return "Image still processing";
        default:                           return "Unknown error";
    }
}

// Base64 decoding table
static const uint8_t modelslab_b64_table[128] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64
};

size_t ModelsLabAI::base64Decode(const char* input, size_t inputLen, uint8_t* output) {
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
        uint8_t val = modelslab_b64_table[(uint8_t)c];
        if (val == 64) continue;
        
        accumulator = (accumulator << 6) | val;
        bits += 6;
        
        if (bits >= 8) {
            bits -= 8;
            output[outputLen++] = (uint8_t)((accumulator >> bits) & 0xFF);
        }
    }
    
    return outputLen;
}

bool ModelsLabAI::parseImageResponse(const char* json, char* urlOrBase64, size_t bufLen, bool* isBase64) {
    *isBase64 = false;
    
    // Check for error in response
    // ModelsLab error format: {"status": "error", "message": "..."}
    const char* statusKey = "\"status\"";
    const char* statusPos = strstr(json, statusKey);
    if (statusPos) {
        // Find the status value
        const char* colonPos = strchr(statusPos, ':');
        if (colonPos) {
            const char* quotePos = strchr(colonPos, '"');
            if (quotePos) {
                quotePos++;
                if (strncmp(quotePos, "error", 5) == 0) {
                    // Extract error message
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
                    return false;
                }
                // Check for "processing" status
                if (strncmp(quotePos, "processing", 10) == 0) {
                    snprintf(_lastError, sizeof(_lastError), "Image still processing");
                    return false;
                }
            }
        }
    }
    
    // Look for output array with URL
    // Format: {"status": "success", "output": ["https://..."]}
    const char* outputKey = "\"output\"";
    const char* pos = strstr(json, outputKey);
    
    if (pos) {
        // Find the opening bracket
        pos = strchr(pos, '[');
        if (pos) {
            pos++;
            // Skip whitespace
            while (*pos == ' ' || *pos == '\n' || *pos == '\r' || *pos == '\t') pos++;
            
            // Find opening quote
            if (*pos == '"') {
                pos++;
                const char* end = strchr(pos, '"');
                if (end) {
                    size_t len = end - pos;
                    if (len >= bufLen) len = bufLen - 1;
                    strncpy(urlOrBase64, pos, len);
                    urlOrBase64[len] = '\0';
                    
                    // Check if it's a URL or base64
                    *isBase64 = (strncmp(urlOrBase64, "http", 4) != 0);
                    return true;
                }
            }
        }
    }
    
    // Also check for direct "image" field (some endpoints)
    const char* imageKey = "\"image\"";
    pos = strstr(json, imageKey);
    if (pos) {
        pos = strchr(pos, ':');
        if (pos) {
            pos = strchr(pos, '"');
            if (pos) {
                pos++;
                const char* end = strchr(pos, '"');
                if (end) {
                    size_t len = end - pos;
                    if (len >= bufLen) len = bufLen - 1;
                    strncpy(urlOrBase64, pos, len);
                    urlOrBase64[len] = '\0';
                    *isBase64 = (strncmp(urlOrBase64, "http", 4) != 0);
                    return true;
                }
            }
        }
    }
    
    snprintf(_lastError, sizeof(_lastError), "No image URL or data in response");
    return false;
}

ModelsLabResult ModelsLabAI::downloadImage(const char* url, uint8_t** outData, size_t* outLen) {
    // Parse URL to extract host and path
    const char* https = "https://";
    const char* http = "http://";
    const char* hostStart;
    int port = 443;
    
    if (strncmp(url, https, strlen(https)) == 0) {
        hostStart = url + strlen(https);
        port = 443;
    } else if (strncmp(url, http, strlen(http)) == 0) {
        hostStart = url + strlen(http);
        port = 80;
    } else {
        snprintf(_lastError, sizeof(_lastError), "Invalid URL scheme");
        return MODELSLAB_ERR_DOWNLOAD_FAILED;
    }
    
    const char* pathStart = strchr(hostStart, '/');
    if (!pathStart) {
        snprintf(_lastError, sizeof(_lastError), "Invalid URL (no path)");
        return MODELSLAB_ERR_DOWNLOAD_FAILED;
    }
    
    // Extract host
    size_t hostLen = pathStart - hostStart;
    char host[128];
    if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
    strncpy(host, hostStart, hostLen);
    host[hostLen] = '\0';
    
    Serial.printf("ModelsLab: Downloading from %s...\n", host);
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);
    
    if (!client.connect(host, port)) {
        snprintf(_lastError, sizeof(_lastError), "Failed to connect to image host");
        return MODELSLAB_ERR_DOWNLOAD_FAILED;
    }
    
    // Send GET request
    client.print("GET ");
    client.print(pathStart);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("Connection: close");
    client.println();
    
    // Wait for response
    uint32_t startTime = millis();
    while (!client.available()) {
        if (millis() - startTime > 30000) {
            client.stop();
            snprintf(_lastError, sizeof(_lastError), "Timeout waiting for image");
            return MODELSLAB_ERR_TIMEOUT;
        }
        delay(10);
    }
    
    // Read headers
    size_t contentLength = 0;
    int statusCode = 0;
    
    while (client.connected() || client.available()) {
        if (!client.available()) {
            delay(10);
            continue;
        }
        String line = client.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) break;
        
        if (statusCode == 0 && line.startsWith("HTTP/")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1).toInt();
            }
        }
        String lowerLine = line;
        lowerLine.toLowerCase();
        if (lowerLine.startsWith("content-length:")) {
            contentLength = line.substring(15).toInt();
        }
    }
    
    Serial.printf("ModelsLab: Image download: HTTP %d, Content-Length: %zu\n", 
                  statusCode, contentLength);
    
    if (statusCode != 200) {
        client.stop();
        snprintf(_lastError, sizeof(_lastError), "Image download HTTP %d", statusCode);
        return MODELSLAB_ERR_DOWNLOAD_FAILED;
    }
    
    // Allocate buffer
    size_t bufferSize = contentLength > 0 ? contentLength : 4 * 1024 * 1024;
    uint8_t* buffer = (uint8_t*)pmalloc(bufferSize);
    if (!buffer) {
        client.stop();
        snprintf(_lastError, sizeof(_lastError), "Failed to allocate %zu bytes", bufferSize);
        return MODELSLAB_ERR_ALLOC_FAILED;
    }
    
    // Read image data
    size_t bytesRead = 0;
    uint32_t lastProgress = 0;
    startTime = millis();
    
    while ((contentLength == 0 || bytesRead < contentLength) && bytesRead < bufferSize) {
        if (client.available()) {
            int toRead = client.available();
            if (bytesRead + toRead > bufferSize) {
                toRead = bufferSize - bytesRead;
            }
            int got = client.read(buffer + bytesRead, toRead);
            if (got > 0) {
                bytesRead += got;
                if (bytesRead - lastProgress >= 102400) {
                    Serial.printf("ModelsLab: Downloaded %zu bytes...\n", bytesRead);
                    lastProgress = bytesRead;
                }
            }
        } else if (!client.connected()) {
            break;
        } else {
            delay(10);
        }
        
        if (millis() - startTime > 120000) {
            free(buffer);
            client.stop();
            snprintf(_lastError, sizeof(_lastError), "Timeout downloading image");
            return MODELSLAB_ERR_TIMEOUT;
        }
    }
    
    client.stop();
    Serial.printf("ModelsLab: Download complete: %zu bytes\n", bytesRead);
    
    *outData = buffer;
    *outLen = bytesRead;
    return MODELSLAB_OK;
}

ModelsLabResult ModelsLabAI::generate(const char* prompt, uint8_t** outData, size_t* outLen, 
                                       uint32_t timeoutMs) {
    if (outData) *outData = nullptr;
    if (outLen) *outLen = 0;
    _lastError[0] = '\0';
    
    // Check WiFi
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(_lastError, sizeof(_lastError), "WiFi not connected");
        return MODELSLAB_ERR_NO_WIFI;
    }
    
    Serial.println("ModelsLab: Connecting to API...");
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(timeoutMs / 1000);
    
    if (!client.connect(MODELSLAB_HOST, MODELSLAB_PORT)) {
        snprintf(_lastError, sizeof(_lastError), "Connection to %s failed", MODELSLAB_HOST);
        return MODELSLAB_ERR_CONNECT_FAILED;
    }
    
    Serial.printf("ModelsLab: Connected. Model=%s, Size=%dx%d\n",
                  getModelString(), _width, _height);
    
    // Build JSON request body
    String escapedPrompt = prompt;
    escapedPrompt.replace("\\", "\\\\");
    escapedPrompt.replace("\"", "\\\"");
    escapedPrompt.replace("\n", "\\n");
    escapedPrompt.replace("\r", "\\r");
    escapedPrompt.replace("\t", "\\t");
    
    String body = "{";
    body += "\"key\":\"";
    body += _apiKey;
    body += "\",\"prompt\":\"";
    body += escapedPrompt;
    body += "\",\"width\":";
    body += _width;
    body += ",\"height\":";
    body += _height;
    body += ",\"samples\":1";
    body += ",\"num_inference_steps\":";
    body += _steps;
    body += ",\"guidance_scale\":";
    body += String(_guidance, 1);
    
    // Add model-specific parameters
    if (_model != MODELSLAB_FLUX_SCHNELL && _model != MODELSLAB_FLUX_DEV) {
        // Non-flux models support more parameters
        body += ",\"model_id\":\"";
        body += getModelString();
        body += "\"";
    }
    
    // Negative prompt
    if (_negativePrompt && strlen(_negativePrompt) > 0) {
        String escapedNeg = _negativePrompt;
        escapedNeg.replace("\\", "\\\\");
        escapedNeg.replace("\"", "\\\"");
        body += ",\"negative_prompt\":\"";
        body += escapedNeg;
        body += "\"";
    }
    
    // Scheduler
    if (_scheduler && strlen(_scheduler) > 0) {
        body += ",\"scheduler\":\"";
        body += _scheduler;
        body += "\"";
    }
    
    // Safety checker (disable for art)
    body += ",\"safety_checker\":false";
    
    // Request base64 output (faster than URL for small images)
    body += ",\"base64\":true";
    
    body += "}";
    
    const char* endpoint = getEndpoint();
    
    // Send HTTP request
    client.print("POST ");
    client.print(endpoint);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(MODELSLAB_HOST);
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
    
    Serial.printf("ModelsLab: Request sent to %s (%d bytes), waiting...\n", 
                  endpoint, body.length());
    
    // Wait for response
    uint32_t startTime = millis();
    while (!client.available()) {
        if (millis() - startTime > timeoutMs) {
            snprintf(_lastError, sizeof(_lastError), "Timeout waiting for response");
            client.stop();
            return MODELSLAB_ERR_TIMEOUT;
        }
        delay(100);
    }
    
    Serial.printf("ModelsLab: Response received after %lu ms\n", millis() - startTime);
    
    // Read headers
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
        
        if (statusCode == 0 && line.startsWith("HTTP/")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1).toInt();
                Serial.printf("ModelsLab: HTTP status %d\n", statusCode);
            }
        }
        
        String lowerLine = line;
        lowerLine.toLowerCase();
        if (lowerLine.startsWith("content-length:")) {
            contentLength = line.substring(15).toInt();
        }
        
        if (millis() - startTime > timeoutMs) {
            client.stop();
            return MODELSLAB_ERR_TIMEOUT;
        }
    }
    
    // ModelsLab returns 200 even for errors, need to check response body
    if (statusCode != 200) {
        String errorBody;
        while (client.available() && errorBody.length() < 1024) {
            errorBody += (char)client.read();
        }
        client.stop();
        snprintf(_lastError, sizeof(_lastError), "HTTP error %d", statusCode);
        Serial.printf("ModelsLab: Error: %s\n", errorBody.c_str());
        return MODELSLAB_ERR_RESPONSE_ERROR;
    }
    
    Serial.printf("ModelsLab: Content-Length: %zu bytes\n", contentLength);
    
    // Allocate buffer for response
    size_t bufferSize = contentLength > 0 ? contentLength + 1 : 8 * 1024 * 1024;
    char* responseBuffer = (char*)pmalloc(bufferSize);
    if (!responseBuffer) {
        client.stop();
        snprintf(_lastError, sizeof(_lastError), "Failed to allocate response buffer");
        return MODELSLAB_ERR_ALLOC_FAILED;
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
                if (bytesRead - lastProgress >= 102400) {
                    Serial.printf("ModelsLab: Received %zu bytes...\n", bytesRead);
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
            return MODELSLAB_ERR_TIMEOUT;
        }
    }
    
    responseBuffer[bytesRead] = '\0';
    client.stop();
    
    Serial.printf("ModelsLab: Response complete: %zu bytes\n", bytesRead);
    
    // Parse response - could be URL or base64
    char* imageData = (char*)pmalloc(bytesRead);  // Worst case same size as response
    if (!imageData) {
        free(responseBuffer);
        return MODELSLAB_ERR_ALLOC_FAILED;
    }
    
    bool isBase64 = false;
    if (!parseImageResponse(responseBuffer, imageData, bytesRead, &isBase64)) {
        free(responseBuffer);
        free(imageData);
        return MODELSLAB_ERR_NO_IMAGE;
    }
    
    Serial.printf("ModelsLab: Got %s data\n", isBase64 ? "base64" : "URL");
    
    if (isBase64) {
        // Decode base64
        size_t base64Len = strlen(imageData);
        size_t decodedMaxSize = (base64Len * 3) / 4 + 4;
        
        uint8_t* imageBuffer = (uint8_t*)pmalloc(decodedMaxSize);
        if (!imageBuffer) {
            free(responseBuffer);
            free(imageData);
            return MODELSLAB_ERR_ALLOC_FAILED;
        }
        
        size_t decodedLen = base64Decode(imageData, base64Len, imageBuffer);
        free(responseBuffer);
        free(imageData);
        
        if (decodedLen == 0) {
            free(imageBuffer);
            snprintf(_lastError, sizeof(_lastError), "Base64 decode failed");
            return MODELSLAB_ERR_BASE64_DECODE;
        }
        
        Serial.printf("ModelsLab: Decoded image: %zu bytes\n", decodedLen);
        
        *outData = imageBuffer;
        *outLen = decodedLen;
    } else {
        // Download from URL
        free(responseBuffer);
        
        ModelsLabResult dlResult = downloadImage(imageData, outData, outLen);
        free(imageData);
        
        if (dlResult != MODELSLAB_OK) {
            return dlResult;
        }
    }
    
    // Verify image format
    if (*outLen >= 8) {
        static const uint8_t PNG_SIG[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        static const uint8_t JPEG_SIG[] = {0xFF, 0xD8, 0xFF};
        
        bool isPng = true;
        for (int i = 0; i < 8; i++) {
            if ((*outData)[i] != PNG_SIG[i]) {
                isPng = false;
                break;
            }
        }
        
        bool isJpeg = ((*outData)[0] == JPEG_SIG[0] && 
                       (*outData)[1] == JPEG_SIG[1] && 
                       (*outData)[2] == JPEG_SIG[2]);
        
        if (isPng) {
            Serial.println("ModelsLab: Valid PNG image");
        } else if (isJpeg) {
            Serial.println("ModelsLab: Valid JPEG image");
        } else {
            Serial.printf("ModelsLab: Warning - unknown format (bytes: %02X %02X %02X %02X)\n",
                         (*outData)[0], (*outData)[1], (*outData)[2], (*outData)[3]);
        }
    }
    
    return MODELSLAB_OK;
}
