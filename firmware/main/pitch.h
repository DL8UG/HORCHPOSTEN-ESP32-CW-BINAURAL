/*
 * Auto pitch: find the strongest CW tone in a block of audio.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define PITCH_CAPTURE_MS  300

/*
 * Search fmin..fmax Hz (sample rate fs) for the strongest tone.
 * Returns true and the frequency in *hz if the peak stands at least
 * 14 dB above the median of the searched band.
 * The buffer is overwritten (windowed).
 */
bool pitch_detect(float *buf, size_t n, int fs, int fmin, int fmax, float *hz);
