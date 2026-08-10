# vp_engine.c - Headless Video Playback Engine

> **File:** [`src/vp_engine.c`](../../src/vp_engine.c) / [`src/vp_engine.h`](../../src/vp_engine.h)
> **Package:** `app`
> **Purpose:** ffplay-quality decode/playback core with no UI dependency

## Overview

[`vp_engine.c`](../../src/vp_engine.c) is a headless video playback engine (mirroring ffplay's architecture). It owns the demuxer, decoder threads, A/V sync (audio-master clock), filtering, and the GL texture. It has **no UI dependency** — a host editor drives it from its own timeline, transport buttons, and scrubber.

### Threading Contract

- Create the engine, call `vp_engine_update()`, read the texture, and call **all** control/query functions from the render (GL/main) thread.
- The engine spins up its own demux/decode/audio threads internally; the host never touches them.

### Architecture

| Thread | Role |
|--------|------|
| `read_thread` | Demux only; routes packets into per-stream `PacketQueue`s; owns seek + track-change + EOF draining |
| `video_thread` | Pulls video packets → decode → video filtergraph → filtered RGB24 frames into `VFrameQueue` |
| `audio_thread` | Pulls audio packets → decode → audio filtergraph → interleaved float PCM into the ring buffer; publishes the audio clock |
| `ma_data_cb` | Drains the ring at the hardware rate (muted while seeking/paused) |
| `vp_render` | (main thread) Computes the master clock (= audio clock), pops due video frames, uploads to the GL texture |

## Types

### VPStreamInfo
**File:** [`vp_engine.h`](../../src/vp_engine.h:37)

| Field | Type | Description |
|-------|------|-------------|
| `index` | `int` | Stream index to pass to `vp_engine_set_*_track` |
| `lang` | `char[64]` | Language tag, `"und"` if none |
| `title` | `char[128]` | Title metadata, `""` if none |

### VPMediaInfo
**File:** [`vp_engine.h`](../../src/vp_engine.h:43)

| Field | Type | Description |
|-------|------|-------------|
| `src_width` / `src_height` | `int` | Native video resolution |
| `width` / `height` | `int` | Current decoded/scaled output size |
| `fps` | `double` | Frames per second (>0) |
| `duration` | `double` | Seconds (<=0 if unknown) |
| `frame_count` | `int64_t` | Estimated total frames (<=0 unknown) |
| `has_video` / `has_audio` | `bool` | Stream presence |
| `sample_rate` / `channels` | `int` | Audio device rate / channels |

### VPFrameRGB
**File:** [`vp_engine.h`](../../src/vp_engine.h:57)

| Field | Type | Description |
|-------|------|-------------|
| `width` / `height` | `int` | Frame dimensions |
| `stride` | `int` | Bytes per row (== width*3) |
| `pixels` | `uint8_t *` | RGB24, top-down |

### VPEvent
**File:** [`vp_engine.h`](../../src/vp_engine.h:65)

| Value | Description |
|-------|-------------|
| `VP_EVENT_STATE_CHANGED` | Play <-> pause toggled |
| `VP_EVENT_SEEKED` | A seek finished; new frame on screen |
| `VP_EVENT_ENDED` | Playback reached the end of the media |

## Lifecycle

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_create(path, scale)` | `path: const char*, scale: float` | `VPEngine *` | Opens `path` and starts the engine (NULL on failure) |
| `vp_engine_destroy(e)` | `e: VPEngine*` | `void` | Destroys the engine |
| `vp_engine_update(e)` | `e: VPEngine*` | `void` | Call once per frame on the GL thread before sampling the texture |
| `vp_engine_texture(e, w, h)` | `e: VPEngine*, w: int*, h: int*` | `unsigned int` | The GL texture of the latest frame (0 until ready) |

## Transport

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_play(e)` | `e: VPEngine*` | `void` | Start playback |
| `vp_engine_pause(e)` | `e: VPEngine*` | `void` | Pause playback |
| `vp_engine_toggle(e)` | `e: VPEngine*` | `void` | Toggle play/pause |
| `vp_engine_is_playing(e)` | `e: VPEngine*` | `bool` | Whether playing |

## Seeking & Stepping

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_seek(e, seconds, precise)` | `e: VPEngine*, seconds: double, precise: bool` | `void` | Seek to `seconds`; frame-accurate when paused + precise |
| `vp_engine_seek_frame(e, frame)` | `e: VPEngine*, frame: int64_t` | `void` | Seek to an exact frame index (pauses first) |
| `vp_engine_step(e, delta)` | `e: VPEngine*, delta: int` | `void` | Step `delta` frames (pauses, lands frame-accurately) |
| `vp_engine_scrub_begin(e)` | `e: VPEngine*` | `void` | Begin an interactive scrub (mutes audio, free-runs display) |
| `vp_engine_scrub_end(e)` | `e: VPEngine*` | `void` | End an interactive scrub |

## Rate & Volume

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_set_rate(e, rate)` | `e: VPEngine*, rate: double` | `void` | Playback speed 0.25..4.0 (atempo tempo-shift) |
| `vp_engine_get_rate(e)` | `e: VPEngine*` | `double` | Current playback speed |
| `vp_engine_set_volume(e, vol01)` | `e: VPEngine*, vol01: float` | `void` | Set volume 0..1 |
| `vp_engine_get_volume(e)` | `e: VPEngine*` | `float` | Current volume |
| `vp_engine_set_muted(e, muted)` | `e: VPEngine*, muted: bool` | `void` | Set mute |
| `vp_engine_is_muted(e)` | `e: VPEngine*` | `bool` | Whether muted |

## Loop / In-Out Region

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_set_loop(e, in_sec, out_sec, enabled)` | `e: VPEngine*, in_sec: double, out_sec: double, enabled: bool` | `void` | Loop the playhead between in/out |
| `vp_engine_clear_loop(e)` | `e: VPEngine*` | `void` | Clear the loop region |

## Filters & Tracks

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_set_vf(e, vf)` | `e: VPEngine*, vf: const char*` | `void` | Set video filter (NULL/"" = passthrough) |
| `vp_engine_set_af(e, af)` | `e: VPEngine*, af: const char*` | `void` | Set audio filter |
| `vp_engine_set_scale(e, scale)` | `e: VPEngine*, scale: float` | `void` | Set decode scale |
| `vp_engine_refresh(e)` | `e: VPEngine*` | `void` | Re-render the current frame through the filter graph |
| `vp_engine_set_video_track(e, idx)` | `e: VPEngine*, idx: int` | `void` | Select a video stream |
| `vp_engine_set_audio_track(e, idx)` | `e: VPEngine*, idx: int` | `void` | Select an audio stream |
| `vp_engine_video_tracks(e, out, max)` | `e: VPEngine*, out: VPStreamInfo*, max: int` | `int` | List video tracks |
| `vp_engine_audio_tracks(e, out, max)` | `e: VPEngine*, out: VPStreamInfo*, max: int` | `int` | List audio tracks |

## Queries

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_position(e)` | `e: VPEngine*` | `double` | Current time in seconds |
| `vp_engine_duration(e)` | `e: VPEngine*` | `double` | Total time (<=0 = unknown) |
| `vp_engine_fps(e)` | `e: VPEngine*` | `double` | Frames per second |
| `vp_engine_frame_index(e)` | `e: VPEngine*` | `int64_t` | Current frame number |
| `vp_engine_frame_count(e)` | `e: VPEngine*` | `int64_t` | Total frames (<=0 unknown) |
| `vp_engine_ended(e)` | `e: VPEngine*` | `bool` | Whether playback ended |
| `vp_engine_get_info(e, out)` | `e: VPEngine*, out: VPMediaInfo*` | `void` | Fills media info |

## Snapshot & Events

| Function | Input | Output | Description |
|----------|-------|--------|-------------|
| `vp_engine_snapshot(e, out)` | `e: VPEngine*, out: VPFrameRGB*` | `bool` | Copies the current frame (malloc'd; free with `vp_engine_frame_free`) |
| `vp_engine_frame_free(f)` | `f: VPFrameRGB*` | `void` | Frees a snapshot |
| `vp_engine_set_callback(e, cb, user)` | `e: VPEngine*, cb: VPEventCb, user: void*` | `void` | Registers an event callback |

## Implementation Notes

- **Sync:** video is presented against the audio clock (derived from how much PCM the device consumed), so decode never blocks the demuxer and audio is never starved.
- **EOF:** shutdown aborts every queue + ring and signals every cond before joining; EOF flushes decoders so trailing frames play out.
- **Seeking:** uses ffplay's serial mechanism — flushing a `PacketQueue` bumps its serial; frames/PCM carry the serial they were produced under; stale-serial data is discarded.
- **miniaudio** is compiled inside this file (`MA_IMPLEMENTATION`).

## Navigation

| ← Previous | Up | Next → |
|-----------|-----|--------|
| [util.c](util.md) | [src/](index.md) | [ve_export.c](ve_export.md) |