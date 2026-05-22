# Video Processing Application Requirements Specification

## 1. Project Overview

A lightweight, user-friendly desktop video processing application distributed as a single executable file.  
The application should support common video editing, transformation, audio, subtitle, thumbnail, transcoding, and repair operations with minimal system resource usage.

---

# 2. Non-Functional Requirements

## 2.1 User Friendly
- Clean and simple UI.
- Drag-and-drop video support.
- Real-time validation and user feedback.
- Preview before processing.
- Progress bar with ETA.
- Clear success/error messages.
- Minimal learning curve.
- Keyboard shortcuts for common actions.
- Dark/Light mode support.

---

## 2.2 Single Executable
- Entire application distributed as:
  - `.exe` for Windows
  - optional AppImage for Linux
- No external dependencies required from user.
- FFmpeg bundled internally.
- Portable mode support.

---

## 2.3 Lightweight
- Low RAM consumption.
- Fast startup time.
- Minimal background processes.
- Efficient temporary file management.
- GPU acceleration support when available.
- CPU fallback support.

---

# 3. Functional Requirements

# 3.1 Video Transformation

## Rotation
User can:
- Rotate video by:
  - 90°
  - 180°
  - 270°
  - Custom angle

### Requirements
- Preview before export.
- Preserve aspect ratio when rotating.
- Auto-fill empty areas for custom rotation:
  - black background
  - blur background
  - crop mode

---

# 3.2 Scaling & Aspect Ratio

## Resize Video

### Supported Modes
- Resize while maintaining aspect ratio.
- Resize without maintaining aspect ratio.

### User Feedback
- Show original resolution.
- Show estimated output resolution.
- Warn on excessive upscaling.
- Preview changes.

---

## Change Aspect Ratio

### Supported Ratios
- 16:9
- 4:3
- 1:1
- 9:16
- Custom ratio

### Processing Modes
- Stretch
- Crop
- Add padding

### User Feedback
- Live preview.
- Safe-area guide overlay.

---

# 3.3 Audio Processing

## Audio Operations
- Remove/strip audio.
- Adjust audio volume.
- Convert stereo to mono.
- Add external audio track.
- Remove selected audio track.

---

## Audio Controls
### Volume
- Slider:
  - 0% to 300%
- Custom preset.
- Real-time preview.

### Add Audio
Supported formats:
- All audio file supported by ffmpeg.

### Track Management
- Detect multiple audio tracks.
- Allow user selection.

---

# 3.4 Subtitle Processing

## Supported Operations
- Add subtitles.
- Remove subtitles.

## Supported Subtitle Formats
- All subtitle file supported by ffmpeg.

## Subtitle Modes
- Soft subtitles.
- Burned-in subtitles.

## Additional Features(in future)
- Subtitle timing adjustment.

---

# 3.5 Thumbnail Processing

## Supported Operations
- Remove thumbnail.
- Replace thumbnail.
- Select frame from video as thumbnail.

## Features
- Frame picker timeline.
- Thumbnail preview.
- Auto-generate thumbnail suggestions.

---

# 3.6 Trim Video

## Supported Modes
- Start/End trimming.
- Multi-segment trimming.

## Features
- Timeline-based selection.
- Frame-accurate trimming.
- Preview trimmed section.

---

# 3.7 Crop Video

## Crop Features
- Freeform crop.
- Fixed ratio crop:
  - 16:9
  - 1:1
  - 9:16
  - 4:3
  custom


# 3.8 Output File Naming

## Naming Convention
Output files must use:

```text
[video_file]_Processed.ext
```

Example:

```text
input_Processed.mp4
```

## Additional Rules
- Avoid overwriting existing files.
- Allow custom output directory.
- Preserve original extension if possible.

---

# 3.9 Video Transcoding

## Supported Formats

### Input
- All video formatsupported by ffmpeg.
### Output
- All video formatsupported by ffmpeg.

---

## Codec Support

### Video
- codec supported by ffmpeg
### Audio
- codec supported by ffmpeg

---

## Quality Controls
- Bitrate control.
- CRF quality mode.
- Presets:
  - Fast
  - Medium
  - High Quality

---

# 3.10 Video Repair

## Repair Features
Repair corrupted video files using:
- Sample correct video from same camera/device.

## Supported Repair Cases
- Broken metadata.
- Missing moov atom.
- Damaged indexes.
- Incomplete recordings.

## User Flow
1. User selects corrupted file.
2. User selects healthy sample file.
3. Application analyzes structure.
4. Application attempts repair.

---


# 4 Error Handling
## Must Handle
- Unsupported codecs.
- Corrupted input files.
- Missing audio streams.
- Invalid subtitles.
- Disk full errors.
- Interrupted processing.

## User Feedback
- Human-readable error messages.
- Suggested fixes.
- Retry option.

---

# 7. Logging

- Processing logs.
- Error logs.
- Export logs.
- Optional debug mode.

---
# 8. Recommended Technical Stack

## Backend
- FFmpeg
- FFprobe
