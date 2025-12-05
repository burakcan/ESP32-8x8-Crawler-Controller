#ifndef VERSION_H
#define VERSION_H

// Firmware version - update these for each release
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

// Build date - auto-generated via CMake (YYMMDD.HHMM format)
#ifndef FW_BUILD_DATE
#define FW_BUILD_DATE "000000.0000"
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
