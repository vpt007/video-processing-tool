# vp_thumb.c - Thumbnail Capture Helpers

> **File:** [`src/vp_thumb.c`](../../src/vp_thumb.c)
> **Package:** `app`
> **Purpose:** Grab the currently displayed frame from the playback engine as RGB24

## Overview

[`vp_thumb.c`](../../src/vp_thumb.c) provides a small utility to grab the currently displayed frame from the playback engine as an RGB24 buffer, which the UI can use to build a cover thumbnail. It is included by the unity build after [`vp_engine.c`](vp_engine.md) so the `VPEngine` API is available.

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_thumb_capture(eng, out)` | `eng: VPEngine*, out: VPFrameRGB*` | `int` | Copies the engine's current frame into a caller-owned `VPFrameRGB` (pixels malloc'd; free with `vp_engine_frame_free`); 1 on success, 0 if no frame |
| `vp_thumb_has_frame(eng)` | `eng: VPEngine*` | `int` | Convenience: captures and immediately frees the frame, returning whether a frame was available |

## Implementation Notes

- `vp_thumb_capture` wraps `vp_engine_snapshot`.
- `vp_thumb_has_frame` is useful for "is there a frame yet?" checks without keeping a copy.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [audio_wave.c](audio_wave.md) | [src/](index.md) | [test.c](test.md) |