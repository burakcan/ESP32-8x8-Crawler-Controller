#ifndef VERSION_H
#define VERSION_H

// Firmware version - update these for each release
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

// Build number - auto-generated from git commit count via CMake
// FW_BUILD_NUMBER is defined by CMakeLists.txt as a compiler flag
#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif

// Git hash - auto-generated via CMake
#ifndef FW_GIT_HASH
#define FW_GIT_HASH "unknown"
#endif

// Version string macro
#define FW_VERSION_STR_HELPER(major, minor, patch) #major "." #minor "." #patch
#define FW_VERSION_STR(major, minor, patch) FW_VERSION_STR_HELPER(major, minor, patch)
#define FW_VERSION FW_VERSION_STR(FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH)

#endif // VERSION_H
