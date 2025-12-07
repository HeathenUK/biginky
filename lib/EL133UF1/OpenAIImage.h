/**
 * @file OpenAIImage.h
 * @brief OpenAI DALL-E image generation client for ESP/Pico
 * 
 * This client uses the OpenAI Images API to generate images from text prompts.
 * Images are downloaded and can be decoded with EL133UF1_PNG.
 * 
 * API Reference: https://platform.openai.com/docs/api-reference/images/create
 * 
 * Memory considerations:
 * - Generated images are stored in PSRAM (typically 500KB-2MB for PNG)
 * - The DALL-E 3 model returns 1024x1024, 1792x1024, or 1024x1792 images
 * - DALL-E 2 returns 256x256, 512x512, or 1024x1024 images
 * 
 * Usage:
 *   OpenAIImage ai;
 *   ai.begin("your-api-key");
 *   
 *   uint8_t* imageData = nullptr;
 *   size_t imageLen = 0;
 *   
 *   if (ai.generate("A serene forest at dawn", &imageData, &imageLen)) {
 *       png.draw(0, 0, imageData, imageLen);
 *       free(imageData);  // Don't forget to free!
 *   }
 */

#ifndef OPENAI_IMAGE_H
#define OPENAI_IMAGE_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

// Image sizes for DALL-E models
enum DALLESize {
    DALLE_256x256,      // DALL-E 2 only
    DALLE_512x512,      // DALL-E 2 only
    DALLE_1024x1024,    // DALL-E 2 and 3
    DALLE_1792x1024,    // DALL-E 3 only (landscape)
    DALLE_1024x1792     // DALL-E 3 only (portrait)
};

// DALL-E models
enum DALLEModel {
    DALLE_2,
    DALLE_3
};

// Quality settings (DALL-E 3 only)
enum DALLEQuality {
    DALLE_STANDARD,
    DALLE_HD
};

// Result codes
enum OpenAIResult {
    OPENAI_OK = 0,
    OPENAI_ERR_NO_WIFI,
    OPENAI_ERR_CONNECT_FAILED,
    OPENAI_ERR_REQUEST_FAILED,
    OPENAI_ERR_RESPONSE_ERROR,
    OPENAI_ERR_JSON_PARSE,
    OPENAI_ERR_NO_URL,
    OPENAI_ERR_DOWNLOAD_FAILED,
    OPENAI_ERR_ALLOC_FAILED,
    OPENAI_ERR_TIMEOUT
};

class OpenAIImage {
public:
    OpenAIImage();
    
    /**
     * @brief Initialize with API key
     * @param apiKey Your OpenAI API key (starts with "sk-")
     */
    void begin(const char* apiKey);
    
    /**
     * @brief Set the model to use
     * @param model DALLE_2 or DALLE_3
     */
    void setModel(DALLEModel model) { _model = model; }
    
    /**
     * @brief Set the image size
     * @param size One of the DALLESize values
     */
    void setSize(DALLESize size) { _size = size; }
    
    /**
     * @brief Set quality (DALL-E 3 only)
     * @param quality DALLE_STANDARD or DALLE_HD
     */
    void setQuality(DALLEQuality quality) { _quality = quality; }
    
    /**
     * @brief Generate an image from a text prompt
     * 
     * @param prompt Text description of the image to generate
     * @param outData Pointer to receive allocated image data (caller must free!)
     * @param outLen Pointer to receive image data length
     * @param timeoutMs Timeout in milliseconds (default 60 seconds)
     * @return OpenAIResult code
     * 
     * @note The returned data is allocated with pmalloc (PSRAM). 
     *       Caller is responsible for calling free() on outData.
     */
    OpenAIResult generate(const char* prompt, uint8_t** outData, size_t* outLen, 
                          uint32_t timeoutMs = 60000);
    
    /**
     * @brief Get the last error message
     */
    const char* getLastError() const { return _lastError; }
    
    /**
     * @brief Get error string for result code
     */
    static const char* getErrorString(OpenAIResult result);
    
private:
    const char* _apiKey;
    DALLEModel _model;
    DALLESize _size;
    DALLEQuality _quality;
    char _lastError[128];
    
    const char* getSizeString() const;
    const char* getModelString() const;
    const char* getQualityString() const;
    
    OpenAIResult downloadImage(const char* url, uint8_t** outData, size_t* outLen);
    bool parseImageUrl(const char* json, char* urlBuf, size_t urlBufLen);
};

#endif // OPENAI_IMAGE_H
