/**
 * @file sound_profiles.h
 * @brief Engine sound profiles for 8x8 crawler
 *
 * Defines available sound profiles and provides unified access
 * to sound samples regardless of selected profile.
 */

#ifndef SOUND_PROFILES_H
#define SOUND_PROFILES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Sound profile enumeration
typedef enum {
    SOUND_PROFILE_URAL_4320 = 0,    // Russian military truck (default)
    SOUND_PROFILE_TATRA_813,         // Czech 8x8 military truck
    SOUND_PROFILE_DETROIT_8V92,      // American V8 diesel
    SOUND_PROFILE_CAT_3408,          // Caterpillar V8 diesel
    SOUND_PROFILE_COUNT
} sound_profile_t;

// Sound sample structure
typedef struct {
    const int8_t *samples;
    uint32_t sample_count;
    uint32_t sample_rate;
} sound_sample_t;

// Sound profile structure
typedef struct {
    const char *name;
    const char *description;
    sound_sample_t idle;
    sound_sample_t rev;
    sound_sample_t knock;
    sound_sample_t start;
    sound_sample_t jake_brake;
    bool has_jake_brake;
    uint8_t cylinder_count;         // For knock interval
} sound_profile_def_t;

// Get profile definition by ID
const sound_profile_def_t* sound_profiles_get(sound_profile_t profile);

// Get profile name
const char* sound_profiles_get_name(sound_profile_t profile);

#endif // SOUND_PROFILES_H
