#include "croft/host_audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

// User Requirements: Plucked string, +5/-5 cents envelope, 12 specific harmonics
int32_t host_audio_play_tone(float start_freq, float end_freq, float duration_sec) {
    if (!g_initialized) return -1;
    
    ma_uint32 sampleRate = g_engine.pDevice->sampleRate;
    ma_uint32 channels = g_engine.pDevice->playback.channels;
    ma_uint64 total_frames = (ma_uint64)(duration_sec * sampleRate);
    
    // Allocate buffer for 32-bit float samples, interleaved
    float* pFrames = (float*)malloc(total_frames * channels * sizeof(float));
    if (!pFrames) return -1;
    
    float pitch_sharp_mult = powf(2.0f, 5.0f / 1200.0f);
    float pitch_flat_mult = powf(2.0f, -5.0f / 1200.0f);
    
    // Track phase per harmonic continuously
    float phases[12] = {0};
    
    // Harmonic amplitudes
    float h_amps[12] = { 0.8f, 1.0f, 0.8f, 0.4f, 0.4f, 0.4f, 0.2f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
    // Decay rates
    float h_decays[12] = { 1.5f, 2.0f, 2.5f, 3.0f, 3.0f, 3.0f, 5.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f };
    // Ratios from fundamental
    float h_ratios[12];
    for(int i=0; i<12; i++) {
        h_ratios[i] = (float)(i + 1);
    }
    // 7th harmonic is exactly 31 cents flat
    h_ratios[6] = 7.0f * powf(2.0f, -31.0f / 1200.0f);
    
    for (ma_uint64 i = 0; i < total_frames; i++) {
        float t = (float)i / sampleRate;
        
        // Fast pitch decay for the "pluck" envelope
        // Relaxes quickly (exp(-t*8)) into the long flattening tail
        float pitch_env = expf(-t * 8.0f); 
        float current_fund = start_freq * (pitch_flat_mult + (pitch_sharp_mult - pitch_flat_mult) * pitch_env);
        
        float sample_val = 0.0f;
        for (int h = 0; h < 12; h++) {
            float h_freq = current_fund * h_ratios[h];
            
            // Advance phase continuously based on frequency at this exact sample
            phases[h] += (h_freq * 2.0f * 3.14159265f) / sampleRate;
            if (phases[h] > 2.0f * 3.14159265f) {
                phases[h] -= 2.0f * 3.14159265f;
            }
            
            // Additive synthesis
            float amp = h_amps[h] * expf(-t * h_decays[h]);
            sample_val += amp * sinf(phases[h]);
        }
        
        // Normalization HEADROOM (scale 0-1)
        sample_val *= 0.15f; 
        
        // Attack (Pluck starts instantly but linearly ramps within 5ms to avoid click pop)
        if (t < 0.005f) {
            sample_val *= (t / 0.005f);
        }
        
        // Safety decay bounds check (avoids zero crossing clicks when stream stops)
        if (t > duration_sec - 0.05f) {
            sample_val *= (duration_sec - t) / 0.05f;
        }

        for (ma_uint32 c = 0; c < channels; c++) {
            pFrames[i * channels + c] = sample_val;
        }
    }
    
    ma_audio_buffer_config buf_config = ma_audio_buffer_config_init(
        ma_format_f32, channels, total_frames, pFrames, NULL);
        
    ma_audio_buffer audio_buffer;
    ma_result res = ma_audio_buffer_init(&buf_config, &audio_buffer);
    if (res != MA_SUCCESS) {
        free(pFrames);
        return -1;
    }
    
    ma_sound sound;
    res = ma_sound_init_from_data_source(&g_engine, &audio_buffer, 0, NULL, &sound);
    if (res != MA_SUCCESS) {
        ma_audio_buffer_uninit(&audio_buffer);
        free(pFrames);
        return -1;
    }
    
    ma_sound_start(&sound);
    ma_sleep((ma_uint32)(duration_sec * 1000.0f) + 100);
    
    ma_sound_uninit(&sound);
    ma_audio_buffer_uninit(&audio_buffer);
    free(pFrames);
    
    return 0;
}
