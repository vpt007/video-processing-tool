# main.c - Unity-Build Entry Point & UI

> **File:** [`src/main.c`](../../src/main.c)
> **Package:** `app`
> **Purpose:** The application's entry point: UI, video player widget, edit state, and main render loop

## Overview

[`main.c`](../../src/main.c) is the **unity-build entry point** (2225 lines). It includes most other `.c` files directly (via `#include`), builds the entire Dear ImGui UI, hosts the video player widget, tracks the edit state, and runs the main render loop. It is compiled as C11 and linked against the C++ unity build ([`cimgui_unity.cpp`](cimgui_unity.md)).

## Included Modules

| Include | Purpose |
|---------|---------|
| `cimgui.h` / `cimgui_impl.h` | Dear ImGui C bindings + GLFW/OpenGL3 backends |
| `gl.h` | glad-generated OpenGL loader (`GLAD_GL_IMPLEMENTATION`) |
| `vendor/stb_image.h` | Image loading (`STB_IMAGE_IMPLEMENTATION`) |
| `vendor/tinyfiledialogs.c` | Native file dialogs |
| `logger.h` | File logger |
| `vp_engine.c` | Headless playback engine |
| `config.h` | Shared config types |
| `icon_moon.h` / `icon_font.h` | Icon font data + glyph strings |
| `os.c` | OS abstraction |
| `vp_thumb.c` / `thumb_strip.c` | Thumbnail helpers + filmstrip extractor |
| `test.c` | Self-test harness |
| `ve_export.c` | Background export |
| `audio_wave.c` | Waveform extractor |

## Key Types

### VPWidget
**File:** [`main.c`](../../src/main.c:54)

| Field | Type | Description |
|-------|------|-------------|
| `eng` | `VPEngine *` | The playback engine |
| `ui_vf` / `ui_af` | `char[JH_BUFFER_MAX]` | UI video/audio filter strings |
| `ui_seek_active` | `int` | Whether the seek slider is being dragged |
| `ui_seek_val` | `float` | Current seek slider value |
| `ui_seek_last_sent` | `float` | Last seek value sent to the engine |
| `ui_last_seek_t` | `double` | Timestamp of the last seek |

## Global Variables

| Variable | Type | Description |
|----------|------|-------------|
| `vp` | `VPWidget *` | The active video player widget |
| `window` | `GLFWwindow *` | The main window |
| `icon_font` | `ImFont *` | The icon font |
| `video_config` | `VideoConfig` | Current edit settings |
| `current_video_path` | `char[OS_PATHMAX]` | Path of the loaded video |
| `src_width` / `src_height` | `int` | Native video resolution |
| `src_duration` | `double` | Video duration in seconds |
| `has_audio_src` | `int` | Whether the source has audio |
| `audio_tracks` / `audio_track_count` | `VPStreamInfo[32]` / `int` | Detected audio tracks |
| `audio_removed` | `bool[32]` | Per-track removal flags |
| `crop_enabled` | `bool` | Whether crop is active |
| `crop_l/t/r/b` | `float` | Normalized crop region (0..1) |
| `should_trim` | `bool` | Whether trim is active |
| `scale_enabled` / `scale_w` / `scale_h` | `int` | Scale settings |
| `add_thumbnail_path` / `add_subtitle_path` | `char[OS_PATHMAX]` | Pending thumbnail/subtitle paths |
| `error_message` / `error_title` | `const char *` | Error dialog state |
| `logger` | `Logger` | The file logger |

## Video Player Widget

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_create(path, scale)` | `path: const char*, scale: float` | `VPWidget *` | Creates a player widget + engine |
| `vp_destroy(vp)` | `vp: VPWidget*` | `void` | Destroys the widget + engine |
| `vp_set_scale(vp, scale)` | `vp: VPWidget*, scale: float` | `void` | Sets decode scale |
| `vp_set_vf(vp, vf)` | `vp: VPWidget*, vf: const char*` | `void` | Sets the video filter |
| `vp_set_af(vp, af)` | `vp: VPWidget*, af: const char*` | `void` | Sets the audio filter |
| `vp_set_video_track(vp, idx)` | `vp: VPWidget*, idx: int` | `void` | Selects a video track |
| `vp_set_audio_track(vp, idx)` | `vp: VPWidget*, idx: int` | `void` | Selects an audio track |
| `vp_get_video_tracks(vp, out, max)` | `vp: VPWidget*, out: VPStreamInfo*, max: int` | `int` | Lists video tracks |
| `vp_get_audio_tracks(vp, out, max)` | `vp: VPWidget*, out: VPStreamInfo*, max: int` | `int` | Lists audio tracks |
| `vp_get_texture(vp, w, h)` | `vp: VPWidget*, w: int*, h: int*` | `unsigned int` | Returns the GL texture |
| `vp_render(ctx, w, h)` | `ctx: VPWidget*, w: float, h: float` | `void` | Renders the preview + transport controls |

## UI Widgets

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `jh_chk_button(label, status, size)` | `label: const char*, status: bool*, size: ImVec2` | `bool` | Toggle button bound to a bool |
| `crop_widget(img_min, img_size, angle, box_min, box_max, active)` | `ImVec2, ImVec2, float, ImVec2, ImVec2, bool` | `void` | Interactive crop overlay with draggable handles |
| `TimelineTrimWidget(label, trim_start, trim_end, duration, size, out_dragging, out_scrub, show_trim)` | `const char*, float*, float*, float, ImVec2, int*, float*, bool` | `void` | Trim timeline with filmstrip + draggable handles |
| `AudioTracksTimeline(width, trim_start, trim_end, dur)` | `float, float, float, float` | `void` | Audio-track timeline with waveforms |
| `error_dialog_render()` | — | `void` | Renders the error dialog modal |

## Edit State & Event Handling

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `reset_edit_state()` | — | `void` | Clears all edit state on new file load |
| `refresh_video_info()` | — | `void` | Refreshes media info and kicks off waveform/filmstrip workers |
| `drop_callback(window, count, paths)` | `GLFWwindow*, int, const char**` | `void` | Handles drag-and-drop of a file |
| `handle_shortcut()` | — | `void` | Handles keyboard shortcuts (P/Space play, M mute) |
| `handle_add_thumbnail(ud)` | `void*` | `void` | Opens a dialog to pick a cover thumbnail |
| `handle_add_subtitle(ud)` | `void*` | `void` | Opens a dialog to pick a subtitle file |
| `handle_remove_subtitle(ud)` | `void*` | `void` | Flags the remove-subtitle popup |
| `load_texture_from_mem(data, size, out_tex, w, h)` | `const void*, size_t, GLuint*, int*, int*` | `bool` | Loads an image from memory into a GL texture |
| `load_texture_from_file(path, out_tex, w, h)` | `const char*, GLuint*, int*, int*` | `bool` | Loads an image file into a GL texture |

## main()

[`main()`](../../src/main.c:1957) sets up GLFW + OpenGL + ImGui, loads fonts, initializes the logger, and runs the main render loop (poll events, build the UI, draw, swap buffers). It creates a 1600×900 window (16:9), enables vsync, and initializes the cimgui GLFW/OpenGL3 backends.

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [src/](index.md) | [src/](index.md) | [config.h](config.md) |