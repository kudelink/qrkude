# Copyright 2026 The QRKude Authors
# SPDX-License-Identifier: Apache-2.0
if(IDF_VERSION_MAJOR GREATER_EQUAL 6 OR
   (IDF_VERSION_MAJOR EQUAL 5 AND IDF_VERSION_MINOR GREATER_EQUAL 3))
    set(reqs esp_timer esp_lcd esp_driver_spi esp_driver_gpio)
else()
    set(reqs esp_timer esp_lcd driver)
endif()
# With main/idf_component.yml (packaged example) the component manager adds
# the qrkude dependency itself; in the monorepo it comes from the shim.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/idf_component.yml")
    list(APPEND reqs qrkude)
endif()
idf_component_register(SRCS "main.c" "example_lcd.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES ${reqs})
