# ve_plan.c - Execution Plan Builder

> **File:** [`src/ve/ve_plan.c`](../../../src/ve/ve_plan.c)
> **Package:** `ve`
> **Purpose:** Turn ops into an execution plan (COPY / TRANSCODE / DROP per stream)

## Overview

[`ve_plan.c`](../../../src/ve/ve_plan.c) builds a `VePlan` from a parsed `VeScript`. The core policy: video and audio each start as `COPY`. The first op that needs to decode that stream flips it to `TRANSCODE` and starts a filter chain. Lossless ops only touch the container/metadata. The whole edit is "lossless" iff neither stream ended up `TRANSCODE`.

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ve_plan(s, out, err, errlen)` | `s: const VeScript*, out: VePlan*, err: char*, errlen: int` | `bool` | Builds an execution plan |
| `ve_plan_print(p)` | `p: const VePlan*` | `void` | Prints a human-readable plan summary (for `--dry-run`) |

## Planning Rules

| Op | Plan Effect |
|----|-------------|
| `VE_MUTE` | `drop_all_audio = true` |
| `VE_REMOVE_AUDIO` / `VE_REMOVE_SUBTITLE` | Adds to `drop_audio_index` / `drop_sub_index` |
| `VE_ADD_AUDIO` / `VE_ADD_SUBTITLE` | Adds to `add_audio` / `add_sub` |
| `VE_ADD_THUMBNAIL` / `VE_REMOVE_THUMBNAIL` | Sets `thumbnail` / `remove_thumbnail` |
| `VE_METADATA` | Adds `key=value` to `meta` |
| `VE_FORMAT` | Sets `format` |
| `VE_ASPECT` | Sets `sar_num`/`sar_den` and forces video transcode |
| `VE_ROTATE` | Quadrant (×90°) → `rotate_quadrant` + transcode; else `rotate=` filter |
| `VE_TRIM` | Sets trim window; forces both streams to transcode (for sync) |
| `VE_MERGE` | Adds merge inputs |
| Video filters | Appends to `vf` (flip, scale, crop, eq, hue, fps, reverse, denoise, sharpen, pad) |
| Audio filters | Appends to `af` (volume, mono, areverse) |
| `VE_SPEED` | `setpts` (video) + `atempo` chain (audio) |
| `VE_FADE` | `fade`/`afade` filters per target |

## Implementation Notes

- `fappend` joins filters with `,` separators.
- `atempo_chain` chains `atempo` stages to reach an arbitrary speed factor (atempo only accepts 0.5..2.0).
- Trim forces transcode so the `trim`/`atrim` filters cut both streams to the same window and `setpts`/`asetpts` rebase timestamps to zero (keeping sync).
- `lossless` is true iff neither stream is `TRANSCODE`.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [ve_ops.c](ve_ops.md) | [ve/](index.md) | [ve_exec.c](ve_exec.md) |