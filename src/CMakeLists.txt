# ESP-IDF component registration
# Note: ESP32-P4 uses Arduino framework (esp32p4 environment)
# This file is kept for ESP-IDF builds but is not used for Arduino framework builds
idf_component_register(SRCS "es8311_simple.cpp"
                       INCLUDE_DIRS ".")
