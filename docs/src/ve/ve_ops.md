# ve_ops.c - Operation Classification

> **File:** [`src/ve/ve_ops.c`](../../../src/ve/ve_ops.c)
> **Package:** `ve`
> **Purpose:** Classification: does an operation force a re-encode?

## Overview

[`ve_ops.c`](../../../src/ve/ve_ops.c) implements `ve_classify()`, which decides for each operation whether it forces a video/audio decode (transcode), is container-only (lossless), or is structural. It also provides `ve_copy()` and `ve_op_name()`.

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ve_copy(dst, cap, src)` | `dst: char*, cap: unsigned long, src: const char*` | `void` | Safe bounded string copy |
| `ve_op_name(k)` | `k: VeOpKind` | `const char *` | Human-readable op name |
| `ve_classify(op)` | `op: const VeOp*` | `VeOpClass` | Classifies an op |

## Classification Rules

| Category | Ops | Result |
|----------|-----|--------|
| **Container / metadata (lossless)** | `VE_MUTE`, `VE_REMOVE_AUDIO`, `VE_ADD_AUDIO`, `VE_REMOVE_SUBTITLE`, `VE_ADD_SUBTITLE`, `VE_ADD_THUMBNAIL`, `VE_REMOVE_THUMBNAIL`, `VE_METADATA`, `VE_FORMAT`, `VE_ASPECT` | `container_only = true` |
| **Rotate** | `VE_ROTATE` | `container_only` if a multiple of 90°, else `needs_video_decode` |
| **Structural** | `VE_TRIM`, `VE_MERGE` | `structural = true`, `container_only = true` (attempted losslessly) |
| **Force video transcode** | `VE_FLIP`, `VE_SCALE`, `VE_CROP`, `VE_BRIGHTNESS`, `VE_CONTRAST`, `VE_HUE`, `VE_SATURATION`, `VE_GAMMA`, `VE_GRAYSCALE`, `VE_FPS`, `VE_REVERSE_VIDEO`, `VE_DENOISE`, `VE_SHARPEN`, `VE_PAD` | `needs_video_decode = true` |
| **Force audio transcode** | `VE_VOLUME`, `VE_MONO`, `VE_REVERSE_AUDIO` | `needs_audio_decode = true` |
| **Both streams** | `VE_SPEED` | `needs_video_decode` + `needs_audio_decode` |
| **Fade** | `VE_FADE` | Depends on target (`v`/`a`/`b`) |

## Implementation Notes

- Classification can depend on parameters (e.g., `rotate 90` is lossless, `rotate 37` is not), so `ve_classify` takes the whole op.
- `VE_ASPECT` is classified as container-only here, but the planner forces transcode so the SAR is baked into the bitstream.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [ve_dsl.c](ve_dsl.md) | [ve/](index.md) | [ve_plan.c](ve_plan.md) |