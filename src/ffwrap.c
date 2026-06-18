#include "os.c"
#include "util.c"
#include "vector.h"
#include "vendor/cJSON.c"
#include "vendor/subprocess.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FFWRAP_MAX_BUFFER_SIZE (1 << 10)
const int CMD__FLAG =
    subprocess_option_search_user_path | subprocess_option_no_window;

typedef enum {
	ROTATE,
	FLIP_H,
	FLIP_V,

	MUTE,
	ADD_AUDIO_TRACK,
	SCALE_VOLUME,
	REMOVE_AUDIO_TRACK,

	STERO_TO_MONO,

	SCALE,
	CROP,

	MERGE,
	TRIM,
	REVERSE_AUDIO,
	REVERSE_VIDEO,
	ADD_THUMBNAIL,

	ADD_SUBTITLE,
	REMOVE_SUBTITLE,

	BRIGHTNESS,
	CONTARAST,
	HUE,
	ASCPECT_RATIO,
} CommandKind;

typedef struct {
	vpopulate(char);
} String;

typedef struct {
	vpopulate(char *);
} Args;

typedef struct {
	CommandKind kind;
} Command;

typedef struct {
	Command base;
	float angle;
	bool clockwise;
} CmdRotate;

typedef struct {
	Command base;
	int volume;
} CmdScaleVolume;

typedef struct {
	Command base;
	int x, y;
} CmdChangeAspectRatio;

typedef struct {
	Command base;
	const char *src;
} CmdAddAudioTrack;

typedef struct {
	Command base;
	int idx;
} CmdRemoveAudioTrack;

typedef struct {
	Command base;
	const char *src;
} CmdAddSubtitle;

typedef struct {
	Command base;
	char **paths;
} CmdMerge;

typedef struct {
	Command base;
	int x, y, w, h;
} CmdCrop;

/* typedef struct { */
/* 	Command base; */
/* 	char* path; */
/* } */

typedef struct {
	char lang[16];
	int index;
} ff_track;

typedef struct {
	double start;
	double end;
	char title[256];
} ff_chapter;

typedef struct {
	double duration;
	int width;
	int height;
	ff_track *audio;
	int audio_count;
	ff_track *subtitle;
	int subtitle_count;
	ff_chapter *chapters;
	int chapter_count;
	int thumbnail_stream_index;
} ff_info;



typedef struct {
	String vf;
	String af;
	Args args;
	String *filter_complex;
	int mute;
} CommandBuilder;

void print_command_builder(CommandBuilder *builder) {}

#define add_filter(string, src)                                                \
	do {                                                                   \
		if (string.count)                                              \
			vpush(string, ',');                                    \
		char *temp = src;                                              \
		while (*temp) {                                                \
			vpush(string, *temp++);                                \
		}                                                              \
	} while (0)

// Handle error
#define add_args(string, src)                                                  \
	do {                                                                   \
		char *tem__v = str_duplicate((src));                           \
		vpush(string, tem__v);                                         \
	} while (0)

void visit_add_subtitle(CmdAddSubtitle *sub, CommandBuilder *b) {}

void visit_flip_h(CommandBuilder *b) { add_filter(b->vf, "hflip"); }

void visit_flip_v(CommandBuilder *b) { add_filter(b->vf, "vflip"); }

void visit_reverse_audio(CommandBuilder *b) { add_filter(b->vf, "reverse"); }

void visit_reverse_video(CommandBuilder *b) { add_filter(b->vf, "areverse"); }

void visit_mute(CommandBuilder *b) { add_args(b->args, "-an"); }

void visit_scale_volume(CmdScaleVolume *cmd, CommandBuilder *b)
{
	char tmp[FFWRAP_MAX_BUFFER_SIZE];
	snprintf(tmp, 100, "volume=%.6f",
		 (float)(cmd->volume % 100) / (100.0f));
	add_args(b->args, "-filter:a");
	add_args(b->args, tmp);
}

/* void video_change_bitrate(Video *video, char* bitrate) */
/* { */
/*     if(!bitrate) return; */
/*     video_add_etc(video, "-b:v"); */
/*     video_add_etc(video, bitrate); */
/* } */
void visit_change_aspect_ratio(CmdChangeAspectRatio *ratio, CommandBuilder *b)
{

	char buffer[FFWRAP_MAX_BUFFER_SIZE];
	snprintf(buffer, sizeof(buffer), "setdar=%d/%d", ratio->x, ratio->y);
	add_filter(b->vf, buffer);
}

void visit_crop(CmdCrop *crop, CommandBuilder *b)
{
	char buffer[FFWRAP_MAX_BUFFER_SIZE] = {0};
	snprintf(buffer, sizeof(buffer), "crop=%d:%d:%d:%d", crop->w, crop->h,
		 crop->x, crop->y);
	add_filter(b->vf, buffer);
}

void visit_rotate(CmdRotate *cmd, CommandBuilder *builder)
{
	int angle = cmd->angle;
	if (angle <= 0)
		return;

	char *filter = NULL;
	bool clockwise = cmd->clockwise;
	switch (angle) {
	case 90: {
		filter = "transpose=1";
		break;
	}
	case 180: {
		filter = "transpose=2";
		break;
	}
	case 270: {
		filter = "transpose=3";
		break;
	}
	default: {
		char buffer[FFWRAP_MAX_BUFFER_SIZE];
		if (clockwise)
			snprintf(buffer, sizeof(buffer), "rotate=%d*PI/180",
				 angle);
		else
			snprintf(buffer, sizeof(buffer), "rotate=%d*PI/180",
				 -angle);
		add_filter(builder->vf, buffer);
		return;
	}
	}

	if (!filter)
		return;
	add_filter(builder->vf, filter);
}

void *ff_slurp__stream(FILE *f, size_t *outlen)
{
	size_t capacity = (1 << 20);
	size_t index = 0;
#define CHUNK (1 << 10)
	unsigned char *data = malloc(capacity);
	if (!data) {
		*outlen = 0;
		return NULL;
	}
	int bytes_read;
	do {
		bytes_read = fread(data + index, 1, CHUNK, f);
		if (bytes_read > 0) {
			index += bytes_read;
			if (index + CHUNK + 1 > capacity) {
				capacity <<= 1;
				unsigned char *temp = realloc(data, capacity);
				if (!temp) {
					free(data);
					*outlen = 0;
					return NULL;
				}
				data = temp;
			}
		}
	} while (bytes_read > 0);

	if (index == 0) {
		free(data);
		*outlen = 0;
		return NULL;
	}

	data[index] = '\0';
	*outlen = index;
	return data;
}

void ff_info_free(ff_info *info)
{
	if (!info)
		return;
	free(info->audio);
	free(info->subtitle);
	free(info->chapters);
	free(info);
}
ff_info *ff_parse_info(const char *json_str)
{
	cJSON *root = cJSON_Parse(json_str);
	if (!root)
		return NULL;

	ff_info *info = calloc(1, sizeof(ff_info));
	info->thumbnail_stream_index = -1;

	cJSON *format = cJSON_GetObjectItem(root, "format");
	if (format) {
		cJSON *dur = cJSON_GetObjectItem(format, "duration");
		if (dur)
			info->duration = atof(dur->valuestring);
	}

	cJSON *streams = cJSON_GetObjectItem(root, "streams");
	/* bool found_video = false; */
	if (streams) {
		int n = cJSON_GetArraySize(streams);
		for (int i = 0; i < n; i++) {
			cJSON *s = cJSON_GetArrayItem(streams, i);
			cJSON *type = cJSON_GetObjectItem(s, "codec_type");
			cJSON *idx = cJSON_GetObjectItem(s, "index");
			cJSON *tags = cJSON_GetObjectItem(s, "tags");
			cJSON *lang =
			    tags ? cJSON_GetObjectItem(tags, "language") : NULL;

			if (!type)
				continue;

			if (strcmp(type->valuestring, "audio") == 0) {
				info->audio = realloc(info->audio,
						      (info->audio_count + 1) *
							  sizeof(ff_track));
				ff_track *t = &info->audio[info->audio_count++];
				t->index = idx ? idx->valueint : i;
				strncpy(t->lang,
					lang ? lang->valuestring : "und",
					sizeof(t->lang) - 1);
			} else if (strcmp(type->valuestring, "subtitle") == 0) {
				info->subtitle = realloc(
				    info->subtitle, (info->subtitle_count + 1) *
							sizeof(ff_track));
				ff_track *t =
				    &info->subtitle[info->subtitle_count++];
				t->index = idx ? idx->valueint : i;
				strncpy(t->lang,
					lang ? lang->valuestring : "und",
					sizeof(t->lang) - 1);
			} else if (strcmp(type->valuestring, "video") == 0) {
				cJSON *w = cJSON_GetObjectItem(s, "width");
				cJSON *h = cJSON_GetObjectItem(s, "height");
				/* found_video = true; */
				if (w)
					info->width = w->valueint;
				if (h)
					info->height = h->valueint;
				cJSON *dispo =
				    cJSON_GetObjectItem(s, "disposition");
				if (dispo) {
					cJSON *ap = cJSON_GetObjectItem(
					    dispo, "attached_pic");
					if (ap && ap->valueint == 1)
						info->thumbnail_stream_index =
						    idx ? idx->valueint : i;
				}
			}
		}
	}

	cJSON *chapters = cJSON_GetObjectItem(root, "chapters");
	if (chapters) {
		int n = cJSON_GetArraySize(chapters);
		info->chapters = calloc(n, sizeof(ff_chapter));
		info->chapter_count = n;
		for (int i = 0; i < n; i++) {
			cJSON *c = cJSON_GetArrayItem(chapters, i);
			ff_chapter *ch = &info->chapters[i];
			cJSON *st = cJSON_GetObjectItem(c, "start_time");
			cJSON *en = cJSON_GetObjectItem(c, "end_time");
			cJSON *tags = cJSON_GetObjectItem(c, "tags");
			cJSON *title =
			    tags ? cJSON_GetObjectItem(tags, "title") : NULL;
			if (st)
				ch->start = atof(st->valuestring);
			if (en)
				ch->end = atof(en->valuestring);
			if (title)
				strncpy(ch->title, title->valuestring,
					sizeof(ch->title) - 1);
		}
	}

	cJSON_Delete(root);
	/* if(!found_video){ */
	/* 	ff_info_free(info); */
	/* 	return NULL; */
	/* } */
	return info;
}


ff_info *ff_get_video_info(const char *ffprobe, const char *file_name)
{

	const char *command[] = {ffprobe,
				 "-v",
				 "quiet",
				 "-print_format",
				 "json",
				 "-show_format",
				 "-show_streams",
				 "-show_chapters",
				 "-show_programs",
				 file_name,
				 NULL};

	struct subprocess_s process;
	int result = subprocess_create(command, CMD__FLAG, &process);
	if (result != 0) {
		return NULL;
	}
	size_t outlen;
	FILE *f = subprocess_stdout(&process);
	char* stdout_data = ff_slurp__stream(f, &outlen);
	if(!outlen)
		return NULL;
	ff_info *info = ff_parse_info(stdout_data);
	free(stdout_data);
	return info;
}

// TODO: handle error
void *ff_get_thumbnail(const char *ffmpeg,size_t sec,const char *file_name,size_t *outlen)
{
	int h = sec / 3600;
	int m = (sec % 3600) / 60;
	int s = sec % 60;
	char time_stamp[64];
	snprintf(time_stamp, sizeof(time_stamp), "%02d:%02d:%02d", h, m, s);
	const char *command[] = {
	    ffmpeg, "-i",	  file_name, "-ss", time_stamp, "-vframes", "1",
	    "-f",   "image2pipe", "-vcodec", "bmp", "-",	NULL};
	struct subprocess_s process;
	int result = subprocess_create(command, CMD__FLAG, &process);
	if (result != 0) {
		*outlen = 0;
		return NULL;
	}

	FILE *f = subprocess_stdout(&process);
	return ff_slurp__stream(f, outlen);
}

void ff_build_cmd(Command *cmd)
{
	CommandBuilder builder = {0};
	while (cmd) {
		switch (cmd->kind) {
		case MUTE: {
			visit_mute(&builder);
			break;
		}
		case CROP: {
			visit_crop((CmdCrop *)cmd, &builder);
			break;
		}
		case MERGE: {
			/* visit_merge((CmdMerge*)cmd,&builder); */
			break;
		}
		case SCALE_VOLUME: {
			visit_scale_volume((CmdScaleVolume *)cmd, &builder);
		}
		case ROTATE: {
			visit_rotate((CmdRotate *)cmd, &builder);
			break;
		}
		}
		cmd++;
	}
}

#if 0
int main()
{
	/* size_t out; */
	/* unsigned char *c =
	 * ff_get_thumbnail("/home/main/.bin/ffmpeg","/home/main/programming/guis/video-processing-tool/experiment/input.mp4",&out);
	 */
	/* FILE* f = fopen("a.bmp","w"); */
	/* fwrite(c,1,out,f); */
	/* fclose(f); */
	/* CommandBuilder b = {0}; */
	/* CmdRotate rt = { */
	/* 	.angle = 180, */
	/* }	; */
	/* visit_rotate(&rt,&b); */
	/* vpush(b.vf,'\0'); */
	/* puts(b.vf.items); */

	size_t len;
	ff_info* info = ff_get_video_info("/home/main/.bin/ffprobe",
				       "/home/main/Videos/t/a.mp4");

	printf("duration: %f\n", info->duration);

	for (int i = 0; i < info->audio_count; i++)
		printf("audio[%d]: index=%d lang=%s\n", i, info->audio[i].index,
		       info->audio[i].lang);

	for (int i = 0; i < info->subtitle_count; i++)
		printf("sub[%d]: index=%d lang=%s\n", i,
		       info->subtitle[i].index, info->subtitle[i].lang);

	for (int i = 0; i < info->chapter_count; i++)
		printf("chapter[%d]: %.2f-%.2f %s\n", i,
		       info->chapters[i].start, info->chapters[i].end,
		       info->chapters[i].title);

	if (info->thumbnail_stream_index != -1)
		printf("thumbnail stream: %d\n", info->thumbnail_stream_index);
	printf("%d %d\n", info->width, info->height);
	ff_info_free(info);
}
#endif
