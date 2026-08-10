/* audio_wave.c — background audio waveform (peak envelope) extractor.
 *
 * aw_begin(path, tracks, count) spawns a worker that decodes each audio stream
 * with libav and computes a normalized peak envelope (a fixed number of
 * buckets). The UI thread polls aw_ready(i) and reads the peaks with
 * aw_peaks(i, &n). Shared state is guarded by a mutex.
 *
 * The GL texture rasterization happens on the UI thread (see build_wave_texture
 * in main.c); this file stays pure CPU so it is safe to run off the main
 * thread.
 */
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "vp_engine.h"

#define AW_BUCKETS    1000
#define AW_MAX_TRACKS 32

typedef struct {
	float *peaks; /* AW_BUCKETS floats, 0..1 */
	int npk;
	int ready;
} AwTrack;

static AwTrack g_tracks[AW_MAX_TRACKS];
static pthread_mutex_t aw_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_t aw_thread;
static int aw_running = 0;

int aw_ready(int i)
{
	if (i < 0 || i >= AW_MAX_TRACKS)
		return 0;
	pthread_mutex_lock(&aw_mu);
	int r = g_tracks[i].ready;
	pthread_mutex_unlock(&aw_mu);
	return r;
}

const float *aw_peaks(int i, int *npk)
{
	if (i < 0 || i >= AW_MAX_TRACKS)
		return NULL;
	pthread_mutex_lock(&aw_mu);
	const float *p = g_tracks[i].ready ? g_tracks[i].peaks : NULL;
	int n = g_tracks[i].npk;
	pthread_mutex_unlock(&aw_mu);
	if (npk)
		*npk = n;
	return p;
}

static void aw_reset_tracks(void)
{
	for (int i = 0; i < AW_MAX_TRACKS; i++) {
		free(g_tracks[i].peaks);
		g_tracks[i].peaks = NULL;
		g_tracks[i].npk = 0;
		g_tracks[i].ready = 0;
	}
}

/* Decode one audio stream and fill its peak envelope. */
static void aw_decode_stream(AVFormatContext *fmt, int stream_index,
			     int track_index)
{
	AVStream *st = fmt->streams[stream_index];
	const AVCodec *dec = avcodec_find_decoder(st->codecpar->codec_id);
	if (!dec)
		return;
	AVCodecContext *ctx = avcodec_alloc_context3(dec);
	if (!ctx)
		return;
	if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
		avcodec_free_context(&ctx);
		return;
	}
	if (avcodec_open2(ctx, dec, NULL) < 0) {
		avcodec_free_context(&ctx);
		return;
	}

	int channels = ctx->ch_layout.nb_channels;
	if (channels <= 0)
		channels = 2;

	float *peaks = calloc(AW_BUCKETS, sizeof(float));
	if (!peaks) {
		avcodec_free_context(&ctx);
		return;
	}
	float global_max = 1e-6f;
	float *bucket_max = calloc(AW_BUCKETS, sizeof(float));
	if (!bucket_max) {
		free(peaks);
		avcodec_free_context(&ctx);
		return;
	}

	AVFrame *frame = av_frame_alloc();
	AVPacket *pkt = av_packet_alloc();
	if (!frame || !pkt) {
		av_frame_free(&frame);
		av_packet_free(&pkt);
		free(peaks);
		free(bucket_max);
		avcodec_free_context(&ctx);
		return;
	}

	long long total_samples = 0;
	while (av_read_frame(fmt, pkt) >= 0) {
		if (pkt->stream_index != stream_index) {
			av_packet_unref(pkt);
			continue;
		}
		if (avcodec_send_packet(ctx, pkt) == 0) {
			while (avcodec_receive_frame(ctx, frame) == 0) {
				int n = frame->nb_samples;
				if (n <= 0)
					continue;
				float peak = 0.0f;
				for (int c = 0; c < channels; c++) {
					const float *d =
					    (const float *)frame->extended_data[c];
					for (int s = 0; s < n; s++) {
						float v = fabsf(d[s]);
						if (v > peak)
							peak = v;
					}
				}
				int b = (int)((long long)total_samples *
					      AW_BUCKETS /
					      (total_samples + n));
				if (b < 0)
					b = 0;
				if (b >= AW_BUCKETS)
					b = AW_BUCKETS - 1;
				if (peak > bucket_max[b])
					bucket_max[b] = peak;
				if (peak > global_max)
					global_max = peak;
				total_samples += n;
			}
		}
		av_packet_unref(pkt);
	}

	for (int b = 0; b < AW_BUCKETS; b++)
		peaks[b] = bucket_max[b] / global_max;

	av_frame_free(&frame);
	av_packet_free(&pkt);
	free(bucket_max);
	avcodec_free_context(&ctx);

	pthread_mutex_lock(&aw_mu);
	free(g_tracks[track_index].peaks);
	g_tracks[track_index].peaks = peaks;
	g_tracks[track_index].npk = AW_BUCKETS;
	g_tracks[track_index].ready = 1;
	pthread_mutex_unlock(&aw_mu);
}

static void *aw_worker(void *arg)
{
	const char *path = (const char *)arg;
	AVFormatContext *fmt = NULL;

	if (avformat_open_input(&fmt, path, NULL, NULL) < 0)
		goto done;
	if (avformat_find_stream_info(fmt, NULL) < 0)
		goto done;

	int track_index = 0;
	for (unsigned i = 0; i < fmt->nb_streams && track_index < AW_MAX_TRACKS;
	     i++) {
		if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			aw_decode_stream(fmt, (int)i, track_index++);
	}

done:
	if (fmt)
		avformat_close_input(&fmt);
	free((void *)path);
	pthread_mutex_lock(&aw_mu);
	aw_running = 0;
	pthread_mutex_unlock(&aw_mu);
	return NULL;
}

/* Kick off the waveform worker for `path`. `tracks`/`count` are accepted for
 * API symmetry with the caller (refresh_video_info) but the worker decodes
 * every audio stream it finds. If a worker is already running the request is
 * ignored. */
void aw_begin(const char *path, VPStreamInfo *tracks, int count)
{
	(void)tracks;
	(void)count;
	if (!path)
		return;
	pthread_mutex_lock(&aw_mu);
	if (aw_running) {
		pthread_mutex_unlock(&aw_mu);
		return;
	}
	aw_running = 1;
	pthread_mutex_unlock(&aw_mu);

	aw_reset_tracks();
	char *p = strdup(path);
	if (!p) {
		pthread_mutex_lock(&aw_mu);
		aw_running = 0;
		pthread_mutex_unlock(&aw_mu);
		return;
	}
	pthread_create(&aw_thread, NULL, aw_worker, p);
	pthread_detach(aw_thread);
}