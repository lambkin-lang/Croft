#include "croft/host_audio.h"
#include <stdio.h>

int main(void) {
    printf("Starting Tier 8 Audio Demo.\n");
    
    if (host_audio_init() != 0) {
        printf("Failed to initialize audio subsystem.\n");
        return 1;
    }
    
    printf("Playing 1.5 second tone sweep: 440 Hz (A4) starting slightly sharp and ending slightly flat...\n");
    
    // Play an A4 tone dropping from 450Hz down to 430Hz to satisfy "slightly sharp -> slightly flat"
    // Also tests the ADSR fade in and fade out under the hood.
    host_audio_play_tone(450.0f, 430.0f, 1.5f);
    
    host_audio_terminate();
    printf("Audio finished cleanly.\n");
    return 0;
}
