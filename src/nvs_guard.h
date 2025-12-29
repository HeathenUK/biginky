/**
 * @file nvs_guard.h
 * @brief RAII wrapper for ESP32 Preferences (NVS) to ensure proper begin/end pairing
 * 
 * This wrapper ensures that Preferences.begin() and Preferences.end() are always
 * called in pairs, even if an exception or early return occurs. This prevents
 * NVS locks and resource leaks.
 * 
 * Usage:
 *   // For local Preferences objects:
 *   {
 *       NVSGuard guard("namespace", true);  // read-only
 *       if (!guard.isOpen()) {
 *           // Handle error
 *           return;
 *       }
 *       Preferences& p = guard.get();
 *       String value = p.getString("key", "");
 *   } // Automatically calls end() here
 * 
 *   // For global Preferences objects:
 *   {
 *       NVSGuard guard(volumePrefs, "volume", false);  // read-write
 *       if (!guard.isOpen()) {
 *           return;
 *       }
 *       guard.get().putInt("level", 50);
 *   } // Automatically calls end() here
 */

#ifndef NVS_GUARD_H
#define NVS_GUARD_H

#include <Preferences.h>

/**
 * RAII wrapper for Preferences to ensure begin/end pairing
 */
class NVSGuard {
public:
    /**
     * Constructor for local Preferences object (creates its own Preferences instance)
     * @param namespace_name NVS namespace to open
     * @param read_only If true, open in read-only mode; if false, open in read-write mode
     */
    NVSGuard(const char* namespace_name, bool read_only = true)
        : prefs_(nullptr)
        , owns_prefs_(true)
        , is_open_(false)
    {
        prefs_ = new Preferences();
        if (prefs_ != nullptr) {
            is_open_ = prefs_->begin(namespace_name, read_only);
        }
    }

    /**
     * Constructor for existing Preferences object (uses provided Preferences instance)
     * @param prefs Reference to existing Preferences object
     * @param namespace_name NVS namespace to open
     * @param read_only If true, open in read-only mode; if false, open in read-write mode
     */
    NVSGuard(Preferences& prefs, const char* namespace_name, bool read_only = true)
        : prefs_(&prefs)
        , owns_prefs_(false)
        , is_open_(false)
    {
        is_open_ = prefs_->begin(namespace_name, read_only);
    }

    /**
     * Destructor - automatically calls end() if begin() succeeded
     */
    ~NVSGuard() {
        if (is_open_ && prefs_ != nullptr) {
            prefs_->end();
        }
        if (owns_prefs_ && prefs_ != nullptr) {
            delete prefs_;
            prefs_ = nullptr;
        }
    }

    // Delete copy constructor and assignment operator (non-copyable)
    NVSGuard(const NVSGuard&) = delete;
    NVSGuard& operator=(const NVSGuard&) = delete;

    /**
     * Check if the Preferences namespace was successfully opened
     * @return true if begin() succeeded, false otherwise
     */
    bool isOpen() const {
        return is_open_;
    }

    /**
     * Get reference to the Preferences object
     * @return Reference to Preferences object (valid only if isOpen() returns true)
     */
    Preferences& get() {
        return *prefs_;
    }

    /**
     * Operator overload for convenience (allows guard.get() to be written as guard->)
     * @return Pointer to Preferences object
     */
    Preferences* operator->() {
        return prefs_;
    }

private:
    Preferences* prefs_;      // Pointer to Preferences object (owned or external)
    bool owns_prefs_;         // True if we own the Preferences object, false if external
    bool is_open_;            // True if begin() succeeded
};

#endif // NVS_GUARD_H

