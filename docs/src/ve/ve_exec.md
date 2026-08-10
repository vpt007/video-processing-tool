# ve_exec.c - Plan Execution (libav)

> **File:** [`src/ve/ve_exec.c`](../../../src/ve/ve_exec.c)
> **Package:** `ve`
> **Purpose:** Execute a `VePlan` with libav (no subprocess)

## Overview

[`ve_exec.c`](../../../src/ve/ve_exec.c) executes a `VePlan` using the FFmpeg libav API. It has two paths:

- **`remux_lossless()`** — when `plan->lossless`: copy packets, edit only the container (drop streams, set SAR, rotate via display matrix, keyframe-aligned trim, metadata). No decode/encode.
- **`transcode()`** — decode → filter (`plan->vf` / `plan->af`) → encode, but only for the stream(s) the planner marked `TRANSCODE`; the other stream is still copied.

This file is compiled out unless `VE_WITH_LIBAV` is defined, so the rest of the tool builds with no dependencies. It targets FFmpeg ≥ 7.0 (libavformat ≥ 61); built/tested against 8.1.

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ve_execute(p, on_progress, user, err, errlen)` | `p: const VePlan*, on_progress: VeProgressCb, user: void*, err: char*, errlen: int` | `int` | Executes the plan; 0 = success, `VE_ECANCELED` = cancelled, negative = error |

### Progress Callback

```c
typedef int (*VeProgressCb)(void *user, double fraction, double cur_sec, double total_sec);
```

- `fraction` is 0..1, or <0 when the total duration is unknown.
- Return **non-zero to cancel**: `ve_execute` stops, deletes the partial output, and returns `VE_ECANCELED`.
- Return 0 to continue.

## Execution Paths

### remux_lossless()
- Opens input, allocates output context.
- Adds thumbnail / extra audio / subtitle streams (muxed by copy).
- Drops streams per `keep_stream()` (mute, drop-audio, drop-sub, remove-thumbnail).
- Applies metadata.
- Writes header, copies packets (with optional keyframe-aligned trim and PTS rebasing), writes trailer.
- Guards: errors if `rotate_quadrant` or `sar` is set while `lossless` (the planner must mark those as transcode).

### transcode()
- Per-stream `StreamCtx` with mode `S_DROP` / `S_COPY` / `S_XCODE`.
- Opens decoders, builds filter graphs (`buffersrc` → chain → `buffersink`), encodes with H.264 (video) / AAC (audio) defaults.
- Formats are pinned (`yuv420p` / `fltp`) via appended `format`/`aformat` filters.

## Implementation Notes

- Encoder defaults are H.264 for video and AAC for audio.
- `emit_progress` throttles progress callbacks to ~1% steps.
- On cancellation, the partial output file is removed.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [ve_plan.c](ve_plan.md) | [ve/](index.md) | [src/](../index.md) |