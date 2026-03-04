#ifndef CROFT_HOST_AUDIO_H
#define CROFT_HOST_AUDIO_H

#include "croft/platform.h"

//
// Host Audio Subsystem (Tier 8)
//

int32_t host_audio_init(void);

void host_audio_terminate(void);

// Plays a short generated audio sine-wave sweep with a basic ADSR envelope.
int32_t host_audio_play_tone(float start_freq, float end_freq, float duration_sec);

#endif // CROFT_HOST_AUDIO_H
