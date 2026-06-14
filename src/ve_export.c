#define VE_WITH_LIBAV

#include "ve/ve.h"
#include "ve/ve_dsl.c"
#include "ve/ve_exec.c"
#include "ve/ve_ops.c"
#include "ve/ve_plan.c"
#include <pthread.h>
#include <stdatomic.h>

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
} ExportRequest;

typedef struct {
	pthread_t thread;
	int running;
	atomic_int cancel;
	atomic_int done;
	_Atomic double fraction;
	int rc;
	char err[256];
	char output[VE_PATH_MAX];
	ExportRequest req;
} ExportJob;

static ExportJob g_export = {0};

static int export_progress(void *user, double frac, double cur, double total)
{
	ExportJob *j = (ExportJob *)user;
	(void)cur;
	(void)total;
	atomic_store(&j->fraction, frac);
	return atomic_load(&j->cancel);
}

static void export_build_script(const ExportRequest *r, char *out, int cap)
{
	int n = 0;
	n += snprintf(out + n, cap - n, "in \"%s\"\n", r->input);
	n += snprintf(out + n, cap - n, "out \"%s\"\n", r->output);

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

	int rot = ((r->cfg.rotate % 360) + 360) % 360;
	if (rot != 0)
		n += snprintf(out + n, cap - n, "rotate %d\n", rot);

	if (r->cfg.flip_h)
		n += snprintf(out + n, cap - n, "flip h\n");
	if (r->cfg.flip_v)
		n += snprintf(out + n, cap - n, "flip v\n");

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
	if (r->subtitle[0])
		n += snprintf(out + n, cap - n, "subtitle \"%s\" %s\n",
			      r->subtitle,
			      r->subtitle_lang[0] ? r->subtitle_lang : "und");
	if (r->thumbnail[0])
		n += snprintf(out + n, cap - n, "thumbnail \"%s\"\n",
			      r->thumbnail);

	if (r->cfg.mute) {
		n += snprintf(out + n, cap - n, "mute\n");
	} else {
		if (r->cfg.scale_volume != 0) {
			float v = 1.0f + (float)r->cfg.scale_volume / 100.0f;
			if (v < 0.0f)
				v = 0.0f;
			n += snprintf(out + n, cap - n, "volume %.3f\n", v);
		}
		if (r->cfg.sterio_to_mono)
			n += snprintf(out + n, cap - n, "mono\n");
	}

	if (r->cfg.vf[0])
		n += snprintf(out + n, cap - n, "%s\n", r->cfg.vf);
	if (r->cfg.af[0])
		n += snprintf(out + n, cap - n, "%s\n", r->cfg.af);
}

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

static void export_output_path(const ExportRequest *r, char *dst, int cap)
{
	const char *base = strrchr(r->input, '/');
	const char *base_w = strrchr(r->input, '\\');
	if (base_w > base)
		base = base_w;
	base = base ? base + 1 : r->input;

	char name[OS_PATHMAX];
	ve_copy(name, sizeof(name), base);
	char *dot = strrchr(name, '.');
	const char *ext = dot ? dot : "";
	char stem[OS_PATHMAX];
	ve_copy(stem, sizeof(stem), name);
	if (dot)
		stem[dot - name] = '\0';

	char folder[OS_PATHMAX];
	if (r->cfg.af[0] == 1) { /* never */
	}
	os_path_dirname(folder, sizeof(folder), r->input);

	snprintf(dst, cap, "%s/%s_edited%s", folder, stem, ext);
}

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
		const char *base = strrchr(input_path, '/');
		const char *base_w = strrchr(input_path, '\\');
		if (base_w > base)
			base = base_w;
		base = base ? base + 1 : input_path;
		char name[OS_PATHMAX];
		ve_copy(name, sizeof(name), base);
		char *dot = strrchr(name, '.');
		const char *ext = dot ? dot : "";
		char stem[OS_PATHMAX];
		ve_copy(stem, sizeof(stem), name);
		if (dot)
			stem[dot - name] = '\0';
		stem[OS_PATHMAX - 64] = '\0';
		snprintf(g_export.req.output, sizeof(g_export.req.output),
			 "%s/%s_Processed%s", output_dir, stem, ext);
	} else {
		export_output_path(&g_export.req, g_export.req.output,
				   sizeof(g_export.req.output));
	}
	ve_copy(g_export.output, sizeof(g_export.output), g_export.req.output);

	atomic_store(&g_export.cancel, 0);
	atomic_store(&g_export.done, 0);
	atomic_store(&g_export.fraction, 0.0);
	g_export.rc = 0;
	g_export.running = 1;
	pthread_create(&g_export.thread, NULL, export_worker, &g_export);
}

int export_active(void)
{
	return g_export.running && !atomic_load(&g_export.done);
}

double export_fraction(void)
{
	double f = atomic_load(&g_export.fraction);
	return f < 0.0 ? 0.0 : f;
}

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
	} else if (g_export.rc == VE_ECANCELED) {
		error_title = "Export cancelled";
		error_message = "The export was cancelled.";
	} else {
		static char msg[320];
		snprintf(msg, sizeof(msg), "%s", g_export.err);
		error_title = "Export failed";
		error_message = msg;
	}
}
