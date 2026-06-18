/* vp_engine.h — headless video playback engine for editor previews
 * ─────────────────────────────────────────────────────────────────────────
 * An ffplay-quality decode/playback core with NO UI dependency. It owns the
 * demuxer, decoder threads, A/V sync (audio-master clock), filtering, and the
 * GL texture. You drive it from your own editor: your timeline, your transport
 * buttons, your scrubber. Everything is controllable and queryable from code.
 *
 * Threading contract — this is the only rule you must follow:
 *   • Create the engine, call vp_engine_update(), read the texture, and call
 *     ALL control/query functions from your render (GL/main) thread.
 *   • The engine spins up its own demux/decode/audio threads internally; you
 *     never touch them.
 *
 * Typical per-frame use in your editor's render loop:
 *
 *     vp_engine_update(eng);                 // advances video, fires callbacks
 *     int w, h;
 *     unsigned int tex = vp_engine_texture(eng, &w, &h);
 *     // ... draw `tex` into your preview panel however you like ...
 *
 * Link with: -lavformat -lavcodec -lavfilter -lavutil -lswscale -lswresample
 *            -lpthread  (miniaudio is compiled inside vp_engine.c)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VPEngine VPEngine;

/* ── stream / media info ──────────────────────────────────────────────────── */

typedef struct {
    int  index;        /* stream index to pass to vp_engine_set_*_track   */
    char lang[64];     /* language tag, "und" if none                     */
    char title[128];   /* title metadata, "" if none                      */
} VPStreamInfo;

typedef struct {
    int     src_width, src_height; /* native video resolution             */
    int     width, height;         /* current decoded/scaled output size  */
    double  fps;                   /* frames per second (>0)              */
    double  duration;              /* seconds, <=0 if unknown             */
    int64_t frame_count;           /* estimated total frames, <=0 unknown */
    bool    has_video;
    bool    has_audio;
    int     sample_rate;           /* audio device rate (0 if no audio)   */
    int     channels;              /* audio channels   (0 if no audio)    */
} VPMediaInfo;

/* A CPU-side RGB24 frame copy (for thumbnails / exporting the current frame).
   Free with vp_engine_frame_free(). */
typedef struct {
    int      width, height;
    int      stride;     /* bytes per row (== width*3)        */
    uint8_t* pixels;     /* RGB24, top-down                   */
} VPFrameRGB;

/* Events delivered from vp_engine_update() (i.e. on your render thread, so the
   callback is safe to touch your UI state directly — no locking needed). */
typedef enum {
    VP_EVENT_STATE_CHANGED = 0, /* play <-> pause toggled                 */
    VP_EVENT_SEEKED        = 1, /* a seek finished; new frame is on screen*/
    VP_EVENT_ENDED         = 2  /* playback reached the end of the media  */
} VPEvent;

typedef void (*VPEventCb)(void* user, VPEvent ev);

/* ── lifecycle ────────────────────────────────────────────────────────────── */

/* Open `path` and start the engine (begins paused-ready / playing=1 like a
   normal player; call vp_engine_pause() right after if you want it to start
   paused). `scale`: 1.0 = native, 0.5 = half-res decode, etc. (cost ~ scale^2).
   Returns NULL on failure. */
VPEngine* vp_engine_create(const char* path, float scale);
void      vp_engine_destroy(VPEngine* e);

/* Call once per render frame on the GL/main thread BEFORE sampling the texture.
   Uploads the due video frame, handles looping, end-of-stream, and callbacks. */
void vp_engine_update(VPEngine* e);

/* The GL texture the latest frame was uploaded into. w/h = texture size.
   Returns 0 until the first frame is ready. Safe to sample any time on the
   render thread. */
unsigned int vp_engine_texture(VPEngine* e, int* w, int* h);

/* ── transport ────────────────────────────────────────────────────────────── */

void vp_engine_play(VPEngine* e);
void vp_engine_pause(VPEngine* e);
void vp_engine_toggle(VPEngine* e);
bool vp_engine_is_playing(VPEngine* e);

/* ── seeking & stepping ───────────────────────────────────────────────────── */

/* Seek to `seconds`. If `precise` is true AND playback is paused, the engine
   decodes forward from the prior keyframe and lands on the EXACT frame that
   contains `seconds` (frame-accurate). While playing, seeks are keyframe-fast
   regardless of `precise` (audio/video stay in sync that way). */
void vp_engine_seek(VPEngine* e, double seconds, bool precise);

/* Seek to an exact frame index (always frame-accurate; pauses first). */
void vp_engine_seek_frame(VPEngine* e, int64_t frame);

/* Step `delta` frames (e.g. +1 / -1). Pauses, then lands frame-accurately. */
void vp_engine_step(VPEngine* e, int delta);

/* Bracket an interactive scrub (slider drag). Between begin/end, audio is muted
   and the display free-runs to the latest decoded frame so dragging feels live.
   During the drag call vp_engine_seek(e, t, false) on each value change. */
void vp_engine_scrub_begin(VPEngine* e);
void vp_engine_scrub_end(VPEngine* e);

/* ── rate / volume ────────────────────────────────────────────────────────── */

/* Playback speed, 0.25 .. 4.0 (1.0 = normal). Audio is tempo-shifted to match
   (pitch preserved) via atempo; video re-times to the audio. */
void   vp_engine_set_rate(VPEngine* e, double rate);
double vp_engine_get_rate(VPEngine* e);

void  vp_engine_set_volume(VPEngine* e, float vol01); /* 0.0 .. 1.0          */
float vp_engine_get_volume(VPEngine* e);
void  vp_engine_set_muted(VPEngine* e, bool muted);
bool  vp_engine_is_muted(VPEngine* e);

/* ── loop / in-out region ─────────────────────────────────────────────────── */

/* When enabled and playing, the playhead jumps back to `in_sec` upon reaching
   `out_sec`. Great for previewing a trimmed clip region. */
void vp_engine_set_loop(VPEngine* e, double in_sec, double out_sec, bool enabled);
void vp_engine_clear_loop(VPEngine* e);

/* ── filters & tracks (hot-swappable while playing) ───────────────────────── */

void vp_engine_set_vf(VPEngine* e, const char* vf);  /* NULL/"" = passthrough */
void vp_engine_set_af(VPEngine* e, const char* af);
void vp_engine_set_scale(VPEngine* e, float scale);

/* Re-render the current frame through the (possibly just-changed) filter graph.
   Useful while paused, where no new frames flow so a vf/af change wouldn't show
   until playback resumes. Safe to call any time. */
void vp_engine_refresh(VPEngine* e);

void vp_engine_set_video_track(VPEngine* e, int stream_index);
void vp_engine_set_audio_track(VPEngine* e, int stream_index);
int  vp_engine_video_tracks(VPEngine* e, VPStreamInfo* out, int max);
int  vp_engine_audio_tracks(VPEngine* e, VPStreamInfo* out, int max);

/* ── queries ──────────────────────────────────────────────────────────────── */

double  vp_engine_position(VPEngine* e);    /* current time, seconds         */
double  vp_engine_duration(VPEngine* e);    /* total time, seconds (<=0 = ?) */
double  vp_engine_fps(VPEngine* e);
int64_t vp_engine_frame_index(VPEngine* e); /* current frame number          */
int64_t vp_engine_frame_count(VPEngine* e); /* total frames (<=0 = unknown)  */
bool    vp_engine_ended(VPEngine* e);
void    vp_engine_get_info(VPEngine* e, VPMediaInfo* out);

/* ── snapshot (current displayed frame, RGB24) ────────────────────────────── */

/* Copies the most recently displayed frame into *out (out->pixels malloc'd).
   Returns false if no frame is available yet. Free with vp_engine_frame_free. */
bool vp_engine_snapshot(VPEngine* e, VPFrameRGB* out);
void vp_engine_frame_free(VPFrameRGB* f);

/* ── events ───────────────────────────────────────────────────────────────── */

void vp_engine_set_callback(VPEngine* e, VPEventCb cb, void* user);

#ifdef __cplusplus
}
#endif
