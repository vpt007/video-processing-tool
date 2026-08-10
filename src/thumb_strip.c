/* thumb_strip.c — background filmstrip thumbnail extractor for the trim track.
 *
 * ts_begin(path) spawns a worker thread that opens the video with libav, seeks
 * to TS_CELLS evenly-spaced timestamps, decodes one frame at each, scales it to
 * a small RGB24 cell, and stores it. The UI thread polls ts_cell_ready() and
 * reads the pixels with ts_cell_rgb(). All shared state is guarded by a mutex.
 *
 * The GL texture upload happens on the UI thread (see ts_upload_tex in
 * main.c); this file stays pure CPU so it is safe to run off the main thread.
 */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TS_CELLS 24
#define TS_W     160
#define TS_H     90

typedef struct {
	uint8_t *rgb; /* TS_W * TS_H * 3, RGB24 */
	int ready;
} TsCell;

static TsCell g_cells[TS_CELLS];
static pthread_mutex_t ts_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t ts_thread;
static int ts_running = 0;

int ts_count(void) { return TS_CELLS; }

int ts_cell_ready(int ci)
{
	if (ci < 0 || ci >= TS_CELLS)
		return 0;
	pthread_mutex_lock(&ts_mu);
	int r = g_cells[ci].ready;
	pthread_mutex_unlock(&ts_mu);
	return r;
}

const uint8_t *ts_cell_rgb(int ci, int *w, int *h)
{
	if (ci < 0 || ci >= TS_CELLS)
		return NULL;
	pthread_mutex_lock(&ts_mu);
	const uint8_t *p = g_cells[ci].ready ? g_cells[ci].rgb : NULL;
	pthread_mutex_unlock(&ts_mu);
	if (p) {
		if (w)
			*w = TS_W;
		if (h)
			*h = TS_H;
	}
	return p;
}

static void ts_reset_cells(void)
{
	for (int i = 0; i < TS_CELLS; i++) {
		free(g_cells[i].rgb);
		g_cells[i].rgb = NULL;
		g_cells[i].ready = 0;
	}
}

static void *ts_worker(void *arg)
{
	const char *path = (const char *)arg;
	AVFormatContext *fmt = NULL;

	if (avformat_open_input(&fmt, path, NULL, NULL) < 0)
		goto done;
	if (avformat_find_stream_info(fmt, NULL) < 0)
		goto done;

	int vs = -1;
	for (unsigned i = 0; i < fmt->nb_streams; i++) {
		if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			vs = (int)i;
			break;
		}
	}
	if (vs < 0)
		goto done;

	AVStream *st = fmt->streams[vs];
	const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
	if (!dec)
		goto done;
	AVCodecContext *ctx = avcodec_alloc_context3(dec);
	if (!ctx)
		goto done;
	if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
		avcodec_free_context(&ctx);
		goto done;
	}
	if (avcodec_open2(ctx, dec, NULL) < 0) {
		avcodec_free_context(&ctx);
		goto done;
	}

	double dur = (fmt->duration > 0) ? (double)fmt->duration / AV_TIME_BASE
					 : 0.0;
	if (dur <= 0.0)
		dur = 10.0;

	struct SwsContext *sws = sws_getContext(
	    ctx->width, ctx->height, ctx->pix_fmt, TS_W, TS_H,
	    AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
	AVFrame *frame = av_frame_alloc();
	AVPacket *pkt = av_packet_alloc();
	if (!sws || !frame || !pkt) {
		av_packet_free(&pkt);
		av_frame_free(&frame);
		if (sws)
			sws_freeContext(sws);
		avcodec_free_context(&ctx);
		goto done;
	}

	for (int ci = 0; ci < TS_CELLS; ci++) {
		double target = dur * (ci + 0.5) / TS_CELLS;
		int64_t ts = (int64_t)(target * AV_TIME_BASE);
		av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
		avcodec_flush_buffers(ctx);
		int got = 0;
		while (av_read_frame(fmt, pkt) >= 0) {
			if (pkt->stream_index != vs) {
				av_packet_unref(pkt);
				continue;
			}
			if (avcodec_send_packet(ctx, pkt) == 0) {
				while (avcodec_receive_frame(ctx, frame) == 0) {
					double fts =
					    (frame->pts != AV_NOPTS_VALUE)
						? frame->pts *
							  av_q2d(st->time_base)
						: 0.0;
					if (fts >= target || ci == 0) {
						uint8_t *rgb =
						    malloc(TS_W * TS_H * 3);
						if (rgb) {
							uint8_t *dst[4] = {
							    rgb, NULL, NULL,
							    NULL};
							int dstlines[4] = {
							    TS_W * 3, 0, 0,
							    0};
							sws_scale(
							    sws,
							    (const uint8_t *const *)
								frame->data,
							    frame->linesize, 0,
							    ctx->height, dst,
							    dstlines);
							pthread_mutex_lock(
							    &ts_mu);
							free(g_cells[ci].rgb);
							g_cells[ci].rgb = rgb;
							g_cells[ci].ready = 1;
							pthread_mutex_unlock(
							    &ts_mu);
						}
						got = 1;
						break;
					}
				}
			}
			av_packet_unref(pkt);
			if (got)
				break;
		}
	}

	av_packet_free(&pkt);
	av_frame_free(&frame);
	sws_freeContext(sws);
	avcodec_free_context(&ctx);

done:
	if (fmt)
		avformat_close_input(&fmt);
	free((void *)path);
	pthread_mutex_lock(&ts_mu);
	ts_running = 0;
	pthread_mutex_unlock(&ts_mu);
	return NULL;
}

/* Kick off the filmstrip worker for `path`. If a worker is already running the
 * request is ignored (the UI resets state before calling this). */
void ts_begin(const char *path)
{
	if (!path)
		return;
	pthread_mutex_lock(&ts_mu);
	if (ts_running) {
		pthread_mutex_unlock(&ts_mu);
		return;
	}
	ts_running = 1;
	pthread_mutex_unlock(&ts_mu);

	ts_reset_cells();
	char *p = strdup(path);
	if (!p) {
		pthread_mutex_lock(&ts_mu);
		ts_running = 0;
		pthread_mutex_unlock(&ts_mu);
		return;
	}
	pthread_create(&ts_thread, NULL, ts_worker, p);
	pthread_detach(ts_thread);
}