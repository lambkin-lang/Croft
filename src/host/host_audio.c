#include "croft/host_audio.h"
#include <stdio.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static ma_engine g_engine;
static int g_initialized = 0;

int32_t host_audio_init(void) {
    if (g_initialized) return 0;
    
    ma_result result = ma_engine_init(NULL, &g_engine);
    if (result != MA_SUCCESS) {
        printf("Failed to initialize miniaudio engine.\n");
        return -1;
    }
    
    g_initialized = 1;
    return 0;
}

void host_audio_terminate(void) {
    if (!g_initialized) return;
    
    ma_engine_uninit(&g_engine);
    g_initialized = 0;
}

// User Requirements: "ADSR and a frequency that starts slightly sharp and ends slighly flat."
int32_t host_audio_play_tone(float start_freq, float end_freq, float duration_sec) {
    if (!g_initialized) return -1;
    
    // N.B. miniaudio doesn't have an out-of-the-box sweeping oscillator with ADSR,
    // but we can spawn a waveform and manually fade it if we build a custom node...
    // simpler: use a sound group or direct manipulation of ma_waveform.
    
    // Wait, the easiest primitive we can do natively is ma_waveform for a steady tone,
    // combined with a volume fade out (decay/release).
    ma_waveform_config config = ma_waveform_config_init(
        g_engine.pDevice->playback.format,
        g_engine.pDevice->playback.channels,
        g_engine.pDevice->sampleRate,
        ma_waveform_type_sine,
        0.5,    // Amplitude
        start_freq
    );
    
    // We allocate a sound node dynamically that plays and then kills itself.
    // However, ma_engine_play_sound takes a file path.
    // For pure generative, we can't just pass the waveform into play_sound easily 
    // without an explicit data source tree.
    
    // Instead of raw ma_engine, let's just initialize a custom data source 
    // and route it to an `ma_sound`.
    ma_sound* sound = (ma_sound*)malloc(sizeof(ma_sound));
    ma_waveform* pSineWave = (ma_waveform*)malloc(sizeof(ma_waveform));
    
    ma_waveform_init(&config, pSineWave);
    
    // Initialize the sound from the data source
    ma_result result = ma_sound_init_from_data_source(&g_engine, pSineWave, 0, NULL, sound);
    if (result != MA_SUCCESS) {
        free(sound);
        free(pSineWave);
        return -1;
    }
    
    // Apply pitch pitch logic using miniaudio's built-in pitch scaler!
    // If start=450 and end=440, we pitch shift over time.
    ma_sound_set_pitch(sound, 1.0f); 
    
    // For ADSR, we can use built in fades!
    // Attack: fade in over 0.05s
    ma_sound_set_volume(sound, 0.0f);
    ma_sound_set_fade_in_milliseconds(sound, 0.0f, 1.0f, 50);
    
    // Release: fade out at the end of duration
    // We'll calculate the sample offset for the fade out to start
    ma_uint64 total_samples = (ma_uint64)(duration_sec * g_engine.pDevice->sampleRate);
    ma_uint64 attack_samples = (ma_uint64)(0.05f * g_engine.pDevice->sampleRate);
    ma_uint64 release_samples = (ma_uint64)(0.2f * g_engine.pDevice->sampleRate);
    
    // Start playback
    ma_sound_start(sound);
    
    // Because we are synchronous for this test tone just to prove it works:
    // (A real app wouldn't block, but the test runner will).
    // Let's implement the slight pitch bend via polling
    
    ma_uint64 current_sample = 0;
    while (current_sample < total_samples) {
        float progress = (float)current_sample / total_samples;
        
        // Pitch bend: interpolate frequency via miniaudio Pitch modifier
        float current_freq = start_freq + progress * (end_freq - start_freq);
        ma_sound_set_pitch(sound, current_freq / start_freq);
        
        // Trigger decay/release phase
        if (current_sample == total_samples - release_samples) {
            ma_sound_set_fade_in_milliseconds(sound, 1.0f, 0.0f, 200);
        }
        
        // Sleep 15ms
        ma_sleep(15);
        current_sample += (ma_uint64)(0.015f * g_engine.pDevice->sampleRate);
    }
    
    ma_sound_stop(sound);
    ma_sound_uninit(sound);
    ma_waveform_uninit(pSineWave);
    
    free(sound);
    free(pSineWave);
    
    return 0;
}
