/* ve_plan.c — turn ops into an execution plan.
 *
 * Core policy: video and audio each start as COPY. The first op that needs to
 * decode that stream flips it to TRANSCODE and starts a filter chain. Lossless
 * ops only ever touch the container/metadata. The whole edit is "lossless" iff
 * neither stream ended up TRANSCODE.
 */
#include "ve.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static void fappend(char* buf, int cap, const char* filt) {
    int len = (int)strlen(buf);
    if (len && len < cap - 1) buf[len++] = ',';
    snprintf(buf + len, (size_t)(cap - len), "%s", filt);
}

/* atempo only accepts 0.5..2.0; chain stages to reach an arbitrary factor */
static void atempo_chain(char* buf, int cap, double f) {
    char tmp[256]; tmp[0] = 0; int tl = 0;
    while (f > 2.0 + 1e-9) { tl += snprintf(tmp+tl, sizeof(tmp)-tl, "atempo=2.0,"); f /= 2.0; }
    while (f < 0.5 - 1e-9) { tl += snprintf(tmp+tl, sizeof(tmp)-tl, "atempo=0.5,"); f /= 0.5; }
    snprintf(tmp+tl, sizeof(tmp)-tl, "atempo=%.6f", f);
    fappend(buf, cap, tmp);
}

bool ve_plan(const VeScript* s, VePlan* p, char* err, int errlen) {
    memset(p, 0, sizeof(*p));
    if (!s->input[0])  { snprintf(err, errlen, "no input ('in <file>')");  return false; }
    if (!s->output[0]) { snprintf(err, errlen, "no output ('out <file>')"); return false; }
    ve_copy(p->input, sizeof(p->input), s->input);
    ve_copy(p->output, sizeof(p->output), s->output);
    p->video = VE_COPY;
    p->audio = VE_COPY;

    for (int i = 0; i < s->n_ops; i++) {
        const VeOp* o = &s->ops[i];
        VeOpClass c = ve_classify(o);
        if (c.needs_video_decode && p->video == VE_COPY) p->video = VE_TRANSCODE;
        if (c.needs_audio_decode && p->audio == VE_COPY) p->audio = VE_TRANSCODE;
        char tmp[512];

        switch (o->kind) {
        /* ---- lossless container / metadata ---- */
        case VE_MUTE:            p->drop_all_audio = true; break;
        case VE_REMOVE_AUDIO:
            if (p->n_drop_audio < 16) p->drop_audio_index[p->n_drop_audio++] = o->u.remove_audio.index;
            break;
        case VE_REMOVE_SUBTITLE:
            if (p->n_drop_sub < 16) p->drop_sub_index[p->n_drop_sub++] = o->u.remove_sub.index;
            break;
        case VE_ADD_AUDIO:
            if (p->n_add_audio < 8) {
                ve_copy(p->add_audio[p->n_add_audio], sizeof(p->add_audio[p->n_add_audio]), o->u.add_audio.path);
                ve_copy(p->add_audio_lang[p->n_add_audio], sizeof(p->add_audio_lang[p->n_add_audio]), o->u.add_audio.lang);
                p->n_add_audio++;
            } break;
        case VE_ADD_SUBTITLE:
            if (p->n_add_sub < 8) {
                ve_copy(p->add_sub[p->n_add_sub], sizeof(p->add_sub[p->n_add_sub]), o->u.subtitle.path);
                ve_copy(p->add_sub_lang[p->n_add_sub], sizeof(p->add_sub_lang[p->n_add_sub]), o->u.subtitle.lang);
                p->n_add_sub++;
            } break;
        case VE_ADD_THUMBNAIL:   ve_copy(p->thumbnail, sizeof(p->thumbnail), o->u.thumbnail.path); break;
        case VE_REMOVE_THUMBNAIL:p->remove_thumbnail = true; break;
        case VE_METADATA:
            if (p->n_meta < 16) snprintf(p->meta[p->n_meta++], 256, "%s=%s", o->u.meta.key, o->u.meta.val);
            break;
        case VE_FORMAT:          ve_copy(p->format, sizeof(p->format), o->u.format.name); break;
        case VE_ASPECT:
            p->sar_num = o->u.aspect.num; p->sar_den = o->u.aspect.den;
            /* SAR is applied via a setsar filter in ve_exec.c (baked into the
               bitstream), not as a container header hint.  Force transcode so
               remux_lossless() is never reached with sar_num > 0. */
            p->video = VE_TRANSCODE;
            break;

        /* ---- rotate ---- */
        case VE_ROTATE: {
            double d = o->u.rotate.deg; long q = (long)d;
            if ((double)q == d && (q % 90) == 0) {
                long steps = ((o->u.rotate.ccw ? -q : q) / 90) % 4;   /* clockwise quarter-turns */
                p->rotate_quadrant = (int)((steps % 4 + 4) % 4);
                /* Rotation is baked into pixels via transpose in ve_exec.c —
                   never a metadata-only display-matrix write.  Force transcode
                   so remux_lossless() is never reached with rotate_quadrant != 0
                   (its guard would abort with an error otherwise). */
                if (p->rotate_quadrant != 0)
                    p->video = VE_TRANSCODE;
            } else {
                snprintf(tmp, sizeof(tmp), "rotate=%g*PI/180", o->u.rotate.ccw ? -d : d);
                fappend(p->vf, VE_FILTER_MAX, tmp);
                /* non-quadrant rotate: ve_classify already set needs_video_decode
                   so p->video was flipped to TRANSCODE at the top of the loop */
            }
            break;
        }

        /* ---- structural ---- */
        case VE_TRIM:
            p->has_trim = true; p->trim_start = o->u.trim.start; p->trim_end = o->u.trim.end;
            /* Trim must transcode every non-dropped stream so that:
               (a) The trim/atrim filters cut both streams to exactly the same
                   window, and (b) setpts/asetpts rebase timestamps to zero so
                   video and audio stay in sync.  An S_COPY stream is not
                   filtered at all — it would pass packets from outside the
                   window with unrebaseed PTS, causing sync drift. */
            if (p->video == VE_COPY) p->video = VE_TRANSCODE;
            if (p->audio == VE_COPY) p->audio = VE_TRANSCODE;
            break;
        case VE_MERGE:
            for (int k = 0; k < o->u.merge.n && p->n_merge < VE_MERGE_MAX; k++)
                ve_copy(p->merge[p->n_merge++], sizeof(p->merge[p->n_merge++]), o->u.merge.paths[k]);
            break;

        /* ---- video filters ---- */
        case VE_FLIP:       fappend(p->vf, VE_FILTER_MAX, o->u.flip.axis=='h'?"hflip":"vflip"); break;
        case VE_SCALE:      snprintf(tmp,sizeof(tmp),"scale=%d:%d",o->u.scale.w,o->u.scale.h); fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_CROP:       snprintf(tmp,sizeof(tmp),"crop=%d:%d:%d:%d",o->u.crop.w,o->u.crop.h,o->u.crop.x,o->u.crop.y); fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_BRIGHTNESS: snprintf(tmp,sizeof(tmp),"eq=brightness=%g",o->u.adjust.value); fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_CONTRAST:   snprintf(tmp,sizeof(tmp),"eq=contrast=%g",o->u.adjust.value);   fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_SATURATION: snprintf(tmp,sizeof(tmp),"eq=saturation=%g",o->u.adjust.value); fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_GAMMA:      snprintf(tmp,sizeof(tmp),"eq=gamma=%g",o->u.adjust.value);       fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_HUE:        snprintf(tmp,sizeof(tmp),"hue=h=%g",o->u.adjust.value);          fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_GRAYSCALE:  fappend(p->vf,VE_FILTER_MAX,"hue=s=0"); break;
        case VE_FPS:        snprintf(tmp,sizeof(tmp),"fps=%g",o->u.fps.value); fappend(p->vf,VE_FILTER_MAX,tmp); break;
        case VE_REVERSE_VIDEO: fappend(p->vf,VE_FILTER_MAX,"reverse"); break;
        case VE_DENOISE:    fappend(p->vf,VE_FILTER_MAX,"hqdn3d"); break;
        case VE_SHARPEN:    fappend(p->vf,VE_FILTER_MAX,"unsharp"); break;
        case VE_PAD:
            snprintf(tmp,sizeof(tmp),
                "scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2",
                o->u.pad.w,o->u.pad.h,o->u.pad.w,o->u.pad.h);
            fappend(p->vf,VE_FILTER_MAX,tmp); break;

        /* ---- audio filters ---- */
        case VE_VOLUME:     snprintf(tmp,sizeof(tmp),"volume=%g",o->u.volume.factor); fappend(p->af,VE_FILTER_MAX,tmp); break;
        case VE_MONO:       fappend(p->af,VE_FILTER_MAX,"aformat=channel_layouts=mono"); break;
        case VE_REVERSE_AUDIO: fappend(p->af,VE_FILTER_MAX,"areverse"); break;

        /* ---- both streams ---- */
        case VE_SPEED:
            snprintf(tmp,sizeof(tmp),"setpts=%.6f*PTS",1.0/o->u.speed.factor); fappend(p->vf,VE_FILTER_MAX,tmp);
            atempo_chain(p->af, VE_FILTER_MAX, o->u.speed.factor);
            break;
        case VE_FADE: {
            const char* t = (o->u.fade.dir=='o') ? "out" : "in";
            if (o->u.fade.target=='v'||o->u.fade.target=='b') {
                if (o->u.fade.dir=='i') snprintf(tmp,sizeof(tmp),"fade=t=in:st=0:d=%g",o->u.fade.dur);
                else                    snprintf(tmp,sizeof(tmp),"fade=t=out:d=%g",o->u.fade.dur); /* st filled at exec */
                fappend(p->vf,VE_FILTER_MAX,tmp);
            }
            if (o->u.fade.target=='a'||o->u.fade.target=='b') {
                if (o->u.fade.dir=='i') snprintf(tmp,sizeof(tmp),"afade=t=in:st=0:d=%g",o->u.fade.dur);
                else                    snprintf(tmp,sizeof(tmp),"afade=t=out:d=%g",o->u.fade.dur);
                fappend(p->af,VE_FILTER_MAX,tmp);
            }
            (void)t; break;
        }
        default: break;
        }
    }

    /* if every audio stream is dropped, audio transcode is moot */
    if (p->drop_all_audio) p->audio = VE_DROP;

    p->lossless = (p->video != VE_TRANSCODE && p->audio != VE_TRANSCODE);
    return true;
}

/* ── dry-run summary ──────────────────────────────────────────────────────── */

static const char* act(VeStreamAction a) {
    return a==VE_COPY?"COPY (lossless)":a==VE_TRANSCODE?"TRANSCODE (re-encode)":"DROP";
}

void ve_plan_print(const VePlan* p) {
    printf("input : %s\n", p->input);
    printf("output: %s%s%s\n", p->output, p->format[0]?"  format=":"", p->format);
    printf("mode  : %s\n", p->lossless ? "LOSSLESS  (remux / metadata only)"
                                        : "MIXED     (re-encode only the streams that need it)");
    printf("video : %s%s%s\n", act(p->video), p->vf[0]?"  -vf ":"", p->vf);
    printf("audio : %s%s%s\n", act(p->audio), p->af[0]?"  -af ":"", p->af);
    if (p->rotate_quadrant) printf("        rotate: %d x 90 clockwise (transpose filter, re-encode)\n", p->rotate_quadrant);
    if (p->sar_num)         printf("        aspect: %d:%d (setsar filter, re-encode)\n", p->sar_num, p->sar_den);
    if (p->has_trim) {
        char endbuf[32];
        if (p->trim_end < 0) snprintf(endbuf, sizeof(endbuf), "end");
        else                 snprintf(endbuf, sizeof(endbuf), "%.3f", p->trim_end);
        printf("        trim  : %.3f .. %s  (keyframe-aligned copy)\n", p->trim_start, endbuf);
    }
    for (int i=0;i<p->n_merge;i++)     printf("        merge : + %s\n", p->merge[i]);
    if (p->drop_all_audio)             printf("        audio : all tracks dropped (mute)\n");
    for (int i=0;i<p->n_drop_audio;i++)printf("        drop audio track #%d\n", p->drop_audio_index[i]);
    for (int i=0;i<p->n_add_audio;i++) printf("        + audio  %s [%s] (copy, transcode if incompatible)\n", p->add_audio[i], p->add_audio_lang[i]);
    for (int i=0;i<p->n_add_sub;i++)   printf("        + sub    %s [%s]\n", p->add_sub[i], p->add_sub_lang[i]);
    if (p->thumbnail[0])               printf("        + cover  %s\n", p->thumbnail);
    if (p->remove_thumbnail)           printf("        cover removed\n");
    for (int i=0;i<p->n_meta;i++)      printf("        meta  : %s\n", p->meta[i]);
}
