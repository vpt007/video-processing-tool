# thumb_strip.c - Filmstrip Thumbnail Extractor

> **File:** [`src/thumb_strip.c`](../../src/thumb_strip.c)
> **Package:** `app`
> **Purpose:** Background filmstrip thumbnail extraction for the trim track

## Overview

[`thumb_strip.c`](../../src/thumb_strip.c) spawns a worker thread on `ts_begin(path)` that opens the video with libav, seeks to `TS_CELLS` evenly-spaced timestamps, decodes one frame at each, scales it to a small RGB24 cell, and stores it. The UI thread polls `ts_cell_ready()` and reads the pixels with `ts_cell_rgb()`. All shared state is guarded by a mutex. The GL texture upload happens on the UI thread (see `ts_upload_tex` in [`main.c`](main.md)); this file stays pure CPU so it is safe to run off the main thread.

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `TS_CELLS` | `24` | Number of filmstrip cells |
| `TS_W` / `TS_H` | `160` / `90` | Cell size in pixels |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ts_begin(path)` | `path: const char*` | `void` | Kicks off the filmstrip worker |
| `ts_count()` | — | `int` | Number of cells (`TS_CELLS`) |
| `ts_cell_ready(ci)` | `ci: int` | `int` | 1 if cell `ci` is ready |
| `ts_cell_rgb(ci, w, h)` | `ci: int, w: int*, h: int*` | `const uint8_t *` | RGB24 pixels for cell `ci` (NULL if not ready) |

## Implementation Notes

- Worker seeks to `duration * (i + 0.5) / TS_CELLS` for each cell, decodes until a frame with pts ≥ the target, and scales it with `sws_scale` to `TS_W × TS_H` RGB24.
- Uses `pthread_mutex_t` to guard the cell array; the worker is `pthread_detach`-ed.
- `ts_begin` ignores the request if a worker is already running.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [ffwrap.c](ffwrap.md) | [src/](index.md) | [audio_wave.c](audio_wave.md) |