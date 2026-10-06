# How Horchposten works

This document explains the signal chain and the auto pitch in detail: what
each stage does, why it is built that way, and the numbers it uses. The
code is in [firmware/main/dsp.c](../firmware/main/dsp.c),
[firmware/main/pitch.c](../firmware/main/pitch.c) and
[firmware/main/audio_io.c](../firmware/main/audio_io.c); the host tests in
[firmware/test/host](../firmware/test/host/dsp_test.c) check the claims made
here.

## Contents

- [Overview](#overview)
- [Audio path and clocks](#audio-path-and-clocks)
- [DC block](#dc-block)
- [CW band pass](#cw-band-pass)
- [AGC](#agc)
- [Hilbert pair: I and Q](#hilbert-pair-i-and-q)
- [Stereo modes](#stereo-modes)
  - [Binaural by pitch](#binaural-by-pitch)
  - [Binaural 90 degrees](#binaural-90-degrees)
  - [Haas](#haas)
  - [Mono and swap](#mono-and-swap)
- [Soft limiter](#soft-limiter)
- [Changes without clicks](#changes-without-clicks)
  - [Handing over new settings](#handing-over-new-settings)
  - [Mode, width and swap](#mode-width-and-swap)
  - [Centre pitch](#centre-pitch)
  - [Band pass](#band-pass)
  - [AGC on and off](#agc-on-and-off)
  - [Output fader and volume](#output-fader-and-volume)
- [Auto pitch](#auto-pitch)
- [How it is tested](#how-it-is-tested)

## Overview

```
LINE IN ─► ADC ─► DC block ─► band pass ─► AGC ─► Hilbert pair ─┬─ I ─► stereo stage ─► limiter ─► fader ─► DAC ─► headphones
 (left)    16 kHz  20 Hz      off/500/     35 dB   90° apart    └─ Q ─►  (mode, width,
                              250/100 Hz                                  swap)
```

Everything runs on one mono input (the left channel of LINE IN) at
16 kHz, in blocks of 64 samples (4 ms). The stereo stage is the only part
that makes two channels. Every change of a setting is faded, so nothing
clicks.

## Audio path and clocks

The ESP32 is I2S master: it sends the bit clock (BCK, 32 × fs), the word
clock (WS, fs) and the codec's master clock (MCLK, 256 × fs) on GPIO0.
The codec (ES8388 on the Audio Kit V2.2) is slave, 16 bit Philips format.

MCLK comes from the ESP32's default PLL, **not from the APLL**. With MCLK
from the APLL, the ES8388 DAC hissed loudly whenever it got any non-zero
data, louder than a tone at −20 dBFS; with the default PLL the output is
clean (found on the board, see the commit history).

The audio task runs on core 1 at high priority: read a block from the
ADC, process it, write it to the DAC. Four DMA buffers of one block each
give 16 ms of buffering.

## DC block

A one-pole high pass removes any DC offset of the ADC before the filters:

```
y[n] = x[n] − x[n−1] + 0.992 · y[n−1]
```

The corner is at (1 − 0.992) · fs / 2π ≈ 20 Hz, far below any CW pitch.

## CW band pass

Four settings: off, 500, 250 and 100 Hz wide, centred on the centre
pitch (300…1000 Hz).

**Design.** A Butterworth band pass of order 8, built from 4 second order
sections (biquads), designed in the firmware when a setting changes:

1. Low pass prototype of order 4: poles on the left half of the unit
   circle, `p_k = e^{jπ(2k + 5)/8}`.
2. Low pass to band pass transform with the band edges pre-warped for the
   bilinear transform: `ω_l = 2fs·tan(π f_l / fs)`, `ω_h` likewise,
   `B = ω_h − ω_l`, `ω0² = ω_l·ω_h`. Each prototype pole gives two band
   pass poles, roots of `s² − p·B·s + ω0² = 0`.
3. Bilinear transform `z = (2fs + s) / (2fs − s)`; only the poles with a
   positive imaginary part are kept, their conjugates are implied.
4. Each section has its zeros at z = 1 and z = −1 (numerator 1 − z⁻²) and
   is scaled to 0 dB at the centre.

The result is −3 dB at fc ± bw/2 and strongly damped at fc ± 2·bw (the
tests ask for more than 20 dB). The sections run in transposed direct
form II, in single precision.

**Why order 8.** A CW filter must be steep enough to cut a neighbour
100 Hz away, but every extra order adds ringing. Order 8 is a usual
compromise for 100…500 Hz CW filters.

How a new filter or a new centre pitch is faded in is described under
[Band pass](#band-pass) further down.

## AGC

The AGC keeps strong and weak signals at about the same level (target
peak 0.3, at most 60 × ≈ 35 dB of gain).

- **Envelope:** peak follower on the filtered signal. A new peak is taken
  at once; the envelope then holds for 150 ms (hang, to bridge the gaps
  between dots and dashes) and decays with a time constant of 300 ms.
- **Gain:** `wanted = 0.3 / max(envelope, 0.3/60)`. The gain follows it
  with a time constant of 0.4 ms when it must go down and 3 ms when it
  goes up (no click when it rises).
- **Look-ahead:** the level is measured on the input, but the gain is
  applied 3 ms later (a 48 sample delay line). So when a station starts
  after a pause, the gain is already down when its first element comes
  out: the first dot is not louder than the rest. The delay is there with
  the AGC off too, so switching it does not shift the signal.

## Hilbert pair: I and Q

The stereo modes need the signal twice, 90° apart over the whole band.
Horchposten uses Olli Niemitalo's allpass design: two chains of four
second order allpass sections,

```
y[n] = a² · (x[n] + y[n−2]) − x[n−2]
```

with the coefficients `a` of the two chains chosen so that their phase
difference stays at 90° ± 0.7° over nearly the whole band. Chain A is
delayed by one more sample. Its output is **I**, the output of chain B is
**Q**; Q leads I by 90°. Both chains have a gain of exactly 1, so the
level is not changed.

Together they form the analytic signal `A = I − jQ`: for a tone
`cos(ωt)` it is `e^{jωt}`, a pointer turning with the tone. Delaying the
signal by D samples turns this pointer back by ωD; multiplying it by
`e^{jθ}` turns it forward by θ. The output of each ear is the real part
of such a pointer. I and Q go into a history of 512 samples, so the
stereo stage can take them with any delay up to 32 ms.

## Stereo modes

### Binaural by pitch

The main mode. One ear gets the signal as it is, the other a version that
is delayed by D and turned by the angle θ = ωc·D, ωc being the centre
pitch:

```
right = I(t)
left  = Re{ A(t − D) · e^{jωc·D} } = I(t − D)·cos(ωc D) + Q(t − D)·sin(ωc D)
```

For a tone at frequency f the left ear lags the right one by

```
Δφ = 2π · (f − fc) · D
```

- at the centre pitch Δφ = 0: the tone is in the middle of the head;
- a higher tone lags in the left ear, so the right ear hears it first:
  it moves to the right;
- a lower tone leads in the left ear: it moves to the left.

The brain locates low tones by the phase difference between the ears, so
each station in the pass band gets its own place between left and right,
by its pitch. That is what makes a pile-up readable: the stations no
longer sit on top of each other.

The width sets D, and so how far from the centre pitch a tone reaches
90° (a clear left or right):

| Width | D | 90° at |
|---|---|---|
| narrow | 8 samples, 0.5 ms | ±500 Hz from the centre |
| medium | 13 samples, 0.81 ms | ±310 Hz |
| wide | 20 samples, 1.25 ms | ±200 Hz |

The level is the same in both ears for every pitch, because I and Q have
the same amplitude.

### Binaural 90 degrees

```
left = I(t),  right = Q(t)
```

Every tone has 90° between the ears, whatever its pitch. This gives a
wide, diffuse image ("in the room" instead of "in the head"); the
stations are not spread by pitch.

### Haas

```
left = I(t),  right = I(t − T)
```

with T = 8, 15 or 25 ms (narrow, medium, wide). The earlier ear wins (the
precedence or Haas effect): the signal seems to come from the left, but
the delayed copy makes it broader and easier to listen to for a long
time.

### Mono and swap

Mono puts I(t) on both ears, as a plain receiver would. Swap exchanges
left and right in every mode.

## Soft limiter

Last stage before the output, against a very hot input or an overshoot:

```
|v| ≤ 0.7 :  v
|v| > 0.7 :  0.7 + 0.3 · o / (1 + o),  o = (|v| − 0.7) / 0.3
```

Linear up to 0.7, then a smooth curve that approaches 1.0 but never
reaches it. Values beyond ±4 are clamped first. With the AGC on, the
limiter only works on short peaks.

## Changes without clicks

A setting that changes from one sample to the next is a step in the
waveform, and a step is a click. Every change in Horchposten is therefore
spread over 20 ms. While a fade runs, a further change waits until it is
over, so a fade is never cut off.

### Handing over new settings

The user interface runs on the other core. It hands new settings over
with a sequence lock: it counts a sequence number up before and after
writing them (odd = being written). At the start of a block the audio task
takes them if the number is even, has changed and did not change while
copying, and if no fade is running. All fades of one change share one
counter.

### Mode, width and swap

The stereo stage runs twice during the fade: once with the old settings,
once with the new ones. A plain mix of the two outputs fails here: for
some pitches the old and the new output are in antiphase (for example
Haas swap at 633 Hz), and a mix of two opposite signals cancels; the
level dropped to 2…5 % for some milliseconds.

So each ear is computed as an analytic signal (a pointer, see above), and
the fade glides from the old pointer zo to the new one zn:

```
level  = |zo| + g · (|zn| − |zo|)
phase  = arg(zo) + g · φ,   φ = arg(zn · conj(zo))
output = level · cos(phase),   g = 0 → 1 over 20 ms
```

φ, the angle between the two pointers, is followed from sample to sample
(unwrapped), so it never jumps by 2π while the level is up. For a single
tone the result keeps its full level and glides in phase; its pitch moves
by at most 25 Hz for these 20 ms, which is not heard as a click.

### Centre pitch

A new centre pitch only changes the angle θ = ωc·D. It is not faded as
above but turned: θ moves in equal steps from the old to the new value
over the 20 ms. The rotation pointer is multiplied by a fixed small turn
per sample (no sine or cosine per sample); the last sample gets the exact
target.

### Band pass

Two cases:

- **A small step of the same filter** (the pitch moves by at most half
  the bandwidth, e.g. 25 Hz with the 100 Hz filter): the poles of the
  running filter glide to the new centre. Each pole is turned and moved
  in equal steps per sample, and the gain follows. The filter keeps its
  state, so a tone in the pass band goes on without a dip. A second
  filter faded in instead would differ in phase at the tone and dip the
  level.
- **Another filter, or a bigger jump**: a glide would sweep over all the
  tones in between. The new filter starts from rest in a second slot and
  is cross-faded with the old one over 20 ms.

The cross-fade of two different filters can still dip a tone for a few
milliseconds where their phases differ much (measured on the host: up to
−10 dB from 500 Hz to off for a tone 200 Hz from the centre). This is an
open point.

### AGC on and off

The AGC's envelope and gain run all the time, also with the AGC off.
Switching only fades a mix factor between 1 and the AGC gain over 20 ms.

### Output fader and volume

After the signal chain, the audio task multiplies the output with a
fader level that ramps in 10 ms. It goes to 0 for mute and for a short
"duck" around hard steps elsewhere:

- a change of the input gain (6 dB per press, a hard step in the codec's
  input amplifier);
- writing the settings to flash, which stalls both cores for some
  milliseconds (about 50 ms of silence in all).

The volume is set in the ES8388's digital DAC volume, which has its own
soft ramp (0.5 dB per 32 samples); the output stage stays fixed at
+4.5 dB. A volume step therefore does not click.

## Auto pitch

A long press on KEY4 records 300 ms of the raw input and looks for the
strongest tone between 300 and 1000 Hz. If it finds one, the centre pitch
is set to it, so that station sits in the middle and the filter is
centred on it.

1. **Window:** the 4800 samples get a Hann window, to keep the side lobes
   of a strong tone away from the bins around it.
2. **Filter bank:** the power at every 5 Hz from 240 to 1060 Hz is
   computed with the Goertzel algorithm (one second order recursion per
   frequency, 165 frequencies). The bank reaches 60 Hz beyond the range
   on purpose (see step 5).
3. **Peak:** the strongest bin.
4. **Is it a tone?** The noise reference is the mean power 20…60 Hz
   beside the peak, without the strongest bin there (another station),
   taken on the side with more noise. The peak must be at least 14 dB
   (25 × in power) above it. A reference from the whole band would fail
   behind a narrow CW filter in the transceiver: most of the band is then
   stop band, and every noise peak in the pass band would count as a
   tone.
5. **Out of range:** if the peak lies outside 300…1000 Hz (more than half
   a bin), the strongest tone is not in the range and there is no result.
   Without the extra bins, a tone at 1015 Hz was taken as 1000 Hz, and
   behind a narrow filter at the edge of the range noise passed as a
   tone in 15…19 % of the captures.
6. **Fine frequency:** a parabola through the logarithmic power of the
   peak bin and its two neighbours gives the frequency between the bins.

Measured over 2000 noise captures each, with and without a 100 or 250 Hz
filter and at the ends of the range, noise passes as a tone in at most
0.15 %. A threshold of 15 dB would remove these but miss five times as
many weak tones (0.015 in noise of 0.1), so it stays at 14 dB.

## How it is tested

The host tests (`make -C firmware/test/host`) run the same C code on the
PC:

- each mode: phase and level between the ears for tones across the band;
- the band pass: level at the centre, −3 dB at the edges, damping far off;
- the AGC: 40 dB of input range gives less than 3 dB of output range; the
  first element after a pause is not louder than the rest;
- clicks: every kind of change on a clean tone; the largest step between
  two samples may be at most 15 % above the tone's own. The test is
  checked against a copy of the code with the fades removed: it must fail
  there;
- dropouts: every mode, width and swap change on tones from 310 to
  1000 Hz; neither ear may drop below 90 % of the level;
- auto pitch: tones at and beyond the edges of the range, noise behind
  transceiver filters, 100 runs each.

`make -C firmware/test/host wav` also writes listening examples, and
`make -C firmware/test/host pileup` a 10 minute busy band (3…8 stations
calling CQ) to play into LINE IN.
