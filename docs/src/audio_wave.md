# audio_wave.c - Audio Waveform Extractor

> **File:** [`src/audio_wave.c`](../../src/audio_wave.c)
> **Package:** `app`
> **Purpose:** Background audio waveform (peak envelope) extraction

## Overview

[`audio_wave.c`](../../src/audio_wave.c) spawns a worker on `aw_begin(path, tracks, count)` that decodes each audio stream with libav and computes a normalized peak envelope (a fixed number of buckets). The UI thread polls `aw_ready(i)` and reads the peaks with `aw_peaks(i, &n)`. Shared state is guarded by a mutex. The GL texture rasterization happens on the UI thread (see `build_wave_texture` in [`main.c`](main.md)); this file stays pure CPU.

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `AW_BUCKETS` | `1000` | Number of peak buckets per track |
| `AW_MAX_TRACKS` | `32` | Max audio tracks analyzed |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `aw_begin(path, tracks, count)` | `path: const char*, tracks: VPStreamInfo*, count: int` | `void` | Kicks off the waveform worker |
| `aw_ready(i)` | `i: int` | `int` | 1 if track `i` is ready |
| `aw_peaks(i, npk)` | `i: int, npk: int*` | `const float *` | Normalized peak array (0..1) for track `i` |

## Implementation Notes

- The worker decodes every audio stream it finds and computes per-bucket peak maxima across all channels.
- Each bucket is normalized to 0..1 by the global peak.
- `tracks`/`count` are accepted for API symmetry with the caller (`refresh_video_info`) but are not used by the worker.
- The worker is `pthread_detach`-ed; `aw_begin` ignores the request if a worker is already running.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [thumb_strip.c](thumb_strip.md) | [src/](index.md) | [vp_thumb.c](vp_thumb.md) |