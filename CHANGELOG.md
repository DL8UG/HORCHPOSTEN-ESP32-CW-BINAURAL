# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

- Signal chain: DC block, CW band pass (off/500/250/100 Hz), AGC, Hilbert
  pair, stereo modes binaural by pitch, binaural 90°, Haas and mono, soft
  limiter. Every change of mode, width, filter, pitch and AGC is faded,
  without clicks.
- Auto pitch on the strongest tone (300–1000 Hz).
- Firmware for the ESP32-Audio-Kit: codec detection ES8388 / AC101, six
  keys with short and long press, status LED, settings kept in flash.
  Volume, input gain and mute are faded too, and so is the output while
  the settings are written to flash.
- Host tests of the signal chain with WAV output.
- Parametric 3D printable enclosure (OpenSCAD).
- CI: host tests, firmware build, documentation check; release workflow.
