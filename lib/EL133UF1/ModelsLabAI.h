/**
 * @file ModelsLabAI.h
 * @brief ModelsLab (Stable Diffusion API) image generation client for ESP/Pico
 * 
 * This client uses the ModelsLab API to generate images from text prompts.
 * Supports various models including Stable Diffusion, SDXL, Flux, and more.
 * 
 * API Reference: https://docs.modelslab.com/
 * 
 * Memory considerations:
 * - Generated images are stored in PSRAM
 * - Base64 decoding requires ~33% more memory temporarily during decode
 * - Typical image sizes: 512x512 to 1024x1024
 * 
 * Usage:
 *   ModelsLabAI ai;
 *   ai.begin("your-api-key");
 *   ai.setModel(MODELSLAB_FLUX_SCHNELL);
 *   
 *   uint8_t* imageData = nullptr;
 *   size_t imageLen = 0;
 *   
 *   if (ai.generate("A serene forest at dawn", &imageData, &imageLen)) {
 *       png.draw(0, 0, imageData, imageLen);
 *       free(imageData);  // Don't forget to free!
 *   }
 */

#ifndef MODELSLAB_AI_H
#define MODELSLAB_AI_H

#include <Arduino.h>
#include <WiFiClientSecure.h>

// Available models on ModelsLab
enum ModelsLabModel {
    // Flux models
    MODELSLAB_FLUX_SCHNELL,     // flux-schnell (fast)
    MODELSLAB_FLUX_DEV,         // flux-dev
    
    // Stable Diffusion models
    MODELSLAB_SD_1_5,           // sd-1.5
    MODELSLAB_SD_2_1,           // sd-2.1
    MODELSLAB_SDXL,             // sdxl
    MODELSLAB_SDXL_TURBO,       // sdxl-turbo (fast)
    
    // Specialized models
    MODELSLAB_REALISTIC_VISION, // realistic-vision-v5.1
    MODELSLAB_DREAMSHAPER,      // dreamshaper-v8
    MODELSLAB_DELIBERATE,       // deliberate-v3
};

// Result codes
enum ModelsLabResult {
    MODELSLAB_OK = 0,
    MODELSLAB_ERR_NO_WIFI,
    MODELSLAB_ERR_CONNECT_FAILED,
    MODELSLAB_ERR_REQUEST_FAILED,
    MODELSLAB_ERR_RESPONSE_ERROR,
    MODELSLAB_ERR_JSON_PARSE,
    MODELSLAB_ERR_NO_IMAGE,
    MODELSLAB_ERR_DOWNLOAD_FAILED,
    MODELSLAB_ERR_BASE64_DECODE,
    MODELSLAB_ERR_ALLOC_FAILED,
    MODELSLAB_ERR_TIMEOUT,
    MODELSLAB_ERR_PROCESSING     // Image still processing (async)
};

class ModelsLabAI {
public:
    ModelsLabAI();
    
    /**
     * @brief Initialize with API key
     * @param apiKey Your ModelsLab API key
     */
    void begin(const char* apiKey);
    
    /**
     * @brief Set the model to use
     * @param model One of the ModelsLabModel values
     */
    void setModel(ModelsLabModel model) { _model = model; }
    
    /**
     * @brief Set the image dimensions
     * @param width Image width (typically 512, 768, or 1024)
     * @param height Image height (typically 512, 768, or 1024)
     */
    void setSize(int width, int height) { _width = width; _height = height; }
    
    /**
     * @brief Set number of inference steps
     * @param steps Number of steps (more = higher quality but slower)
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
     * @brief Set scheduler/sampler
     * @param scheduler Scheduler name (e.g., "UniPCMultistepScheduler", "DPMSolverMultistepScheduler")
     */
    void setScheduler(const char* scheduler) { _scheduler = scheduler; }
    
    /**
     * @brief Generate an image from a text prompt
     * 
     * @param prompt Text description of the image to generate
     * @param outData Pointer to receive allocated image data (caller must free!)
     * @param outLen Pointer to receive image data length
     * @param timeoutMs Timeout in milliseconds (default 90 seconds)
     * @return ModelsLabResult code
     * 
     * @note The returned data is allocated with pmalloc (PSRAM). 
     *       Caller is responsible for calling free() on outData.
     */
    ModelsLabResult generate(const char* prompt, uint8_t** outData, size_t* outLen, 
                              uint32_t timeoutMs = 90000);
    
    /**
     * @brief Get the last error message
     */
    const char* getLastError() const { return _lastError; }
    
    /**
     * @brief Get error string for result code
     */
    static const char* getErrorString(ModelsLabResult result);
    
private:
    const char* _apiKey;
    ModelsLabModel _model;
    int _width;
    int _height;
    int _steps;
    float _guidance;
    const char* _negativePrompt;
    const char* _scheduler;
    char _lastError[128];
    
    const char* getModelString() const;
    const char* getEndpoint() const;
    
    bool parseImageResponse(const char* json, char* urlOrBase64, size_t bufLen, bool* isBase64);
    ModelsLabResult downloadImage(const char* url, uint8_t** outData, size_t* outLen);
    size_t base64Decode(const char* input, size_t inputLen, uint8_t* output);
};

#endif // MODELSLAB_AI_H
