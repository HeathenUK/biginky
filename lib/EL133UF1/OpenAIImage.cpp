/**
 * @file OpenAIImage.cpp
 * @brief OpenAI DALL-E image generation client implementation
 */

#include "OpenAIImage.h"
#include <WiFi.h>

// OpenAI API endpoint
static const char* OPENAI_HOST = "api.openai.com";
static const int OPENAI_PORT = 443;

OpenAIImage::OpenAIImage() 
    : _apiKey(nullptr), _model(DALLE_3), _size(DALLE_1024x1024), _quality(DALLE_STANDARD) {
    _lastError[0] = '\0';
}

void OpenAIImage::begin(const char* apiKey) {
    _apiKey = apiKey;
}

const char* OpenAIImage::getSizeString() const {
    switch (_size) {
        case DALLE_256x256:   return "256x256";
        case DALLE_512x512:   return "512x512";
        case DALLE_1024x1024: return "1024x1024";
        case DALLE_1792x1024: return "1792x1024";
        case DALLE_1024x1792: return "1024x1792";
        default:              return "1024x1024";
    }
}

const char* OpenAIImage::getModelString() const {
    return (_model == DALLE_3) ? "dall-e-3" : "dall-e-2";
}

const char* OpenAIImage::getQualityString() const {
    return (_quality == DALLE_HD) ? "hd" : "standard";
}

const char* OpenAIImage::getErrorString(OpenAIResult result) {
    switch (result) {
        case OPENAI_OK:               return "OK";
        case OPENAI_ERR_NO_WIFI:      return "WiFi not connected";
        case OPENAI_ERR_CONNECT_FAILED: return "Failed to connect to API";
        case OPENAI_ERR_REQUEST_FAILED: return "HTTP request failed";
        case OPENAI_ERR_RESPONSE_ERROR: return "API returned error";
        case OPENAI_ERR_JSON_PARSE:   return "Failed to parse JSON response";
        case OPENAI_ERR_NO_URL:       return "No image URL in response";
        case OPENAI_ERR_DOWNLOAD_FAILED: return "Failed to download image";
        case OPENAI_ERR_ALLOC_FAILED: return "Memory allocation failed";
        case OPENAI_ERR_TIMEOUT:      return "Request timeout";
        default:                      return "Unknown error";
    }
}

bool OpenAIImage::parseImageUrl(const char* json, char* urlBuf, size_t urlBufLen) {
    // Simple JSON parsing - find "url": "..."
    // This is a minimal parser, not a full JSON parser
    
    const char* urlKey = "\"url\"";
    const char* pos = strstr(json, urlKey);
    if (!pos) {
        // Also check for error message
        const char* errKey = "\"error\"";
        if (strstr(json, errKey)) {
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
    
    // Find the colon after "url"
    pos = strchr(pos, ':');
    if (!pos) return false;
    
    // Find the opening quote
    pos = strchr(pos, '"');
    if (!pos) return false;
    pos++; // Skip the quote
    
    // Find the closing quote
    const char* end = strchr(pos, '"');
    if (!end) return false;
    
    size_t len = end - pos;
    if (len >= urlBufLen) return false;
    
    strncpy(urlBuf, pos, len);
    urlBuf[len] = '\0';
    
    return true;
}

OpenAIResult OpenAIImage::generate(const char* prompt, uint8_t** outData, size_t* outLen, 
                                    uint32_t timeoutMs) {
    if (outData) *outData = nullptr;
    if (outLen) *outLen = 0;
    _lastError[0] = '\0';
    
    // Check WiFi
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(_lastError, sizeof(_lastError), "WiFi not connected");
        return OPENAI_ERR_NO_WIFI;
    }
    
    Serial.println("OpenAI: Connecting to API...");
    
    WiFiClientSecure client;
    client.setInsecure();  // Skip certificate verification (or use proper CA cert)
    client.setTimeout(timeoutMs / 1000);
    
    if (!client.connect(OPENAI_HOST, OPENAI_PORT)) {
        snprintf(_lastError, sizeof(_lastError), "Connection to %s failed", OPENAI_HOST);
        return OPENAI_ERR_CONNECT_FAILED;
    }
    
    Serial.printf("OpenAI: Connected. Model=%s, Size=%s, Quality=%s\n",
                  getModelString(), getSizeString(), getQualityString());
    
    // Build JSON request body
    // Escape special characters in prompt
    String escapedPrompt = prompt;
    escapedPrompt.replace("\\", "\\\\");
    escapedPrompt.replace("\"", "\\\"");
    escapedPrompt.replace("\n", "\\n");
    escapedPrompt.replace("\r", "\\r");
    escapedPrompt.replace("\t", "\\t");
    
    String body = "{";
    body += "\"model\":\"";
    body += getModelString();
    body += "\",\"prompt\":\"";
    body += escapedPrompt;
    body += "\",\"n\":1,\"size\":\"";
    body += getSizeString();
    body += "\",\"response_format\":\"url\"";
    
    // Add quality for DALL-E 3
    if (_model == DALLE_3) {
        body += ",\"quality\":\"";
        body += getQualityString();
        body += "\"";
    }
    
    body += "}";
    
    // Send HTTP request
    client.println("POST /v1/images/generations HTTP/1.1");
    client.print("Host: ");
    client.println(OPENAI_HOST);
    client.print("Authorization: Bearer ");
    client.println(_apiKey);
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
    
    Serial.printf("OpenAI: Request sent (%d bytes), waiting for response...\n", body.length());
    
    // Wait for response with timeout
    uint32_t startTime = millis();
    while (!client.available()) {
        if (millis() - startTime > timeoutMs) {
            snprintf(_lastError, sizeof(_lastError), "Timeout waiting for response");
            client.stop();
            return OPENAI_ERR_TIMEOUT;
        }
        delay(100);
    }
    
    Serial.printf("OpenAI: Response received after %lu ms\n", millis() - startTime);
    
    // Read response
    String response;
    bool headersEnded = false;
    int statusCode = 0;
    
    while (client.connected() || client.available()) {
        if (client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            
            if (!headersEnded) {
                // Parse status code from first line
                if (statusCode == 0 && line.startsWith("HTTP/")) {
                    int spaceIdx = line.indexOf(' ');
                    if (spaceIdx > 0) {
                        statusCode = line.substring(spaceIdx + 1).toInt();
                        Serial.printf("OpenAI: HTTP status %d\n", statusCode);
                    }
                }
                
                // Empty line marks end of headers
                if (line.length() == 0) {
                    headersEnded = true;
                }
            } else {
                // Accumulate body
                response += line;
            }
        }
        
        if (millis() - startTime > timeoutMs) {
            snprintf(_lastError, sizeof(_lastError), "Timeout reading response");
            client.stop();
            return OPENAI_ERR_TIMEOUT;
        }
    }
    
    client.stop();
    
    if (statusCode != 200) {
        snprintf(_lastError, sizeof(_lastError), "HTTP error %d", statusCode);
        Serial.printf("OpenAI: Error response: %s\n", response.c_str());
        return OPENAI_ERR_RESPONSE_ERROR;
    }
    
    Serial.printf("OpenAI: Response body: %d chars\n", response.length());
    
    // Parse URL from JSON response
    char imageUrl[512];
    if (!parseImageUrl(response.c_str(), imageUrl, sizeof(imageUrl))) {
        if (_lastError[0] == '\0') {
            snprintf(_lastError, sizeof(_lastError), "Could not find image URL in response");
        }
        return OPENAI_ERR_NO_URL;
    }
    
    Serial.printf("OpenAI: Image URL: %.60s...\n", imageUrl);
    
    // Download the image
    return downloadImage(imageUrl, outData, outLen);
}

OpenAIResult OpenAIImage::downloadImage(const char* url, uint8_t** outData, size_t* outLen) {
    // Parse URL to extract host and path
    // URL format: https://oaidalleapiprodscus.blob.core.windows.net/...
    
    const char* https = "https://";
    if (strncmp(url, https, strlen(https)) != 0) {
        snprintf(_lastError, sizeof(_lastError), "Invalid URL (not HTTPS)");
        return OPENAI_ERR_DOWNLOAD_FAILED;
    }
    
    const char* hostStart = url + strlen(https);
    const char* pathStart = strchr(hostStart, '/');
    if (!pathStart) {
        snprintf(_lastError, sizeof(_lastError), "Invalid URL (no path)");
        return OPENAI_ERR_DOWNLOAD_FAILED;
    }
    
    // Extract host
    size_t hostLen = pathStart - hostStart;
    char host[128];
    if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
    strncpy(host, hostStart, hostLen);
    host[hostLen] = '\0';
    
    Serial.printf("OpenAI: Downloading from %s...\n", host);
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30);
    
    if (!client.connect(host, 443)) {
        snprintf(_lastError, sizeof(_lastError), "Failed to connect to image host");
        return OPENAI_ERR_DOWNLOAD_FAILED;
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
            return OPENAI_ERR_TIMEOUT;
        }
        delay(10);
    }
    
    // Read headers to get content length
    size_t contentLength = 0;
    int statusCode = 0;
    bool chunked = false;
    
    while (client.connected() || client.available()) {
        if (!client.available()) {
            delay(10);
            continue;
        }
        String line = client.readStringUntil('\n');
        line.trim();
        
        if (line.length() == 0) break;  // End of headers
        
        if (statusCode == 0 && line.startsWith("HTTP/")) {
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                statusCode = line.substring(spaceIdx + 1).toInt();
            }
        }
        // Case-insensitive header matching
        String lowerLine = line;
        lowerLine.toLowerCase();
        if (lowerLine.startsWith("content-length:")) {
            contentLength = line.substring(15).toInt();
        } else if (lowerLine.indexOf("transfer-encoding") >= 0 && lowerLine.indexOf("chunked") >= 0) {
            chunked = true;
        }
    }
    
    Serial.printf("OpenAI: Image download: HTTP %d, Content-Length: %zu, Chunked: %s\n", 
                  statusCode, contentLength, chunked ? "yes" : "no");
    
    if (statusCode != 200) {
        client.stop();
        snprintf(_lastError, sizeof(_lastError), "Image download HTTP %d", statusCode);
        return OPENAI_ERR_DOWNLOAD_FAILED;
    }
    
    // Use known contentLength or allocate max buffer for chunked/unknown
    size_t bufferSize = contentLength;
    bool unknownSize = (contentLength == 0 || chunked);
    if (unknownSize) {
        bufferSize = 4 * 1024 * 1024;  // 4MB max for unknown size
        Serial.println("OpenAI: Content-Length unknown, will read until connection closes");
    }
    
    // Allocate buffer in PSRAM
    uint8_t* buffer = (uint8_t*)pmalloc(bufferSize);
    if (!buffer) {
        client.stop();
        snprintf(_lastError, sizeof(_lastError), "Failed to allocate %zu bytes", bufferSize);
        return OPENAI_ERR_ALLOC_FAILED;
    }
    
    // Read image data
    size_t bytesRead = 0;
    startTime = millis();
    uint32_t lastProgress = 0;
    uint32_t idleStart = 0;
    bool wasIdle = false;
    const uint32_t IDLE_TIMEOUT = 15000;  // 15 seconds of no data before giving up
    
    Serial.printf("OpenAI: Starting image download, expecting %zu bytes\n", contentLength);
    
    // Keep reading until we hit content length (if known) or connection closes (if unknown)
    while (true) {
        // Check if we have enough data
        if (!unknownSize && bytesRead >= contentLength) {
            Serial.printf("OpenAI: Received all %zu expected bytes\n", contentLength);
            break;
        }
        
        // Don't overflow buffer
        if (bytesRead >= bufferSize) {
            Serial.printf("OpenAI: Buffer full at %zu bytes\n", bytesRead);
            break;
        }
        
        // Try to read data
        size_t toRead = bufferSize - bytesRead;
        if (toRead > 8192) toRead = 8192;  // Read in 8KB chunks
        
        int got = client.read(buffer + bytesRead, toRead);
        
        if (got > 0) {
            bytesRead += got;
            wasIdle = false;  // Reset idle state
            
            // Progress indication every 100KB
            if (bytesRead - lastProgress >= 102400) {
                uint32_t elapsed = millis() - startTime;
                float kbps = (bytesRead / 1024.0f) / (elapsed / 1000.0f);
                if (contentLength > 0) {
                    Serial.printf("OpenAI: Downloaded %zu / %zu bytes (%d%%) - %.1f KB/s\n", 
                                  bytesRead, contentLength, (int)(bytesRead * 100 / contentLength), kbps);
                } else {
                    Serial.printf("OpenAI: Downloaded %zu bytes - %.1f KB/s\n", bytesRead, kbps);
                }
                lastProgress = bytesRead;
            }
        } else {
            // No data received this iteration
            if (!wasIdle) {
                idleStart = millis();
                wasIdle = true;
            }
            
            uint32_t idleTime = millis() - idleStart;
            
            // Check for stall / disconnect
            if (idleTime >= IDLE_TIMEOUT) {
                bool connected = client.connected();
                int avail = client.available();
                Serial.printf("OpenAI: Idle for %lu ms, connected=%d, avail=%d\n", 
                              idleTime, connected, avail);
                if (!connected && avail == 0) {
                    Serial.printf("OpenAI: Connection closed after %zu bytes\n", bytesRead);
                    break;
                }
            }
            
            delay(10);
        }
        
        // Overall timeout
        if (millis() - startTime > 180000) {  // 3 minute timeout
            free(buffer);
            client.stop();
            snprintf(_lastError, sizeof(_lastError), "Timeout downloading image at %zu/%zu bytes", 
                     bytesRead, contentLength);
            return OPENAI_ERR_TIMEOUT;
        }
    }
    
    client.stop();
    
    uint32_t elapsed = millis() - startTime;
    float kbps = (bytesRead / 1024.0f) / (elapsed / 1000.0f);
    Serial.printf("OpenAI: Download complete: %zu bytes in %lu ms (%.1f KB/s)\n", 
                  bytesRead, elapsed, kbps);
    
    if (contentLength > 0 && bytesRead < contentLength) {
        Serial.printf("OpenAI: WARNING - Incomplete download! Got %zu of %zu bytes (%d%%)\n",
                      bytesRead, contentLength, (int)(bytesRead * 100 / contentLength));
    }
    
    // Verify we got a PNG (check magic bytes)
    if (bytesRead >= 8) {
        static const uint8_t PNG_SIG[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        bool isPng = true;
        for (int i = 0; i < 8; i++) {
            if (buffer[i] != PNG_SIG[i]) {
                isPng = false;
                break;
            }
        }
        if (!isPng) {
            Serial.printf("OpenAI: Warning - data doesn't look like PNG (first bytes: %02X %02X %02X %02X)\n",
                         buffer[0], buffer[1], buffer[2], buffer[3]);
        }
    }
    
    *outData = buffer;
    *outLen = bytesRead;
    
    return OPENAI_OK;
}
