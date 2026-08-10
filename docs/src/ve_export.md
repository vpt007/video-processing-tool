# ve_export.c - Background Video Export

> **File:** [`src/ve_export.c`](../../src/ve_export.c)
> **Package:** `app`
> **Purpose:** Bridges the UI to the ve/ library and runs exports on a worker thread

## Overview

[`ve_export.c`](../../src/ve_export.c) captures the current editor state into an `ExportRequest`, turns it into a ve/ DSL script, and runs it through the parse → plan → execute pipeline on a dedicated worker thread so the UI never blocks. Progress and cancellation are shared with the UI through atomics.

## Types

### ExportRequest
**File:** [`ve_export.c`](../../src/ve_export.c:29)

A snapshot of the editor's export settings, captured when the user hits "export" so later UI changes don't affect the running job.

| Field | Type | Description |
|-------|------|-------------|
| `input` / `output` | `char[VE_PATH_MAX]` | Input/output paths |
| `cfg` | `VideoConfig` | Edit settings |
| `trim_start` / `trim_end` / `duration` | `float` | Trim window |
| `has_trim` | `int` | Whether trim is active |
| `crop_enabled` / `crop_x/y/w/h` | `int` | Crop settings |
| `scale_enabled` / `scale_w/h` / `scale_keep_aspect` | `int` | Scale settings |
| `thumbnail` / `subtitle` / `subtitle_lang` | `char[]` | Thumbnail/subtitle paths |
| `remove_sub_enabled` / `remove_sub_index` | `int` | Subtitle removal |
| `drop_audio_index` / `n_drop_audio` | `int[16]` / `int` | Audio tracks to drop |

### ExportJob
**File:** [`ve_export.c`](../../src/ve_export.c:50)

| Field | Type | Description |
|-------|------|-------------|
| `thread` | `pthread_t` | The worker thread |
| `running` | `int` | Whether a job is active |
| `cancel` | `atomic_int` | Set by the UI to request cancellation |
| `done` | `atomic_int` | Set by the worker when it finishes |
| `fraction` | `_Atomic double` | 0..1 progress |
| `rc` | `int` | Result code |
| `err` | `char[256]` | Error message |
| `output` | `char[VE_PATH_MAX]` | Output path |
| `req` | `ExportRequest` | The export request |

## Functions

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `export_start(input_path, output_dir, src)` | `input_path: const char*, output_dir: const char*, src: const ExportRequest*` | `void` | Kicks off a new export job |
| `export_active()` | — | `int` | True while an export is in progress |
| `export_fraction()` | — | `double` | Current progress in 0..1 |
| `export_request_cancel()` | — | `void` | Asks the running export to stop |
| `export_poll()` | — | `void` | Call once per frame; joins the worker when done and reports the result |

## Internal Helpers

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `export_build_script(r, out, cap)` | `r: const ExportRequest*, out: char*, cap: int` | `void` | Translates editor settings into a ve/ DSL script |
| `export_worker(arg)` | `arg: void*` | `void *` | Worker thread entry: build script, parse, plan, execute |
| `export_output_path(r, dst, cap)` | `r: const ExportRequest*, dst: char*, cap: int` | `void` | Derives `<name>_Processed<ext>` next to the input |
| `export_progress(user, frac, cur, total)` | `void*, double, double, double` | `int` | Progress callback; returns non-zero to cancel |

## Implementation Notes

- Only one export runs at a time (a single global `g_export` job).
- The output path is `<stem>_Processed<ext>` in the chosen directory (or next to the input).
- `export_poll` reports success/cancel/failure through the app's error dialog (`error_title` / `error_message`).
- The worker uses atomics for `cancel`, `done`, and `fraction`; the UI thread reads them from `export_poll`.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [vp_engine.c](vp_engine.md) | [src/](index.md) | [ffwrap.c](ffwrap.md) |