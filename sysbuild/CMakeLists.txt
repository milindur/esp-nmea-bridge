# Sysbuild resolves boards before the application CMakeLists.txt runs, so keep
# the out-of-tree board root visible to MCUboot and all sysbuild images.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)
