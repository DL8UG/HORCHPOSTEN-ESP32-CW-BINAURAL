/*
 * Signal chain of Horchposten: mono CW audio in, binaural stereo out.
 *
 * Plain C without ESP-IDF dependencies, so the same code runs in the
 * host tests (firmware/test/host).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#define DSP_FS          16000   /* sample rate, Hz */
#define DSP_PITCH_MIN   300     /* centre pitch range, Hz */
#define DSP_PITCH_MAX   1000

typedef enum {
    DSP_MODE_PITCH = 0,  /* binaural by pitch: centre pitch in the middle */
    DSP_MODE_IQ,         /* left and right 90 degrees apart */
    DSP_MODE_HAAS,       /* one ear delayed by some ms */
    DSP_MODE_MONO,       /* both ears the same */
    DSP_MODE_COUNT
} dsp_mode_t;

typedef enum {
    DSP_FILTER_OFF = 0,
    DSP_FILTER_500,
    DSP_FILTER_250,
    DSP_FILTER_100,
    DSP_FILTER_COUNT
} dsp_filter_t;

typedef enum {
    DSP_WIDTH_NARROW = 0,
    DSP_WIDTH_MEDIUM,
    DSP_WIDTH_WIDE,
    DSP_WIDTH_COUNT
} dsp_width_t;

typedef struct {
    dsp_mode_t mode;
    dsp_filter_t filter;
    dsp_width_t width;
    int pitch_hz;   /* centre pitch, DSP_PITCH_MIN..DSP_PITCH_MAX */
    bool agc;
    bool swap;      /* exchange left and right */
} dsp_params_t;

/* Reset all state and apply the parameters. */
void dsp_init(const dsp_params_t *p);

/* Apply new parameters; every change is faded, none clicks. */
void dsp_set_params(const dsp_params_t *p);

/* Bring every field into its range (invalid enum -> default, pitch clamped). */
void dsp_params_sanitize(dsp_params_t *p);

/* Process n mono samples (about -1..1) into interleaved stereo out[2*n]. */
void dsp_process(const float *in, float *out, size_t n);

/* Helpers for the user interface and the tests. */
int dsp_filter_bw_hz(dsp_filter_t f);          /* 0 = off */
int dsp_pitch_delay_samples(dsp_width_t w);    /* delay D of DSP_MODE_PITCH */
int dsp_haas_delay_samples(dsp_width_t w);
const char *dsp_mode_name(dsp_mode_t m);
