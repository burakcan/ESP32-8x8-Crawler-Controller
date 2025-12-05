# 8x8 Crawler Controller
cmake_minimum_required(VERSION 3.16)

# Use custom partition table
set(PARTITION_TABLE_CUSTOM_FILENAME "partitions.csv")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(8x8_crawler)

# Create SPIFFS image from web directory
spiffs_create_partition_image(storage ${CMAKE_CURRENT_SOURCE_DIR}/web FLASH_IN_PROJECT)
