/* ve.h — video-edit operation model + lossless classification
 * ─────────────────────────────────────────────────────────────────────────
 * A library (no ffmpeg subprocess) for describing and planning non-linear
 * video edits. The pipeline is:
 *
 *      text (DSL)  ──ve_parse──▶  VeOp[]  ──ve_plan──▶  VePlan  ──ve_execute──▶ file
 *                                            │
 *                       decides, per stream: COPY (lossless remux) vs
 *                       TRANSCODE (decode→filter→encode) vs DROP.
 *
 * Design rule: a stream is only ever transcoded if some op *forces* a decode.
 * Everything else is a container/metadata edit and stays byte-for-byte.
 *
 * ve.h / ve_dsl.c / ve_plan.c have NO libav dependency (pure C — easy to test
 * and to embed in a CLI). Only ve_exec.c links libav.
 */
#ifndef VE_H
#define VE_H

#include <stdbool.h>
#include <stdint.h>

/* ── operations ───────────────────────────────────────────────────────────── */

typedef enum {
    VE_ROTATE = 0,
    VE_FLIP,
    VE_MUTE,
    VE_ADD_AUDIO,
    VE_REMOVE_AUDIO,
    VE_VOLUME,
    VE_MONO,            /* stereo -> mono                          */
    VE_SCALE,
    VE_CROP,
    VE_MERGE,           /* concat additional files                 */
    VE_TRIM,
    VE_REVERSE_VIDEO,
    VE_REVERSE_AUDIO,
    VE_ADD_THUMBNAIL,
    VE_REMOVE_THUMBNAIL,
    VE_ADD_SUBTITLE,
    VE_REMOVE_SUBTITLE,
    VE_BRIGHTNESS,
    VE_CONTRAST,
    VE_HUE,
    VE_SATURATION,
    VE_GAMMA,
    VE_GRAYSCALE,
    VE_ASPECT,          /* set display aspect ratio (SAR metadata) */
    VE_FPS,
    VE_SPEED,           /* speed factor (affects video + audio)    */
    VE_FADE,            /* fade in/out                             */
    VE_DENOISE,
    VE_SHARPEN,
    VE_PAD,             /* letterbox/pillarbox to w x h            */
    VE_METADATA,        /* set a container metadata key=value      */
    VE_FORMAT,          /* force output container (remux)          */
    VE_OP_COUNT
} VeOpKind;

#define VE_PATH_MAX 1024
#define VE_MERGE_MAX 16

typedef struct {
    VeOpKind kind;
    int      line;        /* source line in the script, for diagnostics */
    union {
        struct { double deg;  bool ccw; }                 rotate;
        struct { char   axis; }                           flip;        /* 'h'/'v' */
        struct { char   path[VE_PATH_MAX]; char lang[16]; } add_audio;
        struct { int    index; }                          remove_audio;
        struct { double factor; }                         volume;      /* 1.0 = unity */
        struct { int    w, h; }                           scale;       /* -1 = keep AR */
        struct { int    w, h, x, y; }                     crop;
        struct { double start, end; }                     trim;        /* seconds; end<0 = to EOF */
        struct { char   which; }                          reverse;     /* 'v'/'a' */
        struct { char   path[VE_PATH_MAX]; }              thumbnail;
        struct { char   path[VE_PATH_MAX]; char lang[16]; } subtitle;
        struct { int    index; }                          remove_sub;
        struct { double value; }                          adjust;      /* bright/contrast/hue/sat/gamma */
        struct { int    num, den; }                       aspect;
        struct { double value; }                          fps;
        struct { double factor; }                         speed;       /* 2.0 = twice as fast */
        struct { char   dir; double dur; char target; }   fade;        /* dir 'i'/'o', target 'v'/'a'/'b' */
        struct { int    w, h; }                           pad;
        struct { char   key[64]; char val[192]; }         meta;
        struct { char   name[16]; }                       format;
        struct { char   paths[VE_MERGE_MAX][VE_PATH_MAX]; int n; } merge;
    } u;
} VeOp;

/* ── classification (the heart of "don't re-encode") ──────────────────────── */

typedef struct {
    bool needs_video_decode;  /* forces the video stream into transcode      */
    bool needs_audio_decode;  /* forces the audio stream into transcode      */
    bool container_only;      /* pure remux/metadata, never touches pixels   */
    bool structural;          /* trim / merge — changes packet timeline      */
} VeOpClass;

/* Classification can depend on parameters (e.g. rotate 90 is lossless,
   rotate 37 is not), so it takes the whole op. */
VeOpClass ve_classify(const VeOp* op);

/* safe bounded string copy (always NUL-terminates) */
void ve_copy(char* dst, unsigned long cap, const char* src);

const char* ve_op_name(VeOpKind k);

/* ── parsing (ve_dsl.c) ───────────────────────────────────────────────────── */

typedef struct {
    char  input[VE_PATH_MAX];
    char  output[VE_PATH_MAX];
    VeOp* ops;
    int   n_ops;
    int   cap_ops;
} VeScript;

/* Parse DSL text. Returns true on success; on failure fills err and returns
   false. Caller frees with ve_script_free. */
bool ve_parse(const char* text, VeScript* out, char* err, int errlen);
void ve_script_free(VeScript* s);

/* ── planning (ve_plan.c) ─────────────────────────────────────────────────── */

typedef enum { VE_COPY = 0, VE_TRANSCODE, VE_DROP } VeStreamAction;

#define VE_FILTER_MAX 4096

typedef struct {
    char  input[VE_PATH_MAX];
    char  output[VE_PATH_MAX];
    char  format[16];                 /* forced container, or "" = by extension */

    /* video plan */
    VeStreamAction video;
    char  vf[VE_FILTER_MAX];          /* filter chain (only if TRANSCODE)        */
    int   rotate_quadrant;            /* lossless display-matrix rotate: 0..3 *90 */
    int   sar_num, sar_den;           /* lossless aspect override (0 = none)      */

    /* audio plan */
    VeStreamAction audio;
    char  af[VE_FILTER_MAX];

    /* structural */
    bool   has_trim;
    double trim_start, trim_end;
    int    n_merge;
    char   merge[VE_MERGE_MAX][VE_PATH_MAX];

    /* added streams (muxed by copy, executor falls back to transcode if the
       codec is incompatible with the chosen container) */
    char  add_audio[8][VE_PATH_MAX];  char add_audio_lang[8][16]; int n_add_audio;
    char  add_sub[8][VE_PATH_MAX];    char add_sub_lang[8][16];   int n_add_sub;
    char  thumbnail[VE_PATH_MAX];     bool remove_thumbnail;

    /* dropped existing streams */
    int   drop_audio_index[16];   int n_drop_audio;   bool drop_all_audio;
    int   drop_sub_index[16];     int n_drop_sub;

    /* metadata */
    char  meta[16][256];  int n_meta;

    /* summary */
    bool  lossless;                   /* true iff no stream is transcoded        */
} VePlan;

/* Build an execution plan from ops. `probe` may be NULL (some lossless ops,
   like fade-out timing, benefit from knowing duration/size, but the plan is
   still valid without it). Returns true on success. */
bool ve_plan(const VeScript* s, VePlan* out, char* err, int errlen);

/* Render a human-readable summary of the plan (for --dry-run). */
void ve_plan_print(const VePlan* p);

/* ── execution (ve_exec.c, links libav) ───────────────────────────────────── */

/* Progress callback, invoked periodically from ve_execute on the calling
   thread. `fraction` is 0..1, or <0 when the total duration is unknown.
   `cur_sec`/`total_sec` are media timestamps. Keep the callback light.
   RETURN non-zero to CANCEL: ve_execute then stops, deletes the partial output,
   and returns VE_ECANCELED. Return 0 to continue. */
typedef int (*VeProgressCb)(void* user, double fraction, double cur_sec, double total_sec);

#define VE_ECANCELED (-2)   /* ve_execute return value when the caller cancelled */

/* Returns 0 on success, VE_ECANCELED if cancelled, other negative on error
   (fills err). Picks the lossless remux path when plan->lossless, otherwise the
   transcode path. Pass NULL for on_progress if you don't want updates. */
int ve_execute(const VePlan* p, VeProgressCb on_progress, void* user, char* err, int errlen);

#endif /* VE_H */
