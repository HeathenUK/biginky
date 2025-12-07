/**
 * @file GetimgAI.h
 * @brief getimg.ai image generation client for ESP/Pico
 * 
 * This client uses the getimg.ai API to generate images from text prompts.
 * Images are returned as base64-encoded data which is decoded to PNG.
 * 
 * API Reference: https://docs.getimg.ai/reference/introduction
 * 
 * Memory considerations:
 * - Generated images are stored in PSRAM
 * - Base64 decoding requires ~33% more memory temporarily during decode
 * - Typical image sizes: 512x512 to 1024x1024
 * 
 * Usage:
 *   GetimgAI ai;
 *   ai.begin("your-api-key");
 *   ai.setModel(GETIMG_FLUX_SCHNELL);  // Fast model
 *   
 *   uint8_t* imageData = nullptr;
 *   size_t imageLen = 0;
 *   
 *   if (ai.generate("A serene forest at dawn", &imageData, &imageLen)) {
 *       png.draw(0, 0, imageData, imageLen);
 *       free(imageData);  // Don't forget to free!
 *   }
 */

#ifndef GETIMG_AI_H
#define GETIMG_AI_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

// Available models on getimg.ai
enum GetimgModel {
    // Stable Diffusion models
    GETIMG_SD_1_5,              // stable-diffusion-v1-5
    GETIMG_SD_2_1,              // stable-diffusion-v2-1
    GETIMG_SDXL_1_0,            // stable-diffusion-xl-v1-0
    
    // Flux models (fast, high quality)
    GETIMG_FLUX_SCHNELL,        // flux-schnell (very fast)
    GETIMG_FLUX_DEV,            // flux-dev (higher quality)
    
    // Other models
    GETIMG_REALISTIC_VISION,    // realistic-vision-v5-1
    GETIMG_DREAM_SHAPER,        // dream-shaper-v8
};

// Output format
enum GetimgFormat {
    GETIMG_PNG,
    GETIMG_JPEG
};

// Result codes
enum GetimgResult {
    GETIMG_OK = 0,
    GETIMG_ERR_NO_WIFI,
    GETIMG_ERR_CONNECT_FAILED,
    GETIMG_ERR_REQUEST_FAILED,
    GETIMG_ERR_RESPONSE_ERROR,
    GETIMG_ERR_JSON_PARSE,
    GETIMG_ERR_NO_IMAGE,
    GETIMG_ERR_BASE64_DECODE,
    GETIMG_ERR_ALLOC_FAILED,
    GETIMG_ERR_TIMEOUT
};

class GetimgAI {
public:
    GetimgAI();
    
    /**
     * @brief Initialize with API key
     * @param apiKey Your getimg.ai API key
     */
    void begin(const char* apiKey);
    
    /**
     * @brief Set the model to use
     * @param model One of the GetimgModel values
     */
    void setModel(GetimgModel model) { _model = model; }
    
    /**
     * @brief Set the image dimensions
     * @param width Image width (typically 512, 768, or 1024)
     * @param height Image height (typically 512, 768, or 1024)
     * @note Some models have restrictions on dimensions
     */
    void setSize(int width, int height) { _width = width; _height = height; }
    
    /**
     * @brief Set output format
     * @param format GETIMG_PNG or GETIMG_JPEG
     */
    void setFormat(GetimgFormat format) { _format = format; }
    
    /**
     * @brief Set number of inference steps
     * @param steps Number of steps (more = higher quality but slower)
     * @note Flux-schnell ignores this (fixed at 4 steps)
     */
    void setSteps(int steps) { _steps = steps; }
    
    /**
     * @brief Set guidance scale (CFG)
     * @param scale How closely to follow the prompt (typically 7.0-15.0)
     */
    void setGuidance(float scale) { _guidance = scale; }
    
    /**
     * @brief Set negative prompt
     * @param prompt Things to avoid in the image
     */
    void setNegativePrompt(const char* prompt) { _negativePrompt = prompt; }
    
    /**
     * @brief Generate an image from a text prompt
     * 
     * @param prompt Text description of the image to generate
     * @param outData Pointer to receive allocated image data (caller must free!)
     * @param outLen Pointer to receive image data length
     * @param timeoutMs Timeout in milliseconds (default 60 seconds)
     * @return GetimgResult code
     * 
     * @note The returned data is allocated with pmalloc (PSRAM). 
     *       Caller is responsible for calling free() on outData.
     */
    GetimgResult generate(const char* prompt, uint8_t** outData, size_t* outLen, 
                          uint32_t timeoutMs = 60000);
    
    /**
     * @brief Get the last error message
     */
    const char* getLastError() const { return _lastError; }
    
    /**
     * @brief Get error string for result code
     */
    static const char* getErrorString(GetimgResult result);
    
private:
    const char* _apiKey;
    GetimgModel _model;
    GetimgFormat _format;
    int _width;
    int _height;
    int _steps;
    float _guidance;
    const char* _negativePrompt;
    char _lastError[128];
    
    const char* getModelString() const;
    const char* getEndpoint() const;
    const char* getFormatString() const;
    
    bool parseBase64Image(const char* json, char** base64Data, size_t* base64Len);
    size_t base64Decode(const char* input, size_t inputLen, uint8_t* output);
};

#endif // GETIMG_AI_H
