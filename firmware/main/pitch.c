/*
 * Goertzel filter bank with a Hann window, 5 Hz steps, parabolic
 * interpolation of the log power around the peak.
 *
 * The bank reaches 60 Hz beyond fmin..fmax: a peak out there means the
 * strongest tone is outside the range (no result, rather than the edge
 * of the range), and a peak near the edge has its noise reference on
 * both sides.
 *
 * The noise reference is the mean power 20..60 Hz beside the peak, on
 * the side with more noise, without its strongest bin (another station).
 * A median of the whole band would sit in the stop band of a narrow CW
 * filter in the transceiver, and every noise peak in its pass band would
 * count as a tone. Measured over 2000 captures each, with and without a
 * 100 or 250 Hz filter, also at the ends of the range: noise passes as a
 * tone in at most 0.15 %.
 */
#include "pitch.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define STEP_HZ   5
#define MAX_BINS  256
#define EXTRA_HZ  60        /* bank beyond fmin..fmax */
#define MIN_SNR   25.0f     /* peak / noise power, 14 dB */
#define GUARD_BINS  (15 / STEP_HZ)      /* the main lobe of the window: 20 Hz on */
#define SIDE_BINS   (60 / STEP_HZ)
#define MIN_SIDE    ((SIDE_BINS - GUARD_BINS) / 2)  /* bins for a usable side */

/* mean power of bins [from, to) without the strongest, clipped to the
 * band; 0 if too few */
static float side_noise(const float *pow_, int bins, int from, int to)
{
    if (from < 0) from = 0;
    if (to > bins) to = bins;
    if (to - from < MIN_SIDE)
        return 0.0f;
    float sum = 0.0f, mx = 0.0f;
    for (int i = from; i < to; i++) {
        sum += pow_[i];
        mx = fmaxf(mx, pow_[i]);
    }
    return (sum - mx) / (to - from - 1);
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
    static float pow_[MAX_BINS];
    int f0 = fmin - EXTRA_HZ;           /* frequency of bin 0 */
    int bins = (fmax + EXTRA_HZ - f0) / STEP_HZ + 1;
    if (n < 64 || fmax <= fmin || f0 <= 0 || bins > MAX_BINS)
        return false;

    for (size_t k = 0; k < n; k++)
        buf[k] *= 0.5f - 0.5f * cosf(2.0f * (float)M_PI * k / (n - 1));

    int peak = 0;
    for (int i = 0; i < bins; i++) {
        pow_[i] = goertzel(buf, n, fs, (float)(f0 + i * STEP_HZ)) + 1e-12f;
        if (pow_[i] > pow_[peak])
            peak = i;
    }
    float lo = side_noise(pow_, bins, peak - SIDE_BINS, peak - GUARD_BINS);
    float hi = side_noise(pow_, bins, peak + GUARD_BINS + 1, peak + SIDE_BINS + 1);
    if (pow_[peak] < MIN_SNR * fmaxf(lo, hi))
        return false;

    float off = 0.0f;
    if (peak > 0 && peak < bins - 1) {
        float a = logf(pow_[peak - 1]), b = logf(pow_[peak]), c = logf(pow_[peak + 1]);
        float den = a - 2.0f * b + c;
        if (den < 0.0f)
            off = 0.5f * (a - c) / den;
    }
    float f = f0 + (peak + off) * STEP_HZ;
    if (f < fmin - STEP_HZ / 2.0f || f > fmax + STEP_HZ / 2.0f)
        return false;           /* the strongest tone is out of range */
    *hz = fminf(fmaxf(f, (float)fmin), (float)fmax);
    return true;
}
