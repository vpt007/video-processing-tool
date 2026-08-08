/* ve_exec.c — execute a VePlan with libav (no subprocess).
 *
 * Two paths:
 *   • remux_lossless()  — plan->lossless: copy packets, edit only the container
 *                         (drop streams, set SAR, rotate via display matrix,
 *                         keyframe-aligned trim, metadata). No decode/encode.
 *   • transcode()       — decode → filter (plan->vf / plan->af) → encode, but
 *                         only for the stream(s) the planner marked TRANSCODE;
 *                         the other stream is still copied.
 *
 * Build (needs FFmpeg >= 7.0 dev headers; built/tested against 8.1 "Hoare"):
 *   cc -DVE_WITH_LIBAV -c ve_exec.c
 *   cc ... ve_exec.o ve_dsl.o ve_plan.o ve_ops.o \
 *      -lavformat -lavcodec -lavfilter -lavutil -lswscale -lswresample
 *
 * NOTE: both paths are implemented. Encoder defaults are H.264 for
 * video and AAC for audio; formats are pinned (yuv420p / fltp) via
 * an appended format/aformat filter so no deprecated AVCodec.pix_fmts lookups
 * are used. ve_exec.c is compiled out unless VE_WITH_LIBAV is defined, so the
 * rest of the tool builds with no dependencies.
 */
#include "ve.h"
#include <stdio.h>

#ifdef VE_WITH_LIBAV
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/display.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/mem.h>
#include <string.h>
#include <math.h>

#if LIBAVFORMAT_VERSION_MAJOR < 61
#error "ve_exec.c targets FFmpeg >= 7.0 (libavformat >= 61); built and tested on 8.1 'Hoare'."
#endif

#define ERR(...) do { snprintf(err, errlen, __VA_ARGS__); goto fail; } while (0)

static int emit_progress(VeProgressCb cb, void* user, double cur, double total, int* last) {
    if (!cb) return 0;
    int key = (total > 0) ? (int)(cur / total * 100.0) : (int)cur;
    if (key == *last) return 0;
    *last = key;
    double frac = (total > 0) ? cur / total : -1.0;
    if (frac > 1.0) frac = 1.0;
    return cb(user, frac, cur, total) != 0;
}

static bool keep_stream(const VePlan* p, AVStream* st) {
    enum AVMediaType t = st->codecpar->codec_type;
    int is_cover = (st->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;

    if (t == AVMEDIA_TYPE_VIDEO) {
        if (is_cover && p->remove_thumbnail) return false;
        return true;
    }
    if (t == AVMEDIA_TYPE_AUDIO) {
        if (p->drop_all_audio) return false;
        for (int i = 0; i < p->n_drop_audio; i++)
            if (p->drop_audio_index[i] == st->index) return false;
        return true;
    }
    if (t == AVMEDIA_TYPE_SUBTITLE) {
        for (int i = 0; i < p->n_drop_sub; i++)
            if (p->drop_sub_index[i] == st->index) return false;
        return true;
    }
    return true;
}

static int remux_lossless(const VePlan* p, VeProgressCb cb, void* user, char* err, int errlen) {
    AVFormatContext* in = NULL, *out = NULL;
    int* map = NULL;
    int rc = -1;

    AVFormatContext* xin[17];
    int xmap[17];
    int nxin = 0;

    if (p->rotate_quadrant != 0)
        ERR("remux_lossless: rotate_quadrant=%d but plan->lossless is set; "
            "the planner must mark the video stream VE_TRANSCODE for rotation",
            p->rotate_quadrant);
    if (p->sar_num > 0 && p->sar_den > 0)
        ERR("remux_lossless: sar %d/%d requested but plan->lossless is set; "
            "the planner must mark the video stream VE_TRANSCODE for SAR changes",
            p->sar_num, p->sar_den);

    if (avformat_open_input(&in, p->input, NULL, NULL) < 0) ERR("cannot open %s", p->input);
    if (avformat_find_stream_info(in, NULL) < 0)            ERR("no stream info");

    double total = (in->duration > 0) ? (double)in->duration / AV_TIME_BASE : 0.0;
    int last_pct = -2;
    int cancelled = 0;

    const char* fmt = p->format[0] ? p->format : NULL;
    if (avformat_alloc_output_context2(&out, NULL, fmt, p->output) < 0 || !out)
        ERR("cannot create output %s", p->output);

    map = av_malloc_array(in->nb_streams, sizeof(int));
    if (!map) ERR("oom");

    if (p->thumbnail[0]) {
        AVFormatContext* ax = NULL;
        if (avformat_open_input(&ax, p->thumbnail, NULL, NULL) < 0) ERR("cannot open %s", p->thumbnail);
        if (avformat_find_stream_info(ax, NULL) < 0) { avformat_close_input(&ax); ERR("no stream info %s", p->thumbnail); }
        for (unsigned j = 0; j < ax->nb_streams; j++) {
            if (ax->streams[j]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
            AVStream* ost = avformat_new_stream(out, NULL);
            if (!ost) { avformat_close_input(&ax); ERR("new stream"); }
            if (avcodec_parameters_copy(ost->codecpar, ax->streams[j]->codecpar) < 0) { avformat_close_input(&ax); ERR("param copy"); }
            ost->codecpar->codec_tag = 0;
            ost->time_base = ax->streams[j]->time_base;
            ost->disposition = AV_DISPOSITION_ATTACHED_PIC;
            xin[nxin] = ax;
            xmap[nxin] = out->nb_streams - 1;
            nxin++;
            break;
        }
    }

    for (unsigned i = 0; i < in->nb_streams; i++) {
        AVStream* ist = in->streams[i];
        map[i] = -1;
        if (!keep_stream(p, ist)) continue;
        AVStream* ost = avformat_new_stream(out, NULL);
        if (!ost) ERR("new stream");
        if (avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0) ERR("param copy");
        ost->codecpar->codec_tag = 0;
        ost->time_base = ist->time_base;
        map[i] = out->nb_streams - 1;
    }

    for (int i = 0; i < p->n_add_audio; i++) {
        AVFormatContext* ax = NULL;
        if (avformat_open_input(&ax, p->add_audio[i], NULL, NULL) < 0) ERR("cannot open %s", p->add_audio[i]);
        if (avformat_find_stream_info(ax, NULL) < 0) { avformat_close_input(&ax); ERR("no stream info %s", p->add_audio[i]); }
        for (unsigned j = 0; j < ax->nb_streams; j++) {
            if (ax->streams[j]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
            AVStream* ost = avformat_new_stream(out, NULL);
            if (!ost) { avformat_close_input(&ax); ERR("new stream"); }
            if (avcodec_parameters_copy(ost->codecpar, ax->streams[j]->codecpar) < 0) { avformat_close_input(&ax); ERR("param copy"); }
            ost->codecpar->codec_tag = 0;
            ost->time_base = ax->streams[j]->time_base;
            if (p->add_audio_lang[i][0])
                av_dict_set(&ost->metadata, "language", p->add_audio_lang[i], 0);
            xin[nxin] = ax;
            xmap[nxin] = out->nb_streams - 1;
            nxin++;
            break;
        }
    }

    for (int i = 0; i < p->n_add_sub; i++) {
        AVFormatContext* ax = NULL;
        if (avformat_open_input(&ax, p->add_sub[i], NULL, NULL) < 0) ERR("cannot open %s", p->add_sub[i]);
        if (avformat_find_stream_info(ax, NULL) < 0) { avformat_close_input(&ax); ERR("no stream info %s", p->add_sub[i]); }
        for (unsigned j = 0; j < ax->nb_streams; j++) {
            if (ax->streams[j]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) continue;
            AVStream* ost = avformat_new_stream(out, NULL);
            if (!ost) { avformat_close_input(&ax); ERR("new stream"); }
            if (avcodec_parameters_copy(ost->codecpar, ax->streams[j]->codecpar) < 0) { avformat_close_input(&ax); ERR("param copy"); }
            ost->codecpar->codec_tag = 0;
            ost->time_base = ax->streams[j]->time_base;
            if (p->add_sub_lang[i][0])
                av_dict_set(&ost->metadata, "language", p->add_sub_lang[i], 0);
            xin[nxin] = ax;
            xmap[nxin] = out->nb_streams - 1;
            nxin++;
            break;
        }
    }

    for (int i = 0; i < p->n_meta; i++) {
        char* eq = strchr(p->meta[i], '=');
        if (!eq) continue;
        *eq = 0;
        av_dict_set(&out->metadata, p->meta[i], eq + 1, 0);
        *eq = '=';
    }

    if (!(out->oformat->flags & AVFMT_NOFILE))
        if (avio_open(&out->pb, p->output, AVIO_FLAG_WRITE) < 0) ERR("cannot write %s", p->output);
    if (avformat_write_header(out, NULL) < 0) ERR("write header");

    for (int xi = 0; xi < nxin; xi++) {
        AVPacket* xpkt = av_packet_alloc();
        if (!xpkt) continue;
        while (av_read_frame(xin[xi], xpkt) >= 0) {
            AVStream* xs = xin[xi]->streams[xpkt->stream_index];
            enum AVMediaType mt = xs->codecpar->codec_type;
            if (mt != AVMEDIA_TYPE_AUDIO && mt != AVMEDIA_TYPE_SUBTITLE && mt != AVMEDIA_TYPE_VIDEO) {
                av_packet_unref(xpkt); continue;
            }
            AVStream* ost = out->streams[xmap[xi]];
            av_packet_rescale_ts(xpkt, xs->time_base, ost->time_base);
            xpkt->stream_index = xmap[xi];
            xpkt->pos = -1;
            av_interleaved_write_frame(out, xpkt);
        }
        av_packet_free(&xpkt);
    }

    int64_t start_ts = p->has_trim ? (int64_t)(p->trim_start * AV_TIME_BASE) : 0;
    if (p->has_trim && start_ts > 0)
        av_seek_frame(in, -1, start_ts, AVSEEK_FLAG_BACKWARD);

    int64_t* off = av_calloc(in->nb_streams, sizeof(int64_t));
    bool* started = av_calloc(in->nb_streams, sizeof(bool));
    if (!off || !started) { av_freep(&off); av_freep(&started); ERR("oom"); }

    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(in, pkt) >= 0) {
        unsigned i = pkt->stream_index;
        if (i >= in->nb_streams || map[i] < 0) { av_packet_unref(pkt); continue; }
        AVStream* ist = in->streams[i];
        AVStream* ost = out->streams[map[i]];

        double tsec = (pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts) * av_q2d(ist->time_base);
        if (p->has_trim && p->trim_end > 0 && tsec > p->trim_end) { av_packet_unref(pkt); break; }
        if (tsec > 0 && emit_progress(cb, user, tsec, total, &last_pct)) {
            av_packet_unref(pkt); cancelled = 1; snprintf(err, errlen, "cancelled by caller"); break;
        }

        if (!started[i]) {
            int64_t base = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
            off[i] = base; started[i] = true;
        }
        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= off[i];
        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= off[i];
        av_packet_rescale_ts(pkt, ist->time_base, ost->time_base);
        pkt->stream_index = map[i];
        pkt->pos = -1;

        if (av_interleaved_write_frame(out, pkt) < 0) { av_packet_unref(pkt); break; }
    }
    av_packet_free(&pkt);
    av_freep(&off); av_freep(&started);

    if (cancelled) { rc = VE_ECANCELED; goto fail; }
    av_write_trailer(out);
    if (cb) cb(user, total > 0 ? 1.0 : -1.0, total, total);
    rc = 0;

fail:
    if (map) av_freep(&map);
    for (int xi = 0; xi < nxin; xi++) avformat_close_input(&xin[xi]);
    if (in)  avformat_close_input(&in);
    if (out) {
        if (out->pb && !(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
        avformat_free_context(out);
    }
    if (cancelled) remove(p->output);
    return rc;
}

typedef enum { S_DROP, S_COPY, S_XCODE } SMode;

typedef struct {
    SMode            mode;
    int              out_index;
    int              is_video;
    AVStream*        ist;
    AVStream*        ost;
    AVCodecContext*  dec;
    AVCodecContext*  enc;
    AVFilterGraph*   graph;
    AVFilterContext* src;
    AVFilterContext* sink;
    int64_t          a_pts;
    bool             needs_flush;
    bool             graph_done;
    int64_t          copy_pts_off;
    bool             copy_started;
} StreamCtx;

static int open_decoder(StreamCtx* sc, char* err, int errlen) {
    const AVCodec* dec = avcodec_find_decoder(sc->ist->codecpar->codec_id);
    if (!dec) { snprintf(err, errlen, "no decoder for stream %d", sc->ist->index); return -1; }
    sc->dec = avcodec_alloc_context3(dec);
    if (!sc->dec) { snprintf(err, errlen, "decoder alloc"); return -1; }
    if (avcodec_parameters_to_context(sc->dec, sc->ist->codecpar) < 0) { snprintf(err,errlen,"dec params"); return -1; }
    sc->dec->pkt_timebase = sc->ist->time_base;
    if (avcodec_open2(sc->dec, dec, NULL) < 0) { snprintf(err,errlen,"open decoder"); return -1; }
    return 0;
}

static int make_graph(StreamCtx* sc, const char* srcname, const char* srcargs,
                      const char* chain, char* err, int errlen) {
    sc->graph = avfilter_graph_alloc();
    if (!sc->graph) { snprintf(err,errlen,"graph alloc"); return -1; }
    const AVFilter* srcf  = avfilter_get_by_name(srcname);
    const AVFilter* sinkf = avfilter_get_by_name(sc->is_video ? "buffersink" : "abuffersink");
    if (avfilter_graph_create_filter(&sc->src,  srcf,  "in",  srcargs, NULL, sc->graph) < 0) { snprintf(err,errlen,"buffersrc");  return -1; }
    if (avfilter_graph_create_filter(&sc->sink, sinkf, "out", NULL,    NULL, sc->graph) < 0) { snprintf(err,errlen,"buffersink"); return -1; }

    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs  = avfilter_inout_alloc();
    if (!outputs || !inputs) { avfilter_inout_free(&outputs); avfilter_inout_free(&inputs); snprintf(err,errlen,"inout alloc"); return -1; }
    outputs->name = av_strdup("in");  outputs->filter_ctx = sc->src;  outputs->pad_idx = 0; outputs->next = NULL;
    inputs->name  = av_strdup("out"); inputs->filter_ctx  = sc->sink; inputs->pad_idx  = 0; inputs->next  = NULL;
    int r = avfilter_graph_parse_ptr(sc->graph, chain, &inputs, &outputs, NULL);
    avfilter_inout_free(&inputs); avfilter_inout_free(&outputs);
    if (r < 0) { snprintf(err,errlen,"filter parse '%s'", chain); return -1; }
    if (avfilter_graph_config(sc->graph, NULL) < 0) { snprintf(err,errlen,"graph config '%s'", chain); return -1; }
    return 0;
}

static void trim_prefix(const VePlan* p, int audio, char* buf, size_t cap) {
    buf[0] = 0;
    if (!p->has_trim) return;
    const char* tf = audio ? "atrim"   : "trim";
    const char* sf = audio ? "asetpts" : "setpts";
    if (p->trim_end > 0)
        snprintf(buf, cap, "%s=start=%.6f:end=%.6f,%s=PTS-STARTPTS,",
                 tf, p->trim_start, p->trim_end, sf);
    else
        snprintf(buf, cap, "%s=start=%.6f,%s=PTS-STARTPTS,",
                 tf, p->trim_start, sf);
}

static int setup_video_xcode(StreamCtx* sc, AVFormatContext* out, const VePlan* p,
                             char* err, int errlen) {
    sc->is_video = 1;
    if (open_decoder(sc, err, errlen) < 0) return -1;

    enum AVPixelFormat in_pix_fmt = sc->dec->pix_fmt;
    if (in_pix_fmt == AV_PIX_FMT_NONE) in_pix_fmt = AV_PIX_FMT_YUV420P;

    enum AVColorSpace   csp   = sc->dec->colorspace;
    enum AVColorRange   range = sc->dec->color_range;
    if (csp   == AVCOL_SPC_UNSPECIFIED)  csp   = AVCOL_SPC_BT709;
    if (range == AVCOL_RANGE_UNSPECIFIED) range = AVCOL_RANGE_MPEG;

    AVRational tb  = sc->ist->time_base;
    AVRational sar = sc->dec->sample_aspect_ratio;
    if (sar.num <= 0 || sar.den <= 0) sar = (AVRational){1,1};

    char args[384];
    snprintf(args, sizeof(args),
        "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d"
        ":pixel_aspect=%d/%d:colorspace=%d:range=%d",
        sc->dec->width, sc->dec->height, (int)in_pix_fmt,
        tb.num, tb.den, sar.num, sar.den,
        (int)csp, (int)range);

    char rot[64] = "";
    if      (p->rotate_quadrant == 1) snprintf(rot, sizeof(rot), "transpose=1,");
    else if (p->rotate_quadrant == 2) snprintf(rot, sizeof(rot), "transpose=1,transpose=1,");
    else if (p->rotate_quadrant == 3) snprintf(rot, sizeof(rot), "transpose=2,");

    char sar_filt[64] = "";
    if (p->sar_num > 0 && p->sar_den > 0)
        snprintf(sar_filt, sizeof(sar_filt), "setsar=%d/%d,", p->sar_num, p->sar_den);

    char trim_filt[128] = "";
    trim_prefix(p, 0, trim_filt, sizeof(trim_filt));

    char chain[VE_FILTER_MAX + 256];
    char sep[4] = "";
    if (p->vf[0]) {
        size_t vlen = strlen(p->vf);
        snprintf(sep, sizeof(sep), "%s", (vlen > 0 && p->vf[vlen-1] == ',') ? "" : ",");
    }
    if (p->vf[0])
        snprintf(chain, sizeof(chain), "%s%s%s%s%sformat=yuv420p",
                 trim_filt, rot, p->vf, sep, sar_filt);
    else
        snprintf(chain, sizeof(chain), "%s%s%sformat=yuv420p",
                 trim_filt, rot, sar_filt);

    if (make_graph(sc, "buffer", args, chain, err, errlen) < 0) return -1;

    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!enc) { snprintf(err,errlen,"no H.264 encoder (libx264) available"); return -1; }
    sc->enc = avcodec_alloc_context3(enc);
    if (!sc->enc) { snprintf(err,errlen,"encoder alloc"); return -1; }
    sc->enc->width   = av_buffersink_get_w(sc->sink);
    sc->enc->height  = av_buffersink_get_h(sc->sink);
    sc->enc->pix_fmt = AV_PIX_FMT_YUV420P;
    sc->enc->time_base = av_buffersink_get_time_base(sc->sink);
    sc->enc->framerate = av_buffersink_get_frame_rate(sc->sink);
    sc->enc->colorspace        = csp;
    sc->enc->color_range       = range;
    sc->enc->color_primaries   = sc->dec->color_primaries;
    sc->enc->color_trc         = sc->dec->color_trc;
    sc->enc->sample_aspect_ratio = av_buffersink_get_sample_aspect_ratio(sc->sink);
    av_opt_set(sc->enc->priv_data, "preset", "medium", 0);

    int64_t src_bitrate = sc->ist->codecpar->bit_rate;
    if (src_bitrate > 0) {
        sc->enc->bit_rate       = src_bitrate;
        sc->enc->rc_max_rate    = src_bitrate;
        sc->enc->rc_min_rate    = src_bitrate;
        sc->enc->rc_buffer_size = (int)(src_bitrate * 2);
        av_opt_set(sc->enc->priv_data, "nal-hrd", "cbr", 0);
    } else {
        av_opt_set(sc->enc->priv_data, "crf", "18", 0);
    }

    if (out->oformat->flags & AVFMT_GLOBALHEADER) sc->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(sc->enc, enc, NULL) < 0) { snprintf(err,errlen,"open H.264 encoder"); return -1; }

    sc->ost = avformat_new_stream(out, NULL);
    if (!sc->ost) { snprintf(err,errlen,"new stream"); return -1; }
    avcodec_parameters_from_context(sc->ost->codecpar, sc->enc);
    sc->ost->time_base = sc->enc->time_base;
    sc->out_index = sc->ost->index;
    return 0;
}

static int setup_audio_xcode(StreamCtx* sc, AVFormatContext* out, const VePlan* p,
                             char* err, int errlen) {
    sc->is_video = 0;
    if (open_decoder(sc, err, errlen) < 0) return -1;

    char lay[128];
    if (av_channel_layout_describe(&sc->dec->ch_layout, lay, sizeof(lay)) < 0)
        snprintf(lay, sizeof(lay), "stereo");
    AVRational tb = sc->ist->time_base;
    char args[256];
    snprintf(args, sizeof(args),
        "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
        tb.num, tb.den, sc->dec->sample_rate,
        av_get_sample_fmt_name(sc->dec->sample_fmt), lay);

    char trim_filt[128] = "";
    trim_prefix(p, 1, trim_filt, sizeof(trim_filt));

    char chain[VE_FILTER_MAX + 128];
    if (p->af[0]) snprintf(chain, sizeof(chain), "%s%s,aformat=sample_fmts=fltp", trim_filt, p->af);
    else if (trim_filt[0]) snprintf(chain, sizeof(chain), "%saformat=sample_fmts=fltp", trim_filt);
    else          snprintf(chain, sizeof(chain), "aformat=sample_fmts=fltp");

    if (make_graph(sc, "abuffer", args, chain, err, errlen) < 0) return -1;

    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!enc) { snprintf(err,errlen,"no AAC encoder available"); return -1; }
    sc->enc = avcodec_alloc_context3(enc);
    if (!sc->enc) { snprintf(err,errlen,"encoder alloc"); return -1; }
    sc->enc->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    sc->enc->sample_rate = av_buffersink_get_sample_rate(sc->sink);
    av_buffersink_get_ch_layout(sc->sink, &sc->enc->ch_layout);

    int64_t src_bitrate = sc->ist->codecpar->bit_rate;
    sc->enc->bit_rate = (src_bitrate > 0) ? src_bitrate : 192000;

    sc->enc->time_base   = (AVRational){1, sc->enc->sample_rate};
    if (out->oformat->flags & AVFMT_GLOBALHEADER) sc->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(sc->enc, enc, NULL) < 0) { snprintf(err,errlen,"open AAC encoder"); return -1; }

    if (sc->enc->frame_size > 0)
        av_buffersink_set_frame_size(sc->sink, sc->enc->frame_size);

    sc->ost = avformat_new_stream(out, NULL);
    if (!sc->ost) { snprintf(err,errlen,"new stream"); return -1; }
    avcodec_parameters_from_context(sc->ost->codecpar, sc->enc);
    sc->ost->time_base = sc->enc->time_base;
    sc->out_index = sc->ost->index;
    return 0;
}

static int encode_write(AVFormatContext* out, StreamCtx* sc, AVFrame* frame, AVPacket* opkt) {
    if (frame && !sc->is_video) {
        frame->pts = sc->a_pts;
        sc->a_pts += frame->nb_samples;
    }
    if (avcodec_send_frame(sc->enc, frame) < 0) return -1;
    for (;;) {
        int r = avcodec_receive_packet(sc->enc, opkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
        if (r < 0) return -1;
        opkt->stream_index = sc->out_index;
        av_packet_rescale_ts(opkt, sc->enc->time_base, sc->ost->time_base);
        av_interleaved_write_frame(out, opkt);
        av_packet_unref(opkt);
    }
}

static int filter_encode(AVFormatContext* out, StreamCtx* sc, AVFrame* dec,
                         AVFrame* filt, AVPacket* opkt) {
    int r = av_buffersrc_add_frame_flags(sc->src, dec, dec ? AV_BUFFERSRC_FLAG_KEEP_REF : 0);
    if (r < 0) {
        if (r == AVERROR_EOF) return AVERROR_EOF;
        return r;
    }
    for (;;) {
        r = av_buffersink_get_frame(sc->sink, filt);
        if (r == AVERROR(EAGAIN)) return 0;
        if (r == AVERROR_EOF)     return AVERROR_EOF;
        if (r < 0)                return r;
        int e = encode_write(out, sc, filt, opkt);
        av_frame_unref(filt);
        if (e < 0) return e;
    }
}

static int transcode(const VePlan* p, VeProgressCb cb, void* user, char* err, int errlen) {
    if (p->n_merge) { snprintf(err,errlen,"merge + re-encode not yet combined"); return -1; }

    AVFormatContext* in = NULL, *out = NULL;
    StreamCtx* S = NULL;
    AVPacket* pkt = NULL; AVFrame* dframe = NULL; AVFrame* fframe = NULL; AVPacket* opkt = NULL;
    int rc = -1;

    if (avformat_open_input(&in, p->input, NULL, NULL) < 0) ERR("cannot open %s", p->input);
    if (avformat_find_stream_info(in, NULL) < 0)            ERR("no stream info");

    double total = (in->duration > 0) ? (double)in->duration / AV_TIME_BASE : 0.0;
    int last_pct = -2;
    int cancelled = 0;

    bool did_seek = false;
    if (p->has_trim && p->trim_start > 0) {
        int64_t seek_ts = (int64_t)(p->trim_start * AV_TIME_BASE);
        av_seek_frame(in, -1, seek_ts, AVSEEK_FLAG_BACKWARD);
        did_seek = true;
    }

    const char* fmt = p->format[0] ? p->format : NULL;
    if (avformat_alloc_output_context2(&out, NULL, fmt, p->output) < 0 || !out)
        ERR("cannot create output %s", p->output);

    S = av_calloc(in->nb_streams, sizeof(StreamCtx));
    if (!S) ERR("oom");

    for (unsigned i = 0; i < in->nb_streams; i++) {
        StreamCtx* sc = &S[i];
        sc->ist = in->streams[i];
        sc->out_index = -1;
        enum AVMediaType t = sc->ist->codecpar->codec_type;
        int is_cover = (sc->ist->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;

        if (!keep_stream(p, sc->ist)) { sc->mode = S_DROP; continue; }

        if (t == AVMEDIA_TYPE_VIDEO && !is_cover && p->video == VE_TRANSCODE) {
            sc->mode = S_XCODE;
            if (setup_video_xcode(sc, out, p, err, errlen) < 0) goto fail;
        } else if (t == AVMEDIA_TYPE_AUDIO && p->audio == VE_TRANSCODE) {
            sc->mode = S_XCODE;
            if (setup_audio_xcode(sc, out, p, err, errlen) < 0) goto fail;
        } else {
            sc->mode = S_COPY;
            sc->ost = avformat_new_stream(out, NULL);
            if (!sc->ost) ERR("new stream");
            if (avcodec_parameters_copy(sc->ost->codecpar, sc->ist->codecpar) < 0) ERR("param copy");
            sc->ost->codecpar->codec_tag = 0;
            sc->ost->time_base = sc->ist->time_base;
            sc->out_index = sc->ost->index;
        }
    }

    if (did_seek) {
        for (unsigned i = 0; i < in->nb_streams; i++) {
            if (S[i].mode == S_XCODE && S[i].dec)
                avcodec_flush_buffers(S[i].dec);
        }
    }

    for (int i = 0; i < p->n_meta; i++) {
        char* eq = strchr(p->meta[i], '=');
        if (!eq)
		continue;
	*eq = 0;
        av_dict_set(&out->metadata, p->meta[i], eq + 1, 0); *eq = '=';
    }

    if (!(out->oformat->flags & AVFMT_NOFILE))
        if (avio_open(&out->pb, p->output, AVIO_FLAG_WRITE) < 0) ERR("cannot write %s", p->output);
    if (avformat_write_header(out, NULL) < 0) ERR("write header");

    pkt = av_packet_alloc(); opkt = av_packet_alloc();
    dframe = av_frame_alloc(); fframe = av_frame_alloc();
    if (!pkt || !opkt || !dframe || !fframe) ERR("oom");

    while (av_read_frame(in, pkt) >= 0) {
        StreamCtx* sc = &S[pkt->stream_index];
        double ts = (pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts) * av_q2d(sc->ist->time_base);
        if (ts > 0 && emit_progress(cb, user, ts, total, &last_pct)) {
            av_packet_unref(pkt); cancelled = 1; snprintf(err, errlen, "cancelled by caller"); break;
        }
        if (sc->mode == S_DROP) { av_packet_unref(pkt); continue; }
        if (sc->mode == S_COPY) {
            if (p->has_trim) {
                int64_t raw = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;
                double tsec = raw * av_q2d(sc->ist->time_base);
                if (tsec < p->trim_start) { av_packet_unref(pkt); continue; }
                if (p->trim_end > 0 && tsec > p->trim_end) { av_packet_unref(pkt); continue; }
                if (!sc->copy_started) {
                    sc->copy_pts_off = raw;
                    sc->copy_started = true;
                }
                if (pkt->pts != AV_NOPTS_VALUE) pkt->pts -= sc->copy_pts_off;
                if (pkt->dts != AV_NOPTS_VALUE) pkt->dts -= sc->copy_pts_off;
            }
            av_packet_rescale_ts(pkt, sc->ist->time_base, sc->ost->time_base);
            pkt->stream_index = sc->out_index; pkt->pos = -1;
            av_interleaved_write_frame(out, pkt);
            continue;
        }
        if (sc->graph_done) { av_packet_unref(pkt); continue; }
        if (avcodec_send_packet(sc->dec, pkt) >= 0) {
            for (;;) {
                int r = avcodec_receive_frame(sc->dec, dframe);
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                if (r < 0) { av_packet_unref(pkt); ERR("decode error stream %d", sc->ist->index); }
                int fe = filter_encode(out, sc, dframe, fframe, opkt);
                av_frame_unref(dframe);
                if (fe == AVERROR_EOF) {
                    encode_write(out, sc, NULL, opkt);
                    sc->graph_done = true;
                    break;
                }
                if (fe < 0) {
                    char av_err[128];
                    av_strerror(fe, av_err, sizeof(av_err));
                    av_packet_unref(pkt);
                    ERR("filter/encode error stream %d: %s", sc->ist->index, av_err);
                }
            }
        }
        av_packet_unref(pkt);
    }

    if (cancelled) { rc = VE_ECANCELED; goto fail; }

    for (unsigned i = 0; i < in->nb_streams; i++) {
        StreamCtx* sc = &S[i];
        if (sc->mode != S_XCODE || sc->graph_done) continue;
        avcodec_send_packet(sc->dec, NULL);
        for (;;) {
            int r = avcodec_receive_frame(sc->dec, dframe);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            if (r < 0) break;
            int fe = filter_encode(out, sc, dframe, fframe, opkt);
            av_frame_unref(dframe);
            if (fe == AVERROR_EOF) { sc->graph_done = true; break; }
            if (fe < 0) break;
        }
        if (!sc->graph_done) {
            int fe = filter_encode(out, sc, NULL, fframe, opkt);
            if (fe < 0 && fe != AVERROR_EOF) {
                char av_err[128]; av_strerror(fe, av_err, sizeof(av_err));
                snprintf(err, errlen, "flush filter error stream %d: %s", sc->ist->index, av_err);
            }
        }
        encode_write(out, sc, NULL, opkt);
    }

    av_write_trailer(out);
    if (cb) cb(user, total > 0 ? 1.0 : -1.0, total, total);
    rc = 0;

fail:
    av_packet_free(&pkt); av_packet_free(&opkt);
    av_frame_free(&dframe); av_frame_free(&fframe);
    if (S) {
        for (unsigned i = 0; in && i < in->nb_streams; i++) {
            avcodec_free_context(&S[i].dec);
            avcodec_free_context(&S[i].enc);
            avfilter_graph_free(&S[i].graph);
        }
        av_freep(&S);
    }
    if (in)  avformat_close_input(&in);
    if (out) {
        if (out->pb && !(out->oformat->flags & AVFMT_NOFILE)) avio_closep(&out->pb);
        avformat_free_context(out);
    }
    if (cancelled) remove(p->output);
    return rc;
}

int ve_execute(const VePlan* p, VeProgressCb on_progress, void* user, char* err, int errlen) {
    bool need_streams = p->n_add_audio || p->n_add_sub || p->thumbnail[0];

    if (p->lossless) {
        return remux_lossless(p, on_progress, user, err, errlen);
    }

    if (!need_streams) {
        return transcode(p, on_progress, user, err, errlen);
    }

    char tmp[VE_PATH_MAX];
    const char* ext = strrchr(p->output, '.');
    if (ext) snprintf(tmp, sizeof(tmp), "%.*s.pass1%s", (int)(ext - p->output), p->output, ext);
    else     snprintf(tmp, sizeof(tmp), "%s.pass1.mp4", p->output);

    VePlan p1 = *p;
    ve_copy(p1.output, sizeof(p1.output), tmp);
    p1.n_add_audio = 0;
    p1.n_add_sub   = 0;
    p1.thumbnail[0] = '\0';

    int r = transcode(&p1, on_progress, user, err, errlen);
    if (r != 0) { remove(tmp); return r; }

    VePlan p2;
    memset(&p2, 0, sizeof(p2));
    ve_copy(p2.input,  sizeof(p2.input),  tmp);
    ve_copy(p2.output, sizeof(p2.output), p->output);
    ve_copy(p2.format, sizeof(p2.format), p->format);
    p2.video = VE_COPY;
    p2.audio = VE_COPY;
    p2.n_add_audio = p->n_add_audio;
    p2.n_add_sub   = p->n_add_sub;
    p2.remove_thumbnail = p->remove_thumbnail;
    for (int i = 0; i < p->n_add_audio; i++) {
        ve_copy(p2.add_audio[i], sizeof(p2.add_audio[i]), p->add_audio[i]);
        ve_copy(p2.add_audio_lang[i], sizeof(p2.add_audio_lang[i]), p->add_audio_lang[i]);
    }
    for (int i = 0; i < p->n_add_sub; i++) {
        ve_copy(p2.add_sub[i], sizeof(p2.add_sub[i]), p->add_sub[i]);
        ve_copy(p2.add_sub_lang[i], sizeof(p2.add_sub_lang[i]), p->add_sub_lang[i]);
    }
    ve_copy(p2.thumbnail, sizeof(p2.thumbnail), p->thumbnail);
    p2.lossless = true;

    r = remux_lossless(&p2, NULL, NULL, err, errlen);
    remove(tmp);
    return r;

}


#else

int ve_execute(const VePlan* p, VeProgressCb on_progress, void* user, char* err, int errlen) {
    (void)p; (void)on_progress; (void)user;
    snprintf(err, errlen, "built without libav (define VE_WITH_LIBAV to enable ve_exec.c)");
    return -1;
}

#endif
