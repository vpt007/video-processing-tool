# ffwrap.c - FFmpeg/FFprobe Command Wrapper (Legacy)

> **File:** [`src/ffwrap.c`](../../src/ffwrap.c)
> **Package:** `app`
> **Purpose:** Thin wrapper around the ffmpeg / ffprobe command-line tools (subprocess-based)

## Overview

[`ffwrap.c`](../../src/ffwrap.c) builds ffmpeg filter/argument strings from a small "command" object model (the `Cmd*` structs) and shells out to ffprobe to read media info (duration, streams, chapters, thumbnail) as JSON. This is the **older subprocess-based path**; the `ve/` library ([`ve_exec.c`](ve/ve_exec.md)) is the in-process replacement.

## Types

### CommandKind
**File:** [`ffwrap.c`](../../src/ffwrap.c:26)

The set of edit operations: `ROTATE`, `FLIP_H`, `FLIP_V`, `MUTE`, `ADD_AUDIO_TRACK`, `SCALE_VOLUME`, `REMOVE_AUDIO_TRACK`, `STERO_TO_MONO`, `SCALE`, `CROP`, `MERGE`, `TRIM`, `REVERSE_AUDIO`, `REVERSE_VIDEO`, `ADD_THUMBNAIL`, `ADD_SUBTITLE`, `REMOVE_SUBTITLE`, `BRIGHTNESS`, `CONTARAST`, `HUE`, `ASCPECT_RATIO`.

### Command Structs
**File:** [`ffwrap.c`](../../src/ffwrap.c:66)

| Struct | Fields | Description |
|--------|--------|-------------|
| `Command` | `kind` | Base: identifies the operation |
| `CmdRotate` | `angle, clockwise` | Rotate by degrees |
| `CmdScaleVolume` | `volume` | Scale audio volume by a percentage |
| `CmdChangeAspectRatio` | `x, y` | Override display aspect ratio |
| `CmdAddAudioTrack` | `src` | Add an extra audio track |
| `CmdRemoveAudioTrack` | `idx` | Remove an audio track |
| `CmdAddSubtitle` | `src` | Add a subtitle file |
| `CmdMerge` | `paths` | Concatenate several inputs |
| `CmdCrop` | `x, y, w, h` | Crop |

### CommandBuilder
**File:** [`ffwrap.c`](../../src/ffwrap.c:157)

Accumulates the pieces of an ffmpeg command line: a video filter chain (`vf`), an audio filter chain (`af`), extra arguments (`args`), and an optional `filter_complex` graph.

### ff_info
**File:** [`ffwrap.c`](../../src/ffwrap.c:139)

Parsed media info from ffprobe's JSON output: `duration`, `width`, `height`, audio/subtitle tracks, chapters, and the thumbnail stream index.

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `visit_flip_h(b)` / `visit_flip_v(b)` | `b: CommandBuilder*` | `void` | Appends `hflip` / `vflip` filter |
| `visit_mute(b)` | `b: CommandBuilder*` | `void` | Appends `-an` |
| `visit_scale_volume(cmd, b)` | `cmd: CmdScaleVolume*, b: CommandBuilder*` | `void` | Appends a `volume=` audio filter |
| `visit_change_aspect_ratio(ratio, b)` | `ratio: CmdChangeAspectRatio*, b: CommandBuilder*` | `void` | Appends `setdar=x/y` |
| `visit_crop(crop, b)` | `crop: CmdCrop*, b: CommandBuilder*` | `void` | Appends `crop=w:h:x:y` |
| `visit_rotate(cmd, b)` | `cmd: CmdRotate*, b: CommandBuilder*` | `void` | Appends `transpose` (90/180/270) or `rotate=` filter |
| `ff_slurp__stream(f, outlen)` | `f: FILE*, outlen: size_t*` | `void *` | Reads a stream into a NUL-terminated heap buffer |

## Macros

| Macro | Description |
|-------|-------------|
| `add_filter(string, src)` | Appends a filter to a chain with a `,` separator |
| `add_args(string, src)` | Appends a heap-allocated copy of `src` to an `Args` vector |

## Implementation Notes

- Uses `subprocess.h` to launch ffmpeg/ffprobe with `subprocess_option_search_user_path | subprocess_option_no_window`.
- `visit_rotate` maps 90/180/270 to `transpose=1/2/3`; arbitrary angles use the `rotate=` filter.
- This is the legacy path; new exports go through the `ve/` library.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [ve_export.c](ve_export.md) | [src/](index.md) | [thumb_strip.c](thumb_strip.md) |