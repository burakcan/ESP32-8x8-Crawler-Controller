# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Use custom partition table
set(PARTITION_TABLE_CUSTOM_FILENAME "partitions.csv")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(8x8_crawler)

# Auto-generate build number from git commit count
execute_process(
    COMMAND git rev-list --count HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_COUNT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_COUNT)
    set(GIT_COMMIT_COUNT 0)
endif()

# Get short git hash for reference
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_SHORT_HASH)
    set(GIT_SHORT_HASH "unknown")
endif()

# Pass to compiler
add_compile_definitions(
    FW_BUILD_NUMBER=${GIT_COMMIT_COUNT}
    FW_GIT_HASH="${GIT_SHORT_HASH}"
)

# Create SPIFFS image from web directory
spiffs_create_partition_image(storage ${CMAKE_CURRENT_SOURCE_DIR}/web FLASH_IN_PROJECT)