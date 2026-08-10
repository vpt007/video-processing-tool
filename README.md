# Video Processing Tool

A lightweight, single-executable **desktop video processor** written in **C**. It lets you perform common video editing operations — rotate, resize, crop, trim, adjust audio, add subtitles, set thumbnails, and transcode — through a clean, drag-and-drop graphical interface.

The app is built as a **unity build**: the entire UI (Dear ImGui via cimgui + GLFW/OpenGL) and the video engine (FFmpeg/libav, in-process) are compiled together into one binary.

## Features

- **Video transformation** — rotate (90°/180°/270°/custom), flip, resize, change aspect ratio
- **Crop & trim** — interactive crop overlay and a filmstrip trim timeline
- **Audio** — mute, volume, stereo→mono, add/remove audio tracks, waveform display
- **Subtitles** — add or remove subtitle tracks
- **Thumbnails** — set or remove a cover thumbnail
- **Transcoding** — convert between formats supported by FFmpeg
- **Live preview** — real-time playback with seek, scrub, and filter preview
- **Drag-and-drop** — drop a video file onto the window to load it

## Tech Stack

| Component | Technology |
|-----------|------------|
| Language | C (C11) + C++ (C++17 for the UI unity build) |
| UI | Dear ImGui via [cimgui](https://github.com/cimgui/cimgui) |
| Windowing / Rendering | GLFW + OpenGL (glad loader) |
| Video processing | FFmpeg / libav (in-process, no subprocess) |
| Audio playback | [miniaudio](https://miniaudio.com/) |
| Build tool | [nob.h](https://github.com/tsoding/nob.h) |

## Prerequisites

You need a C/C++ toolchain and the development libraries for FFmpeg, GLFW, and OpenGL.

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install -y build-essential pkg-config \
    libavformat-dev libavcodec-dev libavfilter-dev \
    libavutil-dev libswscale-dev libswresample-dev \
    libglfw3-dev libgl1-mesa-dev
```

### Windows / macOS

- **Windows:** Install a MinGW-w64 toolchain (or MSVC), plus FFmpeg dev libraries and GLFW. The build recipe currently targets a POSIX-style toolchain (`gcc`/`g++`).
- **macOS:** Install Xcode Command Line Tools, then FFmpeg and GLFW via [Homebrew](https://brew.sh/):
  ```bash
  brew install ffmpeg glfw
  ```

## Build

The project uses a [nob.h](https://github.com/tsoding/nob.h) build recipe ([`nob.c`](nob.c)).

```bash
# 1. Build the nob build tool and run it
cc -o nob nob.c && ./nob
```

The recipe:
1. Compiles [`src/cimgui_unity.cpp`](src/cimgui_unity.cpp) (ImGui + cimgui + backends) → `build/cimgui_unity.o`
2. Compiles [`src/main.c`](src/main.c) (the whole app unity build) → `build/main.o`
3. Links them into **`bin/vpt`**

### Run

```bash
./bin/vpt
```

## Usage

1. Launch the app.
2. **Drag and drop** a video file onto the window (or click the "Drag And Drop Here" button).
3. Use the toolbar to apply edits (rotate, flip, crop, trim, volume, subtitles, thumbnail).
4. Preview your changes live in the player.
5. Export the processed video (saved as `<name>_Processed<ext>`).

## Project Structure

```
├── nob.c                  # Build recipe (nob.h)
├── src/
│   ├── main.c             # Unity-build entry point: UI, player, edit state
│   ├── vp_engine.c/.h     # Headless video playback engine
│   ├── ve_export.c        # Background export (UI → ve/ library)
│   ├── ve/                # Video-edit library (DSL → plan → execute)
│   ├── os.c               # Cross-platform OS abstraction
│   ├── config.h           # Shared config types
│   └── vendor/            # Third-party libraries (cimgui, miniaudio, etc.)
├── asset/                 # Icons and fonts
└── docs/                  # Full documentation
```

## Documentation

See the [`docs/`](docs/index.md) folder for detailed documentation of every module, its APIs, and implementation:

- [Master index](docs/index.md)
- [Build system](docs/build.md)
- [Source code docs](docs/src/index.md)
- [Documentation style guide](docs/DOCUMENTATION_STYLE_GUIDE.md)

## License

This project is provided for educational and personal use. Third-party libraries retain their respective licenses (see [`src/vendor/`](src/vendor/)).