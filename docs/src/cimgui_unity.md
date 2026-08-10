# cimgui_unity.cpp - ImGui + cimgui Unity Build

> **File:** [`src/cimgui_unity.cpp`](../../src/cimgui_unity.cpp)
> **Package:** `app`
> **Purpose:** C++ unity build of the Dear ImGui + cimgui + backends stack

## Overview

[`cimgui_unity.cpp`](../../src/cimgui_unity.cpp) compiles the entire ImGui + cimgui + backend code together as a single translation unit (a "unity build"). This avoids a separate build step per `.cpp` file and lets the compiler inline across translation units. It is compiled as C++17 and linked against the C unity build ([`main.c`](main.md)).

## Configuration Macros

| Macro | Purpose |
|-------|---------|
| `IMGUI_IMPL_OPENGL_LOADER_GL3W` | Request the GL3W-style OpenGL loader (falls back to the bundled IMGL3W loader) |
| `IMGUI_USER_CONFIG` | Points at `../cimconfig.h` (custom imconfig overrides) |
| `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` | Drop deprecated ImGui APIs |
| `IMGUI_IMPL_API extern "C"` | Export backend symbols with C linkage so the C app can use them |
| `IMGUI_DEFINE_MATH_OPERATORS` | Enable `ImVec2`/`ImVec4` operator overloads |

## Included Sources

| Source | Purpose |
|--------|---------|
| `imgui.cpp` | Core ImGui implementation |
| `imgui_draw.cpp` | Drawing primitives |
| `imgui_demo.cpp` | Demo window |
| `imgui_widgets.cpp` | Widgets |
| `imgui_tables.cpp` | Tables |
| `backends/imgui_impl_opengl3.cpp` | OpenGL3 renderer backend |
| `backends/imgui_impl_glfw.cpp` | GLFW platform backend |
| `cimgui.cpp` | The C API wrapper |

## Implementation Notes

- Requires include paths: `src/vendor/cimgui`, `src/vendor/cimgui/imgui`, `src/vendor/cimgui/imgui/backends`.
- The `IMGUI_USER_CONFIG "../cimconfig.h"` resolves to `src/vendor/cimgui/cimconfig.h`.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [test.c](test.md) | [src/](index.md) | [ve/](ve/index.md) |