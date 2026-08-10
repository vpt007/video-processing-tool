# Build System - nob.h Build Recipe

> **File:** [`nob.c`](../nob.c)
> **Purpose:** Build recipe for the video processing tool (uses nob.h)

## Overview

[`nob.c`](../nob.c) is the build recipe for the project, written against the single-header [`nob.h`](../src/vendor/nob.h) build tool. It builds the app as a **unity build**:

1. [`src/cimgui_unity.cpp`](src/cimgui_unity.md) → `build/cimgui_unity.o` (C++17: ImGui + cimgui + backends)
2. [`src/main.c`](src/main.md) → `build/main.o` (C11: the whole app unity build)
3. Link → `bin/vpt`

## Building

```bash
cc -o nob nob.c && ./nob
```

The recipe rebuilds itself automatically (`NOB_GO_REBUILD_URSELF`), creates the `build/` and `bin/` directories, and only recompiles files that are out of date.

## Build Steps

| Step | Command | Output |
|------|---------|--------|
| Compile C++ unity | `g++ -std=c++17 -c src/cimgui_unity.cpp` | `build/cimgui_unity.o` |
| Compile C unity | `gcc -std=c11 -c src/main.c` | `build/main.o` |
| Link | `g++ ... -o bin/vpt` | `bin/vpt` |

## Include Paths

### C++ build (`cimgui_unity.cpp`)
- `src/vendor/cimgui`
- `src/vendor/cimgui/imgui`
- `src/vendor/cimgui/imgui/backends`

### C build (`main.c`)
- `src`, `src/vendor`
- `src/vendor/cimgui`, `src/vendor/cimgui/imgui`, `src/vendor/cimgui/imgui/backends`
- `.` (repo root, so `app_icon.h` resolves)

## Feature-Test Macros

The C build passes `-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE=1`. These must be set on the command line because [`os.c`](src/os.md) defines them itself, but in the unity build `main.c` includes many headers before `os.c`, so the macros must already be active.

## Link Libraries

| Library | Purpose |
|---------|---------|
| `-lavformat -lavcodec -lavfilter -lavutil -lswscale -lswresample` | FFmpeg / libav |
| `-lglfw` | GLFW windowing |
| `-lGL` | OpenGL |
| `-lpthread` | Threading (engine, export, filmstrip, waveform) |
| `-ldl` | Dynamic loading (GLFW/glad) |
| `-lm` | Math |

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [src/](src/index.md) | [Home](index.md) | [Vendor Dependencies](vendor.md) |