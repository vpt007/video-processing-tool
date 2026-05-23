#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "vendor/subprocess.h"
#include "util.c"
#include "os.c"
#include "vector.h"

#define FFWRAP_MAX_BUFFER_SIZE (1<<10)
typedef enum{
	ROTATE,
	FLIP_H,
	FLIP_V,

	MUTE,
	ADD_AUDIO_TRACK,
	REMOVE_AUDIO_TRACK,


	STERO_TO_MONO,

	SCALE,
	CROP,

	MERGE,
	TRIM,

	ADD_THUMBNAIL,
}CommandKind;

typedef struct {
	vpopulate(char);
}String;

typedef struct {
	vpopulate(char* );
}Args;

typedef struct {
    CommandKind kind;
}Command;

typedef struct {
	Command base;
	float angle;
	bool clockwise;
}CmdRotate;
typedef struct{
	Command base;
	int x,y;
}CmdChangeAspectRatio

typedef struct {
	Command base;
	const char* src;
}CmdAddAudioTrack;


typedef struct {
	Command base;
	int idx;
}CmdRemoveAudioTrack;


typedef struct {
	Command base;
	char** paths;
}CmdMerge;


typedef struct {
	Command base;
	int x,y,w,h;
}CmdCrop;

/* typedef struct { */
/* 	Command base; */
/* 	char* path; */
/* } */

typedef struct{
	String vf;
	String af;
	Args args;
	String* filter_complex;
	int mute;
}CommandBuilder;

void print_command_builder(CommandBuilder* builder)
{

}



#define add_filter(string,src)do{\
	if(string.count)\
		vpush(string,',');\
	char* temp = src;\
	while(*temp){\
		vpush(string,*temp++);\
	}\
}while(0)

// Handle error
#define add_args(string,src)do{\
	char* tem__v =	str_duplicate((src));\
	vpush(string,tem__v);\
	}\
}while(0)


void visit_flip_h(CommandBuilder *b) {
    add_filter(b->vf, "hflip");
}

void visit_flip_v(CommandBuilder *b) {
    add_filter(b->vf, "vflip");
}

void visit_mute(CommandBuilder* b)
{
	add_args(builder->args,"-an");
}

void visit_change_aspect_ratio(CmdChangeAspectRatio* ratio,CommandBuilder* b)
{

    char buffer[FFWRAP_MAX_BUFFER_SIZE];
    snprintf(tmp, sizeof(tmp), "setdar=%d/%d", x, y);
    add_filter(builder->vf,filter);
}

void visit_crop(CmdCrop* crop, CommandBuilder* b)
{
	char buffer[FFWRAP_MAX_BUFFER_SIZE]={0};
	snprintf(buffer,sizeof(buffer),"crop=%d:%d:%d:%d",crop->w,crop->h,crop->x,crop->y);
	add_filter(b->vf,buffer);
}


void visit_rotate(CmdRotate* cmd,CommandBuilder* builder)
{
	int angle = cmd->angle;
	if(angle <= 0)
		return;

	char*filter=NULL;
	bool clockwise = cmd->clockwise;
	switch(angle){
	case 90:
	{
		filter = "transpose=1";
		break;
	}
	case 180:
	{
		filter =  "transpose=2";
		break;
	}
	case 270:
	{
		filter =  "transpose=3";
		break;
	}
	default:
	{
		char buffer[FFWRAP_MAX_BUFFER_SIZE];
		if(clockwise)
			snprintf(buffer,sizeof(buffer),"rotate=%d*PI/180",angle);
		else
			snprintf(buffer,sizeof(buffer),"rotate=%d*PI/180",-angle);
		add_filter(builder->vf,buffer);
		return;
	}
	}

	if(!filter)
		return;
	add_filter(builder->vf,filter);
}


void build_cmd(Command *cmd)
{
	CommandBuilder builder = {0};
	while(cmd){
	switch(cmd->kind){
		case MUTE:
		{
			visit_mute(&builder);
			break;
		}
		case CROP:
		{
			visit_crop((CmdCrop*)cmd,&builder);
			break;
		}
		case MERGE:
		{
			visit_merge((CmdMerge*)cmd,&builder);
			break;
		}
		case ROTATE:
		{
			visit_rotate((CmdRotate*)cmd,&builder);
			break;
		}
	}
	cmd++;
	}
}

#if 0
int main(){
	CommandBuilder b = {0};
	CmdRotate rt = {
		.angle = 180,
	}	;
	visit_rotate(&rt,&b);
	vpush(b.vf,'\0');
	puts(b.vf.items);
}
#endif
