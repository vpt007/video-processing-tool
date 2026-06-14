/* vp_engine.c — ffplay-style headless video playback engine
 *
 * Public API: vp_engine.h. No UI dependency — this is the decode/sync/render
 * core a host app (e.g. a video editor) drives from its own controls.
 *
 * Architecture (mirrors ffplay):
 *   read_thread   : demux only. Routes packets into per-stream PacketQueues.
 *                   Owns seek + track-change orchestration + EOF draining.
 *   video_thread  : pulls video packets -> decode -> video filtergraph
 *                   -> pushes filtered RGB24 frames into VFrameQueue.
 *   audio_thread  : pulls audio packets -> decode -> audio filtergraph
 *                   -> writes interleaved float PCM into the ring buffer,
 *                      and publishes the "audio clock" (pts at end of buffer).
 *   ma_data_cb    : drains the ring at the hardware rate. Muted while seeking
 *                   or paused.
 *   vp_render     : (main / GL thread) computes the MASTER CLOCK (= audio
 *                   clock), pops every video frame whose pts is due, uploads
 *                   the newest due one to the GL texture, and draws the UI.
 *
 * Why this fixes the two bugs:
 *   - SYNC: video is presented against the audio clock, which is derived from
 *     how much PCM the *device* has actually consumed (end-of-buffer pts minus
 *     un-played samples). Decode never blocks the demuxer, so audio is never
 *     starved while a video frame waits for its presentation time.
 *   - EOF HANG: shutdown aborts every queue + ring and signals every cond
 *     before joining, so no thread can be parked. EOF also flushes the
 *     decoders so trailing frames play out, then the player parks cleanly.
 *
 * Seeks use ffplay's serial mechanism: flushing a PacketQueue bumps its
 * serial; frames/PCM carry the serial they were produced under; anything with
 * a stale serial is discarded. That is what makes scrubbing instant and keeps
 * audio from playing stale samples.
 */

#include "vp_engine.h"

#include <GLFW/glfw3.h>
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ── tunables ─────────────────────────────────────────────────────────────── */

#define VFQ_CAP            6        /* filtered video frames buffered ahead */
#define RING_SECONDS       2        /* audio ring depth, in seconds */
#define MAX_VIDEO_PKTS     120      /* demux backpressure caps */
#define MAX_AUDIO_PKTS     400
#define AV_SYNC_THRESH_MAX 0.10     /* if video is this far behind, skip frames */

/* present_req modes for present_due_frame */
enum { PR_NORMAL = 0, PR_NEWEST = 1, PR_OLDEST = 2 };

static double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ── small helpers ────────────────────────────────────────────────────────── */

static double now_sec(void) { return av_gettime_relative() / 1000000.0; }

static void sleep_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

/* ── PacketQueue (one per stream) ─────────────────────────────────────────── */

typedef struct PacketList {
    AVPacket*          pkt;
    int                serial;
    struct PacketList* next;
} PacketList;

typedef struct {
    PacketList*     first;
    PacketList*     last;
    int             nb_packets;
    int             serial;       /* bumped on flush; carried by each packet */
    int             abort;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
} PacketQueue;

static void pq_init(PacketQueue* q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->cv, NULL);
    q->serial = 1;
}

static void pq_flush(PacketQueue* q) {
    pthread_mutex_lock(&q->mu);
    PacketList* p = q->first;
    while (p) {
        PacketList* n = p->next;
        av_packet_free(&p->pkt);
        free(p);
        p = n;
    }
    q->first = q->last = NULL;
    q->nb_packets = 0;
    q->serial++;                 /* invalidate everything produced before now */
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

static void pq_abort(PacketQueue* q) {
    pthread_mutex_lock(&q->mu);
    q->abort = 1;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

static void pq_resume(PacketQueue* q) {
    pthread_mutex_lock(&q->mu);
    q->abort = 0;
    pthread_mutex_unlock(&q->mu);
}

static void pq_destroy(PacketQueue* q) {
    pq_flush(q);
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->cv);
}

/* takes ownership of pkt (moves it). returns 0 ok, -1 abort. */
static int pq_put(PacketQueue* q, AVPacket* pkt) {
    PacketList* e = malloc(sizeof(*e));
    if (!e) return -1;
    e->pkt = av_packet_alloc();
    av_packet_move_ref(e->pkt, pkt);
    e->next = NULL;
    pthread_mutex_lock(&q->mu);
    if (q->abort) {
        pthread_mutex_unlock(&q->mu);
        av_packet_free(&e->pkt);
        free(e);
        return -1;
    }
    e->serial = q->serial;
    if (q->last) q->last->next = e; else q->first = e;
    q->last = e;
    q->nb_packets++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
    return 0;
}

/* blocking get. fills *serial. returns 1 ok, 0 abort. */
static int pq_get(PacketQueue* q, AVPacket* pkt, int* serial) {
    pthread_mutex_lock(&q->mu);
    for (;;) {
        if (q->abort) { pthread_mutex_unlock(&q->mu); return 0; }
        if (q->first) {
            PacketList* e = q->first;
            q->first = e->next;
            if (!q->first) q->last = NULL;
            q->nb_packets--;
            av_packet_move_ref(pkt, e->pkt);
            *serial = e->serial;
            av_packet_free(&e->pkt);
            free(e);
            pthread_mutex_unlock(&q->mu);
            return 1;
        }
        pthread_cond_wait(&q->cv, &q->mu);
    }
}

static int pq_nb(PacketQueue* q) {
    pthread_mutex_lock(&q->mu);
    int n = q->nb_packets;
    pthread_mutex_unlock(&q->mu);
    return n;
}

static int pq_serial(PacketQueue* q) {
    pthread_mutex_lock(&q->mu);
    int s = q->serial;
    pthread_mutex_unlock(&q->mu);
    return s;
}

/* ── VFrameQueue (filtered RGB24 video frames) ────────────────────────────── */

typedef struct {
    AVFrame*        f[VFQ_CAP];
    double          pts[VFQ_CAP];
    int             serial[VFQ_CAP];
    int             r, w, n;
    int             abort;
    pthread_mutex_t mu;
    pthread_cond_t  cv;
} VFrameQueue;

static void vfq_init(VFrameQueue* q) {
    memset(q, 0, sizeof(*q));
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->cv, NULL);
}

static void vfq_clear(VFrameQueue* q) {
    pthread_mutex_lock(&q->mu);
    while (q->n) { av_frame_free(&q->f[q->r]); q->r = (q->r + 1) % VFQ_CAP; q->n--; }
    q->r = q->w = q->n = 0;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

static void vfq_abort(VFrameQueue* q) {
    pthread_mutex_lock(&q->mu);
    q->abort = 1;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
}

static void vfq_resume(VFrameQueue* q) {
    pthread_mutex_lock(&q->mu);
    q->abort = 0;
    pthread_mutex_unlock(&q->mu);
}

static void vfq_destroy(VFrameQueue* q) {
    vfq_clear(q);
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->cv);
}

/* blocking push (waits for space). takes ownership of frame. 0 ok / -1 abort */
static int vfq_push(VFrameQueue* q, AVFrame* frame, double pts, int serial) {
    pthread_mutex_lock(&q->mu);
    while (q->n >= VFQ_CAP && !q->abort) pthread_cond_wait(&q->cv, &q->mu);
    if (q->abort) { pthread_mutex_unlock(&q->mu); av_frame_free(&frame); return -1; }
    q->f[q->w]      = frame;
    q->pts[q->w]    = pts;
    q->serial[q->w] = serial;
    q->w = (q->w + 1) % VFQ_CAP;
    q->n++;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
    return 0;
}

/* ── audio ring (interleaved float) with real backpressure ────────────────── */

typedef struct {
    float*          data;
    int             cap;       /* in samples (not frames) */
    int             head;
    int             tail;
    atomic_int      count;
    int             abort;
    pthread_mutex_t mu;
    pthread_cond_t  not_full;
} Ring;

static void ring_init(Ring* r, int cap_samples) {
    r->data  = malloc((size_t)cap_samples * sizeof(float));
    r->cap   = cap_samples;
    r->head  = r->tail = 0;
    r->abort = 0;
    atomic_store(&r->count, 0);
    pthread_mutex_init(&r->mu, NULL);
    pthread_cond_init(&r->not_full, NULL);
}

static void ring_free(Ring* r) {
    free(r->data);
    pthread_mutex_destroy(&r->mu);
    pthread_cond_destroy(&r->not_full);
}

static void ring_clear(Ring* r) {
    pthread_mutex_lock(&r->mu);
    r->head = r->tail = 0;
    atomic_store(&r->count, 0);
    pthread_cond_broadcast(&r->not_full);
    pthread_mutex_unlock(&r->mu);
}

static void ring_abort(Ring* r) {
    pthread_mutex_lock(&r->mu);
    r->abort = 1;
    pthread_cond_broadcast(&r->not_full);
    pthread_mutex_unlock(&r->mu);
}

/* read by the audio callback. never blocks. */
static int ring_read(Ring* r, float* dst, int n) {
    pthread_mutex_lock(&r->mu);
    int got = 0;
    while (got < n && atomic_load(&r->count) > 0) {
        dst[got++] = r->data[r->head];
        r->head = (r->head + 1) % r->cap;
        atomic_fetch_sub(&r->count, 1);
    }
    if (got) pthread_cond_signal(&r->not_full);
    pthread_mutex_unlock(&r->mu);
    return got;
}

/* blocking write — never drops samples. waits until the whole chunk fits. */
static void ring_write(Ring* r, const float* src, int n) {
    int written = 0;
    while (written < n) {
        pthread_mutex_lock(&r->mu);
        while (!r->abort && r->cap - atomic_load(&r->count) == 0)
            pthread_cond_wait(&r->not_full, &r->mu);
        if (r->abort) { pthread_mutex_unlock(&r->mu); return; }
        int space = r->cap - atomic_load(&r->count);
        int chunk = (n - written) < space ? (n - written) : space;
        for (int i = 0; i < chunk; i++) {
            r->data[r->tail] = src[written + i];
            r->tail = (r->tail + 1) % r->cap;
        }
        atomic_fetch_add(&r->count, chunk);
        written += chunk;
        pthread_mutex_unlock(&r->mu);
    }
}

/* ── widget ───────────────────────────────────────────────────────────────── */

struct VPEngine {
    char path[512];

    AVFormatContext* fmt;

    /* decoder contexts are owned by their decode threads */
    AVCodecContext*  vctx;
    AVCodecContext*  actx;
    int              vsi;      /* active video stream index (read_thread owns) */
    int              asi;      /* active audio stream index */
    int              has_audio;
    int              has_video;

    /* filter graphs (video graph touched only by video_thread,
       audio graph only by audio_thread) */
    AVFilterGraph*   vfg;
    AVFilterContext* vf_src;
    AVFilterContext* vf_sink;
    AVRational       vf_tb;

    AVFilterGraph*   afg;
    AVFilterContext* af_src;
    AVFilterContext* af_sink;
    AVRational       af_tb;

    struct SwsContext* sws;
    int                sws_w, sws_h;
    enum AVPixelFormat sws_fmt;

    /* GL texture (main thread only) */
    unsigned int     tex;
    int              tex_w, tex_h;

    /* queues */
    PacketQueue      videoq;
    PacketQueue      audioq;
    VFrameQueue      pictq;
    Ring             pcm;

    /* audio device */
    int              pcm_ch;
    int              pcm_sr;
    int              dev_period;     /* device period in frames (for latency) */
    ma_device        audio_dev;
    bool             audio_ok;

    /* audio master clock bookkeeping (audio_thread writes, render reads) */
    pthread_mutex_t  aclock_mu;
    double           audio_clock_end;    /* pts (s) at end of PCM put in ring */
    int              audio_clock_serial; /* audioq serial that produced it */

    /* external clock, used only when there is no audio stream */
    pthread_mutex_t  eclock_mu;
    double           ext_base_pts;
    double           ext_base_time;
    int              ext_paused;

    /* playback state */
    double           duration;
    double           fps;            /* video frame rate (>0)             */
    int64_t          frame_count;    /* estimated total frames (<=0 = ?)  */
    atomic_int       is_playing;
    atomic_int       is_seeking;     /* mute audio + free-run video display */
    atomic_int       finished;       /* hit EOF and fully drained */
    atomic_int       eof_reached;    /* demuxer hit EOF (cleared on seek) */
    atomic_int       quit;

    /* playback rate (×1000 to stay integer-atomic). 1000 = 1.0× */
    atomic_int       rate_milli;

    /* volume / mute (applied in the audio callback) */
    atomic_int       volume_milli;   /* 0..1000 */
    atomic_int       muted;

    /* loop / in-out region (written under cmd_mu, read loosely on GL thread) */
    atomic_int       loop_on;
    double           loop_in, loop_out;

    /* frame-accurate seek: drop decoded frames before drop_to (one target).
       Engaged only for precise seeks while paused. */
    atomic_int       seek_drop;      /* 1 = dropping toward drop_to        */
    pthread_mutex_t  drop_mu;
    double           drop_to;        /* media seconds of the target frame  */

    /* display request for present_due_frame (set by control fns) */
    atomic_int       present_req;    /* PR_NORMAL / PR_NEWEST / PR_OLDEST  */

    /* snapshot: CPU copy of the last displayed RGB24 frame */
    pthread_mutex_t  snap_mu;
    uint8_t*         snap_rgb;
    int              snap_w, snap_h;

    /* event callback (fired from vp_engine_update on the GL thread) */
    VPEventCb        cb;
    void*            cb_user;
    int              prev_playing;   /* for STATE_CHANGED edge detection   */
    atomic_int       seek_pending;   /* a seek was issued; fire SEEKED once */

    /* threads */
    pthread_t        read_thr, video_thr, audio_thr;
    int              threads_up;

    /* read-thread command channel */
    pthread_mutex_t  cmd_mu;
    pthread_cond_t   cmd_cv;
    atomic_int       seek_req;
    double           seek_to;
    atomic_int       seek_want_precise; /* requested precise (honored if paused) */
    atomic_int       track_req;
    int              req_vsi, req_asi;

    /* decode-thread reconfigure flags (set by read_thread) */
    atomic_int       v_reopen;       /* reopen video codec for vsi + rebuild vf */
    atomic_int       a_reopen;       /* reopen audio codec for asi + rebuild af */
    atomic_int       vf_rebuild;     /* rebuild video filtergraph only */
    atomic_int       af_rebuild;     /* rebuild audio filtergraph only */

    /* filter strings */
    pthread_mutex_t  fstr_mu;
    char             cur_vf[512];
    char             cur_af[512];
    char             ui_vf[512];
    char             ui_af[512];

    /* decode scale */
    float            scale;
    int              out_w, out_h;

    /* UI seek slider */
    atomic_int       ui_seek_active;
    float            ui_seek_val;

    /* last displayed pts (main thread) */
    double           display_pts;
};

/* ── filter graphs ────────────────────────────────────────────────────────── */

static int build_vf(VPEngine* vp) {
    avfilter_graph_free(&vp->vfg);
    if (!vp->vctx) return 0;

    vp->vfg = avfilter_graph_alloc();
    AVStream*  vs  = vp->fmt->streams[vp->vsi];
    AVRational tb  = vs->time_base;
    AVRational fr  = vs->avg_frame_rate;
    AVRational sar = vp->vctx->sample_aspect_ratio;
    if (!sar.num) sar = (AVRational){1,1};

    char args[512];
    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:frame_rate=%d/%d:pixel_aspect=%d/%d",
        vp->vctx->width, vp->vctx->height, (int)vp->vctx->pix_fmt,
        tb.num, tb.den, fr.num ? fr.num : 25, fr.den ? fr.den : 1,
        sar.num, sar.den);

    const AVFilter* bsrc  = avfilter_get_by_name("buffer");
    const AVFilter* bsink = avfilter_get_by_name("buffersink");
    if (avfilter_graph_create_filter(&vp->vf_src,  bsrc,  "in",  args, NULL, vp->vfg) < 0) goto err;
    if (avfilter_graph_create_filter(&vp->vf_sink, bsink, "out", NULL, NULL, vp->vfg) < 0) goto err;

    enum AVPixelFormat out_fmts[] = { AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE };
    av_opt_set_int_list(vp->vf_sink, "pix_fmts", out_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

    AVFilterInOut* outs = avfilter_inout_alloc();
    AVFilterInOut* ins  = avfilter_inout_alloc();
    outs->name = av_strdup("in");  outs->filter_ctx = vp->vf_src;  outs->pad_idx = 0; outs->next = NULL;
    ins->name  = av_strdup("out"); ins->filter_ctx  = vp->vf_sink; ins->pad_idx  = 0; ins->next  = NULL;

    pthread_mutex_lock(&vp->fstr_mu);
    char user_vf[512]; strncpy(user_vf, vp->cur_vf, sizeof(user_vf)); user_vf[511]=0;
    pthread_mutex_unlock(&vp->fstr_mu);

    char fs_buf[1100];
    int need_scale = (vp->out_w > 0 && vp->out_h > 0 &&
                      (vp->out_w != vp->vctx->width || vp->out_h != vp->vctx->height));
    if (need_scale && user_vf[0])
        snprintf(fs_buf, sizeof(fs_buf), "scale=%d:%d,%s", vp->out_w, vp->out_h, user_vf);
    else if (need_scale)
        snprintf(fs_buf, sizeof(fs_buf), "scale=%d:%d", vp->out_w, vp->out_h);
    else
        snprintf(fs_buf, sizeof(fs_buf), "%s", user_vf[0] ? user_vf : "null");

    if (avfilter_graph_parse_ptr(vp->vfg, fs_buf, &ins, &outs, NULL) < 0) {
        avfilter_inout_free(&ins); avfilter_inout_free(&outs); goto err;
    }
    vp->vfg->nb_threads = 2;
    if (avfilter_graph_config(vp->vfg, NULL) < 0) goto err;
    vp->vf_tb = av_buffersink_get_time_base(vp->vf_sink);
    return 0;
err:
    avfilter_graph_free(&vp->vfg);
    return -1;
}

static int build_af(VPEngine* vp) {
    avfilter_graph_free(&vp->afg);
    if (!vp->actx) return 0;

    vp->afg = avfilter_graph_alloc();

    char chl[64];
    av_channel_layout_describe(&vp->actx->ch_layout, chl, sizeof(chl));

    char args[512];
    snprintf(args, sizeof(args),
        "time_base=1/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
        vp->actx->sample_rate, vp->actx->sample_rate,
        av_get_sample_fmt_name(vp->actx->sample_fmt), chl);

    const AVFilter* bsrc  = avfilter_get_by_name("abuffer");
    const AVFilter* bsink = avfilter_get_by_name("abuffersink");
    if (avfilter_graph_create_filter(&vp->af_src,  bsrc,  "in",  args, NULL, vp->afg) < 0) goto err;
    if (avfilter_graph_create_filter(&vp->af_sink, bsink, "out", NULL, NULL, vp->afg) < 0) goto err;

    /* Force the sink to the DEVICE's format/rate/layout. This makes the audio
       graph resample to whatever the device opened with, so audio-filter
       swaps and audio-track switches (which may have a different native rate
       or channel count) always feed miniaudio the right shape. */
    static const enum AVSampleFormat afmts[] = { AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_NONE };
    const int srates[] = { vp->pcm_sr, -1 };
    av_opt_set_int_list(vp->af_sink, "sample_fmts",  afmts,  AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int_list(vp->af_sink, "sample_rates", srates, -1,                 AV_OPT_SEARCH_CHILDREN);
    {
        AVChannelLayout want = {0};
        av_channel_layout_default(&want, vp->pcm_ch);
        char wbuf[64]; av_channel_layout_describe(&want, wbuf, sizeof(wbuf));
        av_opt_set(vp->af_sink, "ch_layouts", wbuf, AV_OPT_SEARCH_CHILDREN);
        av_channel_layout_uninit(&want);
    }

    AVFilterInOut* outs = avfilter_inout_alloc();
    AVFilterInOut* ins  = avfilter_inout_alloc();
    outs->name = av_strdup("in");  outs->filter_ctx = vp->af_src;  outs->pad_idx = 0; outs->next = NULL;
    ins->name  = av_strdup("out"); ins->filter_ctx  = vp->af_sink; ins->pad_idx  = 0; ins->next  = NULL;

    pthread_mutex_lock(&vp->fstr_mu);
    char user_af[512]; strncpy(user_af, vp->cur_af, sizeof(user_af)); user_af[511]=0;
    pthread_mutex_unlock(&vp->fstr_mu);

    /* Realize the playback rate with an atempo chain (each stage clamped to
       atempo's valid 0.5–2.0 range), composed before the user's -af. */
    double rate = atomic_load(&vp->rate_milli) / 1000.0;
    char tempo[160]; tempo[0] = 0;
    if (fabs(rate - 1.0) > 1e-6) {
        double r = rate;
        while (r > 2.0 + 1e-9) { strncat(tempo, "atempo=2.0,", sizeof(tempo)-strlen(tempo)-1); r /= 2.0; }
        while (r < 0.5 - 1e-9) { strncat(tempo, "atempo=0.5,", sizeof(tempo)-strlen(tempo)-1); r /= 0.5; }
        char part[40]; snprintf(part, sizeof(part), "atempo=%.6f", r);
        strncat(tempo, part, sizeof(tempo)-strlen(tempo)-1);
    }

    char fsbuf[1200];
    if (tempo[0] && user_af[0]) snprintf(fsbuf, sizeof(fsbuf), "%s,%s", tempo, user_af);
    else if (tempo[0])          snprintf(fsbuf, sizeof(fsbuf), "%s", tempo);
    else if (user_af[0])        snprintf(fsbuf, sizeof(fsbuf), "%s", user_af);
    else                        snprintf(fsbuf, sizeof(fsbuf), "anull");
    const char* fs = fsbuf;
    if (avfilter_graph_parse_ptr(vp->afg, fs, &ins, &outs, NULL) < 0) {
        avfilter_inout_free(&ins); avfilter_inout_free(&outs); goto err;
    }
    vp->afg->nb_threads = 1;
    if (avfilter_graph_config(vp->afg, NULL) < 0) goto err;
    vp->af_tb = av_buffersink_get_time_base(vp->af_sink);
    return 0;
err:
    avfilter_graph_free(&vp->afg);
    return -1;
}

/* ── codec open/close ─────────────────────────────────────────────────────── */

static int open_video_codec(VPEngine* vp) {
    avcodec_free_context(&vp->vctx);
    if (vp->vsi < 0) return 0;
    AVStream* s = vp->fmt->streams[vp->vsi];
    const AVCodec* c = avcodec_find_decoder(s->codecpar->codec_id);
    if (!c) return -1;
    vp->vctx = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(vp->vctx, s->codecpar);
    vp->vctx->thread_count = 2;
    if (avcodec_open2(vp->vctx, c, NULL) < 0) return -1;

    vp->out_w = (int)(vp->vctx->width  * vp->scale) & ~1;
    vp->out_h = (int)(vp->vctx->height * vp->scale) & ~1;
    if (vp->out_w < 2) vp->out_w = 2;
    if (vp->out_h < 2) vp->out_h = 2;
    return 0;
}

static int open_audio_codec(VPEngine* vp) {
    avcodec_free_context(&vp->actx);
    if (vp->asi < 0) return 0;
    AVStream* s = vp->fmt->streams[vp->asi];
    const AVCodec* c = avcodec_find_decoder(s->codecpar->codec_id);
    if (!c) return -1;
    vp->actx = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(vp->actx, s->codecpar);
    if (avcodec_open2(vp->actx, c, NULL) < 0) return -1;
    return 0;
}

/* ── master clock ─────────────────────────────────────────────────────────── */

/* audio clock = pts at end of buffered PCM, minus what hasn't played yet.
   Equivalent to ffplay's audclk = audio_clock - unplayed_bytes/bytes_per_sec.
   The post-atempo buffersink timeline is compressed by `rate`, so we scale the
   result back to MEDIA seconds by multiplying by rate. */
static double get_audio_clock(VPEngine* vp) {
    pthread_mutex_lock(&vp->aclock_mu);
    double end = vp->audio_clock_end;
    int    serial = vp->audio_clock_serial;
    pthread_mutex_unlock(&vp->aclock_mu);

    if (isnan(end) || serial != pq_serial(&vp->audioq)) return NAN;

    int    buffered_samples = atomic_load(&vp->pcm.count);
    double buffered_sec = (double)(buffered_samples / vp->pcm_ch) / vp->pcm_sr;
    double dev_latency  = (double)vp->dev_period / vp->pcm_sr; /* ~1 period in flight */
    double rate = atomic_load(&vp->rate_milli) / 1000.0;
    return (end - buffered_sec - dev_latency) * rate;
}

static double get_ext_clock(VPEngine* vp) {
    double rate = atomic_load(&vp->rate_milli) / 1000.0;
    pthread_mutex_lock(&vp->eclock_mu);
    double v = vp->ext_paused ? vp->ext_base_pts
                              : vp->ext_base_pts + (now_sec() - vp->ext_base_time) * rate;
    pthread_mutex_unlock(&vp->eclock_mu);
    return v;
}

static void set_ext_clock(VPEngine* vp, double pts, int paused) {
    pthread_mutex_lock(&vp->eclock_mu);
    vp->ext_base_pts  = pts;
    vp->ext_base_time = now_sec();
    vp->ext_paused    = paused;
    pthread_mutex_unlock(&vp->eclock_mu);
}

static double get_master_clock(VPEngine* vp) {
    if (vp->has_audio) return get_audio_clock(vp);   /* may be NAN briefly */
    return get_ext_clock(vp);
}

/* ── miniaudio callback ───────────────────────────────────────────────────── */

static void ma_data_cb(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
    (void)in;
    VPEngine* vp = dev->pUserData;
    float* dst = out;
    int n = (int)(frames * (ma_uint32)vp->pcm_ch);

    /* While paused or scrubbing, emit silence and do NOT drain the ring, so the
       audio clock freezes and video holds its frame. */
    if (!atomic_load(&vp->is_playing) || atomic_load(&vp->is_seeking)) {
        memset(dst, 0, (size_t)n * sizeof(float));
        return;
    }

    int got = ring_read(&vp->pcm, dst, n);
    if (got < n) memset(dst + got, 0, (size_t)(n - got) * sizeof(float));

    /* Volume / mute is applied AFTER draining: muting must not stall the clock
       (the ring is still consumed), it just zeroes the output. */
    if (atomic_load(&vp->muted)) {
        memset(dst, 0, (size_t)got * sizeof(float));
    } else {
        int vol = atomic_load(&vp->volume_milli);
        if (vol != 1000) {
            float g = vol / 1000.0f;
            for (int i = 0; i < got; i++) dst[i] *= g;
        }
    }
}

/* ── video decode thread ──────────────────────────────────────────────────── */

static void* video_thread(void* arg) {
    VPEngine* vp  = arg;
    AVPacket* pkt = av_packet_alloc();
    AVFrame*  raw = av_frame_alloc();
    AVFrame*  flt = av_frame_alloc();
    int       pkt_serial = -1;

    while (!atomic_load(&vp->quit)) {

        if (atomic_load(&vp->v_reopen)) {
            atomic_store(&vp->v_reopen, 0);
            open_video_codec(vp);
            build_vf(vp);
            continue;
        }
        if (atomic_load(&vp->vf_rebuild)) {
            atomic_store(&vp->vf_rebuild, 0);
            build_vf(vp);
            continue;
        }
        if (!vp->vctx || !vp->vfg) { sleep_ms(10); continue; }

        if (!pq_get(&vp->videoq, pkt, &pkt_serial)) break;   /* abort */

        /* drop packets from a flushed (stale) generation */
        if (pkt_serial != pq_serial(&vp->videoq)) { av_packet_unref(pkt); continue; }
        /* a reconfigure may have been requested while we were blocked in
           pq_get — handle it before feeding this packet to the old codec */
        if (atomic_load(&vp->v_reopen) || atomic_load(&vp->vf_rebuild)) {
            av_packet_unref(pkt); continue;
        }

        int is_eof = (pkt->data == NULL);              /* null packet = drain */
        int sret = avcodec_send_packet(vp->vctx, is_eof ? NULL : pkt);
        if (sret == 0 || is_eof) {
            for (;;) {
                int r = avcodec_receive_frame(vp->vctx, raw);
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                if (r < 0) break;
                if (av_buffersrc_add_frame_flags(vp->vf_src, raw, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                    while (av_buffersink_get_frame(vp->vf_sink, flt) >= 0) {
                        double pts = (flt->pts == AV_NOPTS_VALUE)
                                   ? NAN : flt->pts * av_q2d(vp->vf_tb);

                        /* frame-accurate seek: skip frames before the target,
                           then stop dropping on the first frame that reaches it */
                        if (atomic_load(&vp->seek_drop) &&
                            pkt_serial == pq_serial(&vp->videoq) && !isnan(pts)) {
                            pthread_mutex_lock(&vp->drop_mu);
                            double tgt = vp->drop_to;
                            pthread_mutex_unlock(&vp->drop_mu);
                            double half = (vp->fps > 0.0) ? 0.5 / vp->fps : 0.0;
                            if (pts < tgt - half) { av_frame_unref(flt); continue; }
                            atomic_store(&vp->seek_drop, 0);  /* this is the target */
                        }

                        AVFrame* rgb = av_frame_clone(flt);   /* refs same buffer */
                        av_frame_unref(flt);
                        if (vfq_push(&vp->pictq, rgb, pts, pkt_serial) < 0) {
                            av_frame_unref(raw);
                            goto out;   /* aborted while blocked */
                        }
                    }
                }
                av_frame_unref(raw);
            }
            if (is_eof) { avcodec_flush_buffers(vp->vctx); atomic_store(&vp->seek_drop, 0); }
        }
        av_packet_unref(pkt);
    }
out:
    av_packet_free(&pkt);
    av_frame_free(&raw);
    av_frame_free(&flt);
    return NULL;
}

/* ── audio decode thread ──────────────────────────────────────────────────── */

static void* audio_thread(void* arg) {
    VPEngine* vp  = arg;
    AVPacket* pkt = av_packet_alloc();
    AVFrame*  raw = av_frame_alloc();
    AVFrame*  flt = av_frame_alloc();
    int       pkt_serial = -1;

    while (!atomic_load(&vp->quit)) {

        if (atomic_load(&vp->a_reopen)) {
            atomic_store(&vp->a_reopen, 0);
            open_audio_codec(vp);
            build_af(vp);
            continue;
        }
        if (atomic_load(&vp->af_rebuild)) {
            atomic_store(&vp->af_rebuild, 0);
            build_af(vp);
            continue;
        }
        if (!vp->actx || !vp->afg) { sleep_ms(10); continue; }

        if (!pq_get(&vp->audioq, pkt, &pkt_serial)) break;
        if (pkt_serial != pq_serial(&vp->audioq)) { av_packet_unref(pkt); continue; }
        if (atomic_load(&vp->a_reopen) || atomic_load(&vp->af_rebuild)) {
            av_packet_unref(pkt); continue;
        }

        int is_eof = (pkt->data == NULL);
        int sret = avcodec_send_packet(vp->actx, is_eof ? NULL : pkt);
        if (sret == 0 || is_eof) {
            for (;;) {
                int r = avcodec_receive_frame(vp->actx, raw);
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                if (r < 0) break;
                if (av_buffersrc_add_frame_flags(vp->af_src, raw, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                    while (av_buffersink_get_frame(vp->af_sink, flt) >= 0) {
                        double pts = (flt->pts == AV_NOPTS_VALUE)
                                   ? NAN : flt->pts * av_q2d(vp->af_tb);
                        int samples = flt->nb_samples * vp->pcm_ch;

                        /* precise seek: discard audio that lies before the seek
                           target so that, on resume, sound starts at the target
                           rather than at the preceding keyframe. Video owns the
                           exact landing; this only trims the ring lead-in. */
                        if (atomic_load(&vp->seek_drop) &&
                            pkt_serial == pq_serial(&vp->audioq) && !isnan(pts)) {
                            double rate = atomic_load(&vp->rate_milli) / 1000.0;
                            double media_end =
                                (pts + (double)flt->nb_samples / vp->pcm_sr) * rate;
                            pthread_mutex_lock(&vp->drop_mu);
                            double tgt = vp->drop_to;
                            pthread_mutex_unlock(&vp->drop_mu);
                            if (media_end < tgt) { av_frame_unref(flt); continue; }
                        }

                        /* publish clock BEFORE the (possibly blocking) write so
                           the value reflects pts at the END of this chunk */
                        if (!isnan(pts)) {
                            pthread_mutex_lock(&vp->aclock_mu);
                            vp->audio_clock_end    = pts + (double)flt->nb_samples / vp->pcm_sr;
                            vp->audio_clock_serial = pkt_serial;
                            pthread_mutex_unlock(&vp->aclock_mu);
                        }
                        ring_write(&vp->pcm, (const float*)flt->data[0], samples);
                        av_frame_unref(flt);
                    }
                }
                av_frame_unref(raw);
            }
            if (is_eof) avcodec_flush_buffers(vp->actx);
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_frame_free(&raw);
    av_frame_free(&flt);
    return NULL;
}

/* ── read / demux thread ──────────────────────────────────────────────────── */

static void* read_thread(void* arg) {
    VPEngine* vp  = arg;
    AVPacket* pkt = av_packet_alloc();
    int       eof_sent = 0;

    while (!atomic_load(&vp->quit)) {

        /* ── track change ── */
        if (atomic_load(&vp->track_req)) {
            atomic_store(&vp->track_req, 0);
            pthread_mutex_lock(&vp->cmd_mu);
            int nv = vp->req_vsi, na = vp->req_asi;
            pthread_mutex_unlock(&vp->cmd_mu);
            double pos = get_master_clock(vp);
            if (isnan(pos)) pos = vp->display_pts;

            int v_changed = (nv != vp->vsi);
            int a_changed = (na != vp->asi);
            vp->vsi = nv; vp->asi = na;

            /* signal decoders to reopen BEFORE flushing, so by the time they
               pull a new-serial packet they have already swapped codecs */
            if (v_changed && vp->has_video) atomic_store(&vp->v_reopen, 1);
            if (a_changed && vp->has_audio) atomic_store(&vp->a_reopen, 1);
            pq_flush(&vp->videoq);
            pq_flush(&vp->audioq);
            vfq_clear(&vp->pictq);
            ring_clear(&vp->pcm);

            /* refill from the current position */
            vp->seek_to = pos;
            atomic_store(&vp->seek_req, 1);
        }

        /* ── seek ── */
        if (atomic_load(&vp->seek_req)) {
            atomic_store(&vp->seek_req, 0);
            double t = vp->seek_to;
            /* frame-accurate landing only makes sense while paused (while
               playing, a keyframe seek keeps A/V locked; dropping video to an
               exact frame would leave audio running ahead). */
            int precise = atomic_load(&vp->seek_want_precise) &&
                          !atomic_load(&vp->is_playing) && vp->has_video;
            atomic_store(&vp->seek_want_precise, 0);

            int64_t ts = (int64_t)(t * AV_TIME_BASE);
            if (av_seek_frame(vp->fmt, -1, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
                if (precise) {
                    pthread_mutex_lock(&vp->drop_mu);
                    vp->drop_to = t;
                    pthread_mutex_unlock(&vp->drop_mu);
                    atomic_store(&vp->seek_drop, 1);
                } else {
                    atomic_store(&vp->seek_drop, 0);
                }
                pq_flush(&vp->videoq);     /* bumps serials -> stale data dropped */
                pq_flush(&vp->audioq);
                vfq_clear(&vp->pictq);
                ring_clear(&vp->pcm);
                pthread_mutex_lock(&vp->aclock_mu);
                vp->audio_clock_end = NAN;
                pthread_mutex_unlock(&vp->aclock_mu);
                set_ext_clock(vp, t, !atomic_load(&vp->is_playing));
                vp->display_pts = t;
            }
            atomic_store(&vp->finished, 0);
            atomic_store(&vp->eof_reached, 0);
            eof_sent = 0;
            continue;
        }

        /* ── backpressure: don't outrun the decoders ── */
        if (pq_nb(&vp->videoq) > MAX_VIDEO_PKTS || pq_nb(&vp->audioq) > MAX_AUDIO_PKTS) {
            pthread_mutex_lock(&vp->cmd_mu);
            if (!atomic_load(&vp->seek_req) && !atomic_load(&vp->track_req) &&
                !atomic_load(&vp->quit)) {
                struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 10000000L; if (ts.tv_nsec >= 1000000000L){ts.tv_sec++;ts.tv_nsec-=1000000000L;}
                pthread_cond_timedwait(&vp->cmd_cv, &vp->cmd_mu, &ts);
            }
            pthread_mutex_unlock(&vp->cmd_mu);
            continue;
        }

        int r = av_read_frame(vp->fmt, pkt);
        if (r < 0) {
            /* EOF: send one null packet per stream to drain the decoders,
               then idle until a seek/quit arrives. No busy spin, no hang. */
            if (!eof_sent) {
                if (vp->has_video) { pkt->data=NULL; pkt->size=0; pkt->stream_index=vp->vsi; pq_put(&vp->videoq, pkt); }
                if (vp->has_audio) { pkt->data=NULL; pkt->size=0; pkt->stream_index=vp->asi; pq_put(&vp->audioq, pkt); }
                eof_sent = 1;
                atomic_store(&vp->eof_reached, 1);
            }
            pthread_mutex_lock(&vp->cmd_mu);
            if (!atomic_load(&vp->seek_req) && !atomic_load(&vp->track_req) &&
                !atomic_load(&vp->quit)) {
                struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 30000000L; if (ts.tv_nsec >= 1000000000L){ts.tv_sec++;ts.tv_nsec-=1000000000L;}
                pthread_cond_timedwait(&vp->cmd_cv, &vp->cmd_mu, &ts);
            }
            pthread_mutex_unlock(&vp->cmd_mu);
            continue;
        }
        eof_sent = 0;

        if (vp->has_video && pkt->stream_index == vp->vsi)      pq_put(&vp->videoq, pkt);
        else if (vp->has_audio && pkt->stream_index == vp->asi) pq_put(&vp->audioq, pkt);
        else av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    return NULL;
}

/* ── public API ───────────────────────────────────────────────────────────── */

static void wake_read(VPEngine* vp) {
    pthread_mutex_lock(&vp->cmd_mu);
    pthread_cond_signal(&vp->cmd_cv);
    pthread_mutex_unlock(&vp->cmd_mu);
}

VPEngine* vp_engine_create(const char* path, float scale) {
    VPEngine* vp = calloc(1, sizeof(*vp));
    strncpy(vp->path, path, sizeof(vp->path) - 1);
    vp->scale = (scale > 0.0f && scale <= 1.0f) ? scale : 1.0f;
    vp->vsi = vp->asi = -1;
    vp->audio_clock_end = NAN;
    vp->audio_clock_serial = -1;
    atomic_store(&vp->rate_milli, 1000);
    atomic_store(&vp->volume_milli, 1000);
    atomic_store(&vp->present_req, PR_NORMAL);
    vp->prev_playing = 1;
    pthread_mutex_init(&vp->drop_mu, NULL);
    pthread_mutex_init(&vp->snap_mu, NULL);

    if (avformat_open_input(&vp->fmt, path, NULL, NULL) < 0)  goto fail;
    if (avformat_find_stream_info(vp->fmt, NULL) < 0)         goto fail;

    for (unsigned i = 0; i < vp->fmt->nb_streams; i++) {
        enum AVMediaType t = vp->fmt->streams[i]->codecpar->codec_type;
        if (t == AVMEDIA_TYPE_VIDEO && vp->vsi < 0) vp->vsi = (int)i;
        if (t == AVMEDIA_TYPE_AUDIO && vp->asi < 0) vp->asi = (int)i;
    }
    vp->has_video = (vp->vsi >= 0);
    vp->has_audio = (vp->asi >= 0);
    vp->duration  = (double)vp->fmt->duration / AV_TIME_BASE;

    /* frame rate: prefer avg_frame_rate, fall back to r_frame_rate, then 25 */
    vp->fps = 25.0;
    if (vp->has_video) {
        AVStream* vs = vp->fmt->streams[vp->vsi];
        double f = av_q2d(vs->avg_frame_rate);
        if (!(f > 0.0)) f = av_q2d(vs->r_frame_rate);
        if (f > 0.0) vp->fps = f;
        if (vs->nb_frames > 0)            vp->frame_count = vs->nb_frames;
        else if (vp->duration > 0.0)      vp->frame_count = (int64_t)(vp->duration * vp->fps + 0.5);
    }

    pthread_mutex_init(&vp->aclock_mu, NULL);
    pthread_mutex_init(&vp->eclock_mu, NULL);
    pthread_mutex_init(&vp->cmd_mu, NULL);
    pthread_mutex_init(&vp->fstr_mu, NULL);
    pthread_cond_init(&vp->cmd_cv, NULL);
    pq_init(&vp->videoq);
    pq_init(&vp->audioq);
    vfq_init(&vp->pictq);

    if (vp->has_video && open_video_codec(vp) < 0) goto fail;

    /* audio device — fixed format the graph resamples to */
    if (vp->has_audio) {
        if (open_audio_codec(vp) < 0) goto fail;
        vp->pcm_ch     = vp->actx->ch_layout.nb_channels;
        vp->pcm_sr     = vp->actx->sample_rate;
        vp->dev_period = 512;

        ring_init(&vp->pcm, vp->pcm_sr * vp->pcm_ch * RING_SECONDS);

        ma_device_config cfg   = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format    = ma_format_f32;
        cfg.playback.channels  = (ma_uint32)vp->pcm_ch;
        cfg.sampleRate         = (ma_uint32)vp->pcm_sr;
        cfg.dataCallback       = ma_data_cb;
        cfg.pUserData          = vp;
        cfg.periodSizeInFrames = (ma_uint32)vp->dev_period;
        if (ma_device_init(NULL, &cfg, &vp->audio_dev) == MA_SUCCESS) {
            ma_device_start(&vp->audio_dev);
            vp->audio_ok = true;
        }
    } else {
        /* dummy ring so ring_* calls are always valid */
        vp->pcm_ch = 2; vp->pcm_sr = 48000; vp->dev_period = 512;
        ring_init(&vp->pcm, 1024);
    }

    if (vp->has_video) build_vf(vp);
    if (vp->has_audio) build_af(vp);

    set_ext_clock(vp, 0.0, 0);
    atomic_store(&vp->is_playing, 1);

    pthread_create(&vp->read_thr, NULL, read_thread, vp);
    if (vp->has_video) pthread_create(&vp->video_thr, NULL, video_thread, vp);
    if (vp->has_audio) pthread_create(&vp->audio_thr, NULL, audio_thread, vp);
    vp->threads_up = 1;
    return vp;

fail:
    if (vp->fmt) avformat_close_input(&vp->fmt);
    free(vp);
    return NULL;
}

void vp_engine_destroy(VPEngine* vp) {
    if (!vp) return;

    /* 1. tell everyone to quit, 2. break every blocking wait, 3. join. */
    atomic_store(&vp->quit, 1);
    pq_abort(&vp->videoq);
    pq_abort(&vp->audioq);
    vfq_abort(&vp->pictq);
    ring_abort(&vp->pcm);
    wake_read(vp);

    if (vp->threads_up) {
        pthread_join(vp->read_thr, NULL);
        if (vp->has_video) pthread_join(vp->video_thr, NULL);
        if (vp->has_audio) pthread_join(vp->audio_thr, NULL);
    }
    if (vp->audio_ok) { ma_device_stop(&vp->audio_dev); ma_device_uninit(&vp->audio_dev); }

    avfilter_graph_free(&vp->vfg);
    avfilter_graph_free(&vp->afg);
    avcodec_free_context(&vp->vctx);
    avcodec_free_context(&vp->actx);
    avformat_close_input(&vp->fmt);
    sws_freeContext(vp->sws);
    if (vp->tex) glDeleteTextures(1, &vp->tex);

    pq_destroy(&vp->videoq);
    pq_destroy(&vp->audioq);
    vfq_destroy(&vp->pictq);
    ring_free(&vp->pcm);

    pthread_mutex_destroy(&vp->aclock_mu);
    pthread_mutex_destroy(&vp->eclock_mu);
    pthread_mutex_destroy(&vp->cmd_mu);
    pthread_mutex_destroy(&vp->fstr_mu);
    pthread_mutex_destroy(&vp->drop_mu);
    pthread_mutex_destroy(&vp->snap_mu);
    pthread_cond_destroy(&vp->cmd_cv);
    free(vp->snap_rgb);
    free(vp);
}

unsigned int vp_engine_texture(VPEngine* vp, int* w, int* h) {
    if (w) *w = vp->tex_w;
    if (h) *h = vp->tex_h;
    return vp->tex;
}

void vp_engine_set_vf(VPEngine* vp, const char* vf) {
    pthread_mutex_lock(&vp->fstr_mu);
    strncpy(vp->cur_vf, vf ? vf : "", sizeof(vp->cur_vf) - 1);
    vp->cur_vf[sizeof(vp->cur_vf)-1] = 0;
    pthread_mutex_unlock(&vp->fstr_mu);
    atomic_store(&vp->vf_rebuild, 1);
}

void vp_engine_set_af(VPEngine* vp, const char* af) {
    pthread_mutex_lock(&vp->fstr_mu);
    strncpy(vp->cur_af, af ? af : "", sizeof(vp->cur_af) - 1);
    vp->cur_af[sizeof(vp->cur_af)-1] = 0;
    pthread_mutex_unlock(&vp->fstr_mu);
    /* clear stale audio so the new filter doesn't play after queued PCM */
    ring_clear(&vp->pcm);
    atomic_store(&vp->af_rebuild, 1);
}

void vp_engine_set_video_track(VPEngine* vp, int idx) {
    pthread_mutex_lock(&vp->cmd_mu);
    vp->req_vsi = idx;
    vp->req_asi = vp->asi;
    pthread_mutex_unlock(&vp->cmd_mu);
    atomic_store(&vp->track_req, 1);
    wake_read(vp);
}

void vp_engine_set_audio_track(VPEngine* vp, int idx) {
    pthread_mutex_lock(&vp->cmd_mu);
    vp->req_vsi = vp->vsi;
    vp->req_asi = idx;
    pthread_mutex_unlock(&vp->cmd_mu);
    atomic_store(&vp->track_req, 1);
    wake_read(vp);
}

void vp_engine_set_scale(VPEngine* vp, float scale) {
    vp->scale = (scale > 0.0f && scale <= 1.0f) ? scale : 1.0f;
    if (vp->vctx) {
        vp->out_w = (int)(vp->vctx->width  * vp->scale) & ~1;
        vp->out_h = (int)(vp->vctx->height * vp->scale) & ~1;
        if (vp->out_w < 2) vp->out_w = 2;
        if (vp->out_h < 2) vp->out_h = 2;
    }
    atomic_store(&vp->vf_rebuild, 1);
}

int vp_engine_video_tracks(VPEngine* vp, VPStreamInfo* out, int max) {
    int n = 0;
    for (unsigned i = 0; i < vp->fmt->nb_streams && n < max; i++) {
        AVStream* s = vp->fmt->streams[i];
        if (s->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        out[n].index = (int)i;
        AVDictionaryEntry* la = av_dict_get(s->metadata, "language", NULL, 0);
        AVDictionaryEntry* ti = av_dict_get(s->metadata, "title",    NULL, 0);
        strncpy(out[n].lang,  la ? la->value : "und", sizeof(out[n].lang)  - 1);
        strncpy(out[n].title, ti ? ti->value : "",    sizeof(out[n].title) - 1);
        n++;
    }
    return n;
}

int vp_engine_audio_tracks(VPEngine* vp, VPStreamInfo* out, int max) {
    int n = 0;
    for (unsigned i = 0; i < vp->fmt->nb_streams && n < max; i++) {
        AVStream* s = vp->fmt->streams[i];
        if (s->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        out[n].index = (int)i;
        AVDictionaryEntry* la = av_dict_get(s->metadata, "language", NULL, 0);
        AVDictionaryEntry* ti = av_dict_get(s->metadata, "title",    NULL, 0);
        strncpy(out[n].lang,  la ? la->value : "und", sizeof(out[n].lang)  - 1);
        strncpy(out[n].title, ti ? ti->value : "",    sizeof(out[n].title) - 1);
        n++;
    }
    return n;
}

/* ── internal seek request (collapses repeated drag ticks into one) ────────── */

static void request_seek(VPEngine* vp, double t, int precise) {
    if (t < 0) t = 0;
    if (vp->duration > 0 && t > vp->duration) t = vp->duration;
    pthread_mutex_lock(&vp->cmd_mu);
    vp->seek_to = t;
    atomic_store(&vp->seek_want_precise, precise ? 1 : 0);
    atomic_store(&vp->seek_req, 1);
    atomic_store(&vp->seek_pending, 1);
    pthread_cond_signal(&vp->cmd_cv);
    pthread_mutex_unlock(&vp->cmd_mu);
}

static void fire(VPEngine* vp, VPEvent ev) { if (vp->cb) vp->cb(vp->cb_user, ev); }

/* ── present the due video frame (GL thread) ──────────────────────────────── */

static void upload_rgb(VPEngine* vp, AVFrame* f) {
    int w = f->width, h = f->height;
    if (!vp->tex) {
        glGenTextures(1, &vp->tex);
        glBindTexture(GL_TEXTURE_2D, vp->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        vp->tex_w = vp->tex_h = 0;
    }
    glBindTexture(GL_TEXTURE_2D, vp->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* buffersink/filter rows are padded — tell GL the real row stride. */
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->linesize[0] / 3);
    if (w != vp->tex_w || h != vp->tex_h) {
        vp->tex_w = w; vp->tex_h = h;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, f->data[0]);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, f->data[0]);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

/* keep a tightly-packed CPU copy of the displayed frame for snapshots */
static void keep_snapshot(VPEngine* vp, AVFrame* f) {
    int w = f->width, h = f->height;
    pthread_mutex_lock(&vp->snap_mu);
    if (vp->snap_w != w || vp->snap_h != h) {
        free(vp->snap_rgb);
        vp->snap_rgb = malloc((size_t)w * h * 3);
        vp->snap_w = vp->snap_rgb ? w : 0;
        vp->snap_h = vp->snap_rgb ? h : 0;
    }
    if (vp->snap_rgb) {
        for (int y = 0; y < h; y++)
            memcpy(vp->snap_rgb + (size_t)y * w * 3,
                   f->data[0] + (size_t)y * f->linesize[0], (size_t)w * 3);
    }
    pthread_mutex_unlock(&vp->snap_mu);
}

/* Present according to present_req:
     PR_NORMAL  → frames due vs master clock (drop older due, keep newest due);
                  if the clock isn't ready yet, behaves like PR_NEWEST.
     PR_NEWEST  → free-run: show the newest decoded current-serial frame (scrub).
     PR_OLDEST  → show exactly the oldest current-serial frame (seek/step target),
                  then revert to PR_NORMAL.
   Returns 1 if a frame was uploaded this call. */
static int present_due_frame(VPEngine* vp) {
    int req = atomic_load(&vp->present_req);
    double master = get_master_clock(vp);
    int newest = (req == PR_NEWEST) || (req == PR_NORMAL && isnan(master));
    int oldest = (req == PR_OLDEST);

    AVFrame* chosen = NULL;
    double   chosen_pts = NAN;

    pthread_mutex_lock(&vp->pictq.mu);
    int cur_serial = pq_serial(&vp->videoq);
    for (;;) {
        if (vp->pictq.n == 0) break;
        int idx = vp->pictq.r;
        if (vp->pictq.serial[idx] != cur_serial) {        /* drop stale generation */
            av_frame_free(&vp->pictq.f[idx]);
            vp->pictq.r = (vp->pictq.r + 1) % VFQ_CAP; vp->pictq.n--;
            pthread_cond_signal(&vp->pictq.cv);
            continue;
        }
        double pts = vp->pictq.pts[idx];
        if (!oldest && !newest) {
            int due = isnan(pts) || pts <= master;
            if (!due) break;
        }
        AVFrame* f = vp->pictq.f[idx];
        vp->pictq.r = (vp->pictq.r + 1) % VFQ_CAP; vp->pictq.n--;
        pthread_cond_signal(&vp->pictq.cv);

        if (chosen) av_frame_free(&chosen);   /* skip the older frame */
        chosen = f; chosen_pts = pts;
        if (oldest) break;                    /* exactly one frame */
    }
    pthread_mutex_unlock(&vp->pictq.mu);

    if (chosen) {
        if (!isnan(chosen_pts)) vp->display_pts = chosen_pts;
        upload_rgb(vp, chosen);
        keep_snapshot(vp, chosen);
        av_frame_free(&chosen);
        if (oldest) atomic_store(&vp->present_req, PR_NORMAL);  /* consumed */
        return 1;
    }
    return 0;
}

/* ── per-frame update (GL thread) ─────────────────────────────────────────── */

void vp_engine_update(VPEngine* vp) {
    int shown = present_due_frame(vp);

    if (shown && atomic_load(&vp->seek_pending)) {
        atomic_store(&vp->seek_pending, 0);
        fire(vp, VP_EVENT_SEEKED);
    }

    /* loop / in-out region */
    if (atomic_load(&vp->loop_on) && atomic_load(&vp->is_playing)) {
        double pos = vp_engine_position(vp);
        pthread_mutex_lock(&vp->cmd_mu);
        double in = vp->loop_in, out = vp->loop_out;
        pthread_mutex_unlock(&vp->cmd_mu);
        if (out > in && pos >= out) { request_seek(vp, in, 0); wake_read(vp); }
    }

    /* end-of-stream: demuxer hit EOF and everything has been shown/heard */
    if (atomic_load(&vp->is_playing) && vp->pictq.n == 0 &&
        atomic_load(&vp->pcm.count) == 0 && atomic_load(&vp->eof_reached)) {
        atomic_store(&vp->is_playing, 0);
        atomic_store(&vp->finished, 1);
        set_ext_clock(vp, (vp->duration > 0 ? vp->duration : vp->display_pts), 1);
        fire(vp, VP_EVENT_ENDED);
    }

    int pl = atomic_load(&vp->is_playing);
    if (pl != vp->prev_playing) { vp->prev_playing = pl; fire(vp, VP_EVENT_STATE_CHANGED); }
}

/* ── transport ────────────────────────────────────────────────────────────── */

void vp_engine_play(VPEngine* vp) {
    if (atomic_load(&vp->is_playing)) return;
    if (atomic_load(&vp->finished)) {           /* restart from the beginning */
        atomic_store(&vp->finished, 0);
        request_seek(vp, 0.0, 0);
    }
    set_ext_clock(vp, get_ext_clock(vp), 0);
    atomic_store(&vp->present_req, PR_NORMAL);
    atomic_store(&vp->is_playing, 1);
    wake_read(vp);
}

void vp_engine_pause(VPEngine* vp) {
    if (!atomic_load(&vp->is_playing)) return;
    atomic_store(&vp->is_playing, 0);
    set_ext_clock(vp, get_ext_clock(vp), 1);
}

void vp_engine_toggle(VPEngine* vp) {
    if (atomic_load(&vp->is_playing)) vp_engine_pause(vp); else vp_engine_play(vp);
}

bool vp_engine_is_playing(VPEngine* vp) { return atomic_load(&vp->is_playing) != 0; }

/* ── seeking & stepping ───────────────────────────────────────────────────── */

void vp_engine_seek(VPEngine* vp, double seconds, bool precise) {
    int paused = !atomic_load(&vp->is_playing);
    int p = (precise && paused) ? 1 : 0;
    /* Don't fight an in-progress scrub (it owns PR_NEWEST). Otherwise: paused
       lands on one frame (target if precise, else keyframe); playing stays
       NORMAL and the clock-not-ready path shows a frame immediately. */
    if (!atomic_load(&vp->is_seeking))
        atomic_store(&vp->present_req, paused ? PR_OLDEST : PR_NORMAL);
    request_seek(vp, seconds, p);
    wake_read(vp);
}

void vp_engine_seek_frame(VPEngine* vp, int64_t frame) {
    if (frame < 0) frame = 0;
    if (vp->frame_count > 0 && frame >= vp->frame_count) frame = vp->frame_count - 1;
    vp_engine_pause(vp);
    double t = (vp->fps > 0.0) ? (double)frame / vp->fps : 0.0;
    vp_engine_seek(vp, t, true);
}

void vp_engine_step(VPEngine* vp, int delta) {
    vp_engine_pause(vp);
    int64_t cur = vp_engine_frame_index(vp);
    int64_t tgt = cur + delta;
    if (tgt < 0) tgt = 0;
    vp_engine_seek_frame(vp, tgt);
}

void vp_engine_scrub_begin(VPEngine* vp) {
    atomic_store(&vp->is_seeking, 1);
    atomic_store(&vp->present_req, PR_NEWEST);
}

void vp_engine_scrub_end(VPEngine* vp) {
    atomic_store(&vp->is_seeking, 0);
    atomic_store(&vp->present_req,
                 atomic_load(&vp->is_playing) ? PR_NORMAL : PR_OLDEST);
    set_ext_clock(vp, vp->display_pts, !atomic_load(&vp->is_playing));
}

/* ── rate / volume ────────────────────────────────────────────────────────── */

void vp_engine_set_rate(VPEngine* vp, double rate) {
    rate = clampd(rate, 0.25, 4.0);
    double pos = get_master_clock(vp);
    atomic_store(&vp->rate_milli, (int)(rate * 1000.0 + 0.5));
    if (!isnan(pos)) set_ext_clock(vp, pos, !atomic_load(&vp->is_playing));
    if (vp->has_audio) {
        ring_clear(&vp->pcm);               /* drop PCM rendered at the old tempo */
        atomic_store(&vp->af_rebuild, 1);   /* rebuild audio graph with new atempo */
    }
}

double vp_engine_get_rate(VPEngine* vp) { return atomic_load(&vp->rate_milli) / 1000.0; }

void vp_engine_set_volume(VPEngine* vp, float v) {
    if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
    atomic_store(&vp->volume_milli, (int)(v * 1000.0f + 0.5f));
}

float vp_engine_get_volume(VPEngine* vp) { return atomic_load(&vp->volume_milli) / 1000.0f; }
void  vp_engine_set_muted(VPEngine* vp, bool m) { atomic_store(&vp->muted, m ? 1 : 0); }
bool  vp_engine_is_muted(VPEngine* vp) { return atomic_load(&vp->muted) != 0; }

/* ── loop region ──────────────────────────────────────────────────────────── */

void vp_engine_set_loop(VPEngine* vp, double in_sec, double out_sec, bool enabled) {
    pthread_mutex_lock(&vp->cmd_mu);
    vp->loop_in = in_sec; vp->loop_out = out_sec;
    pthread_mutex_unlock(&vp->cmd_mu);
    atomic_store(&vp->loop_on, enabled ? 1 : 0);
}

void vp_engine_clear_loop(VPEngine* vp) { atomic_store(&vp->loop_on, 0); }

/* ── queries ──────────────────────────────────────────────────────────────── */

double vp_engine_position(VPEngine* vp) {
    double m = get_master_clock(vp);
    if (isnan(m)) m = vp->display_pts;
    return m < 0 ? 0 : m;
}

double  vp_engine_duration(VPEngine* vp) { return vp->duration; }
double  vp_engine_fps(VPEngine* vp)      { return vp->fps; }
int64_t vp_engine_frame_count(VPEngine* vp) { return vp->frame_count; }
bool    vp_engine_ended(VPEngine* vp)    { return atomic_load(&vp->finished) != 0; }

int64_t vp_engine_frame_index(VPEngine* vp) {
    return (vp->fps > 0.0) ? (int64_t)(vp_engine_position(vp) * vp->fps + 0.5) : 0;
}

void vp_engine_get_info(VPEngine* vp, VPMediaInfo* o) {
    memset(o, 0, sizeof(*o));
    o->has_video = vp->has_video;
    o->has_audio = vp->has_audio;
    if (vp->has_video && vp->vctx) { o->src_width = vp->vctx->width; o->src_height = vp->vctx->height; }
    o->width = vp->tex_w; o->height = vp->tex_h;
    o->fps = vp->fps; o->duration = vp->duration; o->frame_count = vp->frame_count;
    if (vp->has_audio) { o->sample_rate = vp->pcm_sr; o->channels = vp->pcm_ch; }
}

/* ── snapshot ─────────────────────────────────────────────────────────────── */

bool vp_engine_snapshot(VPEngine* vp, VPFrameRGB* out) {
    pthread_mutex_lock(&vp->snap_mu);
    if (!vp->snap_rgb || vp->snap_w <= 0 || vp->snap_h <= 0) {
        pthread_mutex_unlock(&vp->snap_mu);
        return false;
    }
    int w = vp->snap_w, h = vp->snap_h;
    out->width = w; out->height = h; out->stride = w * 3;
    out->pixels = malloc((size_t)w * h * 3);
    if (out->pixels) memcpy(out->pixels, vp->snap_rgb, (size_t)w * h * 3);
    pthread_mutex_unlock(&vp->snap_mu);
    return out->pixels != NULL;
}

void vp_engine_frame_free(VPFrameRGB* f) {
    if (f) { free(f->pixels); f->pixels = NULL; f->width = f->height = f->stride = 0; }
}

/* ── events ───────────────────────────────────────────────────────────────── */

void vp_engine_set_callback(VPEngine* vp, VPEventCb cb, void* user) {
    vp->cb = cb; vp->cb_user = user;
}
