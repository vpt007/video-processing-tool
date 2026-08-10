# ve.h - Video-Edit Operation Model & API

> **File:** [`src/ve/ve.h`](../../../src/ve/ve.h)
> **Package:** `ve`
> **Purpose:** Operation model, lossless classification, and the public API for the ve/ library

## Overview

[`ve.h`](../../../src/ve/ve.h) defines the operation model (`VeOpKind`, `VeOp`), the classification result (`VeOpClass`), the parsed script (`VeScript`), the execution plan (`VePlan`), and the public API (`ve_parse`, `ve_plan`, `ve_execute`). It has no libav dependency.

## Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `VE_PATH_MAX` | `1024` | Max path length |
| `VE_MERGE_MAX` | `16` | Max merge inputs |
| `VE_FILTER_MAX` | `4096` | Max filter chain length |
| `VE_ECANCELED` | `-2` | `ve_execute` return value when the caller cancelled |

## Types

### VeOpKind
**File:** [`ve.h`](../../../src/ve/ve.h:25)

The set of supported operations: `VE_ROTATE`, `VE_FLIP`, `VE_MUTE`, `VE_ADD_AUDIO`, `VE_REMOVE_AUDIO`, `VE_VOLUME`, `VE_MONO`, `VE_SCALE`, `VE_CROP`, `VE_MERGE`, `VE_TRIM`, `VE_REVERSE_VIDEO`, `VE_REVERSE_AUDIO`, `VE_ADD_THUMBNAIL`, `VE_REMOVE_THUMBNAIL`, `VE_ADD_SUBTITLE`, `VE_REMOVE_SUBTITLE`, `VE_BRIGHTNESS`, `VE_CONTRAST`, `VE_HUE`, `VE_SATURATION`, `VE_GAMMA`, `VE_GRAYSCALE`, `VE_ASPECT`, `VE_FPS`, `VE_SPEED`, `VE_FADE`, `VE_DENOISE`, `VE_SHARPEN`, `VE_PAD`, `VE_METADATA`, `VE_FORMAT`.

### VeOp
**File:** [`ve.h`](../../../src/ve/ve.h:64)

| Field | Type | Description |
|-------|------|-------------|
| `kind` | `VeOpKind` | The operation kind |
| `line` | `int` | Source line in the script (for diagnostics) |
| `u` | `union` | Operation-specific parameters (rotate, flip, crop, trim, etc.) |

### VeOpClass
**File:** [`ve.h`](../../../src/ve/ve.h:94)

| Field | Type | Description |
|-------|------|-------------|
| `needs_video_decode` | `bool` | Forces the video stream into transcode |
| `needs_audio_decode` | `bool` | Forces the audio stream into transcode |
| `container_only` | `bool` | Pure remux/metadata, never touches pixels |
| `structural` | `bool` | Trim/merge — changes packet timeline |

### VeScript
**File:** [`ve.h`](../../../src/ve/ve.h:112)

| Field | Type | Description |
|-------|------|-------------|
| `input` | `char[VE_PATH_MAX]` | Input path |
| `output` | `char[VE_PATH_MAX]` | Output path |
| `ops` | `VeOp *` | Parsed operations |
| `n_ops` / `cap_ops` | `int` | Operation count / capacity |

### VePlan
**File:** [`ve.h`](../../../src/ve/ve.h:131)

| Field | Type | Description |
|-------|------|-------------|
| `input` / `output` | `char[VE_PATH_MAX]` | Input/output paths |
| `format` | `char[16]` | Forced container, or `""` = by extension |
| `video` / `audio` | `VeStreamAction` | COPY / TRANSCODE / DROP per stream |
| `vf` / `af` | `char[VE_FILTER_MAX]` | Filter chains (only if TRANSCODE) |
| `rotate_quadrant` | `int` | Lossless display-matrix rotate: 0..3 × 90° |
| `sar_num` / `sar_den` | `int` | Lossless aspect override (0 = none) |
| `has_trim` / `trim_start` / `trim_end` | `bool` / `double` / `double` | Trim window |
| `n_merge` / `merge` | `int` / `char[16][VE_PATH_MAX]` | Merge inputs |
| `add_audio` / `add_sub` | `char[8][VE_PATH_MAX]` | Added streams (muxed by copy) |
| `thumbnail` / `remove_thumbnail` | `char[VE_PATH_MAX]` / `bool` | Cover thumbnail |
| `drop_audio_index` / `drop_sub_index` | `int[16]` | Dropped existing streams |
| `meta` | `char[16][256]` | Metadata key=value pairs |
| `lossless` | `bool` | True iff no stream is transcoded |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `ve_classify(op)` | `op: const VeOp*` | `VeOpClass` | Classifies an op (may depend on parameters) |
| `ve_copy(dst, cap, src)` | `dst: char*, cap: unsigned long, src: const char*` | `void` | Safe bounded string copy (always NUL-terminates) |
| `ve_op_name(k)` | `k: VeOpKind` | `const char *` | Human-readable op name |
| `ve_parse(text, out, err, errlen)` | `text: const char*, out: VeScript*, err: char*, errlen: int` | `bool` | Parses DSL text into a `VeScript` |
| `ve_script_free(s)` | `s: VeScript*` | `void` | Frees a parsed script |
| `ve_plan(s, out, err, errlen)` | `s: const VeScript*, out: VePlan*, err: char*, errlen: int` | `bool` | Builds an execution plan |
| `ve_plan_print(p)` | `p: const VePlan*` | `void` | Prints a human-readable plan summary |
| `ve_execute(p, on_progress, user, err, errlen)` | `p: const VePlan*, on_progress: VeProgressCb, user: void*, err: char*, errlen: int` | `int` | Executes the plan (0 = success, `VE_ECANCELED` = cancelled, negative = error) |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [ve/](index.md) | [ve/](index.md) | [ve_dsl.c](ve_dsl.md) |