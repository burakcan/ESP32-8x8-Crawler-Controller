/**
 * @file sound_profiles.c
 * @brief Engine sound profile definitions
 *
 * Each profile's sound files use unique prefixed variable names to avoid conflicts.
 */

#include "sound_profiles.h"

// ===========================================================================
// URAL 4320 - Russian military truck (default profile)
// ===========================================================================
#include "ural_idle.h"        // ural_idleSamples, ural_idleSampleCount, ural_idleSampleRate
#include "ural_rev.h"         // revSamples, revSampleCount, revSampleRate
#include "ural_knock.h"       // knockSamples, knockSampleCount, knockSampleRate
#include "ural_start.h"       // startSamples, startSampleCount, startSampleRate
#include "ural_jake_brake.h"  // jakeBrakeSamples, jakeBrakeSampleCount, jakeBrakeSampleRate

// ===========================================================================
// TATRA 813 - Czech 8x8 military truck
// ===========================================================================
#include "tatra813/tatra_idle.h"       // tatra_idleSamples, tatra_idleSampleCount, tatra_idleSampleRate
#include "tatra813/tatra_rev.h"        // tatra_revSamples, tatra_revSampleCount, tatra_revSampleRate
#include "tatra813/tatra_knock.h"      // tatra_knockSamples, tatra_knockSampleCount, tatra_knockSampleRate
#include "tatra813/tatra_start.h"      // tatra_startSamples, tatra_startSampleCount, tatra_startSampleRate
#include "tatra813/tatra_jake_brake.h" // tatra_jakeSamples, tatra_jakeSampleCount, tatra_jakeSampleRate

// ===========================================================================
// DETROIT 8V92 - American V8 diesel (Kenworth W900A)
// ===========================================================================
#include "detroit8v92/detroit_idle.h"       // detroit_idleSamples, detroit_idleSampleCount, detroit_idleSampleRate
#include "detroit8v92/detroit_rev.h"        // detroit_revSamples, detroit_revSampleCount, detroit_revSampleRate
#include "detroit8v92/detroit_knock.h"      // detroit_knockSamples, detroit_knockSampleCount, detroit_knockSampleRate
#include "detroit8v92/detroit_start.h"      // detroit_startSamples, detroit_startSampleCount, detroit_startSampleRate
#include "detroit8v92/detroit_jake_brake.h" // detroit_jakeSamples, detroit_jakeSampleCount, detroit_jakeSampleRate

// ===========================================================================
// CAT 3408 - Caterpillar V8 diesel
// ===========================================================================
#include "cat3408/cat_idle.h"   // cat_idleSamples, cat_idleSampleCount, cat_idleSampleRate
#include "cat3408/cat_rev.h"    // cat_revSamples, cat_revSampleCount, cat_revSampleRate
#include "cat3408/cat_knock.h"  // cat_knockSamples, cat_knockSampleCount, cat_knockSampleRate
#include "cat3408/cat_start.h"  // cat_startSamples, cat_startSampleCount, cat_startSampleRate

// ===========================================================================
// Profile Definitions
// ===========================================================================

static const sound_profile_def_t profiles[SOUND_PROFILE_COUNT] = {
    // URAL 4320 (index 0)
    {
        .name = "URAL 4320",
        .description = "Russian military truck V8",
        .idle = {
            .samples = (const int8_t*)ural_idleSamples,
            .sample_count = ural_idleSampleCount,
            .sample_rate = ural_idleSampleRate
        },
        .rev = {
            .samples = (const int8_t*)revSamples,
            .sample_count = revSampleCount,
            .sample_rate = revSampleRate
        },
        .knock = {
            .samples = (const int8_t*)knockSamples,
            .sample_count = knockSampleCount,
            .sample_rate = knockSampleRate
        },
        .start = {
            .samples = (const int8_t*)startSamples,
            .sample_count = startSampleCount,
            .sample_rate = startSampleRate
        },
        .jake_brake = {
            .samples = (const int8_t*)jakeBrakeSamples,
            .sample_count = jakeBrakeSampleCount,
            .sample_rate = jakeBrakeSampleRate
        },
        .has_jake_brake = true,
        .cylinder_count = 8
    },
    // TATRA 813 (index 1)
    {
        .name = "Tatra 813",
        .description = "Czech 8x8 military truck V12",
        .idle = {
            .samples = (const int8_t*)tatra_idleSamples,
            .sample_count = tatra_idleSampleCount,
            .sample_rate = tatra_idleSampleRate
        },
        .rev = {
            .samples = (const int8_t*)tatra_revSamples,
            .sample_count = tatra_revSampleCount,
            .sample_rate = tatra_revSampleRate
        },
        .knock = {
            .samples = (const int8_t*)tatra_knockSamples,
            .sample_count = tatra_knockSampleCount,
            .sample_rate = tatra_knockSampleRate
        },
        .start = {
            .samples = (const int8_t*)tatra_startSamples,
            .sample_count = tatra_startSampleCount,
            .sample_rate = tatra_startSampleRate
        },
        .jake_brake = {
            .samples = (const int8_t*)tatra_jakeSamples,
            .sample_count = tatra_jakeSampleCount,
            .sample_rate = tatra_jakeSampleRate
        },
        .has_jake_brake = true,
        .cylinder_count = 12
    },
    // DETROIT 8V92 (index 2)
    {
        .name = "Detroit 8V92",
        .description = "American 2-stroke V8 diesel",
        .idle = {
            .samples = (const int8_t*)detroit_idleSamples,
            .sample_count = detroit_idleSampleCount,
            .sample_rate = detroit_idleSampleRate
        },
        .rev = {
            .samples = (const int8_t*)detroit_revSamples,
            .sample_count = detroit_revSampleCount,
            .sample_rate = detroit_revSampleRate
        },
        .knock = {
            .samples = (const int8_t*)detroit_knockSamples,
            .sample_count = detroit_knockSampleCount,
            .sample_rate = detroit_knockSampleRate
        },
        .start = {
            .samples = (const int8_t*)detroit_startSamples,
            .sample_count = detroit_startSampleCount,
            .sample_rate = detroit_startSampleRate
        },
        .jake_brake = {
            .samples = (const int8_t*)detroit_jakeSamples,
            .sample_count = detroit_jakeSampleCount,
            .sample_rate = detroit_jakeSampleRate
        },
        .has_jake_brake = true,
        .cylinder_count = 8
    },
    // CAT 3408 (index 3)
    {
        .name = "CAT 3408",
        .description = "Caterpillar V8 diesel",
        .idle = {
            .samples = (const int8_t*)cat_idleSamples,
            .sample_count = cat_idleSampleCount,
            .sample_rate = cat_idleSampleRate
        },
        .rev = {
            .samples = (const int8_t*)cat_revSamples,
            .sample_count = cat_revSampleCount,
            .sample_rate = cat_revSampleRate
        },
        .knock = {
            .samples = (const int8_t*)cat_knockSamples,
            .sample_count = cat_knockSampleCount,
            .sample_rate = cat_knockSampleRate
        },
        .start = {
            .samples = (const int8_t*)cat_startSamples,
            .sample_count = cat_startSampleCount,
            .sample_rate = cat_startSampleRate
        },
        .jake_brake = {
            .samples = NULL,
            .sample_count = 0,
            .sample_rate = 0
        },
        .has_jake_brake = false,
        .cylinder_count = 8
    }
};

const sound_profile_def_t* sound_profiles_get(sound_profile_t profile) {
    if (profile >= SOUND_PROFILE_COUNT) {
        return &profiles[SOUND_PROFILE_URAL_4320];
    }
    return &profiles[profile];
}

const char* sound_profiles_get_name(sound_profile_t profile) {
    if (profile >= SOUND_PROFILE_COUNT) {
        return "Unknown";
    }
    return profiles[profile].name;
}
