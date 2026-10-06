/*
 * Busy CW band for listening tests: 3..8 stations at a time calling
 * CQ / CQ TEST / CQ POTA / CQ SOTA, coming and going on different
 * pitches (350..950 Hz, 30 Hz apart), with fading, a little drift and
 * receiver noise (300..2700 Hz). Writes a 48 kHz stereo 16 bit WAV (both
 * channels the same) to play into LINE IN, and the timeline: who calls
 * when on which pitch.
 *
 * make -C firmware/test/host pileup      10 minutes, seed 3, in build/wav
 * build/pileup out.wav out.txt [seconds] [seed]
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FS      48000
#define MAXST   64          /* stations alive at once, upper bound */
#define MAXSEG  1024        /* key-down segments per station */
#define MAXALL  1024        /* stations in the whole file */

static uint64_t rs = 88172645463325252ull;
static double urand(void)   /* 0..1 */
{
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return (rs >> 11) * (1.0 / 9007199254740992.0);
}
static double urange(double a, double b) { return a + (b - a) * urand(); }
static int irange(int a, int b) { return a + (int)(urand() * (b - a + 1)); }
static double gauss(void)
{
    double u = urand() + 1e-12, v = urand();
    return sqrt(-2.0 * log(u)) * cos(2.0 * M_PI * v);
}

static const char *morse(char c)
{
    static const char *az[26] = { ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..",
        ".---", "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-",
        "...-", ".--", "-..-", "-.--", "--.." };
    static const char *d09[10] = { "-----", ".----", "..---", "...--", "....-", ".....",
        "-....", "--...", "---..", "----." };
    if (c >= 'A' && c <= 'Z') return az[c - 'A'];
    if (c >= '0' && c <= '9') return d09[c - '0'];
    if (c == '/') return "-..-.";
    if (c == '?') return "..--..";
    return NULL;
}

typedef struct {
    double t0, t1;              /* seconds, key down */
} seg_t;

typedef struct {
    int used;
    char call[16], kind[8];
    double pitch, drift_hz, drift_f, qsb_depth, qsb_f, qsb_ph, amp, wpm;
    double start, end;          /* seconds */
    seg_t seg[MAXSEG];
    int nseg;
} station_t;

static station_t st[MAXST];

/* append the key-down segments of text at time t; returns the end time */
static double send(station_t *s, const char *text, double t)
{
    double dot = 1.2 / s->wpm;
    for (const char *p = text; *p; p++) {
        if (*p == ' ') {
            t += 4 * dot;       /* 3 after the letter already + 4 = 7 */
            continue;
        }
        const char *m = morse(*p);
        if (!m)
            continue;
        for (const char *e = m; *e; e++) {
            double len = (*e == '.' ? 1 : 3) * dot;
            if (s->nseg < MAXSEG)
                s->seg[s->nseg++] = (seg_t){ t, t + len };
            t += len + dot;
        }
        t += 2 * dot;           /* letter gap 3 = 1 + 2 */
    }
    return t;
}

static void make_call(char *out)
{
    static const char *pre[] = { "DL", "DK", "DJ", "DM", "DO", "DF", "G", "M", "F", "ON",
        "PA", "OK", "OM", "SP", "OE", "HB9", "I", "IK", "EA", "SM", "OH", "LY", "YU", "S5",
        "9A", "HA", "OZ", "LA", "W", "K", "N", "VE", "EI", "CT", "SV", "UA" };
    const char *p = pre[irange(0, sizeof(pre) / sizeof(pre[0]) - 1)];
    char suf[4];
    int n = irange(2, 3);
    for (int i = 0; i < n; i++)
        suf[i] = 'A' + irange(0, 25);
    suf[n] = 0;
    if (strlen(p) >= 3 || (p[strlen(p) - 1] >= '0' && p[strlen(p) - 1] <= '9'))
        sprintf(out, "%s%s", p, suf);
    else
        sprintf(out, "%s%d%s", p, irange(0, 9), suf);
}

/* no other station within 30 Hz of f while s is on */
static int pitch_free(const station_t *s, double f)
{
    for (int i = 0; i < MAXST; i++)
        if (&st[i] != s && st[i].used && st[i].end > s->start && st[i].start < s->end
            && fabs(st[i].pitch - f) < 30)
            return 0;
    return 1;
}

/* a new station from time t on */
static station_t *spawn(double t, double total)
{
    station_t *s = NULL;
    for (int i = 0; i < MAXST; i++)
        if (!st[i].used || st[i].end < t - 5) {
            s = &st[i];
            break;
        }
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->used = 1;
    make_call(s->call);
    int k = irange(0, 3);
    static const char *kinds[4] = { "CQ", "TEST", "POTA", "SOTA" };
    strcpy(s->kind, kinds[k]);
    s->wpm = k == 1 ? urange(24, 32) : k >= 2 ? urange(14, 22) : urange(16, 28);
    s->amp = pow(10.0, urange(-32, -9) / 20.0);
    s->qsb_depth = urange(0.0, 0.6);
    s->qsb_f = urange(0.03, 0.2);
    s->qsb_ph = urange(0, 2 * M_PI);
    s->drift_hz = urange(-3, 3);
    s->drift_f = urange(0.005, 0.03);
    s->start = t + urange(0, 1.5);
    /* message */
    char msg[128];
    if (k == 0)
        sprintf(msg, "CQ CQ DE %s %s K", s->call, s->call);
    else if (k == 1)
        sprintf(msg, "CQ TEST %s %s TEST", s->call, s->call);
    else
        sprintf(msg, "CQ %s DE %s %s K", s->kind, s->call, s->call);
    int reps = irange(1, 3);
    double tt = s->start;
    for (int r = 0; r < reps && tt < total - 2; r++) {
        tt = send(s, msg, tt);
        if (r + 1 < reps)
            tt += urange(2, 5);     /* listening */
    }
    s->end = tt + 0.05;
    /* pitch: away from the others for its whole life */
    for (int tries = 0; tries < 50; tries++) {
        s->pitch = urange(350, 950);
        if (pitch_free(s, s->pitch))
            break;
    }
    return s;
}

static void put16(FILE *f, int v) { uint16_t x = (uint16_t)v; fwrite(&x, 2, 1, f); }
static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s out.wav out.txt [seconds] [seed]\n", argv[0]);
        return 2;
    }
    double total = argc > 3 ? atof(argv[3]) : 600;
    if (total < 10 || total > 7200) {
        fprintf(stderr, "seconds: 10..7200\n");
        return 2;
    }
    if (argc > 4)
        rs ^= strtoull(argv[4], NULL, 10) * 0x9E3779B97F4A7C15ull;
    FILE *log = fopen(argv[2], "w");
    if (!log) { perror(argv[2]); return 1; }

    /* schedule: keep 3..8 stations alive, target changes every 20..40 s */
    int target = irange(3, 8);
    double next_target = urange(20, 40);
    /* a copy of every station for the renderer (the slots in st[] are reused) */
    static station_t all[MAXALL];
    int nall = 0;
    for (double t = 0; t < total - 3; t += 0.5) {
        if (t >= next_target) {
            target = irange(3, 8);
            next_target = t + urange(20, 40);
        }
        int alive = 0;
        for (int i = 0; i < MAXST; i++)
            if (st[i].used && st[i].start <= t + 1.5 && st[i].end > t)
                alive++;
        while (alive < target) {
            station_t *s = spawn(t, total);
            if (!s)
                break;
            if (nall == MAXALL) {
                fprintf(stderr, "more than %d stations\n", MAXALL);
                return 1;
            }
            all[nall++] = *s;
            fprintf(log, "%7.1f - %7.1f s  %4.0f Hz  %2.0f WpM  %5.1f dB  %-10s CQ %s\n",
                    s->start, s->end, s->pitch, s->wpm, 20 * log10(s->amp), s->call, s->kind);
            alive++;
        }
    }
    fclose(log);

    size_t n = (size_t)(total * FS);
    float *out = calloc(n, sizeof(float));
    if (!out) { perror("calloc"); return 1; }

    /* receiver noise: Gaussian, 300 Hz high pass, 2700 Hz low pass (2nd order each, twice) */
    {
        double z[4][2] = { { 0 } };
        double b[4][3], a[4][3];
        for (int k = 0; k < 4; k++) {
            int lp = k >= 2;
            double f = lp ? 2700 : 300, q = 0.7071;
            double w = 2 * M_PI * f / FS, al = sin(w) / (2 * q), c = cos(w);
            if (lp) { b[k][0] = (1 - c) / 2; b[k][1] = 1 - c; b[k][2] = (1 - c) / 2; }
            else    { b[k][0] = (1 + c) / 2; b[k][1] = -(1 + c); b[k][2] = (1 + c) / 2; }
            a[k][0] = 1 + al; a[k][1] = -2 * c; a[k][2] = 1 - al;
            for (int j = 0; j < 3; j++)
                b[k][j] /= a[k][0];
            a[k][1] /= a[k][0];
            a[k][2] /= a[k][0];
        }
        /* white RMS before the filters; about -40 dBFS RMS in 300..2700 Hz */
        const double level = pow(10.0, -30.0 / 20.0);
        for (size_t i = 0; i < n; i++) {
            double x = gauss() * level;
            for (int k = 0; k < 4; k++) {
                double y = b[k][0] * x + z[k][0];
                z[k][0] = b[k][1] * x - a[k][1] * y + z[k][1];
                z[k][1] = b[k][2] * x - a[k][2] * y;
                x = y;
            }
            out[i] = (float)x;
        }
    }

    /* stations: raised cosine edges of 5 ms */
    const double edge = 0.005;
    for (int s = 0; s < nall; s++) {
        station_t *p = &all[s];
        for (int g = 0; g < p->nseg; g++) {
            double t0 = fmax(p->seg[g].t0 - edge / 2, 0), t1 = p->seg[g].t1 + edge / 2;
            size_t i0 = (size_t)(t0 * FS), i1 = (size_t)(t1 * FS);
            if (i1 > n) i1 = n;
            for (size_t i = i0; i < i1; i++) {
                double t = (double)i / FS, env = 1;
                if (t < t0 + edge) env = 0.5 - 0.5 * cos(M_PI * (t - t0) / edge);
                else if (t > t1 - edge) env = 0.5 - 0.5 * cos(M_PI * (t1 - t) / edge);
                double qsb = 1 - p->qsb_depth * (0.5 + 0.5 * sin(2 * M_PI * p->qsb_f * t + p->qsb_ph));
                /* phase from the integral of f: continuous across segments */
                double ph = 2 * M_PI * (p->pitch * t - p->drift_hz / (2 * M_PI * p->drift_f)
                                        * cos(2 * M_PI * p->drift_f * t));
                out[i] += (float)(p->amp * qsb * env * sin(ph));
            }
        }
    }

    float pk = 0;
    for (size_t i = 0; i < n; i++)
        pk = fmaxf(pk, fabsf(out[i]));
    float g = pk > 0.89f ? 0.89f / pk : 1.0f;

    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
    uint32_t data = (uint32_t)(n * 4);
    fwrite("RIFF", 1, 4, f); put32(f, 36 + data); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 2); put32(f, FS);
    put32(f, FS * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, data);
    for (size_t i = 0; i < n; i++) {
        int v = (int)lrintf(out[i] * g * 32767);
        put16(f, v); put16(f, v);
    }
    if (ferror(f) | fclose(f)) {
        perror(argv[1]);
        return 1;
    }
    printf("%zu stations, peak %.2f, gain %.2f\n", (size_t)nall, pk, g);
    return 0;
}
