cmake_minimum_required(VERSION 3.20.0)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(status_led_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_sources(app PRIVATE
  src/main.c
  ../../src/status_led.c
  ../../src/status_led_policy.c
)
target_sources_ifdef(CONFIG_TEST_FAKE_LED_STRIP app PRIVATE src/fake_led_strip.c)
