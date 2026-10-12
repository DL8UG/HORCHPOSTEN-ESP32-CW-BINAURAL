# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.2.0] - 2026-10-12

Tested on an ESP32-Audio-Kit V2.2 (A618, ES8388) with a transceiver; the
enclosure is printed and fits. The firmware is the same as in 0.1.0.

### Added

- Pictures of both enclosure sides in the enclosure README.
- README: plans, an optional display for a second version.

### Changed

- Enclosure: no longer a draft; printed and fitted on the V2.2 board
  (A618).
- Enclosure: key plungers 8 mm longer; both USB openings 2 mm wider and
  higher, POWER 3 mm further forward; jack openings 2 mm larger (9 mm);
  larger lid lettering, key labels in two staggered rows; wall labels
  below the openings, larger, with symbols for power, phones and line in.

### Known issues

- The two on-board microphones share the line input: remove them (see
  the README, Hardware).
- AC101 boards are untested; their volume steps are hard (no ramp).

## [0.1.0] - 2026-10-06

First version. Tested on an ESP32-Audio-Kit V2.2 (A618, ES8388) with
audio from a PC; not yet with a transceiver.

### Added

- Signal chain: DC block, CW band pass (off/500/250/100 Hz, Butterworth
  8th order), AGC with look-ahead, Hilbert pair, stereo modes binaural by
  pitch, binaural 90°, Haas and mono, soft limiter. Every change of mode,
  width, swap, filter, pitch and AGC is faded without clicks; mode, width
  and swap changes glide in phase, so no tone drops out.
- Auto pitch on the strongest tone (300–1000 Hz); no result for a tone
  outside the range.
- Firmware for the ESP32-Audio-Kit: codec detection ES8388 / AC101, six
  keys with short and long press, status LED confirms every change,
  settings kept in flash (written only when they changed). Volume (soft
  ramp), input gain and mute are faded, and so is the output while the
  settings are written to flash.
- Host tests of the signal chain with WAV output; a generator for a busy
  CW band (`tools/pileup.c`) to play into LINE IN.
- Documentation: how the signal chain works (`docs/algorithms.md`), a
  drawing of the board.
- Parametric 3D printable enclosure (OpenSCAD), a draft.
- CI: host tests, firmware build, documentation check; release workflow.

### Known issues

- The two on-board microphones share the line input: remove them (see
  the README, Hardware).
- Changing the band pass filter, or an auto pitch jump with a filter on,
  can dip a tone for a few milliseconds.
- AC101 boards are untested; their volume steps are hard (no ramp).
- Enclosure: dimensions from a product photo, not measured; not printed
  or tested yet.

[Unreleased]: https://github.com/DL8UG/HORCHPOSTEN-ESP32-CW-BINAURAL/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/DL8UG/HORCHPOSTEN-ESP32-CW-BINAURAL/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/DL8UG/HORCHPOSTEN-ESP32-CW-BINAURAL/releases/tag/v0.1.0
