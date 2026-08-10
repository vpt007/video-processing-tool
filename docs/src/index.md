# src/ - Application Source

> **Path:** [`src/`](../../src/)
> **Purpose:** The application's source code: UI, playback engine, and the video-edit library

## Overview

The `src/` directory contains the entire application. It is compiled as a **unity build**: [`main.c`](main.md) includes most other `.c` files directly, and [`cimgui_unity.cpp`](cimgui_unity.md) compiles the Dear ImGui + cimgui stack. The `ve/` subdirectory is a self-contained video-edit library with no UI dependency.

## Files

| File | Purpose |
|------|---------|
| [`main.c`](main.md) | Unity-build entry point: UI, video player widget, edit state, main loop |
| [`config.h`](config.md) | Shared editor configuration types and globals |
| [`os.c`](os.md) | Cross-platform OS abstraction layer |
| [`vector.h`](vector.md) | Header-only dynamic array (vector) macros |
| [`logger.h`](logger.md) | Minimal file logger |
| [`icon_font.h`](icon_font.md) | Icon glyph strings for the icomoon font |
| [`util.c`](util.md) | Small shared helpers |
| [`vp_engine.c`](vp_engine.md) | Headless video playback engine (decode/sync/render) |
| [`ve_export.c`](ve_export.md) | Background video export bridge (UI → ve/ library) |
| [`ffwrap.c`](ffwrap.md) | Legacy subprocess-based ffmpeg/ffprobe wrapper |
| [`thumb_strip.c`](thumb_strip.md) | Background filmstrip thumbnail extractor |
| [`audio_wave.c`](audio_wave.md) | Background audio waveform (peak) extractor |
| [`vp_thumb.c`](vp_thumb.md) | Thumbnail capture helpers |
| [`test.c`](test.md) | Lightweight self-test harness |
| [`cimgui_unity.cpp`](cimgui_unity.md) | C++ unity build of ImGui + cimgui + backends |
| [`ve/`](ve/index.md) | The video-edit operation library (DSL → plan → execute) |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [Home](../index.md) | [Home](../index.md) | [main.c](main.md) |