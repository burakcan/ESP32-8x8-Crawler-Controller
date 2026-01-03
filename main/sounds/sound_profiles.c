/**
 * @file sound_profiles.c
 * @brief Engine sound profile definitions
 *
 * Each profile's sound files use unique prefixed variable names to avoid conflicts.
 */

#include "sound_profiles.h"

// ===========================================================================
// CAT 3408 - Caterpillar V8 diesel
// ===========================================================================
#include "cat3408/cat_idle.h"   // cat_idleSamples, cat_idleSampleCount, cat_idleSampleRate
#include "cat3408/cat_rev.h"    // cat_revSamples, cat_revSampleCount, cat_revSampleRate
#include "cat3408/cat_knock.h"  // cat_knockSamples, cat_knockSampleCount, cat_knockSampleRate
#include "cat3408/cat_start.h"  // cat_startSamples, cat_startSampleCount, cat_startSampleRate

// ===========================================================================
// UNIMOG U1000 - Mercedes turbo diesel off-road
// ===========================================================================
#include "unimog/UnimogU1000TurboIdle.h"     // unimog_idleSamples, unimog_idleSampleCount, unimog_idleSampleRate
#include "unimog/UnimogU1000TurboRev.h"      // unimog_revSamples, unimog_revSampleCount, unimog_revSampleRate
#include "unimog/UnimogU1000TurboKnock.h"    // unimog_knockSamples, unimog_knockSampleCount, unimog_knockSampleRate
#include "unimog/UnimogU1000TurboJakeBrake.h"// unimog_jakeSamples, unimog_jakeSampleCount, unimog_jakeSampleRate

// ===========================================================================
// MAN KAT - German military truck
// ===========================================================================
#include "mankat/ManKatIdle.h"      // mankat_idleSamples, mankat_idleSampleCount, mankat_idleSampleRate
#include "mankat/ManKatRev.h"       // mankat_revSamples, mankat_revSampleCount, mankat_revSampleRate
#include "mankat/ManKatKnock.h"     // mankat_knockSamples, mankat_knockSampleCount, mankat_knockSampleRate
#include "mankat/ManKatStart.h"     // mankat_startSamples, mankat_startSampleCount, mankat_startSampleRate
#include "mankat/ManKatJakebrake.h" // mankat_jakeSamples, mankat_jakeSampleCount, mankat_jakeSampleRate

// ===========================================================================
// Profile Definitions
// ===========================================================================

static const sound_profile_def_t profiles[SOUND_PROFILE_COUNT] = {
    // CAT 3408 (index 0)
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
    },
    // UNIMOG U1000 (index 1)
    {
        .name = "Unimog U1000",
        .description = "Mercedes turbo diesel off-road",
        .idle = {
            .samples = (const int8_t*)unimog_idleSamples,
            .sample_count = unimog_idleSampleCount,
            .sample_rate = unimog_idleSampleRate
        },
        .rev = {
            .samples = (const int8_t*)unimog_revSamples,
            .sample_count = unimog_revSampleCount,
            .sample_rate = unimog_revSampleRate
        },
        .knock = {
            .samples = (const int8_t*)unimog_knockSamples,
            .sample_count = unimog_knockSampleCount,
            .sample_rate = unimog_knockSampleRate
        },
        .start = {
            .samples = NULL,
            .sample_count = 0,
            .sample_rate = 0
        },
        .jake_brake = {
            .samples = (const int8_t*)unimog_jakeSamples,
            .sample_count = unimog_jakeSampleCount,
            .sample_rate = unimog_jakeSampleRate
        },
        .has_jake_brake = true,
        .cylinder_count = 6
    },
    // MAN KAT (index 2)
    {
        .name = "MAN KAT",
        .description = "German military truck diesel",
        .idle = {
            .samples = (const int8_t*)mankat_idleSamples,
            .sample_count = mankat_idleSampleCount,
            .sample_rate = mankat_idleSampleRate
        },
        .rev = {
            .samples = (const int8_t*)mankat_revSamples,
            .sample_count = mankat_revSampleCount,
            .sample_rate = mankat_revSampleRate
        },
        .knock = {
            .samples = (const int8_t*)mankat_knockSamples,
            .sample_count = mankat_knockSampleCount,
            .sample_rate = mankat_knockSampleRate
        },
        .start = {
            .samples = (const int8_t*)mankat_startSamples,
            .sample_count = mankat_startSampleCount,
            .sample_rate = mankat_startSampleRate
        },
        .jake_brake = {
            .samples = (const int8_t*)mankat_jakeSamples,
            .sample_count = mankat_jakeSampleCount,
            .sample_rate = mankat_jakeSampleRate
        },
        .has_jake_brake = true,
        .cylinder_count = 8
    }
};

const sound_profile_def_t* sound_profiles_get(sound_profile_t profile) {
    if (profile >= SOUND_PROFILE_COUNT) {
        return &profiles[SOUND_PROFILE_CAT_3408];
    }
    return &profiles[profile];
}

const char* sound_profiles_get_name(sound_profile_t profile) {
    if (profile >= SOUND_PROFILE_COUNT) {
        return "Unknown";
    }
    return profiles[profile].name;
}
