cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esp_nmea_bridge)

target_sources(app PRIVATE
  src/main.c
  src/ais_mmsi_filter.c
  src/bridge_config.c
  src/nmea_bridge.c
  src/uart_nmea.c
  src/wifi_manager.c
  src/tcp_nmea_server.c
  src/tcp_nmea_session.c
  src/tcp_nmea_client.c
)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/web_assets.S.in
  ${CMAKE_CURRENT_BINARY_DIR}/web_assets.S
  @ONLY
)

# Keep every embedded WebApp asset listed here so UI-only OTA changes
# invalidate the generated incbin object without relying on implicit scans.
set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/web_assets.S PROPERTY OBJECT_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/web_assets/index.html
  ${CMAKE_CURRENT_SOURCE_DIR}/src/web_assets/app.js
  ${CMAKE_CURRENT_SOURCE_DIR}/src/web_assets/style.css
)

target_sources_ifdef(CONFIG_ESP_NMEA_BRIDGE_WEB_APP_ENABLE app PRIVATE
  src/bridge_telemetry.c
  src/web_app.c
  ${CMAKE_CURRENT_BINARY_DIR}/web_assets.S
)

target_sources_ifdef(CONFIG_ESP_NMEA_BRIDGE_OTA_UPDATE_ENABLE app PRIVATE
  src/ota_update.c
)

target_sources_ifdef(CONFIG_ESP_NMEA_BRIDGE_STATUS_LED_ENABLE app PRIVATE
  src/status_led.c
  src/status_led_policy.c
)

target_sources_ifdef(CONFIG_ESP_NMEA_BRIDGE_DNS_SD_ENABLE app PRIVATE
  src/nmea_dns_sd.c
)
