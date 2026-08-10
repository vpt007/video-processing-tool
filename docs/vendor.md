# Vendor Dependencies

> **Path:** [`src/vendor/`](../src/vendor/)
> **Purpose:** Third-party libraries vendored into the project

## Overview

The project vendors its third-party dependencies under [`src/vendor/`](../src/vendor/) to keep the build self-contained and the binary a single executable. These are third-party files and are not documented in detail here; this page lists them for reference.

## Vendored Libraries

| Library | Path | Purpose |
|---------|------|---------|
| **cimgui** | `src/vendor/cimgui/` | C bindings for Dear ImGui (includes the full ImGui source under `cimgui/imgui/`) |
| **miniaudio** | `src/vendor/miniaudio.h` | Single-header audio playback library (compiled inside `vp_engine.c`) |
| **nob.h** | `src/vendor/nob.h` | Single-header build tool used by [`nob.c`](../nob.c) |
| **stb_image** | `src/vendor/stb_image.h` | Single-header image loader (used by `main.c`) |
| **tinyfiledialogs** | `src/vendor/tinyfiledialogs.c` / `.h` | Native file dialogs (used by `main.c`) |
| **subprocess.h** | `src/vendor/subprocess.h` | Single-header subprocess launcher (used by `ffwrap.c`) |
| **cJSON** | `src/vendor/cJSON.c` / `.h` | JSON parser (used by `ffwrap.c` for ffprobe output) |

## Generated / Non-Source Files

| File | Purpose |
|------|---------|
| `src/gl.h` | glad-generated OpenGL loader (header-only) |
| `src/icon_moon.h` | Embedded icomoon font byte array (generated) |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [Build System](build.md) | [Home](index.md) | [Style Guide](DOCUMENTATION_STYLE_GUIDE.md) |