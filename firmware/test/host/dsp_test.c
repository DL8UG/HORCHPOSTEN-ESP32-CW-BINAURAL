/*
 * Host tests of the signal chain (dsp.c) and the auto pitch (pitch.c).
 *
 * make -C firmware/test/host        run the checks
 * make -C firmware/test/host wav    also write stereo WAV files to listen to
 */
#include "dsp.h"
#include "pitch.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static dsp_params_t defaults(void)
{
    dsp_params_t p = {
        .mode = DSP_MODE_PITCH, .filter = DSP_FILTER_OFF, .width = DSP_WIDTH_MEDIUM,
        .pitch_hz = 600, .agc = false, .swap = false,
    };
    return p;
}

/* deterministic noise, uniform -1..1 */
static uint32_t rng = 12345;
static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(rng >> 8) / (float)(1u << 23) - 1.0f;
}

/* run n samples of a tone through the chain, return the stereo output */
static float *run_tone(const dsp_params_t *p, float f, float amp, size_t n)
{
    float *in = malloc(n * sizeof(float));
    float *out = malloc(2 * n * sizeof(float));
    for (size_t k = 0; k < n; k++)
        in[k] = amp * sinf(2.0f * (float)M_PI * f * k / DSP_FS);
    dsp_init(p);
    dsp_process(in, out, n);
    free(in);
    return out;
}

/* phase of right minus left in degrees (-180..180) and the level of each
 * channel at frequency f, measured over the last m samples */
static void measure(const float *st, size_t n, size_t m, float f,
                    float *dphi, float *lvl_l, float *lvl_r)
{
    double lr = 0, li = 0, rr = 0, ri = 0;
    for (size_t k = n - m; k < n; k++) {
        double w = 2.0 * M_PI * f * k / DSP_FS;
        lr += st[2 * k] * cos(w);
        li -= st[2 * k] * sin(w);
        rr += st[2 * k + 1] * cos(w);
        ri -= st[2 * k + 1] * sin(w);
    }
    double pl = atan2(li, lr), pr = atan2(ri, rr);
    double d = (pr - pl) * 180.0 / M_PI;
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    *dphi = (float)d;
    *lvl_l = (float)(2.0 * hypot(lr, li) / m);
    *lvl_r = (float)(2.0 * hypot(rr, ri) / m);
}

static void test_pitch_mode(void)
{
    const size_t n = DSP_FS;     /* 1 s */
    dsp_params_t p = defaults();
    int d = dsp_pitch_delay_samples(p.width);

    for (int off = -300; off <= 300; off += 50) {
        float f = (float)(p.pitch_hz + off);
        float *st = run_tone(&p, f, 0.3f, n);
        float dphi, l, r;
        measure(st, n, n / 2, f, &dphi, &l, &r);
        /* expected: right leads by 2 pi (f - fc) D, i.e. right ear earlier
         * for a higher pitch -> heard on the right */
        float want = 360.0f * off * d / DSP_FS;
        while (want > 180) want -= 360;
        while (want < -180) want += 360;
        CHECK(fabsf(dphi - want) < 3.0f, "pitch mode f=%.0f: phase R-L %.1f, want %.1f", f, dphi, want);
        CHECK(fabsf(l - r) < 0.01f, "pitch mode f=%.0f: levels L %.3f R %.3f differ", f, l, r);
        CHECK(fabsf(l - 0.3f) < 0.01f, "pitch mode f=%.0f: level %.3f, want 0.3", f, l);
        free(st);
    }

    /* swapped: higher pitch -> left */
    p.swap = true;
    float *st = run_tone(&p, 750, 0.3f, n);
    float dphi, l, r;
    measure(st, n, n / 2, 750, &dphi, &l, &r);
    CHECK(dphi < -30, "swap: phase R-L %.1f should be negative", dphi);
    free(st);
}

static void test_iq_mode(void)
{
    const size_t n = DSP_FS / 2;
    dsp_params_t p = defaults();
    p.mode = DSP_MODE_IQ;
    for (int f = 200; f <= 3000; f += 400) {
        float *st = run_tone(&p, (float)f, 0.3f, n);
        float dphi, l, r;
        measure(st, n, n / 2, (float)f, &dphi, &l, &r);
        CHECK(fabsf(fabsf(dphi) - 90.0f) < 2.0f, "iq f=%d: phase R-L %.1f, want +-90", f, dphi);
        CHECK(fabsf(l - r) < 0.01f, "iq f=%d: levels L %.3f R %.3f", f, l, r);
    }
}

static void test_haas_mono(void)
{
    const size_t n = DSP_FS / 2;
    dsp_params_t p = defaults();
    p.mode = DSP_MODE_HAAS;
    /* impulse: right comes D samples after left */
    float *in = calloc(n, sizeof(float));
    float *out = malloc(2 * n * sizeof(float));
    in[100] = 1.0f;
    dsp_init(&p);
    dsp_process(in, out, n);
    size_t pl = 0, pr = 0;
    for (size_t k = 0; k < n; k++) {
        if (fabsf(out[2 * k]) > fabsf(out[2 * pl])) pl = k;
        if (fabsf(out[2 * k + 1]) > fabsf(out[2 * pr + 1])) pr = k;
    }
    CHECK((int)(pr - pl) == dsp_haas_delay_samples(p.width),
          "haas: right %zu samples after left, want %d", pr - pl, dsp_haas_delay_samples(p.width));

    p.mode = DSP_MODE_MONO;
    dsp_init(&p);
    dsp_process(in, out, n);
    int same = 1;
    for (size_t k = 0; k < n; k++)
        same &= out[2 * k] == out[2 * k + 1];
    CHECK(same, "mono: left and right differ");
    free(in);
    free(out);
}

static void test_filter(void)
{
    const size_t n = DSP_FS;
    for (dsp_filter_t flt = DSP_FILTER_500; flt < DSP_FILTER_COUNT; flt++) {
        dsp_params_t p = defaults();
        p.mode = DSP_MODE_MONO;
        p.filter = flt;
        int bw = dsp_filter_bw_hz(flt);
        float l, r, dphi;

        float *st = run_tone(&p, 600, 0.3f, n);
        measure(st, n, n / 2, 600, &dphi, &l, &r);
        free(st);
        CHECK(fabsf(l - 0.3f) < 0.01f, "filter %d: centre level %.3f, want 0.3", bw, l);

        /* -3 dB at about +-bw/2 */
        float fe = 600 + bw / 2.0f;
        st = run_tone(&p, fe, 0.3f, n);
        measure(st, n, n / 2, fe, &dphi, &l, &r);
        free(st);
        float db = 20 * log10f(l / 0.3f);
        CHECK(db < -2.0f && db > -4.5f, "filter %d: %.1f dB at band edge, want about -3", bw, db);

        /* far off: strongly damped */
        float ff = 600 + 2.0f * bw;
        st = run_tone(&p, ff, 0.3f, n);
        measure(st, n, n / 2, ff, &dphi, &l, &r);
        free(st);
        db = 20 * log10f(l / 0.3f);
        CHECK(db < -20.0f, "filter %d: only %.1f dB at %.0f Hz", bw, db, ff);
    }
}

static void test_agc(void)
{
    const size_t n = 2 * DSP_FS;
    dsp_params_t p = defaults();
    p.mode = DSP_MODE_MONO;
    p.agc = true;
    float lv[3];
    const float amps[3] = { 0.005f, 0.05f, 0.5f };
    for (int i = 0; i < 3; i++) {
        float *st = run_tone(&p, 600, amps[i], n);
        float dphi, r;
        measure(st, n, n / 4, 600, &dphi, &lv[i], &r);
        free(st);
    }
    /* 40 dB input range -> output within 3 dB */
    float spread = 20 * log10f(fmaxf(fmaxf(lv[0], lv[1]), lv[2]) / fminf(fminf(lv[0], lv[1]), lv[2]));
    CHECK(spread < 3.0f, "agc: output levels %.3f %.3f %.3f (spread %.1f dB)", lv[0], lv[1], lv[2], spread);

    /* nothing above full scale even for a hot input */
    float *st = run_tone(&p, 600, 3.0f, n);
    float mx = 0;
    for (size_t k = 0; k < 2 * n; k++)
        mx = fmaxf(mx, fabsf(st[k]));
    free(st);
    CHECK(mx <= 1.0f, "agc: peak %.3f above full scale", mx);
}

static void test_xfade(void)
{
    /* a mode change must not jump: max step between samples stays small */
    const size_t n = DSP_FS / 2, blk = 64;
    dsp_params_t p = defaults();
    float in[64], out[128];
    dsp_init(&p);
    float last_l = 0, last_r = 0, maxstep = 0;
    size_t k = 0;
    for (size_t b = 0; b < n / blk; b++) {
        if (b == n / blk / 2) {
            p.mode = DSP_MODE_HAAS;
            dsp_set_params(&p);
        }
        for (size_t i = 0; i < blk; i++, k++)
            in[i] = 0.3f * sinf(2.0f * (float)M_PI * 600 * k / DSP_FS);
        dsp_process(in, out, blk);
        for (size_t i = 0; i < blk; i++) {
            if (b > 4) {
                maxstep = fmaxf(maxstep, fabsf(out[2 * i] - last_l));
                maxstep = fmaxf(maxstep, fabsf(out[2 * i + 1] - last_r));
            }
            last_l = out[2 * i];
            last_r = out[2 * i + 1];
        }
    }
    /* a 600 Hz sine of 0.3 moves at most 0.3 * 2 pi 600 / 16000 = 0.071 */
    CHECK(maxstep < 0.08f, "xfade: step of %.3f at the mode change", maxstep);
}

/* ---- parameter changes and keyed signals ---- */

typedef struct {
    size_t at;          /* sample index of the change */
    dsp_params_t p;
} change_t;

/* run in[] in blocks of 64 like the firmware, applying the changes on the
 * way; returns the stereo output */
static float *run_changes(const dsp_params_t *p, const change_t *ch, size_t nch,
                          const float *in, size_t n)
{
    const size_t blk = 64;
    float *out = malloc(2 * n * sizeof(float));
    dsp_init(p);
    size_t c = 0;
    for (size_t k = 0; k < n; k += blk) {
        while (c < nch && ch[c].at <= k)
            dsp_set_params(&ch[c++].p);
        dsp_process(in + k, out + 2 * k, n - k < blk ? n - k : blk);
    }
    return out;
}

/* tone of amplitude amp from sample on to off, small noise everywhere */
static float *make_tone(float f, float amp, size_t on, size_t off, size_t n)
{
    float *in = malloc(n * sizeof(float));
    for (size_t k = 0; k < n; k++)
        in[k] = 0.002f * noise()
              + (k >= on && k < off ? amp * sinf(2.0f * (float)M_PI * f * k / DSP_FS) : 0.0f);
    return in;
}

static float peak(const float *st, size_t from, size_t to)
{
    float mx = 0;
    for (size_t k = 2 * from; k < 2 * to; k++)
        mx = fmaxf(mx, fabsf(st[k]));
    return mx;
}

/* largest step between neighbouring samples of either channel */
static float max_step(const float *st, size_t from, size_t to)
{
    float mx = 0;
    for (size_t k = from + 1; k < to; k++)
        for (int c = 0; c < 2; c++)
            mx = fmaxf(mx, fabsf(st[2 * k + c] - st[2 * (k - 1) + c]));
    return mx;
}

/* smallest peak level of channel c over 2 ms windows: finds dropouts */
static float min_level(const float *st, size_t from, size_t to, int c)
{
    const size_t w = DSP_FS / 500;
    float mn = 1e9f;
    for (size_t k = from; k + w <= to; k += w) {
        float mx = 0;
        for (size_t i = k; i < k + w; i++)
            mx = fmaxf(mx, fabsf(st[2 * i + c]));
        mn = fminf(mn, mx);
    }
    return mn;
}

static void test_agc_onset(void)
{
    /* the first element after a pause must not be louder than the rest */
    const size_t n = 3 * DSP_FS, ms = DSP_FS / 1000;
    dsp_params_t p = defaults();
    p.mode = DSP_MODE_MONO;
    p.filter = DSP_FILTER_500;
    p.agc = true;

    /* after 2 s of silence */
    float *in = make_tone(600, 0.3f, 2 * DSP_FS, n, n);
    float *st = run_changes(&p, NULL, 0, in, n);
    float steady = peak(st, n - 200 * ms, n), first = peak(st, 2 * DSP_FS, 2 * DSP_FS + 50 * ms);
    CHECK(first < 1.12f * steady, "agc: onset after silence %.3f, steady %.3f", first, steady);
    free(in);
    free(st);

    /* after a word gap of 0.3 s */
    in = make_tone(600, 0.3f, DSP_FS, n, n);
    for (size_t k = 1500 * ms; k < 1800 * ms; k++)
        in[k] = 0.002f * noise();
    st = run_changes(&p, NULL, 0, in, n);
    steady = peak(st, n - 200 * ms, n);
    first = peak(st, 1800 * ms, 1850 * ms);
    CHECK(first < 1.12f * steady, "agc: onset after word gap %.3f, steady %.3f", first, steady);
    free(in);
    free(st);

    /* AGC on during silence, off while the signal starts, then on again */
    change_t ch[2] = { { DSP_FS, p }, { 2 * DSP_FS, p } };
    ch[0].p.agc = false;
    in = make_tone(600, 0.3f, 1500 * ms, n, n);
    st = run_changes(&p, ch, 2, in, n);
    steady = peak(st, n - 200 * ms, n);
    first = peak(st, 2 * DSP_FS, 2 * DSP_FS + 50 * ms);
    CHECK(first < 1.12f * steady, "agc: switched on %.3f, steady %.3f", first, steady);
    CHECK(max_step(st, DSP_FS / 10, n) < 0.08f, "agc: step of %.3f on off/on",
          max_step(st, DSP_FS / 10, n));
    free(in);
    free(st);
}

static void test_pitch_change(void)
{
    /* pitch mode: a new centre pitch must neither click nor drop out */
    const size_t n = DSP_FS / 2, at = n / 2, end = at + DSP_FS / 20;
    static const struct { dsp_filter_t flt; int to; } cases[] = {
        { DSP_FILTER_OFF, 1000 }, { DSP_FILTER_OFF, 625 }, { DSP_FILTER_500, 625 },
        { DSP_FILTER_250, 650 },
    };
    float *in = make_tone(600, 0.3f, 0, n, n);
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        dsp_params_t p = defaults();
        p.width = DSP_WIDTH_WIDE;
        p.filter = cases[c].flt;
        change_t ch = { at, p };
        ch.p.pitch_hz = cases[c].to;
        float *st = run_changes(&p, &ch, 1, in, n);
        int bw = dsp_filter_bw_hz(p.filter);
        float step = max_step(st, DSP_FS / 10, n);
        CHECK(step < 0.08f, "pitch 600->%d, filter %d: step of %.3f", cases[c].to, bw, step);
        for (int lr = 0; lr < 2; lr++) {
            float lv = min_level(st, at, end, lr);
            CHECK(lv > 0.2f, "pitch 600->%d, filter %d: %s drops to %.3f", cases[c].to, bw,
                  lr ? "right" : "left", lv);
        }
        free(st);
    }
    free(in);
}

static void test_filter_retune(void)
{
    /* a narrow filter moved away from a tone must not release a burst */
    const size_t n = DSP_FS, at = n / 2;
    dsp_params_t p = defaults();
    p.mode = DSP_MODE_MONO;
    p.filter = DSP_FILTER_100;
    p.pitch_hz = 1000;
    change_t ch = { at, p };
    ch.p.pitch_hz = 300;
    float *in = make_tone(700, 0.3f, 0, n, n);
    float *st = run_changes(&p, &ch, 1, in, n);
    float pk = peak(st, at, n);
    CHECK(pk < 0.1f, "filter 100 moved 1000->300 Hz, tone 700 Hz: peak %.3f", pk);
    free(st);
    free(in);

    /* switching the filter on and off must not click */
    in = make_tone(600, 0.3f, 0, n, n);
    p = defaults();
    p.mode = DSP_MODE_MONO;
    change_t onoff[2] = { { at, p }, { at + DSP_FS / 4, p } };
    onoff[0].p.filter = DSP_FILTER_100;
    st = run_changes(&p, onoff, 2, in, n);
    float step = max_step(st, DSP_FS / 10, n);
    CHECK(step < 0.08f, "filter off/on: step of %.3f", step);
    free(st);
    free(in);
}

static void test_auto_pitch(void)
{
    const size_t n = DSP_FS * PITCH_CAPTURE_MS / 1000;
    float *buf = malloc(n * sizeof(float));
    const float tones[] = { 312, 487, 600, 733, 958 };
    for (size_t t = 0; t < sizeof(tones) / sizeof(tones[0]); t++) {
        for (size_t k = 0; k < n; k++)
            buf[k] = 0.05f * sinf(2.0f * (float)M_PI * tones[t] * k / DSP_FS)
                   + 0.02f * sinf(2.0f * (float)M_PI * 450 * k / DSP_FS)  /* weaker one */
                   + 0.1f * noise();
        float hz = 0;
        bool ok = pitch_detect(buf, n, DSP_FS, DSP_PITCH_MIN, DSP_PITCH_MAX, &hz);
        CHECK(ok && fabsf(hz - tones[t]) < 5.0f, "auto pitch %.0f Hz: found %d %.1f", tones[t], ok, hz);
    }
    /* noise only: no result */
    for (size_t k = 0; k < n; k++)
        buf[k] = 0.1f * noise();
    float hz;
    CHECK(!pitch_detect(buf, n, DSP_FS, DSP_PITCH_MIN, DSP_PITCH_MAX, &hz),
          "auto pitch: found %.1f Hz in pure noise", hz);
    free(buf);
}

/* ---- WAV files to listen to ---- */

#define WAV_DIR "build/wav"

/* open WAV_DIR/name; a failure counts as a test failure */
static FILE *open_out(const char *name, const char *mode)
{
    char path[96];
    snprintf(path, sizeof(path), WAV_DIR "/%s", name);
    FILE *f = fopen(path, mode);
    if (!f) {
        perror(path);
        failures++;
    } else {
        printf("wrote %s\n", path);
    }
    return f;
}

static void write_wav(const char *name, const float *st, size_t frames)
{
    FILE *f = open_out(name, "wb");
    if (!f)
        return;
    uint32_t data = (uint32_t)frames * 4, riff = 36 + data, fs = DSP_FS, br = DSP_FS * 4;
    uint32_t fmt_len = 16;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmt_len, 4, 1, f); fwrite(&pcm, 2, 1, f);
    fwrite(&ch, 2, 1, f); fwrite(&fs, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (size_t k = 0; k < 2 * frames; k++) {
        int16_t v = (int16_t)lrintf(fmaxf(-1, fminf(1, st[k])) * 32767);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
}

/* C   Q   T E S T, as dot units: 1 = key down */
static const char CQ[] = "1110101110100000111011101011100000001110001000101010001110000000";

/* key state of a station sending CQ at wpm, started offset_s early */
static float keying(int wpm, size_t k, float offset_s)
{
    float dot = 1.2f / wpm;
    float t = (float)k / DSP_FS + offset_s;
    size_t i = (size_t)(t / dot) % (sizeof(CQ) - 1);
    return CQ[i] == '1' ? 1.0f : 0.0f;
}

/* add a keyed CW station to in[from..to) with soft keying edges (5 ms);
 * the key is released 40 ms before the end, so the station never stops
 * in the middle of a dot or dash */
static void add_station(float *in, size_t from, size_t to, float f, float amp,
                        int wpm, float offset_s)
{
    const size_t release = DSP_FS / 25;
    float env = 0;
    double ph = 0;
    for (size_t k = from; k < to; k++) {
        float key = k + release < to ? keying(wpm, k - from, offset_s) : 0.0f;
        env += (key - env) * 0.0125f;
        ph += 2.0 * M_PI * f / DSP_FS;
        in[k] += amp * env * (float)sin(ph);
    }
}

static float *make_noise(float amp, size_t n)
{
    float *in = malloc(n * sizeof(float));
    for (size_t k = 0; k < n; k++)
        in[k] = amp * noise();
    return in;
}

/* the pile-up: three stations at 480, 600 and 760 Hz in noise */
static float *scene_pileup(size_t n)
{
    float *in = make_noise(0.05f, n);
    add_station(in, 0, n, 480, 0.04f, 18, 0.0f);
    add_station(in, 0, n, 600, 0.05f, 22, 0.37f);
    add_station(in, 0, n, 760, 0.03f, 26, 0.74f);
    return in;
}

static void wav_run(const char *name, const dsp_params_t *p, const change_t *ch,
                    size_t nch, const float *in, size_t n)
{
    float *out = run_changes(p, ch, nch, in, n);
    write_wav(name, out, n);
    free(out);
}

static void wav_input(const char *name, const float *in, size_t n)
{
    float *st = malloc(2 * n * sizeof(float));
    for (size_t k = 0; k < n; k++)
        st[2 * k] = st[2 * k + 1] = in[k];
    write_wav(name, st, n);
    free(st);
}

static void write_wavs(void)
{
    const size_t n = 12 * DSP_FS;
    dsp_params_t p = defaults();
    p.agc = true;
    if ((mkdir("build", 0755) && errno != EEXIST) || (mkdir(WAV_DIR, 0755) && errno != EEXIST)) {
        perror(WAV_DIR);
        failures++;
        return;
    }

    /* 1: the modes on the pile-up, input first for comparison */
    float *in = scene_pileup(n);
    wav_input("01_pileup_input.wav", in, n);
    static const struct { const char *name; dsp_mode_t mode; } modes[] = {
        { "02_pileup_mono.wav", DSP_MODE_MONO }, { "03_pileup_pitch.wav", DSP_MODE_PITCH },
        { "04_pileup_iq90.wav", DSP_MODE_IQ }, { "05_pileup_haas.wav", DSP_MODE_HAAS },
    };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        dsp_params_t q = p;
        q.mode = modes[i].mode;
        wav_run(modes[i].name, &q, NULL, 0, in, n);
    }

    /* 2: pitch mode, the other widths (medium is the default: 03) */
    _Static_assert(DSP_WIDTH_COUNT == 3, "new width: add a WAV file");
    static const struct { const char *name; dsp_width_t width; } widths[] = {
        { "06_width_narrow.wav", DSP_WIDTH_NARROW }, { "07_width_wide.wav", DSP_WIDTH_WIDE },
    };
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        dsp_params_t q = p;
        q.width = widths[i].width;
        wav_run(widths[i].name, &q, NULL, 0, in, n);
    }

    /* 3: pitch mode, the filters */
    _Static_assert(DSP_FILTER_COUNT == 4, "new filter: add a WAV file");
    static const struct { const char *name; dsp_filter_t filter; } filters[] = {
        { "08_filter_500.wav", DSP_FILTER_500 }, { "09_filter_250.wav", DSP_FILTER_250 },
        { "10_filter_100.wav", DSP_FILTER_100 },
    };
    for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
        dsp_params_t q = p;
        q.filter = filters[i].filter;
        wav_run(filters[i].name, &q, NULL, 0, in, n);
    }
    free(in);

    /* 4: one steady tone gliding 300 -> 1000 Hz: travels from left to right,
     * in the middle at the centre pitch of 600 Hz */
    in = make_noise(0.005f, n);
    double ph = 0;
    for (size_t k = 0; k < n; k++) {
        double f = 300.0 + 700.0 * k / n;
        ph += 2.0 * M_PI * f / DSP_FS;
        in[k] += 0.1f * (float)sin(ph);
    }
    wav_run("11_sweep_300_1000_pitch.wav", &p, NULL, 0, in, n);
    free(in);

    /* 5: AGC: weak station, pause, very strong one, pause, weak again */
    in = make_noise(0.003f, n);
    add_station(in, 0, 4 * DSP_FS, 600, 0.01f, 20, 0.0f);
    add_station(in, 5 * DSP_FS, 8 * DSP_FS, 650, 0.6f, 24, 0.0f);
    add_station(in, 9 * DSP_FS, n, 600, 0.01f, 20, 0.0f);
    wav_input("12_agc_input.wav", in, n);
    wav_run("13_agc_on.wav", &p, NULL, 0, in, n);
    dsp_params_t q = p;
    q.agc = false;
    wav_run("14_agc_off.wav", &q, NULL, 0, in, n);
    free(in);

    /* 6: a change every 1.5 s while the pile-up goes on; must never click */
    static const struct { const char *what; int key; int val; } steps[] = {
        { "pitch 625 Hz", 'p', 625 }, { "pitch 650 Hz", 'p', 650 },
        { "pitch 800 Hz (auto pitch jump)", 'p', 800 }, { "pitch 600 Hz", 'p', 600 },
        { "mode 90 deg", 'm', DSP_MODE_IQ }, { "mode Haas", 'm', DSP_MODE_HAAS },
        { "mode mono", 'm', DSP_MODE_MONO }, { "mode pitch", 'm', DSP_MODE_PITCH },
        { "filter 250 Hz", 'f', DSP_FILTER_250 }, { "filter 100 Hz", 'f', DSP_FILTER_100 },
        { "pitch 650 Hz with filter 100", 'p', 650 }, { "filter off", 'f', DSP_FILTER_OFF },
        { "AGC off", 'a', 0 }, { "AGC on", 'a', 1 },
        { "width wide", 'w', DSP_WIDTH_WIDE }, { "swap left/right", 's', 1 },
    };
    const size_t nst = sizeof(steps) / sizeof(steps[0]), gap = DSP_FS * 3 / 2;
    const size_t nc = (nst + 2) * gap;
    in = scene_pileup(nc);
    change_t ch[sizeof(steps) / sizeof(steps[0])];
    dsp_params_t cur = p;
    FILE *log = open_out("15_changes.txt", "w");
    for (size_t i = 0; i < nst; i++) {
        switch (steps[i].key) {
        case 'p': cur.pitch_hz = steps[i].val; break;
        case 'm': cur.mode = (dsp_mode_t)steps[i].val; break;
        case 'f': cur.filter = (dsp_filter_t)steps[i].val; break;
        case 'a': cur.agc = steps[i].val; break;
        case 'w': cur.width = (dsp_width_t)steps[i].val; break;
        case 's': cur.swap = steps[i].val; break;
        }
        ch[i].at = (i + 1) * gap;
        ch[i].p = cur;
        if (log)
            fprintf(log, "%5.1f s  %s\n", (double)ch[i].at / DSP_FS, steps[i].what);
    }
    if (log)
        fclose(log);
    wav_run("15_changes.wav", &p, ch, nst, in, nc);
    free(in);
}

int main(int argc, char **argv)
{
    test_pitch_mode();
    test_iq_mode();
    test_haas_mono();
    test_filter();
    test_agc();
    test_xfade();
    test_agc_onset();
    test_pitch_change();
    test_filter_retune();
    test_auto_pitch();
    if (argc > 1 && strcmp(argv[1], "--wav") == 0)
        write_wavs();
    printf("%s (%d failures)\n", failures ? "FAILED" : "all tests passed", failures);
    return failures ? 1 : 0;
}
