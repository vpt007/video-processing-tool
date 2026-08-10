# Video Processing Tool - Documentation

> **Path:** [`/`](../)
> **Purpose:** Master index for the video-processing-tool project documentation

## Overview

A lightweight, single-executable desktop video processing application written in **C** (with a C++ unity build for the UI stack). It provides video transformation (rotate, scale, crop, trim), audio processing, subtitles, thumbnails, transcoding, and repair — all driven by FFmpeg/libav in-process.

The application is built as a **unity build**: [`src/main.c`](../src/main.c) `#include`s most other `.c` files into a single translation unit, and [`src/cimgui_unity.cpp`](../src/cimgui_unity.cpp) compiles the entire Dear ImGui + cimgui stack together.

## Documentation Tree

| Section | Purpose |
|---------|---------|
| [`src/`](src/index.md) | Application source code (UI, playback engine, edit library) |
| [`Build System`](build.md) | The nob.h build recipe ([`nob.c`](../nob.c)) |
| [`Vendor Dependencies`](vendor.md) | Third-party libraries vendored under `src/vendor/` |
| [`Style Guide`](DOCUMENTATION_STYLE_GUIDE.md) | The documentation format standard for this project |

## Source Sections

### [`src/`](src/index.md) - Application Source

| File | Purpose |
|------|---------|
| [`main.c`](src/main.md) | Unity-build entry point: UI, video player widget, edit state, main loop |
| [`config.h`](src/config.md) | Shared editor configuration types and globals |
| [`os.c`](src/os.md) | Cross-platform OS abstraction layer |
| [`vector.h`](src/vector.md) | Header-only dynamic array (vector) macros |
| [`logger.h`](src/logger.md) | Minimal file logger |
| [`icon_font.h`](src/icon_font.md) | Icon glyph strings for the icomoon font |
| [`util.c`](src/util.md) | Small shared helpers |
| [`vp_engine.c`](src/vp_engine.md) | Headless video playback engine (decode/sync/render) |
| [`ve_export.c`](src/ve_export.md) | Background video export bridge (UI → ve/ library) |
| [`ffwrap.c`](src/ffwrap.md) | Legacy subprocess-based ffmpeg/ffprobe wrapper |
| [`thumb_strip.c`](src/thumb_strip.md) | Background filmstrip thumbnail extractor |
| [`audio_wave.c`](src/audio_wave.md) | Background audio waveform (peak) extractor |
| [`vp_thumb.c`](src/vp_thumb.md) | Thumbnail capture helpers |
| [`test.c`](src/test.md) | Lightweight self-test harness |
| [`cimgui_unity.cpp`](src/cimgui_unity.md) | C++ unity build of ImGui + cimgui + backends |
| [`ve/`](src/ve/index.md) | The video-edit operation library (DSL → plan → execute) |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [Style Guide](DOCUMENTATION_STYLE_GUIDE.md) | [Home](index.md) | [src/](src/index.md) |