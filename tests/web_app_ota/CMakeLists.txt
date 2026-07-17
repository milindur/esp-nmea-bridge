cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(web_app_ota_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_compile_definitions(app PRIVATE
  CONFIG_ESP_NMEA_BRIDGE_WEB_APP_ENABLE=1
  CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE=1
  CONFIG_ESP_NMEA_BRIDGE_OTA_UPLOAD_TRUSTED_NETWORK_ENABLE=1
)
target_sources(app PRIVATE
  src/main.c
  ../../src/web_app.c
)
