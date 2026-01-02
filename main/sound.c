/**
 * @file sound.c
 * @brief Polyphonic sound system for 8x8 Crawler Controller
 *
 * Features:
 * - Additive synthesis for realistic bell/chime sounds
 * - Polyphonic voice mixing (up to 6 simultaneous voices)
 * - ADSR envelope generator
 * - Multiple waveform types
 *
 * Uses ESP-IDF I2S driver for MAX98357A amplifier output.
 */

#include "sound.h"
#include "config.h"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "SOUND";

// Audio parameters
#define SAMPLE_RATE         16000   // 16kHz (more compatible)
#define BITS_PER_SAMPLE     16
#define MAX_VOICES          6       // Maximum simultaneous voices
#define BELL_PARTIALS       9       // Number of partials for bell synthesis

// Math constants
#define PI                  3.14159265358979f
#define TWO_PI              6.28318530717959f

// I2S channel handle
static i2s_chan_handle_t tx_handle = NULL;
static bool sound_initialized = false;
static bool sound_playing = false;
static uint8_t master_volume = 70;

// Bell partial frequency ratios (inharmonic series for realistic bell sound)
// Based on analysis of real bells - these create the metallic "bell" timbre
static const float bell_ratios[BELL_PARTIALS] = {
    0.56f,   // Sub-harmonic
    0.92f,   // Below fundamental
    1.00f,   // Fundamental
    1.19f,   // Slightly sharp
    1.71f,   // ~major 6th
    2.00f,   // Octave
    2.74f,   // ~10th
    3.00f,   // Octave + 5th
    3.76f    // High partial
};

// Amplitude ratios for each partial (decreasing for natural sound)
static const float bell_amps[BELL_PARTIALS] = {
    0.12f,   // Sub-harmonic (subtle)
    0.25f,   // Below fundamental
    1.00f,   // Fundamental (loudest)
    0.50f,   // Slightly sharp
    0.35f,   // Major 6th
    0.25f,   // Octave
    0.15f,   // 10th
    0.10f,   // High
    0.05f    // Highest (very subtle)
};

// Decay rates for each partial (higher partials decay faster)
static const float bell_decays[BELL_PARTIALS] = {
    2.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f
};

// ADSR Envelope structure
typedef struct {
    float attack;       // Attack time in seconds
    float decay;        // Decay time in seconds
    float sustain;      // Sustain level (0-1)
    float release;      // Release time in seconds
} adsr_t;

// Voice structure for polyphonic playback
typedef struct {
    bool active;
    float frequency;
    float amplitude;
    float phase[BELL_PARTIALS];     // Phase for each partial
    float partial_amp[BELL_PARTIALS]; // Current amplitude of each partial
    uint32_t sample_count;
    uint32_t total_samples;
    adsr_t envelope;
    float env_level;
    bool releasing;
    uint32_t release_start;
} voice_t;

static voice_t voices[MAX_VOICES];

// Waveform generators
static inline float sine_wave(float phase) {
    return sinf(phase);
}

static inline float triangle_wave(float phase) {
    float t = phase / TWO_PI;
    return 4.0f * fabsf(t - floorf(t + 0.5f)) - 1.0f;
}

// Fast approximation of sin for better performance
static inline float fast_sin(float x) {
    // Normalize to -PI to PI
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;

    // Parabola approximation
    const float B = 4.0f / PI;
    const float C = -4.0f / (PI * PI);
    float y = B * x + C * x * fabsf(x);

    // Extra precision
    const float P = 0.225f;
    y = P * (y * fabsf(y) - y) + y;
    return y;
}

/**
 * @brief Calculate ADSR envelope value
 */
static float calc_envelope(voice_t *voice, uint32_t sample_idx) {
    float time = (float)sample_idx / SAMPLE_RATE;
    adsr_t *env = &voice->envelope;

    if (voice->releasing) {
        // Release phase
        float release_time = (float)(sample_idx - voice->release_start) / SAMPLE_RATE;
        if (release_time >= env->release) {
            return 0.0f;
        }
        return voice->env_level * (1.0f - release_time / env->release);
    }

    if (time < env->attack) {
        // Attack phase
        return time / env->attack;
    }

    time -= env->attack;
    if (time < env->decay) {
        // Decay phase
        float decay_progress = time / env->decay;
        return 1.0f - decay_progress * (1.0f - env->sustain);
    }

    // Sustain phase
    return env->sustain;
}

/**
 * @brief Initialize a voice for bell synthesis
 */
static void init_bell_voice(int voice_idx, float frequency, float amplitude,
                            float attack, float decay, float sustain, float release,
                            uint32_t duration_ms) {
    voice_t *v = &voices[voice_idx];

    v->active = true;
    v->frequency = frequency;
    v->amplitude = amplitude;
    v->sample_count = 0;
    v->total_samples = (SAMPLE_RATE * duration_ms) / 1000;
    v->releasing = false;
    v->release_start = 0;
    v->env_level = 1.0f;

    v->envelope.attack = attack;
    v->envelope.decay = decay;
    v->envelope.sustain = sustain;
    v->envelope.release = release;

    // Initialize phases and partial amplitudes
    for (int i = 0; i < BELL_PARTIALS; i++) {
        v->phase[i] = 0.0f;
        v->partial_amp[i] = bell_amps[i];
    }
}

/**
 * @brief Find a free voice slot
 */
static int find_free_voice(void) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            return i;
        }
    }
    // Steal oldest voice
    return 0;
}

/**
 * @brief Generate one sample from a bell voice using additive synthesis
 */
static float generate_bell_sample(voice_t *v) {
    if (!v->active) return 0.0f;

    float time = (float)v->sample_count / SAMPLE_RATE;
    float sample = 0.0f;

    // Simple exponential decay envelope (bypass complex ADSR for now)
    float env = expf(-3.0f * time);  // ~1 second decay

    // Sum just a few partials for simpler bell sound
    for (int i = 0; i < 5; i++) {  // Only use first 5 partials
        // Each partial decays at its own rate
        float partial_decay = expf(-bell_decays[i] * time);

        // Use standard sinf instead of fast_sin for debugging
        float phase = v->phase[i];
        float partial_sample = sinf(phase) * bell_amps[i] * partial_decay;
        sample += partial_sample;

        // Advance phase for this partial
        float partial_freq = v->frequency * bell_ratios[i];
        v->phase[i] += TWO_PI * partial_freq / SAMPLE_RATE;
        if (v->phase[i] >= TWO_PI) {
            v->phase[i] -= TWO_PI;
        }
    }

    // Apply envelope and amplitude
    sample *= env * v->amplitude;

    // Normalize
    sample /= 2.0f;

    v->sample_count++;

    // Check if voice is done
    if (v->sample_count >= v->total_samples || env < 0.01f) {
        v->active = false;
    }

    return sample;
}

/**
 * @brief Mix all active voices into a buffer
 */
static void mix_voices(int16_t *buffer, size_t num_samples) {
    for (size_t i = 0; i < num_samples; i++) {
        float mix = 0.0f;

        // Sum all active voices
        for (int v = 0; v < MAX_VOICES; v++) {
            if (voices[v].active) {
                mix += generate_bell_sample(&voices[v]);
            }
        }

        // Apply master volume and clamp
        mix *= (float)master_volume / 100.0f;

        // Simple hard clipping
        if (mix > 1.0f) mix = 1.0f;
        if (mix < -1.0f) mix = -1.0f;

        // Convert to 16-bit
        int16_t sample_int = (int16_t)(mix * 32000.0f);

        // Stereo output (same on both channels)
        buffer[i * 2] = sample_int;
        buffer[i * 2 + 1] = sample_int;
    }
}

/**
 * @brief Check if any voice is still active
 */
static bool any_voice_active(void) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active) return true;
    }
    return false;
}

/**
 * @brief Clear all voices
 */
static void clear_all_voices(void) {
    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].active = false;
    }
}

/**
 * @brief Play all queued voices until complete
 */
static esp_err_t play_voices(void) {
    if (!sound_initialized || tx_handle == NULL) {
        ESP_LOGE(TAG, "play_voices: not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Count active voices
    int active_count = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active) active_count++;
    }
    ESP_LOGI(TAG, "Playing %d active voices", active_count);

    if (active_count == 0) {
        ESP_LOGW(TAG, "No active voices to play");
        return ESP_OK;
    }

    const size_t buffer_samples = 256;
    int16_t *buffer = heap_caps_malloc(buffer_samples * sizeof(int16_t) * 2, MALLOC_CAP_DMA);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffer");
        return ESP_ERR_NO_MEM;
    }

    sound_playing = true;
    size_t bytes_written = 0;
    int loop_count = 0;

    while (any_voice_active() && sound_playing) {
        mix_voices(buffer, buffer_samples);

        esp_err_t ret = i2s_channel_write(tx_handle, buffer,
                                          buffer_samples * sizeof(int16_t) * 2,
                                          &bytes_written, pdMS_TO_TICKS(1000));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(ret));
            break;
        }
        loop_count++;
    }
    ESP_LOGI(TAG, "Played %d buffers", loop_count);

    // Play a bit of silence to let the sound ring out
    memset(buffer, 0, buffer_samples * sizeof(int16_t) * 2);
    for (int i = 0; i < 4; i++) {
        i2s_channel_write(tx_handle, buffer,
                          buffer_samples * sizeof(int16_t) * 2,
                          &bytes_written, pdMS_TO_TICKS(1000));
    }

    free(buffer);
    sound_playing = false;

    return ESP_OK;
}

/**
 * @brief Play a simple sine tone (for basic effects)
 */
static esp_err_t generate_simple_tone(uint32_t frequency_hz, uint32_t duration_ms, uint8_t volume) {
    if (!sound_initialized || tx_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t total_samples = (SAMPLE_RATE * duration_ms) / 1000;
    size_t buffer_samples = 256;
    int16_t *buffer = heap_caps_malloc(buffer_samples * sizeof(int16_t) * 2, MALLOC_CAP_DMA);
    if (!buffer) {
        return ESP_ERR_NO_MEM;
    }

    float amplitude = (32767.0f * volume * master_volume) / 10000.0f;
    float phase = 0.0f;
    float phase_increment = TWO_PI * frequency_hz / SAMPLE_RATE;
    uint32_t attack_samples = SAMPLE_RATE / 100;
    uint32_t decay_samples = SAMPLE_RATE / 50;

    sound_playing = true;
    size_t bytes_written = 0;
    uint32_t sample_index = 0;

    while (sample_index < total_samples && sound_playing) {
        uint32_t chunk_samples = total_samples - sample_index;
        if (chunk_samples > buffer_samples) chunk_samples = buffer_samples;

        for (uint32_t i = 0; i < chunk_samples; i++) {
            float envelope = 1.0f;
            uint32_t abs_sample = sample_index + i;
            if (abs_sample < attack_samples) {
                envelope = (float)abs_sample / attack_samples;
            } else if (abs_sample > total_samples - decay_samples) {
                envelope = (float)(total_samples - abs_sample) / decay_samples;
            }

            int16_t sample_int = (int16_t)(fast_sin(phase) * amplitude * envelope);
            buffer[i * 2] = sample_int;
            buffer[i * 2 + 1] = sample_int;

            phase += phase_increment;
            if (phase >= TWO_PI) phase -= TWO_PI;
        }

        i2s_channel_write(tx_handle, buffer, chunk_samples * sizeof(int16_t) * 2,
                          &bytes_written, pdMS_TO_TICKS(1000));
        sample_index += chunk_samples;
    }

    free(buffer);
    sound_playing = false;
    return ESP_OK;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t sound_init(void) {
    if (sound_initialized) {
        ESP_LOGW(TAG, "Sound already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing I2S for MAX98357A (BCLK=%d, LRC=%d, DOUT=%d)",
             PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DOUT);

    // Clear voice array
    clear_all_voices();

    // Channel configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;

    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2S channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // Standard mode configuration
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws = PIN_I2S_LRC,
            .dout = PIN_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return ret;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable I2S channel: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
        return ret;
    }

    sound_initialized = true;
    ESP_LOGI(TAG, "Sound system initialized (sample rate: %d Hz, %d voices)",
             SAMPLE_RATE, MAX_VOICES);

    return ESP_OK;
}

esp_err_t sound_deinit(void) {
    if (!sound_initialized) return ESP_OK;

    sound_stop();

    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }

    sound_initialized = false;
    ESP_LOGI(TAG, "Sound system deinitialized");
    return ESP_OK;
}

esp_err_t sound_play_boot_chime(void) {
    if (!sound_initialized) {
        ESP_LOGW(TAG, "Sound not initialized, skipping boot chime");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Playing boot chime...");

    // === 8x8 CRAWLER BOOT SEQUENCE ===
    // Theme: Rugged, powerful, ready for adventure

    // Part 1: Power-up rumble (low frequency "engine starting" feel)
    ESP_LOGI(TAG, "Power up...");
    generate_simple_tone(65, 150, 90);    // Low C2 rumble
    generate_simple_tone(82, 100, 85);    // E2
    generate_simple_tone(98, 80, 80);     // G2
    vTaskDelay(pdMS_TO_TICKS(50));

    // Part 2: Systems online - rising power sequence
    ESP_LOGI(TAG, "Systems online...");
    const float power_up[] = {130.81f, 164.81f, 196.0f, 261.63f};  // C3, E3, G3, C4
    for (int i = 0; i < 4; i++) {
        generate_simple_tone((uint32_t)power_up[i], 60, 75);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // Part 3: Ready signal - triumphant bell chord (power fifth + octave)
    ESP_LOGI(TAG, "Ready...");
    clear_all_voices();

    // Epic power chord: Root + Fifth + Octave (like a vehicle horn fanfare)
    // Using E major for a bright, powerful sound
    const float ready_chord[] = {
        329.63f,   // E4 (root)
        493.88f,   // B4 (fifth)
        659.25f,   // E5 (octave)
    };

    for (int i = 0; i < 3; i++) {
        int voice = find_free_voice();
        float amp = 0.7f - (i * 0.1f);
        init_bell_voice(voice, ready_chord[i], amp, 0.005f, 0.25f, 0.15f, 0.8f, 1200);
    }
    play_voices();

    vTaskDelay(pdMS_TO_TICKS(150));

    // Part 4: Final accent - high "locked and loaded" ping
    clear_all_voices();
    int v = find_free_voice();
    init_bell_voice(v, 1318.5f, 0.5f, 0.002f, 0.15f, 0.05f, 0.4f, 600);  // E6
    play_voices();

    ESP_LOGI(TAG, "Boot chime complete");
    return ESP_OK;
}

esp_err_t sound_play(sound_effect_t effect) {
    if (!sound_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    clear_all_voices();

    switch (effect) {
        case SOUND_BOOT_CHIME:
            return sound_play_boot_chime();

        case SOUND_WIFI_ON: {
            // Rising bell arpeggio
            const float freqs[] = {659.25f, 830.61f, 987.77f};  // E5, G#5, B5
            for (int i = 0; i < 3; i++) {
                vTaskDelay(pdMS_TO_TICKS(60));
                int v = find_free_voice();
                init_bell_voice(v, freqs[i], 0.5f, 0.003f, 0.2f, 0.1f, 0.4f, 500);
            }
            play_voices();
            break;
        }

        case SOUND_WIFI_OFF: {
            // Falling bell arpeggio
            const float freqs[] = {987.77f, 830.61f, 659.25f};  // B5, G#5, E5
            for (int i = 0; i < 3; i++) {
                vTaskDelay(pdMS_TO_TICKS(60));
                int v = find_free_voice();
                init_bell_voice(v, freqs[i], 0.5f, 0.003f, 0.2f, 0.1f, 0.4f, 500);
            }
            play_voices();
            break;
        }

        case SOUND_CALIBRATION: {
            // Attention bell - single clear tone
            int v = find_free_voice();
            init_bell_voice(v, 880.0f, 0.6f, 0.002f, 0.3f, 0.1f, 0.5f, 600);
            play_voices();
            break;
        }

        case SOUND_ERROR: {
            // Dissonant low bells
            int v1 = find_free_voice();
            int v2 = find_free_voice();
            init_bell_voice(v1, 220.0f, 0.6f, 0.01f, 0.4f, 0.2f, 0.3f, 500);
            init_bell_voice(v2, 233.08f, 0.5f, 0.01f, 0.4f, 0.2f, 0.3f, 500);  // Slightly detuned
            play_voices();
            break;
        }

        default:
            return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t sound_play_tone(uint32_t frequency_hz, uint32_t duration_ms, uint8_t volume) {
    if (!sound_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (frequency_hz < 20 || frequency_hz > 20000) {
        return ESP_ERR_INVALID_ARG;
    }

    if (volume > 100) volume = 100;

    return generate_simple_tone(frequency_hz, duration_ms, volume);
}

esp_err_t sound_stop(void) {
    sound_playing = false;
    clear_all_voices();
    return ESP_OK;
}

bool sound_is_playing(void) {
    return sound_playing;
}

void sound_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    master_volume = volume;
}

uint8_t sound_get_volume(void) {
    return master_volume;
}
