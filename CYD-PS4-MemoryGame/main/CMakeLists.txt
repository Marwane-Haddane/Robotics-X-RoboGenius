set(srcs
        "main.c"
        "sketch.cpp"
        "cyd_display.cpp")

set(requires "bluepad32" "bluepad32_arduino" "arduino" "btstack"
             "esp_lcd" "esp_driver_gpio" "esp_driver_spi")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    REQUIRES "${requires}")
