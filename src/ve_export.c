/* ve_export.c — background video export for the editor UI.
 *
 * Bridges the UI (main.c) to the ve/ library (ve_dsl / ve_plan / ve_exec).
 * An ExportRequest captures the current editor state (trim, crop, scale,
 * filters, tracks, ...) and is turned into a DSL script, parsed, planned and
 * executed on a dedicated worker thread so the UI never blocks. Progress and
 * cancellation are shared with the UI through atomics.
 */
#define VE_WITH_LIBAV

/* Self-containment for standalone analysis: in the unity build these are
   already provided by main.c (which includes os.c and config.h before this
   file), but including them here lets the IDE resolve VideoConfig, OS_PATHMAX,
   the os_* helpers, and the error_* globals when this file is opened on its
   own. Both headers are include-guarded, so no redefinition in the unity build. */
#include "config.h"
#include "os.c"

#include "ve/ve.h"
#include "ve/ve_dsl.c"
#include "ve/ve_exec.c"
#include "ve/ve_ops.c"
#include "ve/ve_plan.c"
#include <pthread.h>
#include <stdatomic.h>

/* A snapshot of the editor's export settings, captured when the user hits
   "export" so later UI changes don't affect the running job. */
typedef struct {
	char input[VE_PATH_MAX];
	char output[VE_PATH_MAX];
	VideoConfig cfg;
	float trim_start;
	float trim_end;
	float duration;
	int has_trim;
	int crop_enabled;
	int crop_x, crop_y, crop_w, crop_h;
	int scale_enabled, scale_w, scale_h, scale_keep_aspect;
	char thumbnail[VE_PATH_MAX];
	char subtitle[VE_PATH_MAX];
	char subtitle_lang[16];
	int remove_sub_enabled, remove_sub_index;
	int drop_audio_index[16];
	int n_drop_audio;
} ExportRequest;

/* State of a single (at most one at a time) export job. The worker thread
   writes the atomics; the UI thread reads them from export_poll(). */
typedef struct {
	pthread_t thread;
	int running;
	atomic_int cancel;   /* set by the UI to request cancellation */
	atomic_int done;     /* set by the worker when it finishes */
	_Atomic double fraction; /* 0..1 progress, written by the worker */
	int rc;
	char err[256];
	char output[VE_PATH_MAX];
	ExportRequest req;
} ExportJob;

/* The single global export job (the app only ever runs one export at a time). */
static ExportJob g_export = {0};

/* Progress callback handed to ve_execute(). Stores the fraction for the UI and
   returns non-zero (cancelling the export) if the UI has requested a cancel. */
static int export_progress(void *user, double frac, double cur, double total)
{
	ExportJob *j = (ExportJob *)user;
	(void)cur;
	(void)total;
	atomic_store(&j->fraction, frac);
	return atomic_load(&j->cancel);
}

/* Translate the editor's export settings into a ve/ DSL script. Each enabled
   option emits one DSL statement; the resulting text is parsed by ve_parse. */
static void export_build_script(const ExportRequest *r, char *out, int cap)
{
	int n = 0;
	n += snprintf(out + n, cap - n, "in \"%s\"\n", r->input);
	n += snprintf(out + n, cap - n, "out \"%s\"\n", r->output);

	/* Trim only when it actually cuts something off the clip. */
	if (r->has_trim && r->duration > 0.0f) {
		float s = r->trim_start, e = r->trim_end;
		if (e <= s)
			e = r->duration;
		if (s > 0.0f || e < r->duration)
			n += snprintf(out + n, cap - n, "trim %.3f %.3f\n", s,
				      e);
	}

	if (r->crop_enabled && r->crop_w >= 2 && r->crop_h >= 2)
		n += snprintf(out + n, cap - n, "crop %d %d %d %d\n", r->crop_w,
			      r->crop_h, r->crop_x, r->crop_y);

	/* Normalize the rotation angle into 0..359 so only a real rotation is
	   emitted (0 is a no-op). */
	int rot = ((r->cfg.rotate % 360) + 360) % 360;
	if (rot != 0)
		n += snprintf(out + n, cap - n, "rotate %d\n", rot);

	if (r->cfg.flip_h)
		n += snprintf(out + n, cap - n, "flip h\n");
	if (r->cfg.flip_v)
		n += snprintf(out + n, cap - n, "flip v\n");

	/* Scale: -1 for the height keeps the aspect ratio. */
	if (r->scale_enabled && r->scale_w >= 2) {
		if (r->scale_keep_aspect)
			n += snprintf(out + n, cap - n, "scale %d -1\n",
				      r->scale_w);
		else if (r->scale_h >= 2)
			n += snprintf(out + n, cap - n, "scale %d %d\n",
				      r->scale_w, r->scale_h);
	}

	if (r->cfg.aspect_ratio.x > 0.0f && r->cfg.aspect_ratio.y > 0.0f)
		n += snprintf(out + n, cap - n, "aspect %d %d\n",
			      (int)r->cfg.aspect_ratio.x,
			      (int)r->cfg.aspect_ratio.y);

	if (r->remove_sub_enabled)
		n += snprintf(out + n, cap - n, "remove-subtitle %d\n",
			      r->remove_sub_index);
	for (int i = 0; i < r->n_drop_audio; i++)
		n += snprintf(out + n, cap - n, "remove-audio %d\n",
			      r->drop_audio_index[i]);
	if (r->subtitle[0])
		n += snprintf(out + n, cap - n, "subtitle \"%s\" %s\n",
			      r->subtitle,
			      r->subtitle_lang[0] ? r->subtitle_lang : "und");
	if (r->thumbnail[0])
		n += snprintf(out + n, cap - n, "thumbnail \"%s\"\n",
			      r->thumbnail);

	/* Audio: mute drops all audio; otherwise apply volume and mono. */
	if (r->cfg.mute) {
		n += snprintf(out + n, cap - n, "mute\n");
	} else {
		if (r->cfg.scale_volume != 0) {
			/* scale_volume is a +/- percent offset from unity. */
			float v = 1.0f + (float)r->cfg.scale_volume / 100.0f;
			if (v < 0.0f)
				v = 0.0f;
			n += snprintf(out + n, cap - n, "volume %.3f\n", v);
		}
		if (r->cfg.sterio_to_mono)
			n += snprintf(out + n, cap - n, "mono\n");
	}

	/* Raw custom filter strings pass straight through to the DSL. */
	if (r->cfg.vf[0])
		n += snprintf(out + n, cap - n, "%s\n", r->cfg.vf);
	if (r->cfg.af[0])
		n += snprintf(out + n, cap - n, "%s\n", r->cfg.af);
}

/* Worker thread entry point: build the script, then run it through the
   parse -> plan -> execute pipeline. Results are published via atomics. */
static void *export_worker(void *arg)
{
	ExportJob *j = (ExportJob *)arg;
	char script[4096];
	export_build_script(&j->req, script, sizeof(script));

	VeScript s;
	if (!ve_parse(script, &s, j->err, sizeof(j->err))) {
		j->rc = -1;
		atomic_store(&j->done, 1);
		return NULL;
	}
	VePlan p;
	if (!ve_plan(&s, &p, j->err, sizeof(j->err))) {
		j->rc = -1;
		ve_script_free(&s);
		atomic_store(&j->done, 1);
		return NULL;
	}
	j->rc = ve_execute(&p, export_progress, j, j->err, sizeof(j->err));
	ve_script_free(&s);
	atomic_store(&j->fraction, 1.0);
	atomic_store(&j->done, 1);
	return NULL;
}

/* Derive a default output path next to the input: "<name>_Processed<ext>". */
static void export_output_path(const ExportRequest *r, char *dst, int cap)
{
	const char *base = os_path_basename(r->input);
	const char *ext = os_path_ext(base);
	char stem[OS_PATHMAX];
	ve_copy(stem, sizeof(stem), base);
	if (ext[0])
		stem[ext - base - 1] = '\0';
	char folder[OS_PATHMAX];
	os_path_dirname(folder, sizeof(folder), r->input);
	char fname[OS_PATHMAX];
	snprintf(fname, sizeof(fname), "%s_Processed%s", stem, ext[0] ? ext - 1 : "");
	os_path_join(dst, cap, folder, fname);
}

/* Kick off a new export job. If a previous job is still running it is ignored;
   if it has finished, its thread is joined and reused. The output path is
   either placed in `output_dir` or auto-derived next to the input. */
void export_start(const char *input_path, const char *output_dir,
		  const ExportRequest *src)
{
	if (g_export.running && !atomic_load(&g_export.done))
		return;
	if (g_export.running) {
		pthread_join(g_export.thread, NULL);
		g_export.running = 0;
	}
	memset(&g_export, 0, sizeof(g_export));
	g_export.req = *src;
	ve_copy(g_export.req.input, sizeof(g_export.req.input), input_path);
	if (output_dir && output_dir[0]) {
		/* Build "<stem>_Processed<ext>" inside the chosen directory. */
		const char *base = os_path_basename(input_path);
		const char *ext = os_path_ext(base);
		char stem[OS_PATHMAX];
		ve_copy(stem, sizeof(stem), base);
		if (ext[0])
			stem[ext - base] = '\0';
		stem[OS_PATHMAX - 64] = '\0';
		char fname[OS_PATHMAX];
		snprintf(fname, sizeof(fname), "%s_Processed%s", stem, ext);
		os_path_join(g_export.req.output, sizeof(g_export.req.output),
			 output_dir, fname);
	} else {
		export_output_path(&g_export.req, g_export.req.output,
				   sizeof(g_export.req.output));
	}
	ve_copy(g_export.output, sizeof(g_export.output), g_export.req.output);
	/* Reset the shared state before starting the worker. */
	atomic_store(&g_export.cancel, 0);
	atomic_store(&g_export.done, 0);
	atomic_store(&g_export.fraction, 0.0);
	g_export.rc = 0;
	g_export.running = 1;
	pthread_create(&g_export.thread, NULL, export_worker, &g_export);
}

/* True while an export is in progress (started but not yet finished). */
int export_active(void)
{
	return g_export.running && !atomic_load(&g_export.done);
}

/* Current export progress in 0..1 (clamped; 0 if unknown/negative). */
double export_fraction(void)
{
	double f = atomic_load(&g_export.fraction);
	return f < 0.0 ? 0.0 : f;
}

/* Ask the running export to stop; the worker checks this via export_progress. */
void export_request_cancel(void) { atomic_store(&g_export.cancel, 1); }

/* Call once per frame. When the worker finishes, joins it and reports the
   result through the app's error dialog. */
void export_poll(void)
{
	if (!g_export.running || !atomic_load(&g_export.done))
		return;
	pthread_join(g_export.thread, NULL);
	g_export.running = 0;
	if (g_export.rc == 0) {
		static char msg[VE_PATH_MAX + 32];
		snprintf(msg, sizeof(msg), "Saved to:\n%s", g_export.output);
		error_title = "Export complete";
		error_message = msg;
		log_info(&logger, "Export complete: %s", g_export.output);
	} else if (g_export.rc == VE_ECANCELED) {
		error_title = "Export cancelled";
		error_message = "The export was cancelled.";
		log_warn(&logger, "Export cancelled");
	} else {
		static char msg[320];
		snprintf(msg, sizeof(msg), "%s", g_export.err);
		error_title = "Export failed";
		error_message = msg;
		log_error(&logger, "Export failed: %s", g_export.err);
	}
}
