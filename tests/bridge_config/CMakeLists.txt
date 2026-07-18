cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bridge_config_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_compile_definitions(app PRIVATE
  CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_ENABLE=1
  CONFIG_ESP_NMEA_BRIDGE_AIS_SELF_MMSI_FILTER_MMSI=123456789
)
target_sources(app PRIVATE
  src/main.c
  ../../src/bridge_config.c
)
