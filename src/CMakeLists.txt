# Only compile main_espidf.c for ESP-IDF build
idf_component_register(SRCS "main_espidf.c"
                       INCLUDE_DIRS ".")
