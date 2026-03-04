#include "croft/host_audio.h"
#include <stdio.h>

int main(void) {
    printf("Starting Tier 8 Audio Demo.\n");
    
    if (host_audio_init() != 0) {
        printf("Failed to initialize audio subsystem.\n");
        return 1;
    }
    
    printf("Playing Mozart: Eine kleine Nachtmusik (K. 525)\n");

    // Frequencies (Hz) for G Major
    float G4 = 392.00f;
    float D4 = 293.66f;
    float G5 = 783.99f;
    float D5 = 587.33f;
    float C5 = 523.25f;
    float B4 = 493.88f;
    float A4 = 440.0f;
    
    // Notes: freq, duration
    struct { float f; float d; } melody[] = {
        { G4, 0.5f }, { 0.0f, 0.1f },
        { D4, 0.25f }, { G4, 0.5f }, { 0.0f, 0.1f },
        { D4, 0.25f }, { G4, 0.25f }, { D4, 0.25f },
        { G4, 0.25f }, { B4, 0.25f }, { D5, 0.5f }, { 0.0f, 0.1f },
        
        { C5, 0.5f }, { 0.0f, 0.1f },
        { A4, 0.25f }, { C5, 0.5f }, { 0.0f, 0.1f },
        { A4, 0.25f }, { C5, 0.25f }, { A4, 0.25f },
        { 220.0f /* F#4 */, 0.25f }, { A4, 0.25f }, { D4, 0.5f }, { 0.0f, 0.1f }
    };
    
    int num_notes = sizeof(melody) / sizeof(melody[0]);
    float pitch_sharp = 1.00289f; // +5 cents
    float pitch_flat = 0.99711f;  // -5 cents
    
    for (int i = 0; i < num_notes; i++) {
        if (melody[i].f == 0.0f) {
            // Rest
            // Our api is synchronous, miniaudio sleeps inside host_audio_play_tone for duration,
            // but we don't expose a sleep API.
            // A 0Hz sine wave technically generates silence, so let's use that as a hacky rest.
            host_audio_play_tone(0.0f, 0.0f, melody[i].d);
        } else {
            host_audio_play_tone(melody[i].f * pitch_sharp, melody[i].f * pitch_flat, melody[i].d);
        }
    }
    
    host_audio_terminate();
    printf("Audio finished cleanly.\n");
    return 0;
}
