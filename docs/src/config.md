# config.h - Shared Editor Configuration

> **File:** [`src/config.h`](../../src/config.h)
> **Package:** `app`
> **Purpose:** Shared configuration types, constants, and app-wide globals

## Overview

[`config.h`](../../src/config.h) holds the common `VideoConfig` struct, the buffer-size constant, app window geometry, and the app-wide error-message globals. It is included by [`main.c`](main.md) (the unity-build entry point) and by [`ve_export.c`](ve_export.md) so the export layer can be analyzed/compiled independently.

## Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `JH_BUFFER_MAX` | `int` | `1 << 10` | Max size of filter/string buffers |
| `WIDTH` | `int` | `100` | Base window width unit (×16 = 1600 px) |
| `HEIGHT` | `int` | `100` | Base window height unit (×9 = 900 px) |
| `APP_TITLE` | `char[]` | `"Video Processing Tool"` | Window title |

## Structs

### VideoConfig
**File:** [`config.h`](../../src/config.h:16)

The full set of video/audio edit settings the user can change.

| Field | Type | Description |
|-------|------|-------------|
| `aspect_ratio` | `ImVec2` | Display aspect ratio (x:y) |
| `scale` | `ImVec2` | Output scale (w:h) |
| `rotate` | `int` | Rotation angle in degrees |
| `scale_volume` | `int` | Volume offset as a +/- percent from unity |
| `flip_h` | `bool` | Flip horizontally |
| `flip_v` | `bool` | Flip vertically |
| `mute` | `bool` | Strip all audio |
| `sterio_to_mono` | `bool` | Downmix stereo to mono |
| `vf` | `char[JH_BUFFER_MAX]` | Raw video filter chain string |
| `af` | `char[JH_BUFFER_MAX]` | Raw audio filter chain string |

## Global Variables

| Variable | Type | Description |
|----------|------|-------------|
| `error_message` | `const char *` | App-wide error dialog message (defined in main.c) |
| `error_title` | `const char *` | App-wide error dialog title (defined in main.c) |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [main.c](main.md) | [src/](index.md) | [os.c](os.md) |