/*
 * Goertzel filter bank with a Hann window, 5 Hz steps, parabolic
 * interpolation of the log power around the peak.
 */
#include "pitch.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STEP_HZ   5
#define MAX_BINS  256
#define MIN_SNR   25.0f     /* peak / median power, 14 dB; at 10 dB white noise
                               passed as a tone in about 16 % of the captures */

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static float goertzel(const float *buf, size_t n, int fs, float f)
{
    float w = 2.0f * (float)M_PI * f / fs;
    float c = 2.0f * cosf(w);
    float s1 = 0.0f, s2 = 0.0f;
    for (size_t k = 0; k < n; k++) {
        float s0 = buf[k] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

bool pitch_detect(float *buf, size_t n, int fs, int fmin, int fmax, float *hz)
{
    static float pow_[MAX_BINS], sorted[MAX_BINS];
    int bins = (fmax - fmin) / STEP_HZ + 1;
    if (n < 64 || bins < 3 || bins > MAX_BINS)
        return false;

    for (size_t k = 0; k < n; k++)
        buf[k] *= 0.5f - 0.5f * cosf(2.0f * (float)M_PI * k / (n - 1));

    int peak = 0;
    for (int i = 0; i < bins; i++) {
        pow_[i] = goertzel(buf, n, fs, (float)(fmin + i * STEP_HZ)) + 1e-12f;
        sorted[i] = pow_[i];
        if (pow_[i] > pow_[peak])
            peak = i;
    }
    qsort(sorted, bins, sizeof(float), cmp_float);
    if (pow_[peak] < MIN_SNR * sorted[bins / 2])
        return false;

    float off = 0.0f;
    if (peak > 0 && peak < bins - 1) {
        float a = logf(pow_[peak - 1]), b = logf(pow_[peak]), c = logf(pow_[peak + 1]);
        float den = a - 2.0f * b + c;
        if (den < 0.0f)
            off = 0.5f * (a - c) / den;
    }
    *hz = fmin + (peak + off) * STEP_HZ;
    return true;
}
