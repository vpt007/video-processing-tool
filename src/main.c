#include <math.h>
#include <float.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"
#include "cimgui_impl.h"
#define GLAD_GL_IMPLEMENTATION
#include "gl.h"
#include <GLFW/glfw3.h>
#ifdef _MSC_VER
#include <windows.h>
#endif

#define _CRT_SECURE_NO_WARNINGS
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

#include "vendor/tinyfiledialogs.c"


#include "logger.h"
#include "vp_engine.c"
#include "config.h"
#include "icon_moon.h"
#include "icon_font.h"
#include "os.c"
#include "vp_thumb.c"
#include "thumb_strip.c"

#define JRGB(R,G,B) (ImVec4){(float)R/255,(float)G/255,(float)B/255,1.0f}
#ifdef IMGUI_HAS_IMSTR
#define igBegin igBegin_Str
#define igSliderFloat igSliderFloat_Str
#define igCheckbox igCheckbox_Str
#define igColorEdit3 igColorEdit3_Str
#define igButton igButton_Str
#endif
#define igGetIO igGetIO_Nil
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define ARR_LEN(x) (sizeof((x)) / sizeof((x)[0]))
#define IM_COL32(R, G, B, A)                                                   \
	(((ImU32)(A) << 24) | ((ImU32)(B) << 16) | ((ImU32)(G) << 8) |         \
	 ((ImU32)(R)))

#define JH_BUFFER_MAX (1 << 10)
typedef struct {
	VPEngine *eng;
	char ui_vf[JH_BUFFER_MAX];
	char ui_af[JH_BUFFER_MAX];
	int ui_seek_active;
	float ui_seek_val;
	float ui_seek_last_sent;
	double ui_last_seek_t;
} VPWidget;

void drop_callback(GLFWwindow *window, int count, const char **paths);
void reset_edit_state(void);

VPWidget *vp = NULL;
GLFWwindow *window;
ImFont *icon_font;

/* Actual on-screen rectangle of the video image inside the preview box, in
   screen coordinates. Written by vp_render every frame; consumed by the crop
   overlay so it can sit exactly over the image. When crop is active the frame
   is drawn rotated in screen space: img_min/max is then the UN-rotated source
   rect (centred in the box) and g_preview_angle is the rotation the overlay
   must apply so its handles track the rotated image. g_preview_box_* is the
   full preview box, used to clip the overlay. */
ImVec2 g_preview_img_min = {0, 0};
ImVec2 g_preview_img_max = {0, 0};
ImVec2 g_preview_box_min = {0, 0};
ImVec2 g_preview_box_max = {0, 0};
float  g_preview_angle   = 0.0f;
int    g_preview_has_img = 0;

#define tool_tip_size 16
#define small_icon_size 55
#define big_icon_size 105
#define BIN_FOLDER "bin"
#define THUMBNAIL_SIZE 400
#define tooltip(x)                                                             \
	if (igIsItemHovered(ImGuiHoveredFlags_DelayShort)) {                   \
		igSetTooltip(x);                                               \
	}


/* Video Player  widget */


typedef struct {
	ImVec2 aspect_ratio;
	ImVec2 scale;
	int rotate;
	int scale_volume;
	bool flip_h;
	bool flip_v;
	bool mute;
	bool sterio_to_mono;
	char vf[JH_BUFFER_MAX];
	char af[JH_BUFFER_MAX];
} VideoConfig;

/* Defined further down with the rest of the edit state, but vp_render (above
   them) needs them for the in-preview crop overlay. */
extern VideoConfig video_config;
extern int crop_enabled;

VPWidget *vp_create(const char *path, float scale)
{
	VPWidget *vp = calloc(1, sizeof(*vp));
	if (!vp)
		return NULL;
	vp->eng = vp_engine_create(path,0.3);
	if (!vp->eng) {
		free(vp);
		return NULL;
	}
	return vp;
}
void vp_destroy(VPWidget *vp)
{
	if (!vp)
		return;
	vp_engine_destroy(vp->eng);
	free(vp);
}
void vp_set_scale(VPWidget *vp, float scale)
{
	vp_engine_set_scale(vp->eng, scale);
}
void vp_set_vf(VPWidget *vp, const char *vf) { vp_engine_set_vf(vp->eng, vf); }
void vp_set_af(VPWidget *vp, const char *af) { vp_engine_set_af(vp->eng, af); }
void vp_set_video_track(VPWidget *vp, int idx)
{
	vp_engine_set_video_track(vp->eng, idx);
}
void vp_set_audio_track(VPWidget *vp, int idx)
{
	vp_engine_set_audio_track(vp->eng, idx);
}
int vp_get_video_tracks(VPWidget *vp, VPStreamInfo *out, int max)
{
	return vp_engine_video_tracks(vp->eng, out, max);
}
int vp_get_audio_tracks(VPWidget *vp, VPStreamInfo *out, int max)
{
	return vp_engine_audio_tracks(vp->eng, out, max);
}
unsigned int vp_get_texture(VPWidget *vp, int *w, int *h)
{
	return vp_engine_texture(vp->eng, w, h);
}

void vp_render(VPWidget *ctx, float w, float h)
{
	if(!ctx){
		g_preview_has_img = 0;
		igPushFont(NULL,small_icon_size-20);
		if(igButton("Drag And Drop Here",(ImVec2){w,h})){

			const char *f =
	    			tinyfd_openFileDialog("Open Video File", "", 0, NULL, NULL, 0);
			if (f)
				drop_callback(window,1,&f);
		}
		igPopFont();
		return;
	}
	VPEngine *e = ctx->eng;
	igPushID_Ptr(ctx);
	vp_engine_update(e);
	int texture_width = 0, texture_height = 0;
	unsigned int tex =
	    vp_engine_texture(e, &texture_width, &texture_height);
	float box_w = w;
	float box_h = h;
	if (box_h < 1)
		box_h = 1;
	ImVec2_c box_origin = igGetCursorScreenPos();
	ImDrawList *dl = igGetWindowDrawList();
	g_preview_box_min = (ImVec2){box_origin.x, box_origin.y};
	g_preview_box_max = (ImVec2){box_origin.x + box_w, box_origin.y + box_h};
	if (tex && texture_width > 0 && texture_height > 0) {
		float tex_aspect_ratio =
		    (float)texture_width / (float)texture_height;
		float display_width = box_w,
		      display_height = display_width / tex_aspect_ratio;
		if (display_height > box_h) {
			display_height = box_h;
			display_width = display_height * tex_aspect_ratio;
		}
		float ox = (box_w - display_width) * 0.5f;
		float oy = (box_h - display_height) * 0.5f;
		ImVec2 p0 = {box_origin.x + ox, box_origin.y + oy};
		ImVec2 p1 = {p0.x + display_width, p0.y + display_height};
		ImTextureRef tex_ref = {0};
		tex_ref._TexID = (ImTextureID)(uintptr_t)tex;
		ImDrawList_AddImage(dl, tex_ref, p0, p1, (ImVec2){0, 0},
				    (ImVec2){1, 1}, 0xFFFFFFFF);
		g_preview_img_min = p0;
		g_preview_img_max = p1;
		g_preview_has_img = 1;
	} else {
		g_preview_has_img = 0;
	}
	ImVec2 border_p0 = {box_origin.x, box_origin.y};
	ImVec2 border_p1 = {box_origin.x + box_w, box_origin.y + box_h};
	ImDrawList_AddRect(dl, border_p0, border_p1, 0xFF888888, 0.0f, 1.0f, 0);
	igDummy((ImVec2_c){box_w, box_h});


	double cur = vp_engine_position(e);
	double dur_d = vp_engine_duration(e);
	float dur = (float)(dur_d > 0.0 ? dur_d : 1.0);

	float sval = vp->ui_seek_active ? vp->ui_seek_val : (float)cur;

	if (vp_engine_is_playing(e)) {
		if (igButton(i_pause"##vp", (ImVec2_c){0, 0}))
			vp_engine_pause(e);
	} else {
		if (igButton(i_play3"##vp", (ImVec2_c){0, 0}))
			vp_engine_play(e);
	}
	igSameLine(0.0f, -1.0f);
	igSetNextItemWidth(w-32);
	if (igSliderFloat("##seek", &sval, 0.0f, dur, "%.2f s", 0)) {
		if (!vp->ui_seek_active) { /* drag start */
			vp->ui_seek_active = 1;
			vp_engine_scrub_begin(e);
			vp_engine_pause(e);
			vp->ui_seek_last_sent = -1.0f;
			vp->ui_last_seek_t = -1.0;
		}
		vp->ui_seek_val = sval;
	}
	/* While dragging, emit FAST (keyframe) seeks throttled to ~25/s so the
	   decoder isn't flooded with accurate seeks (which causes the video to
	   hang and trail the audio until the backlog drains). */
	if (vp->ui_seek_active) {
		double now = glfwGetTime();
		if (vp->ui_seek_val != vp->ui_seek_last_sent &&
		    (vp->ui_last_seek_t < 0.0 ||
		     now - vp->ui_last_seek_t >= 0.04)) {
			vp_engine_seek(e, vp->ui_seek_val, false);
			vp->ui_seek_last_sent = vp->ui_seek_val;
			vp->ui_last_seek_t = now;
		}
	}
	if (vp->ui_seek_active && !igIsItemActive()) { /* drag released */
		vp->ui_seek_active = 0;
		vp_engine_seek(e, vp->ui_seek_val, true); /* land exactly */
		vp_engine_scrub_end(e);
		vp_engine_play(e);
	}


	/* igSameLine(0.0f, -1.0f); */
	if(igButton(i_fire" Remove Video",(ImVec2){0,0})){
		vp_destroy(ctx);
		vp = NULL;
		reset_edit_state();
		/* refresh_video_info(); */
	}
	igPopID();
}


/* Video Player widget end*/

VideoConfig video_config                    = {0};

char current_video_path[OS_PATHMAX]         = {0};

int src_width                               = 0;
int src_height                              = 0;
double src_duration                         = 0.0;
int has_audio_src                           = 0;

VPStreamInfo audio_tracks[32];
int audio_track_count                       = 0;
bool audio_removed[32]                      = {0};

int crop_enabled                            = 0;
float crop_l                                = 0.0f;
float crop_t                                = 0.0f;
float crop_r                                = 1.0f;
float crop_b                                = 1.0f;

bool should_trim                            = false;
int preview_dirty                           = 1;

int scale_enabled                           = 0;
int scale_w                                 = 1280;
int scale_h                                 = 720;
int scale_keep_aspect                       = 1;

char add_thumbnail_path[OS_PATHMAX]         = {0};

char add_subtitle_path[OS_PATHMAX]          = {0};
char add_subtitle_lang[16]                  = "und";

int remove_sub_enabled                      = 0;
int remove_sub_index                        = 0;

int app_reset_seq                           = 0;

char original_aspect_ratio[1 << 8];
char original_aspect_ratio_reverse[1 << 8];

GLuint thumbnail_texture                    = 0;
int texture_width                           = 0;
int texture_height                          = 0;

char tmp_buffer[1 << 12];

const char *error_message                   = NULL;
const char *error_title                     = NULL;

bool show_crop                              = false;

Logger logger ;

ImVec2 rotate_point(ImVec2 p, float deg)
{
	float r = deg * (float)M_PI / 180.0f;
	return (ImVec2){p.x * cosf(r) - p.y * sinf(r),
			p.x * sinf(r) + p.y * cosf(r)};
}
static inline void os_human_size(long long bytes, char *out, int size) {
    const char *u[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
    double n = (double)bytes;
    int i = 0;

    while (n >= 1024.0 && i < 6) {
        n /= 1024.0;
        i++;
    }

    if (i == 0)
        snprintf(out, size, "%lld %s", bytes, u[i]);
    else
        snprintf(out, size, "%.2f %s", n, u[i]);
}



void jh_generate_log()
{

	char human_readable_size[32];
	long long file_size = os_filesize(current_video_path);
	os_human_size(file_size,human_readable_size,sizeof(human_readable_size));

	log_info(&logger, "Input File     :%s", current_video_path);
	log_info(&logger, "Input File Size:%s",human_readable_size);
	log_info(&logger, "Input File:         %s",current_video_path);
	log_info(&logger, "Input File:         %s",current_video_path);
	log_info(&logger, "Input File:         %s",current_video_path);
}


// https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples#example-for-opengl-users
bool load_texture_from_mem(const void *data, size_t data_size,
			   GLuint *out_texture, int *out_width, int *out_height)
{
	// Load from file
	int image_width = 0;
	int image_height = 0;
	unsigned char *image_data =
	    stbi_load_from_memory((const unsigned char *)data, (int)data_size,
				  &image_width, &image_height, NULL, 4);
	if (image_data == NULL)
		return false;
	// Create a OpenGL texture identifier
	GLuint image_texture;
	glGenTextures(1, &image_texture);
	glBindTexture(GL_TEXTURE_2D, image_texture);
	// Setup filtering parameters for display
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// Upload pixels into texture
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, image_data);
	stbi_image_free(image_data);
	*out_texture = image_texture;
	*out_width = image_width;
	*out_height = image_height;
	return true;
}

bool load_texture_from_file(const char *file_name, GLuint *out_texture,
			    int *out_width, int *out_height)
{
	FILE *f = fopen(file_name, "rb");
	if (f == NULL)
		return false;
	fseek(f, 0, SEEK_END);
	size_t file_size = (size_t)ftell(f);
	if (file_size == -1)
		return false;
	fseek(f, 0, SEEK_SET);
	void *file_data = malloc(file_size);
	fread(file_data, 1, file_size, f);
	fclose(f);
	bool ret = load_texture_from_mem(file_data, file_size, out_texture,
					 out_width, out_height);
	free(file_data);
	return ret;
}

void error_dialog_render()
{
	if (!error_message)
		return;
	if (!error_title)
		error_title = "Error";
	igOpenPopup_Str(error_title, 0);
	ImGuiViewport *vp = igGetMainViewport();
	ImVec2 center = {vp->WorkPos.x + vp->WorkSize.x * 0.5f,
			 vp->WorkPos.y + vp->WorkSize.y * 0.5f};
	igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
	if (igBeginPopupModal(error_title, NULL,
			      ImGuiWindowFlags_AlwaysAutoResize)) {
		igText(error_message);
		igSpacing();
		if (igButton("OK", (ImVec2){120, 0})) {
			igCloseCurrentPopup();
			error_title = NULL;
			error_message = NULL;
		}
		igEndPopup();
	}
}

int gcd(int a, int b)
{ 
	return (b == 0) ? a : gcd(b, a % b); 
}

ImVec2 get_aspect_ratio(ImVec2 dimension)
{
	int hcf = gcd(dimension.x, dimension.y);
	return (ImVec2){dimension.x / hcf, dimension.y / hcf};
}

ImVec2 fit_image(int square_size, ImVec2 input)
{
	ImVec2 res = get_aspect_ratio(input);
	float ratio = res.x / res.y;
	float width = square_size;
	float height = width / ratio;
	if (height > square_size) {
		height = square_size;
		width = height * ratio;
	}
	return (ImVec2){width, height};
}

#define A(x, y) #x ":" #y
#define AR(x, y) #y ":" #x
void rebuild_preview_filters(void)
{
	if (!vp)
		return;
	VPEngine *e = vp->eng;
	char vf[JH_BUFFER_MAX];
	int n = 0;
	vf[0] = 0;

	/* Crop is never baked into the preview: it is an interactive overlay
	   drawn on the displayed (rotated/flipped) frame, just like a real
	   editor. Rotate/flip/scale stay applied so the preview reflects them
	   while cropping. Only the aspect-ratio pad/scale is suppressed during
	   crop, so the user crops the pre-aspect frame (export crops before it
	   applies the aspect filter too). */
	int rot = ((video_config.rotate % 360) + 360) % 360;
	if (rot == 90)
		n += snprintf(vf + n, sizeof(vf) - n, "%stranspose=1",
			      n ? "," : "");
	else if (rot == 180)
		n += snprintf(vf + n, sizeof(vf) - n,
			      "%stranspose=1,transpose=1", n ? "," : "");
	else if (rot == 270)
		n += snprintf(vf + n, sizeof(vf) - n, "%stranspose=2",
			      n ? "," : "");
	else if (rot != 0)
		n += snprintf(
		    vf + n, sizeof(vf) - n,
		    "%srotate=%d*PI/180:ow=rotw(%d*PI/180):oh=roth(%d*PI/180)",
		    n ? "," : "", rot, rot, rot);

	if (video_config.flip_h)
		n += snprintf(vf + n, sizeof(vf) - n, "%shflip", n ? "," : "");
	if (video_config.flip_v)
		n += snprintf(vf + n, sizeof(vf) - n, "%svflip", n ? "," : "");

	if (scale_enabled && scale_w >= 2) {
		if (scale_keep_aspect)
			n += snprintf(vf + n, sizeof(vf) - n, "%sscale=%d:-2",
				      n ? "," : "", scale_w & ~1);
		else if (scale_h >= 2)
			n += snprintf(vf + n, sizeof(vf) - n, "%sscale=%d:%d",
				      n ? "," : "", scale_w & ~1, scale_h & ~1);
	}

	if (!crop_enabled && video_config.aspect_ratio.x > 0.0f &&
	    video_config.aspect_ratio.y > 0.0f)
		n += snprintf(vf + n, sizeof(vf) - n,
			      "%sscale=trunc(ih*%d/%d/2)*2:ih,setsar=1",
			      n ? "," : "", (int)video_config.aspect_ratio.x,
			      (int)video_config.aspect_ratio.y);

	vp_engine_set_vf(e, vf[0] ? vf : NULL);

	char af[JH_BUFFER_MAX];
	int m = 0;
	af[0] = 0;
	if (video_config.mute) {
		vp_engine_set_muted(e, true);
	} else {
		vp_engine_set_muted(e, false);
		if (video_config.scale_volume != 0) {
			float v =
			    1.0f + (float)video_config.scale_volume / 100.0f;
			if (v < 0.0f)
				v = 0.0f;
			m += snprintf(af + m, sizeof(af) - m, "volume=%.3f", v);
		}
		if (video_config.sterio_to_mono)
			m += snprintf(af + m, sizeof(af) - m,
				      "%saformat=channel_layouts=mono",
				      m ? "," : "");
	}
	vp_engine_set_af(e, af[0] ? af : NULL);
}

void set_rotation(int angle) { video_config.rotate = angle; }

/* The crop overlay sits on the DISPLAYED frame (source after rotate+flip).
   The export pipeline crops the SOURCE first (before it rotates/flips), so the
   displayed-frame crop fractions have to be mapped back into source-frame
   fractions. Exact for rotations that are multiples of 90 degrees; arbitrary
   custom angles are snapped to the nearest quarter-turn for this mapping. */
static void crop_disp_to_src(float dl, float dt, float dr, float db, int rotate,
			     bool fh, bool fv, float *sl, float *st, float *sr,
			     float *sb)
{
	int R = ((rotate % 360) + 360) % 360;
	int q = ((R + 45) / 90) % 4; /* 0->0, 1->90, 2->180, 3->270 (CW) */
	float cx[4] = {dl, dr, dr, dl};
	float cy[4] = {dt, dt, db, db};
	float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
	for (int i = 0; i < 4; i++) {
		float u = cx[i], v = cy[i];
		/* export applies flip AFTER rotate, so undo flip first */
		if (fh)
			u = 1.0f - u;
		if (fv)
			v = 1.0f - v;
		float su, sv;
		switch (q) {
		case 1: su = v;        sv = 1.0f - u; break; /* undo 90 CW  */
		case 2: su = 1.0f - u; sv = 1.0f - v; break; /* undo 180    */
		case 3: su = 1.0f - v; sv = u;        break; /* undo 270 CW */
		default: su = u;       sv = v;        break; /* 0           */
		}
		if (su < mnx) mnx = su;
		if (su > mxx) mxx = su;
		if (sv < mny) mny = sv;
		if (sv > mxy) mxy = sv;
	}
	if (mnx < 0.0f) mnx = 0.0f;
	if (mny < 0.0f) mny = 0.0f;
	if (mxx > 1.0f) mxx = 1.0f;
	if (mxy > 1.0f) mxy = 1.0f;
	*sl = mnx;
	*st = mny;
	*sr = mxx;
	*sb = mxy;
}

typedef struct {
	char *label;
	char *tooltip;
	void (*on_click)(void *);
} BtnItem;

typedef struct {
	char *label;
	bool *status;
	char *tooltip;
	void (*on_click)(void *);
} ChkBtnItem;

typedef struct {
	char *label;
	int id;
	char *tooltip;
	void (*on_click)(void *);
	void *user_data;
} RadioBtnItem;


bool jh_chk_button(const char *label, bool *status, ImVec2 size)
{
	if (*status) {
			if(vp)
				igPushStyleColor_Vec4(ImGuiCol_Button,JRGB(22.0f,222.0f,53.0f));
		else
				igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.3f, 0.3f, 0.3f, 1.0f});

	}
	bool clicked = igButton(label, size);
	if (*status) {
		igPopStyleColor(1);
	}
	if (clicked) {
		*status = !(*status);
	}
	return *status;
}

bool jh_radio_button(const char *label, int *v, int id, ImVec2 size)
{
	bool is_selected = (*v == id);
	if (is_selected) {

		igPushStyleColor_Vec4(ImGuiCol_Button,JRGB(22.0f,222.0f,53.0f));
	}
	bool clicked = igButton(label, size);
	if (is_selected) {
		igPopStyleColor(1);
	}
	if (clicked) {
		if (is_selected) {
			*v = -1;
			return false;
		}
		*v = id;
		return true;
	}
	return false;
}

/* Filmstrip texture cache for the trim track. The worker (thumb_strip.c)
   produces RGB24 cells; we upload each to a GL texture once, on the UI thread,
   and free them all on every new file load via ts_tex_reset(). */
static unsigned int g_ts_tex[TS_CELLS];
static int          g_ts_tex_built[TS_CELLS];
static int          g_ts_tex_w[TS_CELLS], g_ts_tex_h[TS_CELLS];

static unsigned int ts_upload_tex(const uint8_t *rgb, int w, int h)
{
	if (!rgb || w <= 0 || h <= 0)
		return 0;
	unsigned int tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB,
		     GL_UNSIGNED_BYTE, rgb);
	glBindTexture(GL_TEXTURE_2D, 0);
	return tex;
}

/* Must run on the main/GL thread. Called from reset_edit_state on load. */
void ts_tex_reset(void)
{
	for (int i = 0; i < TS_CELLS; i++) {
		if (g_ts_tex_built[i] && g_ts_tex[i])
			glDeleteTextures(1, &g_ts_tex[i]);
		g_ts_tex[i] = 0;
		g_ts_tex_built[i] = 0;
		g_ts_tex_w[i] = g_ts_tex_h[i] = 0;
	}
}

#define TL_HEADER_W 132.0f


void TimelineTrimWidget(const char *label, float *trim_start, float *trim_end,
			float duration, ImVec2 size, int *out_dragging,
			float *out_scrub, bool show_trim)
{
	static int dragging = 0;
	static float drag_offset = 0.0f;
	ImDrawList *dl = igGetWindowDrawList();
	ImVec2 pos = igGetCursorScreenPos();
	ImVec2 mouse = igGetMousePos();
	float w = size.x;
	float h = size.y;
	float hw = 12.0f;
	float y0 = pos.y;
	float y1 = pos.y + h;

	float tx0 = pos.x + TL_HEADER_W;
	float tw = w - TL_HEADER_W;
	if (tw < 10.0f)
		tw = 10.0f;

	float sx = tx0 + (*trim_start / duration) * tw;
	float ex = tx0 + (*trim_end / duration) * tw;
	float min_px = hw * 2 + 4.0f;
	bool hit_left = show_trim && mouse.x >= sx - hw && mouse.x <= sx + hw &&
			mouse.y >= y0 && mouse.y <= y1;
	bool hit_right = show_trim && mouse.x >= ex - hw && mouse.x <= ex + hw &&
			 mouse.y >= y0 && mouse.y <= y1;
	bool hit_body = show_trim && !hit_left && !hit_right && mouse.x > sx + hw &&
			mouse.x < ex - hw && mouse.y >= y0 && mouse.y <= y1;
	igSetCursorScreenPos(pos);
	igSetNextItemAllowOverlap();
	igDummy(size);
	ImVec2 after = igGetCursorScreenPos();
	if (igIsMouseClicked_Bool(0, false)) {
		if (hit_right) {
			dragging = 2;
		} else if (hit_left) {
			dragging = 1;
		} else if (hit_body) {
			dragging = 3;
			drag_offset = mouse.x - sx;
		}
	}
	if (igIsMouseReleased_Nil(0))
		dragging = 0;
	if (igIsMouseDown_Nil(0) && dragging > 0) {
		if (dragging == 1) {
			float t = (mouse.x - tx0) / tw * duration;
			float min_start = (hw / tw) * duration;
			float max_start = *trim_end - (min_px / tw * duration);
			t = fmaxf(min_start, fminf(t, max_start));
			*trim_start = t;
			igSetMouseCursor(ImGuiMouseCursor_ResizeEW);
		} else if (dragging == 2) {
			float t = (mouse.x - tx0) / tw * duration;
			float min_end = *trim_start + (min_px / tw * duration);
			float max_end = duration - (hw / tw) * duration;
			t = fminf(max_end, fmaxf(t, min_end));
			*trim_end = t;
			igSetMouseCursor(ImGuiMouseCursor_ResizeEW);
		} else if (dragging == 3) {
			float len = *trim_end - *trim_start;
			float new_sx = mouse.x - drag_offset;
			float t = (new_sx - tx0) / tw * duration;
			float t_min = (hw / tw) * duration;
			float t_max = duration - len - (hw / tw) * duration;
			t = fmaxf(t_min, fminf(t, t_max));
			*trim_start = t;
			*trim_end = t + len;
			igSetMouseCursor(ImGuiMouseCursor_ResizeAll);
		}
	} else if (dragging == 0) {
		if (hit_left || hit_right)
			igSetMouseCursor(ImGuiMouseCursor_ResizeEW);
		else if (hit_body)
			igSetMouseCursor(ImGuiMouseCursor_ResizeAll);
	}
	sx = tx0 + (*trim_start / duration) * tw;
	ex = tx0 + (*trim_end / duration) * tw;
	ImDrawList_PushClipRect(dl, (ImVec2){pos.x, y0},
				(ImVec2){pos.x + w, y1}, true);

	ImDrawList_AddRectFilled(dl, (ImVec2){pos.x, y0}, (ImVec2){tx0, y1},
				 IM_COL32(18, 18, 22, 255), 0, 0);

	int tsn = ts_count();
	if (tsn > 0) {
		float cellw = tw / (float)tsn;
		for (int ci = 0; ci < tsn; ci++) {
			float cx0 = tx0 + ci * cellw;
			float cx1 = cx0 + cellw;
			if (!g_ts_tex_built[ci] && ts_cell_ready(ci) == 1) {
				int tw2 = 0, thh = 0;
				const uint8_t *rgb =
				    ts_cell_rgb(ci, &tw2, &thh);
				if (rgb) {
					g_ts_tex[ci] =
					    ts_upload_tex(rgb, tw2, thh);
					g_ts_tex_w[ci] = tw2;
					g_ts_tex_h[ci] = thh;
					g_ts_tex_built[ci] = 1;
				}
			}
			if (g_ts_tex_built[ci] && g_ts_tex[ci]) {
				float cellAR = (cx1 - cx0) / (y1 - y0);
				float texAR = (float)g_ts_tex_w[ci] /
					      (float)g_ts_tex_h[ci];
				float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
				if (texAR > cellAR) {
					float uvw = cellAR / texAR;
					u0 = (1.0f - uvw) * 0.5f;
					u1 = 1.0f - u0;
				} else {
					float uvh = texAR / cellAR;
					v0 = (1.0f - uvh) * 0.5f;
					v1 = 1.0f - v0;
				}
				ImTextureRef ref = {0};
				ref._TexID =
				    (ImTextureID)(uintptr_t)g_ts_tex[ci];
				ImDrawList_AddImage(
				    dl, ref, (ImVec2){cx0, y0},
				    (ImVec2){cx1, y1}, (ImVec2){u0, v0},
				    (ImVec2){u1, v1}, 0xFFFFFFFF);
			} else {
				ImDrawList_AddRectFilled(
				    dl, (ImVec2){cx0, y0}, (ImVec2){cx1, y1},
				    IM_COL32(28, 28, 32, 255), 0, 0);
			}
		}
	} else {
		ImDrawList_AddRectFilled(dl, (ImVec2){tx0, y0},
					 (ImVec2){tx0 + tw, y1}, 0xFF555555,
					 0.0f, 0);
	}

	if (show_trim) {
		ImDrawList_AddRectFilled(dl, (ImVec2){tx0, y0}, (ImVec2){sx, y1},
					 IM_COL32(0, 0, 0, 150), 0, 0);
		ImDrawList_AddRectFilled(dl, (ImVec2){ex, y0}, (ImVec2){tx0 + tw, y1},
					 IM_COL32(0, 0, 0, 150), 0, 0);
		ImDrawList_AddRectFilled(dl, (ImVec2){sx, y0}, (ImVec2){ex, y1},
					 IM_COL32(34, 136, 255, 64), 0, 0);
		ImDrawList_AddRect(dl, (ImVec2){sx, y0}, (ImVec2){ex, y1},
				   IM_COL32(34, 136, 255, 255), 0.0f, 2.0f, 0);
		ImDrawList_AddRectFilled(dl, (ImVec2){sx - hw, y0},
					 (ImVec2){sx + hw, y1}, 0xFFCCCCCC, 4.0f, 0);
		ImDrawList_AddRect(dl, (ImVec2){sx - hw, y0}, (ImVec2){sx + hw, y1},
				   0xFF000000, 4.0f, 1.5f, 0);
		for (int i = 0; i < 3; i++)
			ImDrawList_AddCircleFilled(
			    dl, (ImVec2){sx, y0 + h * 0.5f + (i - 1) * 5.0f}, 2.0f,
			    0xFF333333, 8);
		ImDrawList_AddRectFilled(dl, (ImVec2){ex - hw, y0},
					 (ImVec2){ex + hw, y1}, 0xFFCCCCCC, 4.0f, 0);
		ImDrawList_AddRect(dl, (ImVec2){ex - hw, y0}, (ImVec2){ex + hw, y1},
				   0xFF000000, 4.0f, 1.5f, 0);
		for (int i = 0; i < 3; i++)
			ImDrawList_AddCircleFilled(
			    dl, (ImVec2){ex, y0 + h * 0.5f + (i - 1) * 5.0f}, 2.0f,
			    0xFF333333, 8);
	}
	ImDrawList_PopClipRect(dl);

	igSetCursorScreenPos((ImVec2){pos.x + 8.0f,
				      y0 + (h - igGetTextLineHeight()) * 0.5f});
	igText("%s Video", i_vpu_icon_thumbnail);
	igSetCursorScreenPos(after);

	if (out_dragging)
		*out_dragging = dragging;
	if (out_scrub)
		*out_scrub = (dragging == 2) ? *trim_end : *trim_start;
}
#include "test.c"
#include "ve_export.c"
#include "audio_wave.c"

/* Waveform texture cache (UI layer keeps the GL bits; audio_wave.c stays pure
   CPU). Each track's envelope is rasterized into one alpha texture the first
   time it becomes ready, then blitted (and tinted) every frame instead of
   re-emitting hundreds of line segments. Freed on every new file load. */
#define WAVE_TEX_W 1024
#define WAVE_TEX_H 64
static unsigned int g_wave_tex[32];
static int          g_wave_tex_built[32];
static unsigned int build_wave_texture(const float *pk, int npk)
{
	if (!pk || npk <= 0)
		return 0;
	unsigned char *px = calloc((size_t)WAVE_TEX_W * WAVE_TEX_H * 4, 1);
	if (!px)
		return 0;
	for (int x = 0; x < WAVE_TEX_W; x++) {
		int b = (int)((long long)x * npk / WAVE_TEX_W);
		if (b < 0)
			b = 0;
		else if (b >= npk)
			b = npk - 1;
		float a = pk[b];
		if (a < 0.0f)
			a = 0.0f;
		else if (a > 1.0f)
			a = 1.0f;
		int bar = (int)(a * (WAVE_TEX_H - 1) + 0.5f);
		if (bar < 1 && a > 1e-4f)
			bar = 1;
		for (int dy = 0; dy <= bar; dy++) {
			int y = WAVE_TEX_H - 1 - dy;
			if (y < 0 || y >= WAVE_TEX_H)
				continue;
			unsigned char *p =
			    px + ((size_t)y * WAVE_TEX_W + x) * 4;
			p[0] = p[1] = p[2] = p[3] = 255;
		}
	}
	unsigned int tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WAVE_TEX_W, WAVE_TEX_H, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, px);
	glBindTexture(GL_TEXTURE_2D, 0);
	free(px);
	return tex;
}
static unsigned int build_wave_texture1(const float *pk, int npk)
{
	if (!pk || npk <= 0)
		return 0;
	unsigned char *px = calloc((size_t)WAVE_TEX_W * WAVE_TEX_H * 4, 1);
	if (!px)
		return 0;
	int half = WAVE_TEX_H / 2;
	for (int x = 0; x < WAVE_TEX_W; x++) {
		int b = (int)((long long)x * npk / WAVE_TEX_W);
		if (b < 0)
			b = 0;
		else if (b >= npk)
			b = npk - 1;
		float a = pk[b];
		if (a < 0.0f)
			a = 0.0f;
		else if (a > 1.0f)
			a = 1.0f;
		int bar = (int)(a * (half - 1) + 0.5f);
		if (bar < 1 && a > 1e-4f)
			bar = 1;
		for (int dy = -bar; dy <= bar; dy++) {
			int y = half + dy;
			if (y < 0 || y >= WAVE_TEX_H)
				continue;
			unsigned char *p =
			    px + ((size_t)y * WAVE_TEX_W + x) * 4;
			p[0] = p[1] = p[2] = p[3] = 255; /* white, opaque */
		}
	}
	unsigned int tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WAVE_TEX_W, WAVE_TEX_H, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, px);
	glBindTexture(GL_TEXTURE_2D, 0);
	free(px);
	return tex;
}

/* Must run on the main/GL thread. Called from reset_edit_state on load. */
void wave_tex_reset(void)
{
	for (int i = 0; i < 32; i++) {
		if (g_wave_tex_built[i] && g_wave_tex[i])
			glDeleteTextures(1, &g_wave_tex[i]);
		g_wave_tex[i] = 0;
		g_wave_tex_built[i] = 0;
	}
}

#define AUDIO_TL_MAX_VISIBLE_ROWS 3

static void AudioTracksTimeline(float width, float trim_start, float trim_end,
				float dur)
{
	/* const float H = 23.0f, pad = 6.0f, btn = 30.0f; */
	const float H = 23.0f, pad = 6.0f, btn = 20.0f;
	if (dur <= 0.0f)
		dur = 1.0f;
	if (!vp || audio_track_count <= 0) {
		igTextDisabled("No audio tracks");
		return;
	}

	float sp = igGetStyle()->ItemSpacing.y;
	float stride = H + sp;
	int vis = audio_track_count < AUDIO_TL_MAX_VISIBLE_ROWS
		      ? audio_track_count
		      : AUDIO_TL_MAX_VISIBLE_ROWS;
	float child_h = vis * stride + sp;

	igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){0, 0});
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing,(ImVec2){0, 0});
	igBeginChild_Str("##audio_tracks_scroll", (ImVec2){width, child_h}, 0,
			 0);
	ImDrawList *dl = igGetWindowDrawList();
	double pos = vp_engine_position(vp->eng);

	for (int i = 0; i < audio_track_count; i++) {
		igPushID_Int(i);
		ImVec2 o = igGetCursorScreenPos();
		float x0 = o.x + TL_HEADER_W, x1 = o.x + width, ww = x1 - x0;
		if (ww < 10.0f)
			ww = 10.0f;
		bool removed = audio_removed[i];
		int state = aw_ready(i);

		ImU32 bg = removed ? IM_COL32(38, 38, 42, 255)
				   : IM_COL32(52, 52, 60, 255);
		ImDrawList_AddRectFilled(dl, (ImVec2){x0, o.y},
					 (ImVec2){x1, o.y + H}, bg, 0, 0);
		ImDrawList_AddRectFilled(dl, o, (ImVec2){o.x + TL_HEADER_W,
							 o.y + H},
					 IM_COL32(18, 18, 22, 255), 0, 0);

		ImDrawList_PushClipRect(dl, (ImVec2){x0, o.y},
					(ImVec2){x1, o.y + H}, true);

		if (state == 1) {
			if (!g_wave_tex_built[i]) {
				int npk = 0;
				const float *pk = aw_peaks(i, &npk);
				g_wave_tex[i] = build_wave_texture(pk, npk);
				g_wave_tex_built[i] = 1;
			}
			if (g_wave_tex[i]) {
				ImTextureRef ref = {0};
				ref._TexID =
				    (ImTextureID)(uintptr_t)g_wave_tex[i];
				ImU32 wcol = removed
						 ? IM_COL32(110, 110, 118, 255)
						 : IM_COL32(90, 170, 255, 255);
				ImDrawList_AddImage(
				    dl, ref, (ImVec2){x0, o.y},
				    (ImVec2){x1, o.y + H},
				    (ImVec2){0, 0}, (ImVec2){1, 1}, wcol);
			}
		}

		if (trim_start > 0.0f) {
			float xs = x0 + (trim_start / dur) * ww;
			ImDrawList_AddRectFilled(dl, (ImVec2){x0, o.y},
						 (ImVec2){xs, o.y + H},
						 IM_COL32(0, 0, 0, 120), 0, 0);
		}
		if (trim_end > 0.0f && trim_end < dur) {
			float xe = x0 + (trim_end / dur) * ww;
			ImDrawList_AddRectFilled(dl, (ImVec2){xe, o.y},
						 (ImVec2){x1, o.y + H},
						 IM_COL32(0, 0, 0, 120), 0, 0);
		}
		{
			float xp = x0 + ((float)pos / dur) * ww;
			if (xp >= x0 && xp <= x1)
				ImDrawList_AddLine(
				    dl, (ImVec2){xp, o.y},
				    (ImVec2){xp, o.y + H},
				    IM_COL32(255, 255, 255, 180), 1.0f);
		}
		ImDrawList_PopClipRect(dl);

		igSetCursorScreenPos(
		    (ImVec2){o.x + pad, o.y + (H - btn) * 0.5f});
		const char *blab =
		    removed ? i_music "##rm" : i_vpu_icon_delete_sub "##rm";
		if (igButton(blab, (ImVec2){btn, btn}))
			audio_removed[i] = !audio_removed[i];
		tooltip(removed ? "Restore this audio track"
				: "Remove this audio track");

		igSetCursorScreenPos(
		    (ImVec2){o.x + pad + btn + 8,
			     o.y + (H - igGetTextLineHeight()) * 0.5f});
		const char *lang = audio_tracks[i].lang[0]
				       ? audio_tracks[i].lang
				       : "und";
		if (removed)
			igText("%s #%d", lang, audio_tracks[i].index);
		else if (state == 0)
			igText("%s #%d \xE2\x80\xA6", lang,
			       audio_tracks[i].index);
		else
			igText("%s #%d", lang, audio_tracks[i].index);
		igSetCursorScreenPos((ImVec2){x0, o.y});
		if (igInvisibleButton("wave", (ImVec2){ww, H}, 0) && !removed)
			vp_engine_set_audio_track(vp->eng,
						  audio_tracks[i].index);

		igSetCursorScreenPos(o);
		igDummy((ImVec2){width, H});

		/* if(video_config.mute){ */
		/* 	audio_removed[i] = true; */
		/* } */

		igPopID();
	}
	igEndChild();
	igPopStyleVar(2);
}

void handle_add_thumbnail(void *ud)
{
	(void)ud;
	const char *f = tinyfd_openFileDialog("Select thumbnail image", "", 0,
					      NULL, NULL, 0);
	if (f)
		ve_copy(add_thumbnail_path, sizeof(add_thumbnail_path), f);
}
void handle_add_subtitle(void *ud)
{
	(void)ud;
	const char *f =
	    tinyfd_openFileDialog("Select subtitle file", "", 0, NULL, NULL, 0);
	if (f)
		ve_copy(add_subtitle_path, sizeof(add_subtitle_path), f);
}
static bool open_remove_sub_popup = false;
void handle_remove_subtitle(void *ud)
{
	(void)ud;
	open_remove_sub_popup = true;
}
void handle_trim_noop(void *ud) { (void)ud; }

void render_remove_sub_popup(void)
{
	if (open_remove_sub_popup) {
		igOpenPopup_Str("Remove subtitle", 0);
		open_remove_sub_popup = false;
	}
	ImGuiViewport *mv = igGetMainViewport();
	ImVec2 center = {mv->WorkPos.x + mv->WorkSize.x * 0.5f,
			 mv->WorkPos.y + mv->WorkSize.y * 0.5f};
	igSetNextWindowPos(center, ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
	if (igBeginPopupModal("Remove subtitle", NULL,
			      ImGuiWindowFlags_AlwaysAutoResize)) {
		igText("Subtitle stream index to remove:");
		igSetNextItemWidth(160);
		igInputInt("##rmsub_idx", &remove_sub_index, 1, 1, 0);
		if (remove_sub_index < 0)
			remove_sub_index = 0;
		igSpacing();
		if (igButton("Remove", (ImVec2){120, 0})) {
			remove_sub_enabled = 1;
			igCloseCurrentPopup();
		}
		igSameLine(0, 8);
		if (igButton("Cancel", (ImVec2){120, 0})) {
			remove_sub_enabled = 0;
			igCloseCurrentPopup();
		}
		igEndPopup();
	}
}

void render_tools()
{
	igBeginGroup();
	int n = 7; // total items 
	ImVec2 btn_size = {145,145};
	float total_btn = btn_size.x * n;
	// float spacing = (igGetContentRegionAvail().x - total_btn) / (n - 1);
	// igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){spacing, 0});
	igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){10, 0});
		const int icon_size = big_icon_size;
	igPushFont(icon_font, icon_size);
	if (igButton(i_vpu_icon_thumbnail, btn_size))
		handle_add_thumbnail(NULL);
	igPopFont();
	tooltip("Add a cover thumbnail");
	igSameLine(0.0f, -1.0f);


	igPushFont(icon_font, icon_size);
	if (igButton(i_VPU_Icon_Vector_Remove_Thumbnail_2, btn_size))
		handle_add_thumbnail(NULL);
	igPopFont();
	tooltip("Remove Thumbnail");
	igSameLine(0.0f, -1.0f);


	igPushFont(icon_font, icon_size);
	if (igButton(i_VPU_Icon_Vector_Select_Frame_as_Thumbnail, btn_size))
		handle_add_thumbnail(NULL);
	igPopFont();
	tooltip("This frame as thumbnail");
	igSameLine(0.0f, -1.0f);



	igPushFont(icon_font, icon_size);
	jh_chk_button(i_vpu_icon_crop,&crop_enabled,btn_size);
	igPopFont();
	tooltip("Toogle Crop");
	igSameLine(0.0f, -1.0f);

	igPushFont(icon_font, icon_size);
	jh_chk_button(i_vpu_icon_trim,&should_trim,btn_size);
	igPopFont();
	tooltip("Toogle Trim");
	igSameLine(0.0f, -1.0f);

	igPushFont(icon_font, icon_size);
	if (igButton(i_vpu_icon_add_subs, btn_size))
		handle_add_subtitle(NULL);
	igPopFont();
	tooltip("Add subtitle");
	igSameLine(0.0f, -1.0f);

	igPushFont(icon_font, icon_size);
	if (igButton(i_vpu_icon_delete_sub, btn_size))
		handle_remove_subtitle(NULL);
	igPopFont();
	tooltip("Add subtitle");


	igPopStyleVar(1);
	igEndGroup();

	render_remove_sub_popup();
	if (add_thumbnail_path[0] || add_subtitle_path[0] ||
	    remove_sub_enabled) {
		igSpacing();
		if (add_thumbnail_path[0]) {
			igText("%s %s", i_vpu_icon_thumbnail,
			       add_thumbnail_path);
			igSameLine(0, 6);
			if (igButton("x##thumb", (ImVec2){0, 0}))
				add_thumbnail_path[0] = 0;
		}
		if (add_subtitle_path[0]) {
			igText("%s %s", i_vpu_icon_add_subs, add_subtitle_path);
			igSameLine(0, 6);
			igSetNextItemWidth(80);
			igInputText("lang##sub", add_subtitle_lang,
				    sizeof(add_subtitle_lang), 0, NULL, NULL);
			igSameLine(0, 6);
			if (igButton("x##sub", (ImVec2){0, 0}))
				add_subtitle_path[0] = 0;
		}
		if (remove_sub_enabled) {
			igText("%s remove subtitle #%d", i_vpu_icon_delete_sub,
			       remove_sub_index);
			igSameLine(0, 6);
			if (igButton("x##rmsub", (ImVec2){0, 0}))
				remove_sub_enabled = 0;
		}
	}
}

void render_single_click_items(ImVec2 size)
{
	VideoConfig cfg_before = video_config;
	int crop_before = crop_enabled;
	float cl0 = crop_l, ct0 = crop_t, cr0 = crop_r, cb0 = crop_b;
	enum RotateOption {
		ROTATE_90,
		ROTATE_180,
		ROTATE_270,
		ROTATE_CUSTOM,
		ROTATE_END,
		ROTATE_NONE = -1,
	};
	static int selected_rotation_button = ROTATE_NONE;
	static int local_reset_seq = -1;
	int do_reset = (local_reset_seq != app_reset_seq);
	local_reset_seq = app_reset_seq;
	if (do_reset)
		selected_rotation_button = ROTATE_NONE;
	igBeginChild_Str("##singleclick", size, ImGuiChildFlags_AutoResizeY, 0);
	static int custom_rotate_angle = 0;
	static RadioBtnItem items[ROTATE_END];
	items[ROTATE_90] = (RadioBtnItem){i_vpu_icon_rotate_90, ROTATE_90,
					  "Rotate 90 clockwise", NULL};
	items[ROTATE_180] = (RadioBtnItem){i_vpu_icon_rotate_180, ROTATE_180,
					   "Rotate 180", NULL};
	items[ROTATE_270] =
	    (RadioBtnItem){i_vpu_icon_rotate_270, ROTATE_270,
			   "Rotate 90 counter-clockwise", NULL};
	items[ROTATE_CUSTOM] = (RadioBtnItem){
	    i_vpu_icon_rotate_custom, ROTATE_CUSTOM, "Rotate by a custom angle",
	    NULL, &custom_rotate_angle};
	int n = ROTATE_END;
	float total_width = igGetContentRegionAvail().x;
	float spacing = igGetStyle()->ItemSpacing.x;
	float cell = (total_width - (n - 1) * spacing) / n;
	ImVec2 btn_size = {cell, cell};
	for (int i = 0; i < n; i++) {
		igPushFont(icon_font, small_icon_size);
		jh_radio_button(items[i].label, &selected_rotation_button,
				items[i].id, btn_size);
		igPopFont();
		tooltip(items[i].tooltip);
		if (i < (n - 1))
			igSameLine(0.0f, -1.0f);
	}
	switch (selected_rotation_button) {
	case ROTATE_90:
		video_config.rotate = 90;
		break;
	case ROTATE_180:
		video_config.rotate = 180;
		break;
	case ROTATE_270:
		video_config.rotate = 270;
		break;
	case ROTATE_CUSTOM:
		break;
	default:
		video_config.rotate = 0;
		break;
	}
	/* Reserve a fixed-height row for the custom-angle slider so
	   showing/hiding it never reflows everything below it. */
	{
		ImVec2 cur = igGetCursorScreenPos();
		if (selected_rotation_button == ROTATE_CUSTOM) {
			static float angle_deg = 0.0f;
			static float angle_rad = 0.0f;
			angle_rad = angle_deg * (3.14159265f / 180.0f);
			igSetNextItemWidth(total_width);
			if (igSliderAngle("##custom_angle", &angle_rad, -180.0f,
					  180.0f, "%.0f deg", 0))
				angle_deg = angle_rad * (180.0f / 3.14159265f);
			video_config.rotate = (int)angle_deg;
		} else {
			/* igDummy((ImVec2){total_width, row_h}); */
		}
		(void)cur;
	}

	static ChkBtnItem items2[] = {{i_vpu_icon_fliph, &video_config.flip_h,
				       "Flip horizontally", NULL},
				      {i_vpu_icon_flipv, &video_config.flip_v,
				       "Flip vertically", NULL}};
	n = ARR_LEN(items2);
	cell = (total_width - (n - 1) * spacing) / n;
	btn_size = (ImVec2){cell, cell};
	for (int i = 0; i < n; i++) {
		igPushFont(icon_font, big_icon_size);
		jh_chk_button(items2[i].label, items2[i].status, btn_size);
		igPopFont();
		tooltip(items2[i].tooltip);
		if (i < (n - 1))
			igSameLine(0.0f, -1.0f);
	}
	enum VolumeOption {
		VOLUME_NONE = -1,
		VOLUME_25_UP,
		VOLUME_25_DOWN,
		VOLUME_50_UP,
		VOLUME_50_DOWN
	};
	static RadioBtnItem items3[] = {
	    {i_vpu_icon_volume_50_up, VOLUME_50_UP, "Increase volume by 50%%",NULL},
	    {i_vpu_icon_volume_25_Up, VOLUME_25_UP, "Increase volume by 25%%",NULL},
	    {i_vpu_icon_volume_50_down, VOLUME_50_DOWN, "Reduce volume by 50%%",NULL},
	    {i_vpu_icon_volume_25_down, VOLUME_25_DOWN, "Reduce volume by 25%%",NULL},
	};
	n = ARR_LEN(items3);
	cell = (total_width - (n - 1) * spacing) / n;
	btn_size = (ImVec2){cell, cell};
	static int selected_volume_btn = VOLUME_NONE;
	if (do_reset)
		selected_volume_btn = VOLUME_NONE;
	for (int i = 0; i < n; i++) {
		igPushFont(icon_font, small_icon_size);
		jh_radio_button(items3[i].label, &selected_volume_btn,
				items3[i].id, btn_size);
		igPopFont();
		tooltip(items3[i].tooltip);
		if (i < (n - 1))
			igSameLine(0.0f, -1.0f);
	}
	switch (selected_volume_btn) {
	case VOLUME_25_UP:
		video_config.scale_volume = 25;
		break;
	case VOLUME_25_DOWN:
		video_config.scale_volume = -25;
		break;
	case VOLUME_50_UP:
		video_config.scale_volume = 50;
		break;
	case VOLUME_50_DOWN:
		video_config.scale_volume = -50;
		break;
	default:
		video_config.scale_volume = 0;
		break;
	}
	static ChkBtnItem items4[] = {
	    {i_vpu_icon_volume_mute, &video_config.mute, "Strip all audio",NULL},
	    {i_vpu_icon_stero2mono, &video_config.sterio_to_mono,
	     "Downmix stereo to mono", NULL},
	};
	n = ARR_LEN(items4);
	cell = (total_width - (n - 1) * spacing) / n;
	btn_size = (ImVec2){cell, cell};
	for (int i = 0; i < n; i++) {
		igPushFont(icon_font, big_icon_size);
		jh_chk_button(items4[i].label, items4[i].status, btn_size);
		igPopFont();
		tooltip(items4[i].tooltip);
		if (i < (n - 1))
			igSameLine(0.0f, -1.0f);
	}
	static const char *items_aspect_ratio[] = {A(16, 9),
						   A(4, 3),
						   A(3, 2),
						   A(21, 9),
						   A(5, 4),
						   A(1, 1),
						   A(18, 9),
						   A(32, 9),
						   A(16, 10),
						   A(2, 1),
						   A(4, 1),
						   A(3, 1),
						   original_aspect_ratio};
	static const char *items_aspect_ratio_reverse[] = {
	    AR(16, 9),
	    AR(4, 3),
	    AR(3, 2),
	    AR(21, 9),
	    AR(5, 4),
	    AR(1, 1),
	    AR(18, 9),
	    AR(32, 9),
	    AR(16, 10),
	    AR(2, 1),
	    AR(4, 1),
	    AR(3, 1),
	    original_aspect_ratio_reverse};
	static const char **combo_box_item[] = {items_aspect_ratio,
						items_aspect_ratio_reverse};
	static int idx = 0;
	n = 2;
	cell = (total_width - (n - 1) * spacing) / n;
	igSetNextItemWidth(cell);
	static int aspect_ratio_selected = ARR_LEN(items_aspect_ratio) - 1;
	if (do_reset) {
		idx = 0;
		aspect_ratio_selected = ARR_LEN(items_aspect_ratio) - 1;
	}
	bool ratio_state_changed = igCombo_Str_arr(
	    "##ratio", &aspect_ratio_selected, combo_box_item[idx],
	    ARR_LEN(items_aspect_ratio), 0);
	tooltip("Set the output aspect ratio");
	igSameLine(0.0f, -1.0f);
	btn_size = (ImVec2){cell, igGetItemRectSize().y};
	if (igButton("Flip ratio", btn_size)) {
		idx ^= 1;
		ratio_state_changed = true;
	}
	tooltip("Swap width:height of the chosen ratio");
	if (ratio_state_changed) {
		if (aspect_ratio_selected ==
		    (int)ARR_LEN(items_aspect_ratio) - 1 && idx == 0) {
			video_config.aspect_ratio = (ImVec2){0, 0};
		} else {
			ImVec2 ratio;
			sscanf(combo_box_item[idx][aspect_ratio_selected],
			       "%f:%f", &ratio.x, &ratio.y);
			video_config.aspect_ratio = ratio;
		}
	}
	igSpacing();
#if 0
	igCheckbox("Scale", (bool *)&scale_enabled);
	tooltip("Resize the video to a target resolution");
	if (scale_enabled) {
		igCheckbox("Keep aspect ratio", (bool *)&scale_keep_aspect);
		tooltip("Height is derived from width to preserve the original "
			"ratio");
		float half = (igGetContentRegionAvail().x - spacing) / 2.0f;
		igSetNextItemWidth(half);
		igInputInt("##scale_w", &scale_w, 0, 0, 0);
		tooltip("Output width in pixels");
		if (!scale_keep_aspect) {
			igSameLine(0.0f, -1.0f);
			igSetNextItemWidth(half);
			igInputInt("##scale_h", &scale_h, 0, 0, 0);
			tooltip("Output height in pixels");
		}
		if (scale_w < 2)
			scale_w = 2;
		if (scale_h < 2)
			scale_h = 2;
	}
#endif
	igEndChild();
	static float trim_start;
	static float trim_end = 0;
	if (do_reset) {
		trim_start = 0.0f;
		trim_end = 0.0f;
	}
	float tl_dur = vp ? (float)vp_engine_duration(vp->eng) : 0.0f;
	if (tl_dur <= 0.0f)
		tl_dur = 1.0f;
	if (trim_end <= 0.0f || trim_end > tl_dur)
		trim_end = tl_dur;
	int tl_drag = 0;
	float tl_scrub = 0.0f;
	TimelineTrimWidget("##tl", &trim_start, &trim_end, tl_dur,
			   (ImVec2){igGetContentRegionAvail().x, 50.0f},
			   &tl_drag, &tl_scrub,should_trim);
	if (vp) {
		static int tl_was_dragging = 0;
		static double tl_last_seek_t = -1.0;
		static float tl_last_sent = -1.0f;
		if (tl_drag && !tl_was_dragging) {
			if (vp_engine_is_playing(vp->eng))
				vp_engine_pause(vp->eng);
			vp_engine_scrub_begin(vp->eng);
			tl_last_seek_t = -1.0;
			tl_last_sent = -1.0f;
		}
		if (tl_drag) {
			double now = glfwGetTime();
			if (tl_scrub != tl_last_sent &&
			    (tl_last_seek_t < 0.0 ||
			     now - tl_last_seek_t >= 0.04)) {
				vp_engine_seek(vp->eng, tl_scrub, false);
				tl_last_sent = tl_scrub;
				tl_last_seek_t = now;
			}
		}
		if (!tl_drag && tl_was_dragging) {
			if (tl_last_sent >= 0.0f)
				vp_engine_seek(vp->eng, tl_last_sent, true);
			vp_engine_scrub_end(vp->eng);
		}
		tl_was_dragging = tl_drag;
	}
#if 1
	igBeginGroup();
	{
		const float width = igGetContentRegionAvail().x;

	igBeginDisabled(video_config.mute);	
		if(should_trim)
			AudioTracksTimeline(width, trim_start,trim_end, tl_dur);
		else
			AudioTracksTimeline(width, 0.0f,tl_dur, tl_dur);
	}
	igEndDisabled();

	memset(audio_removed,video_config.mute,sizeof(audio_removed));
	render_tools();
	igEndGroup();
#endif
	igBeginGroup();
	static char out_folder_name[OS_PATHMAX];
	static bool out_same_as_input = true;
	// igCheckbox("Output folder same as input folder", &out_same_as_input);
	btn_size.y = 0;
	btn_size.x = 200;
	jh_chk_button("Quick Save", &out_same_as_input, (ImVec2){0,0});
	igSameLine(0.0f, -1.0f);
	igSetNextItemWidth(igGetContentRegionAvail().x - btn_size.x - spacing-200);
	igBeginDisabled(out_same_as_input);
	igInputTextWithHint("##input_out_folder", "Output Folder",
			    out_folder_name, sizeof(out_folder_name), 0, NULL,
			    NULL);
	igSameLine(0.0f, -1.0f);
	if (igButton(i_folder_open " Browse", btn_size)) {
		const char *folder =
		    tinyfd_selectFolderDialog("Select Folder", "");
		if (folder) {
			strncpy(out_folder_name, folder,
				sizeof(out_folder_name) - 1);
			out_folder_name[sizeof(out_folder_name) - 1] = '\0';
		}
	}
	if(vp && out_same_as_input){
				os_path_dirname(out_folder_name,sizeof(out_folder_name),current_video_path);
	}
	igEndDisabled();
	igSameLine(0.0f, -1.0f);
	/* Apply preview filters only when the user is NOT mid-interaction.
	   Dragging a crop/scale slider changes values every frame; rebuilding
	   the filtergraph and re-seeking on every one of those frames floods
	   the engine and freezes the preview (the "crop hangs the player" bug).
	   Compare against the LAST APPLIED state and commit once interaction
	   settles, so a slider applies on release. */
	static VideoConfig applied_cfg;
	static int applied_init = 0;
	if (do_reset)
		applied_init = 0;
	static int applied_crop_en = 0;
	static int a_sc_en = 0, a_sc_w = 0, a_sc_h = 0, a_sc_ka = 1;
	(void)cfg_before;
	(void)crop_before;
	(void)cl0;
	(void)ct0;
	(void)cr0;
	(void)cb0;
	if (!applied_init) {
		applied_cfg = video_config;
		applied_crop_en = crop_enabled;
		a_sc_en = scale_enabled;
		a_sc_w = scale_w;
		a_sc_h = scale_h;
		a_sc_ka = scale_keep_aspect;
		applied_init = 1;
	}
	int changed =
	    memcmp(&applied_cfg, &video_config, sizeof(VideoConfig)) != 0 ||
	    applied_crop_en != crop_enabled ||
	    a_sc_en != scale_enabled || a_sc_w != scale_w ||
	    a_sc_h != scale_h || a_sc_ka != scale_keep_aspect;
	if ((changed && !igIsAnyItemActive()) || preview_dirty) {
		preview_dirty = 0;
		applied_cfg = video_config;
		applied_crop_en = crop_enabled;
		a_sc_en = scale_enabled;
		a_sc_w = scale_w;
		a_sc_h = scale_h;
		a_sc_ka = scale_keep_aspect;
		rebuild_preview_filters();
	}

	export_poll();
	/* igSetCursorPosY(igGetWindowHeight() - 50 - */
	/* 		igGetStyle()->WindowPadding.y); */

    static double pre_f = 0.0;
	if (export_active()) {
		double f = export_fraction();
		if (f < pre_f) f = pre_f;
		else pre_f = f;
		char ov[32];
		/* snprintf(ov, sizeof(ov), "%.0f%%", f * 100.0); */

		int p = (int)(f * 100.0 + 0.5);
		snprintf(ov, sizeof(ov), "%d%%", p);


		float cancel_w = 100.0f;
		float bar_w = igGetContentRegionAvail().x - cancel_w - spacing;
		if (bar_w < 50.0f)
			bar_w = 50.0f;
		igProgressBar((float)f, (ImVec2){bar_w,0}, ov);
		igSameLine(0.0f, spacing);
		//cancle button
		if (igButton("X", (ImVec2){cancel_w, 0}))
			export_request_cancel();
	} else if (igButton("Render", (ImVec2){-FLT_MIN, 0})) {
		if (current_video_path[0]) {
				pre_f = 0.0f;
			float dur =
			    vp ? (float)vp_engine_duration(vp->eng) : 0.0f;
			float te = (trim_end > 0.0f) ? trim_end : dur;
			int has_trim = (trim_start > 0.0f) ||
				       (trim_end > 0.0f && trim_end < dur);
			const char *out_dir = out_folder_name;
			ExportRequest req;
			memset(&req, 0, sizeof(req));
			req.cfg = video_config;
			req.trim_start = trim_start;
			req.trim_end = te;
			req.duration = dur;
			req.has_trim = has_trim && should_trim;
			req.crop_enabled = crop_enabled;
			if (crop_enabled && src_width > 0 && src_height > 0) {
				float sl, st, sr, sb;
				crop_disp_to_src(crop_l, crop_t, crop_r, crop_b,
						 video_config.rotate,
						 video_config.flip_h,
						 video_config.flip_v, &sl, &st,
						 &sr, &sb);
				req.crop_x = (int)(sl * src_width);
				req.crop_y = (int)(st * src_height);
				req.crop_w = (int)((sr - sl) * src_width) & ~1;
				req.crop_h = (int)((sb - st) * src_height) & ~1;
			}
			req.scale_enabled = scale_enabled;
			req.scale_w = scale_w;
			req.scale_h = scale_h;
			req.scale_keep_aspect = scale_keep_aspect;
			ve_copy(req.thumbnail, sizeof(req.thumbnail),
				add_thumbnail_path);
			ve_copy(req.subtitle, sizeof(req.subtitle),
				add_subtitle_path);
			ve_copy(req.subtitle_lang, sizeof(req.subtitle_lang),
				add_subtitle_lang);
			req.remove_sub_enabled = remove_sub_enabled;
			req.remove_sub_index = remove_sub_index;
			req.n_drop_audio = 0;
			for (int i = 0; i < audio_track_count &&
					req.n_drop_audio < 16;
			     i++)
				if (audio_removed[i])
					req.drop_audio_index
					    [req.n_drop_audio++] =
					    audio_tracks[i].index;
			jh_generate_log();
			export_start(current_video_path, out_dir, &req);
		} else {
			error_title = "No video";
			error_message = "Load a video before rendering.";
		}
	}
		
	igEndGroup();
}
typedef enum { IMAGE, VIDEO, UNKNOWN } FileType;
FileType check_file_type(const char *path)
{
	unsigned char b[16];
	FILE *f = fopen(path, "rb");
	if (!f)
		return UNKNOWN;
	int n = fread(b, 1, 16, f);
	fclose(f);
	if (n < 4)
		return UNKNOWN;
	if (b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF)
		return IMAGE;
	if (b[0] == 0x89 && b[1] == 0x50 && b[2] == 0x4E && b[3] == 0x47)
		return IMAGE;
	if (b[0] == 0x47 && b[1] == 0x49 && b[2] == 0x46)
		return IMAGE;
	if (b[0] == 0x42 && b[1] == 0x4D)
		return IMAGE;
	if (n >= 12 && memcmp(b + 8, "WEBP", 4) == 0)
		return IMAGE;
	// MP4, MOV, M4V, 3GP, 3G2, F4V (ftyp box)
	if (n >= 8 && memcmp(b + 4, "ftyp", 4) == 0)
		return VIDEO;
	// MKV, WEBM (EBML)
	if (b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3)
		return VIDEO;
	// AVI, WAV (RIFF)
	if (b[0] == 0x52 && b[1] == 0x49 && b[2] == 0x46 && b[3] == 0x46)
		return VIDEO;
	// FLV
	if (b[0] == 0x46 && b[1] == 0x4C && b[2] == 0x56)
		return VIDEO;
	// MPEG-1, MPEG-2 (PS)
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01 && b[3] == 0xBA)
		return VIDEO;
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01 && b[3] == 0xB3)
		return VIDEO;
	// MPEG-TS
	if (b[0] == 0x47)
		return VIDEO;
	// WMV, ASF
	if (b[0] == 0x30 && b[1] == 0x26 && b[2] == 0xB2 && b[3] == 0x75 &&
	    b[4] == 0x8E && b[5] == 0x66 && b[6] == 0xCF && b[7] == 0x11)
		return VIDEO;
	// OGG (OGV, OGX)
	if (b[0] == 0x4F && b[1] == 0x67 && b[2] == 0x67 && b[3] == 0x53)
		return VIDEO;
	// RealMedia (.rm, .rmvb)
	if (b[0] == 0x2E && b[1] == 0x52 && b[2] == 0x4D && b[3] == 0x46)
		return VIDEO;
	// DivX/Xvid (VOB)
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x01 && b[3] == 0xE0)
		return VIDEO;
	// Matroska segment
	if (b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3)
		return VIDEO;
	// MPEG-4 Part 2 (raw)
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00 && b[3] == 0x01)
		return VIDEO;
	// DPX
	if ((b[0] == 0x53 && b[1] == 0x44 && b[2] == 0x50 && b[3] == 0x58) ||
	    (b[0] == 0x58 && b[1] == 0x50 && b[2] == 0x44 && b[3] == 0x53))
		return VIDEO;
	// MXF
	if (b[0] == 0x06 && b[1] == 0x0E && b[2] == 0x2B && b[3] == 0x34 &&
	    b[4] == 0x02 && b[5] == 0x05 && b[6] == 0x01 && b[7] == 0x01)
		return VIDEO;
	// NUT
	if (b[0] == 0xF9 && b[1] == 0x52 && b[2] == 0x9A && b[3] == 0x01)
		return VIDEO;
	// GXF
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00 && b[3] == 0x00 &&
	    b[4] == 0x01 && b[5] == 0xBC)
		return VIDEO;
	// LXF
	if (b[0] == 0x4C && b[1] == 0x58 && b[2] == 0x46 && b[3] == 0x20)
		return VIDEO;
	// AIFF (used with video in some containers)
	if (b[0] == 0x46 && b[1] == 0x4F && b[2] == 0x52 && b[3] == 0x4D &&
	    b[8] == 0x41 && b[9] == 0x49 && b[10] == 0x46 && b[11] == 0x46)
		return VIDEO;
	// IVF (VP8/VP9 raw)
	if (b[0] == 0x44 && b[1] == 0x4B && b[2] == 0x49 && b[3] == 0x46)
		return VIDEO;
	// H.264/H.265 Annex B raw
	if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00 && b[3] == 0x01 &&
	    (b[4] == 0x67 || b[4] == 0x40))
		return VIDEO;
	return UNKNOWN;
}
void refresh_video_info(void)
{
	if (!vp)
		return;
	VPMediaInfo mi;
	vp_engine_get_info(vp->eng, &mi);
	src_width = mi.src_width;
	src_height = mi.src_height;
	src_duration = mi.duration;
	has_audio_src = mi.has_audio;
	audio_track_count = vp_engine_audio_tracks(vp->eng, audio_tracks, 32);
	aw_begin(current_video_path, audio_tracks, audio_track_count);
	ts_begin(current_video_path);
	if (src_width > 0 && src_height > 0) {
		ImVec2 ratio =
		    get_aspect_ratio((ImVec2){src_width, src_height});
		snprintf(original_aspect_ratio, sizeof(original_aspect_ratio),
			 "%d:%d", (int)ratio.x, (int)ratio.y);
		snprintf(original_aspect_ratio_reverse,
			 sizeof(original_aspect_ratio_reverse), "%d:%d",
			 (int)ratio.y, (int)ratio.x);
	}
}


void reset_edit_state(void)
{
	video_config = (VideoConfig){0};
	crop_enabled = 0;
	crop_l = crop_t = 0.0f;
	crop_r = crop_b = 1.0f;
	scale_enabled = 0;
	scale_w = 1280;
	scale_h = 720;
	scale_keep_aspect = 1;
	add_thumbnail_path[0] = 0;
	add_subtitle_path[0] = 0;
	ve_copy(add_subtitle_lang, sizeof(add_subtitle_lang), "und");
	remove_sub_enabled = 0;
	remove_sub_index = 0;
	memset(audio_removed, 0, sizeof(audio_removed));
	wave_tex_reset();
	ts_tex_reset();
	preview_dirty = 1;
	app_reset_seq++; /* tells render_single_click_items to reset its statics
			  */
}
void populate_user_thumbnail(const char *path) { (void)path; }


void drop_callback(GLFWwindow *window, int count, const char **paths)
{
	if (count > 1) {
		error_message = "Multiple file not supported.";
		return;
	}
	if (!os_isfile(paths[0]) || !os_exists(paths[0])) {
		goto cleanup;
	}
	switch (check_file_type(paths[0])) {
	case IMAGE: {
		populate_user_thumbnail(paths[0]);
		return;
	}
	case VIDEO: {
		if (export_active()) {
			error_title = "Export in progress";
			error_message = "Finish or cancel the current export "
					"before loading a new file.";
			return;
		}
		ve_copy(current_video_path, sizeof(current_video_path),
			paths[0]);
		if (vp)
			vp_destroy(vp);
		vp = vp_create(paths[0], 1);
		reset_edit_state();
		refresh_video_info();
		if (vp) {
			vp_engine_pause(vp->eng);
			vp_engine_seek(vp->eng, 0.0,
				       true); /* show first frame, paused */
		}
		return;
	}
	case UNKNOWN:
	default:
		break;
	}
cleanup:
	error_message = "File not supported";
}


void handle_shortcut()
{
		if (igIsKeyPressed_Bool(ImGuiKey_P, false) || igIsKeyPressed_Bool(ImGuiKey_Space, false)) {
				if(vp_engine_is_playing(vp->eng)){
						vp_engine_pause(vp->eng);
				}
				else{
						vp_engine_play(vp->eng);
				}
		}else if(igIsKeyPressed_Bool(ImGuiKey_M, false)){
				video_config.mute = !video_config.mute;
		}
}

int main(int argc, char *argv[])
{
	if (!glfwInit())
		return -1;
	// Decide GL+GLSL versions
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
#if __APPLE__
	// GL 3.2 Core + GLSL 150
	const char *glsl_version = "#version 150";
#else
	// GL 3.2 + GLSL 130
	const char *glsl_version = "#version 130";
#endif
	// just an extra window hint for resize
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(
	    glfwGetPrimaryMonitor()); // Valid on GLFW 3.3+ only
	window =
	    glfwCreateWindow((int)(WIDTH * 16),
			     (int)(HEIGHT * 9), APP_TITLE, NULL, NULL);

	if (!window) {
		printf("Failed to create window! Terminating!\n");
		glfwTerminate();
		return -1;
	}
	
	glfwSetWindowSizeLimits(window, WIDTH * 16, HEIGHT * 9,GLFW_DONT_CARE, GLFW_DONT_CARE);
	glfwSetWindowAspectRatio(window, 16, 9);
	glfwMakeContextCurrent(window);
	gladLoadGL(glfwGetProcAddress);



GLFWimage icon[1];
#include "app_icon.h"
icon[0].pixels = stbi_load_from_memory(asset_AppIcon_5_png,asset_AppIcon_5_png_len,&icon[0].width, &icon[0].height, NULL, 4);

if (icon[0].pixels)
{
    glfwSetWindowIcon(window, 1, icon);
    stbi_image_free(icon[0].pixels);
}









	// enable vsync
	glfwSwapInterval(1);
	// check opengl version sdl uses
	printf("opengl version: %s\n", (char *)glGetString(GL_VERSION));
	// setup imgui
	igCreateContext(NULL);
	// set docking
	ImGuiIO *ioptr = igGetIO();
	ioptr->ConfigFlags |=
	    ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	ImGuiStyle *style = igGetStyle();
	ImGuiStyle_ScaleAllSizes(
	    style,
	    main_scale); // Bake a fixed style scale. (until we have a solution
	style->FontScaleDpi = main_scale; // Set initial font scale. (using
#if GLFW_VERSION_MAJOR >= 3 && GLFW_VERSION_MINOR >= 3
	ioptr->ConfigDpiScaleFonts =
	    true; // [Experimental] Automatically overwrite style.FontScaleDpi
	ioptr->ConfigDpiScaleViewports =
	    true; // [Experimental] Scale Dear ImGui and Platform Windows when
#endif
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init(glsl_version);
	igStyleColorsDark(NULL);

	/* ImFontConfig *font_cfg = ImFontConfig_ImFontConfig(); */
	/* font_cfg->SizePixels = 16.0f; */
	/* ImFontAtlas_AddFontDefault(ioptr->Fonts, font_cfg); */
	/* ImFontConfig_destroy(font_cfg); */
	/* ImFontConfig *icon_cfg = ImFontConfig_ImFontConfig(); */
	/* icon_cfg->FontDataOwnedByAtlas = true; */
	/* icon_cfg->MergeMode = true; */
	/* icon_cfg->SizePixels = 16.0f; */
	/* icon_cfg->GlyphOffset.y = 3.0f; */
	/* icon_font = ImFontAtlas_AddFontFromFileTTF( */
	/*     ioptr->Fonts, "asset/fonts/icomoon.ttf", 16.0f, icon_cfg, NULL); */
	/* ImFontConfig_destroy(icon_cfg); */


logger_init(&logger,"vpt_log.txt");
const float ui_font_size   = 16.0f;
const float icon_font_size = ui_font_size * 0.75f;  /* relative; tune 0.7–0.8 */

ImFontConfig *font_cfg = ImFontConfig_ImFontConfig();
font_cfg->SizePixels = ui_font_size;
ImFontAtlas_AddFontDefault(ioptr->Fonts, font_cfg);
ImFontConfig_destroy(font_cfg);

ImFontConfig *icon_cfg = ImFontConfig_ImFontConfig();
icon_cfg->MergeMode        = true;
icon_cfg->PixelSnapH       = true;
icon_cfg->GlyphOffset.y = 0.3f;
icon_cfg->GlyphMinAdvanceX = icon_font_size;
icon_cfg->FontDataOwnedByAtlas = false;

icon_font = ImFontAtlas_AddFontFromMemoryTTF(ioptr->Fonts, icomoon_ttf, icomoon_ttf_len, icon_font_size,icon_cfg, NULL);
ImFontConfig_destroy(icon_cfg);



	
	ImVec4 clearColor;
	clearColor.x = 0.45f;
	clearColor.y = 0.55f;
	clearColor.z = 0.60f;
	clearColor.w = 1.00f;
	glfwSetDropCallback(window, drop_callback);
	if (vp) {
		vp_engine_pause(vp->eng);
		vp_engine_seek(vp->eng, 0.0,
			       true); /* paused on the first frame at startup */
	}


	style->WindowRounding = 0.0f;
	style->FrameRounding = 10.0f;
	style->GrabRounding = 10.0f;
	style->TabRounding = 5.0f;


	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		// start imgui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		igNewFrame();
		if(vp && !ioptr->WantTextInput)
				handle_shortcut();


		ImGuiViewport *viewport = igGetMainViewport();
		igSetNextWindowPos(viewport->Pos, ImGuiCond_Always,
				   (ImVec2){0, 0});
		igSetNextWindowSize(viewport->Size, ImGuiCond_Always);
		{
			igBegin("main_window", NULL,
				ImGuiWindowFlags_NoResize |
				    ImGuiWindowFlags_NoCollapse |
				    ImGuiWindowFlags_NoTitleBar |
				    ImGuiWindowFlags_NoScrollbar);
			if (igBeginTabBar("Tabs", 0)) {
				if (igBeginTabItem("Single Click", NULL, 0)) {
					const int w = 300;
#if 0
					jh_drag_and_drop_area(
					    (ImVec2){
						igGetContentRegionAvail().x -
						    w - 10,
						470},
					    THUMBNAIL_SIZE, 0xFF1A1A1A, 0xFF2D2D2D);
					igSameLine(0.0f, -1.0f);
#endif
					igBeginGroup();
					vp_render(vp,
						  igGetContentRegionAvail().x -
						      w - 10,
						  470);
					igEndGroup();
					igSameLine(0.0f, -1.0f);
					igBeginDisabled(!vp);
					render_single_click_items((ImVec2){w, 0});
					igEndDisabled();
					/* Crop overlay LAST: it draws via the
					   window draw list and positions its
					   handles with absolute screen coords, so
					   keeping it out of the two-column flow
					   above stops it from dragging the panel's
					   bottom section up over the player. */
					if (crop_enabled && g_preview_has_img) {
						ImVec2 save =
						    igGetCursorScreenPos();
						crop_widget(
						    g_preview_img_min,
						    (ImVec2){g_preview_img_max.x -
								 g_preview_img_min
								     .x,
							     g_preview_img_max.y -
								 g_preview_img_min
								     .y},
						    g_preview_angle,
						    g_preview_box_min,
						    g_preview_box_max, false);
						igSetCursorScreenPos(save);
					}
					error_dialog_render();
					igEndTabItem();
				}



#if 1
				if (igBeginTabItem("Convert", NULL, 0)) {
					/* vp_render(vp, 300, 300); */
					igEndTabItem();
				}



				if (igBeginTabItem("Repair", NULL, 0)) {
					igText("Content of Tab 2");
					igEndTabItem();
				}



				if (igBeginTabItem(i_vpu_icon_settings
						   " Settings",
						   NULL, 0)) {
					igText("WIP work in progress");
					igEndTabItem();
				}

				if (igBeginTabItem(i_info, NULL, 0)) {
					igText("WIP work in progress");
					igEndTabItem();
				}
#endif
				igEndTabBar();
			}
			igEnd();
		}
		// render
		igRender();
		glfwMakeContextCurrent(window);
		glViewport(0, 0, (int)ioptr->DisplaySize.x,
			   (int)ioptr->DisplaySize.y);
		glClearColor(clearColor.x, clearColor.y, clearColor.z,
			     clearColor.w);
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplOpenGL3_RenderDrawData(igGetDrawData());
		glfwSwapBuffers(window);
	}
	// clean up
	logger_close(&logger);
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	igDestroyContext(NULL);
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}
