/*
 * Signal chain: DC block -> CW band pass -> AGC -> Hilbert pair (I, Q)
 * -> stereo stage of the selected mode -> soft limiter.
 *
 * All modes take their samples from the same I/Q history, so a mode or
 * width change can be faded by running the stereo stage twice. Each ear
 * comes out as an analytic signal; during the fade its phase glides
 * from the old output to the new one, so two outputs in antiphase do
 * not cancel each other as in a plain mix.
 * A new band pass is cross-faded with the old one. A new
 * centre pitch glides, and so does the band pass along with it. A change
 * that arrives during a fade waits for its end, so a fade never jumps.
 */
#include "dsp.h"

#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define BP_STAGES   4
#define HIST_LEN    512             /* power of two, > longest delay */
#define HIST_MASK   (HIST_LEN - 1)
#define XFADE_N     (DSP_FS / 50)   /* 20 ms */

/*
 * AGC: the level is taken from the input, the gain is applied AGC_LOOK
 * samples later. So the gain is already down when the first element after
 * a pause comes out.
 */
#define AGC_TARGET  0.30f
#define AGC_MAX_GAIN 60.0f          /* about 35 dB */
#define AGC_LOOK    (DSP_FS * 3 / 1000)     /* 3 ms look-ahead */
#define AGC_DL_LEN  64              /* power of two, > AGC_LOOK */
#define AGC_DECAY_S  0.300f
#define AGC_HANG_S   0.150f         /* hold the gain over CW element gaps */
#define AGC_FALL_S   0.0004f        /* gain decrease, well inside the look-ahead */
#define AGC_RISE_S   0.003f         /* gain increase, avoids clicks */

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} biquad_t;

typedef struct {
    biquad_t bq[BP_STAGES];
    bool on;
} bandpass_t;

/* one band pass stage in polar form, for the glide */
typedef struct {
    float r, th, g;     /* pole radius and angle, gain b0 */
} polar_t;

/* second order allpass section y = a2*(x + y[n-2]) - x[n-2] */
typedef struct {
    float a2;
    float x1, x2, y1, y2;
} allpass_t;

/*
 * Hilbert transformer as two allpass chains with a phase difference of
 * 90 degrees +-0.7 over nearly the whole band (Olli Niemitalo's design).
 * Path A is delayed by one more sample.
 */
static const float HILB_A[4] = {
    0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f
};
static const float HILB_B[4] = {
    0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f
};

/* parameters of a stereo stage with the values derived from them */
typedef struct {
    dsp_params_t p;
    int d_pitch, d_haas;
    float th;               /* wc D for DSP_MODE_PITCH */
    float rot_c, rot_s;     /* cos, sin of the rotation in use */
} stage_t;

static struct {
    dsp_params_t cur;       /* in use by the audio path */
    stage_t st, st_old;     /* stereo stage now and fading out */
    int xfade;              /* samples left of the cross-fade, 0 = none */
    float xf_phi[2];        /* phase of new over old output, per ear, unwrapped */
    int glide;              /* samples left of a pitch glide */
    float glide_from;       /* rotation the glide starts from */

    /* pending parameters from another task (seqlock) */
    volatile unsigned seq;
    dsp_params_t pending;
    volatile unsigned pending_seq;
    unsigned taken_seq;

    float dc_x1, dc_y1;
    bandpass_t bp[2];       /* in use and fading out */
    int bp_cur;
    int bp_xfade;
    int bp_glide;           /* samples left of a band pass glide */
    polar_t bpg_from[BP_STAGES], bpg_to[BP_STAGES];

    float agc_env, agc_gain;
    int agc_hang;
    float agc_dec, agc_fall, agc_rise;      /* one pole coefficients */
    float agc_mix;          /* 0 = AGC off .. 1 = on, faded */
    float agc_dl[AGC_DL_LEN];
    unsigned agc_pos;

    allpass_t ap_a[4], ap_b[4];
    float a_delay;          /* one sample delay of path A */

    float hist_i[HIST_LEN], hist_q[HIST_LEN];
    unsigned pos;
} s;

int dsp_filter_bw_hz(dsp_filter_t f)
{
    static const int bw[DSP_FILTER_COUNT] = { 0, 500, 250, 100 };
    return (unsigned)f < DSP_FILTER_COUNT ? bw[f] : 0;
}

int dsp_pitch_delay_samples(dsp_width_t w)
{
    /* 0.5 / 0.8 / 1.25 ms: 90 degrees between the ears at
     * 500 / 310 / 200 Hz from the centre pitch */
    static const int d[DSP_WIDTH_COUNT] = { 8, 13, 20 };
    return (unsigned)w < DSP_WIDTH_COUNT ? d[w] : d[1];
}

int dsp_haas_delay_samples(dsp_width_t w)
{
    static const int d[DSP_WIDTH_COUNT] = { DSP_FS * 8 / 1000, DSP_FS * 15 / 1000,
                                            DSP_FS * 25 / 1000 };
    return (unsigned)w < DSP_WIDTH_COUNT ? d[w] : d[1];
}

const char *dsp_mode_name(dsp_mode_t m)
{
    static const char *n[DSP_MODE_COUNT] = { "pitch", "iq90", "haas", "mono" };
    return (unsigned)m < DSP_MODE_COUNT ? n[m] : "?";
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* a bool from storage may hold any byte, and reading one that is neither
 * 0 nor 1 as a bool is undefined: look at the byte */
static void sanitize_bool(bool *b, bool def)
{
    _Static_assert(sizeof(bool) == 1, "bool is one byte");
    unsigned char v;
    memcpy(&v, b, 1);
    if (v > 1)
        *b = def;
}

void dsp_params_sanitize(dsp_params_t *p)
{
    if ((unsigned)p->mode >= DSP_MODE_COUNT) p->mode = DSP_MODE_PITCH;
    if ((unsigned)p->filter >= DSP_FILTER_COUNT) p->filter = DSP_FILTER_OFF;
    if ((unsigned)p->width >= DSP_WIDTH_COUNT) p->width = DSP_WIDTH_MEDIUM;
    if (p->pitch_hz < DSP_PITCH_MIN) p->pitch_hz = DSP_PITCH_MIN;
    if (p->pitch_hz > DSP_PITCH_MAX) p->pitch_hz = DSP_PITCH_MAX;
    sanitize_bool(&p->agc, true);
    sanitize_bool(&p->swap, false);
}

/*
 * Butterworth band pass of order 2 * BP_STAGES, -3 dB at fc +- bw/2:
 * analog low pass prototype -> band pass transform -> bilinear transform
 * (pre-warped edges). Each stage holds one conjugate pole pair and the
 * zeros at z = 1 and z = -1; each is scaled to 0 dB at the centre.
 */
static void bandpass_design(bandpass_t *f, int fc, int bw)
{
    memset(f, 0, sizeof(*f));
    f->on = bw > 0;
    if (!f->on)
        return;
    const double fs2 = 2.0 * DSP_FS;
    double wl = fs2 * tan(M_PI * (fc - bw / 2.0) / DSP_FS);
    double wh = fs2 * tan(M_PI * (fc + bw / 2.0) / DSP_FS);
    double w0sq = wl * wh, b = wh - wl;
    double complex ez = cexp(I * 2.0 * M_PI * fc / DSP_FS);
    int n = 0;
    for (int k = 0; k < BP_STAGES && n < BP_STAGES; k++) {
        double complex p = cexp(I * M_PI * (2 * k + BP_STAGES + 1) / (2.0 * BP_STAGES));
        double complex root = csqrt(p * p * b * b - 4.0 * w0sq);
        double complex cand[2] = { (p * b + root) / 2.0, (p * b - root) / 2.0 };
        for (int j = 0; j < 2 && n < BP_STAGES; j++) {
            if (cimag(cand[j]) <= 0.0)
                continue;           /* its conjugate is taken instead */
            double complex z = (fs2 + cand[j]) / (fs2 - cand[j]);
            biquad_t *bq = &f->bq[n++];
            double a1 = -2.0 * creal(z), a2 = creal(z) * creal(z) + cimag(z) * cimag(z);
            /* gain at the centre with numerator 1 - z^-2 */
            double complex zi = 1.0 / ez;
            double complex h = (1.0 - zi * zi) / (1.0 + a1 * zi + a2 * zi * zi);
            double g = 1.0 / cabs(h);
            bq->b0 = (float)g;
            bq->b1 = 0.0f;
            bq->b2 = (float)-g;
            bq->a1 = (float)a1;
            bq->a2 = (float)a2;
        }
    }
}

static polar_t to_polar(const biquad_t *b)
{
    float r = sqrtf(b->a2);
    polar_t p = { r, acosf(clampf(-b->a1 / (2.0f * r), -1.0f, 1.0f)), b->b0 };
    return p;
}

/* stage coefficients a fraction g (0..1) of the way through the glide */
static void glide_stage(biquad_t *b, const polar_t *from, const polar_t *to, float g)
{
    float r = from->r + g * (to->r - from->r);
    float th = from->th + g * (to->th - from->th);
    b->a1 = -2.0f * r * cosf(th);
    b->a2 = r * r;
    b->b0 = from->g + g * (to->g - from->g);
    b->b2 = -b->b0;
}

static float biquad(biquad_t *b, float x)
{
    /* transposed direct form II */
    float y = b->b0 * x + b->z1;
    b->z1 = b->b1 * x - b->a1 * y + b->z2;
    b->z2 = b->b2 * x - b->a2 * y;
    return y;
}

static float bandpass(bandpass_t *f, float x)
{
    if (f->on)
        for (int i = 0; i < BP_STAGES; i++)
            x = biquad(&f->bq[i], x);
    return x;
}

static float allpass(allpass_t *a, float x)
{
    float y = a->a2 * (x + a->y2) - a->x2;
    a->x2 = a->x1;
    a->x1 = x;
    a->y2 = a->y1;
    a->y1 = y;
    return y;
}

static float agc(float x)
{
    /* peak envelope with hang; runs while the AGC is off too, so it is
     * up to date when the AGC is switched on */
    float a = fabsf(x);
    if (a > s.agc_env) {
        s.agc_env = a;
        s.agc_hang = (int)(AGC_HANG_S * DSP_FS);
    } else if (s.agc_hang > 0) {
        s.agc_hang--;
    } else {
        s.agc_env += s.agc_dec * (a - s.agc_env);
    }
    float want = AGC_TARGET / fmaxf(s.agc_env, AGC_TARGET / AGC_MAX_GAIN);
    s.agc_gain += (want < s.agc_gain ? s.agc_fall : s.agc_rise) * (want - s.agc_gain);

    /* the delay is there with the AGC off too: same latency, no jump */
    s.agc_dl[s.agc_pos & (AGC_DL_LEN - 1)] = x;
    float xd = s.agc_dl[(s.agc_pos - AGC_LOOK) & (AGC_DL_LEN - 1)];
    s.agc_pos++;

    if (s.cur.agc && s.agc_mix < 1.0f)
        s.agc_mix = fminf(s.agc_mix + 1.0f / XFADE_N, 1.0f);
    else if (!s.cur.agc && s.agc_mix > 0.0f)
        s.agc_mix = fmaxf(s.agc_mix - 1.0f / XFADE_N, 0.0f);
    return xd * (1.0f + s.agc_mix * (s.agc_gain - 1.0f));
}

/* soft limiter: linear up to 0.7, then smoothly towards 1.0 */
static float limit(float v)
{
    float a = fabsf(v);
    if (a <= 0.7f)
        return v;
    float over = (a - 0.7f) / 0.3f;
    float y = 0.7f + 0.3f * over / (1.0f + over);
    return v < 0 ? -y : y;
}

static inline float hist(const float *h, int delay)
{
    return h[(s.pos - (unsigned)delay) & HIST_MASK];
}

/* analytic signal I - jQ, delay samples back */
static inline float complex analytic(int delay)
{
    return hist(s.hist_i, delay) - hist(s.hist_q, delay) * I;
}

static void stage_set(stage_t *st, const dsp_params_t *p)
{
    st->p = *p;
    st->d_pitch = dsp_pitch_delay_samples(p->width);
    st->d_haas = dsp_haas_delay_samples(p->width);
    st->th = 2.0f * (float)M_PI * p->pitch_hz * st->d_pitch / DSP_FS;
    st->rot_c = cosf(st->th);
    st->rot_s = sinf(st->th);
}

/* stereo stage for one sample, history already holds the newest I/Q;
 * each ear as an analytic signal, the output is its real part */
static void stereo(const stage_t *st, float complex *l, float complex *r)
{
    const dsp_params_t *p = &st->p;
    float complex a0 = analytic(0);
    switch (p->mode) {
    case DSP_MODE_PITCH:
        /*
         * One ear gets I(t), the other Re{(I - jQ)(t - D) * e^(+j wc D)}
         * = I(t - D) cos(wc D) + Q(t - D) sin(wc D).
         * For a tone at f this is a phase lag of 2 pi (f - fc) D: zero at
         * the centre pitch, so the signal sits in the middle, a lower tone
         * leads in the delayed ear, a higher one lags there.
         * Default: lower pitch left, higher pitch right.
         */
        *l = analytic(st->d_pitch) * (st->rot_c + st->rot_s * I);
        *r = a0;
        break;
    case DSP_MODE_IQ:
        *l = a0;
        *r = I * a0;            /* Re{j (I - jQ)} = Q */
        break;
    case DSP_MODE_HAAS:
        *l = a0;
        *r = analytic(st->d_haas);
        break;
    default:
        *l = *r = a0;
        break;
    }
    if (p->swap) {
        float complex t = *l;
        *l = *r;
        *r = t;
    }
}

/*
 * One ear during a fade, g = 0 (old) .. 1 (new): the level moves
 * linearly, the phase glides by g times the phase of new over old. That
 * phase is followed from sample to sample, so it never jumps by 2 pi
 * while the level is up.
 */
static float fade(float complex zo, float complex zn, float g, float *phi)
{
    float d = cargf(zn * conjf(zo));
    *phi += remainderf(d - *phi, 2.0f * (float)M_PI);
    float mag = cabsf(zo) + g * (cabsf(zn) - cabsf(zo));
    return mag * cosf(cargf(zo) + g * *phi);
}

static void apply(const dsp_params_t *p)
{
    dsp_params_t n = *p;
    dsp_params_sanitize(&n);
    if (n.mode != s.cur.mode || n.width != s.cur.width || n.swap != s.cur.swap) {
        s.st_old = s.st;
        s.xfade = XFADE_N;
        s.xf_phi[0] = s.xf_phi[1] = 0.0f;
    } else if (n.pitch_hz != s.cur.pitch_hz) {
        /* only the rotation changes: let it glide instead of cross-fading
         * two phases of the same tone, which can cancel each other */
        s.glide_from = s.st.th;
        s.glide = XFADE_N;
    }
    int bw = dsp_filter_bw_hz(n.filter);
    int step = abs(n.pitch_hz - s.cur.pitch_hz);
    if (n.filter == s.cur.filter && bw && step && 2 * step <= bw) {
        /* a small step of the same filter: turn its poles to the new centre
         * while the pitch glides. A second filter cross-faded in would
         * differ in phase at the tone and dip the level. */
        bandpass_t to;
        bandpass_design(&to, n.pitch_hz, bw);
        for (int i = 0; i < BP_STAGES; i++) {
            s.bpg_from[i] = to_polar(&s.bp[s.bp_cur].bq[i]);
            s.bpg_to[i] = to_polar(&to.bq[i]);
        }
        s.bp_glide = XFADE_N;
    } else if (n.filter != s.cur.filter || (bw && step)) {
        /* another filter, or a jump that a glide would sweep over the tones
         * in between: the new filter starts from rest in the other slot */
        s.bp_cur ^= 1;
        bandpass_design(&s.bp[s.bp_cur], n.pitch_hz, bw);
        s.bp_xfade = XFADE_N;
    }
    s.cur = n;
    stage_set(&s.st, &n);
}

void dsp_init(const dsp_params_t *p)
{
    memset(&s, 0, sizeof(s));
    for (int i = 0; i < 4; i++) {
        s.ap_a[i].a2 = HILB_A[i] * HILB_A[i];
        s.ap_b[i].a2 = HILB_B[i] * HILB_B[i];
    }
    s.cur = *p;
    dsp_params_sanitize(&s.cur);
    stage_set(&s.st, &s.cur);
    s.st_old = s.st;
    bandpass_design(&s.bp[0], s.cur.pitch_hz, dsp_filter_bw_hz(s.cur.filter));
    s.agc_gain = 1.0f;
    s.agc_mix = s.cur.agc ? 1.0f : 0.0f;
    s.agc_dec = 1.0f - expf(-1.0f / (AGC_DECAY_S * DSP_FS));
    s.agc_fall = 1.0f - expf(-1.0f / (AGC_FALL_S * DSP_FS));
    s.agc_rise = 1.0f - expf(-1.0f / (AGC_RISE_S * DSP_FS));
}

void dsp_set_params(const dsp_params_t *p)
{
    /* writer side of the seqlock; only one writer task */
    s.seq++;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    s.pending = *p;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    s.seq++;
    s.pending_seq = s.seq;
}

static void take_pending(void)
{
    unsigned want = s.pending_seq;
    if (want == s.taken_seq || s.xfade > 0 || s.glide > 0 || s.bp_xfade > 0 || s.bp_glide > 0)
        return;
    unsigned a = s.seq;
    if (a & 1)
        return;                 /* writer busy, next block */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    dsp_params_t p = s.pending;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (s.seq != a)
        return;
    s.taken_seq = a;
    apply(&p);
}

void dsp_process(const float *in, float *out, size_t n)
{
    take_pending();
    for (size_t k = 0; k < n; k++) {
        /* DC block, corner about 20 Hz */
        float x = in[k];
        float y = x - s.dc_x1 + 0.992f * s.dc_y1;
        s.dc_x1 = x;
        s.dc_y1 = y;

        if (s.bp_glide > 0) {
            float g = 1.0f - (float)--s.bp_glide / XFADE_N;    /* -> 1 */
            for (int i = 0; i < BP_STAGES; i++)
                glide_stage(&s.bp[s.bp_cur].bq[i], &s.bpg_from[i], &s.bpg_to[i], g);
        }
        float f = bandpass(&s.bp[s.bp_cur], y);
        if (s.bp_xfade > 0) {
            float g = (float)s.bp_xfade / XFADE_N;     /* 1 -> 0 */
            f = f * (1.0f - g) + bandpass(&s.bp[s.bp_cur ^ 1], y) * g;
            s.bp_xfade--;
        }

        y = agc(f);

        float a = y, b = y;
        for (int i = 0; i < 4; i++) {
            a = allpass(&s.ap_a[i], a);
            b = allpass(&s.ap_b[i], b);
        }
        float ad = s.a_delay;
        s.a_delay = a;

        s.pos = (s.pos + 1) & HIST_MASK;
        s.hist_i[s.pos] = ad;
        s.hist_q[s.pos] = b;

        if (s.glide > 0) {
            float g = (float)--s.glide / XFADE_N;      /* -> 0 */
            float th = s.st.th + g * (s.glide_from - s.st.th);
            s.st.rot_c = cosf(th);
            s.st.rot_s = sinf(th);
        }

        float complex zl, zr;
        stereo(&s.st, &zl, &zr);
        float l = crealf(zl), r = crealf(zr);
        if (s.xfade > 0) {
            float complex ol, or_;
            stereo(&s.st_old, &ol, &or_);
            float g = 1.0f - (float)s.xfade / XFADE_N;     /* 0 -> 1 */
            l = fade(ol, zl, g, &s.xf_phi[0]);
            r = fade(or_, zr, g, &s.xf_phi[1]);
            s.xfade--;
        }
        out[2 * k] = limit(clampf(l, -4.0f, 4.0f));
        out[2 * k + 1] = limit(clampf(r, -4.0f, 4.0f));
    }
}
